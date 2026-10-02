#include "DispatchCoroutine.hpp"
#include "Job.hpp"
#include "LockFreeMPSC.hpp"
#include "LockFreeSPSC.hpp"
#include "PinnedBuffer.hpp"

#include <asm-generic/socket.h>
#include <bit>
#include <cerrno>
#include <coroutine>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <err.h>
#include <expected>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <print>
#include <pthread.h>
#include <queue>
#include <sched.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

constexpr int PORT                 = 4'790;
constexpr int BACKLOG              = 512;
constexpr int MAX_EVENTS           = 1'024;
constexpr int EVENT_LOOP_NUM       = 16'384;
constexpr int DISPATCH_QUEUE_SIZE  = 16'384;
constexpr int READ_BUFFER_SIZE     = 8'192;
constexpr int MAX_DISPATCH_RETIRES = 128;

struct ReadError {

    enum class Type { Recv, PeerClosed, EpollRegister } type;
    int err;
};

struct AsyncRead {

    explicit AsyncRead(int fd, int epoll, std::span<std::byte> buf) : fd_{fd}, epoll_{epoll}, buf_{buf} {}

    // check if non_blocking
    bool await_ready() {
        this->n_ = recv(this->fd_, this->buf_.data(), buf_.size(), MSG_DONTWAIT);

        if (this->n_ >= 0) {
            if (this->n_ == 0) {
                peer_closed_ = true;
            }
            return true; // got data
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return false; // would block, suspend and try later

        recv_failed_ = true;
        saved_err    = errno;

        return true; // error
                     //
    }

    // EPOLLET so add each time, retrying is handled by EPOLL list
    bool await_suspend(std::coroutine_handle<> h) { // NOLINT non_const
        struct epoll_event ev{};
        ev.events   = EPOLLIN | EPOLLONESHOT | EPOLLET;
        ev.data.ptr = h.address();
        if (epoll_ctl(this->epoll_, EPOLL_CTL_MOD, this->fd_, &ev) == -1) {
            epoll_failed_ = true;
            saved_err     = errno;
            return false;
        }

        return true;
    }

    // return size num of bytes consumed
    std::expected<std::size_t, ReadError> await_resume() {
        if (this->recv_failed_)
            return std::unexpected(ReadError{ReadError::Type::Recv, saved_err});
        if (this->epoll_failed_)
            return std::unexpected(ReadError{ReadError::Type::EpollRegister, saved_err});
        if (this->peer_closed_)
            return std::unexpected(ReadError{ReadError::Type::PeerClosed, 0});

        if (this->n_ > 0)
            return static_cast<std::size_t>(this->n_);

        // slow path, read again
        this->n_ = recv(this->fd_, this->buf_.data(), buf_.size(), MSG_DONTWAIT);

        if (this->n_ == 0)
            return std::unexpected(ReadError{ReadError::Type::PeerClosed, 0});
        if (this->n_ < 0)
            return std::unexpected(ReadError{ReadError::Type::Recv, errno});

        return static_cast<std::size_t>(n_);
    }

  private:
    int fd_;
    int epoll_;
    int saved_err{};
    bool recv_failed_{false};
    bool epoll_failed_{false};
    bool peer_closed_{false};
    ssize_t n_{-1};
    std::span<std::byte> buf_{}; // NOLINT TODO CHANGE TO MONOLITHIC BUFFER FOR WHOLE REACTOR AND USE SPAN
};

class Reactor {

    using MPSC = LockFreeMPSC<std::coroutine_handle<>, EVENT_LOOP_NUM>;
    using SPSC = LockFreeSPSC<Job, DISPATCH_QUEUE_SIZE>;

    Reactor(int cpu_core) {

        // must be in this order
        listener_  = init_listener();
        epoll_     = init_epoll();
        reactor_id = cpu_core;

        pin_core(cpu_core);

        run();
    }

    void pin_core(int cpu_core) {
        pthread_t native = pthread_self();

        cpu_set_t cpuset{};
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);

        if (int err = pthread_setaffinity_np(native, sizeof(cpuset), &cpuset); err != 0) {
            std::print(stderr, "pin to core error {}\n", err);
        }
    }

    int init_listener() {
        struct addrinfo hints{}, *ptr, *result;
        int listener{0};
        const int enable = 1;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = AI_PASSIVE;

        if (int err = getaddrinfo(NULL, std::to_string(PORT).c_str(), &hints, &result); err != 0) { // NOLINT
            std::print(stderr, "getaddrinfo: {}\n", gai_strerror(err));
            exit(EXIT_FAILURE);
        }

        for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
            listener = socket(ptr->ai_family, ptr->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ptr->ai_protocol);

            if (listener == -1)
                continue;

            // set options

            setsockopt(listener, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int));
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

            int cpu = sched_getcpu();
            setsockopt(listener, SOL_SOCKET, SO_INCOMING_CPU, &cpu, sizeof(cpu));

            if (bind(listener, ptr->ai_addr, ptr->ai_addrlen) == 0)
                break;

            close(listener);
            listener = -1;
        }

        freeaddrinfo(result);

        if (listener == -1 || ptr == nullptr) {
            perror("could not bind");
            exit(EXIT_FAILURE);
        }

        if (listen(listener, BACKLOG) == -1) {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        return listener;
    }

    int init_epoll() {
        int epoll = epoll_create1(EPOLL_CLOEXEC);
        if (epoll == -1) {
            perror("epoll_create1");
            std::exit(EXIT_FAILURE);
        }

        struct epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = this->listener_;

        if (epoll_ctl(epoll, EPOLL_CTL_ADD, this->listener_, &ev) == -1) {
            perror("epoll_ctl");
            std::exit(EXIT_FAILURE);
        }

        return epoll;
    }

    void run() {
        int num_fds{};
        struct epoll_event events[MAX_EVENTS]; // NOLINT, C-API

        while (true) {
            num_fds = epoll_wait(this->epoll_, events, MAX_EVENTS, -1);
            if (num_fds == -1) {
                perror("epoll_wait failed");
                break;
            }

            // iterator over fds, either new connection, new data, eventloop wakeup
            for (int i{}; i < num_fds; ++i) {
                int eventfd = events[i].data.fd;

                if (eventfd == listener_) {
                    // TODO PROCESS NEW CONNECTION
                }

                if (eventfd & EPOLLIN) { // new data
                    // TODO PROCESS NEW DATA
                }
            }
        }
    }

    /**
     * Returns a payload span if a complete message is sent, otherwise nullopt
     */
    std::optional<std::span<std::byte>> try_extract(std::span<std::byte> data) {
        // check if header exists
        if (data.size() < 4) {
            return std::nullopt;
        }

        // buffer for header.
        uint32_t len{};
        std::memcpy(&len, data.data(), 4);
        if constexpr (std::endian::native == std::endian::little) {
            len = std::byteswap(len);
        }

        // check for full payload
        if (data.size() < std::size_t{4} + len) {
            return std::nullopt;
        }

        return data.subspan(4, len);
    }

    /**
     * Parses the header data of a message and determines job parameters
     */

    ParsedJob parse_job(std::span<const std::byte> data) {
        uint32_t raw_count;
        constexpr int COUNT_START = 2;
        constexpr int COUNT_END   = 4;
        std::memcpy(&raw_count, data.data() + COUNT_START, COUNT_END);

        if constexpr (std::endian::native == std::endian::little) {
            raw_count = std::byteswap(raw_count);
        }

        return ParsedJob{
            .op      = static_cast<OperationType>(data[0]), // first byte is operation
            .type    = static_cast<DataType>(data[1]),      // second byte is data type
            .count   = raw_count,
            .payload = data.subspan(COUNT_START + COUNT_END),
        };
    }

    template <std::integral T> void decode_bytes(std::span<T> dst, std::span<const std::byte> src) {
        // make unsigned for byteswap, have to byteswap float/double as int for IEE
        using U =
            std::conditional_t<sizeof(T) == 8, std::uint64_t,
                               std::conditional_t<sizeof(T) == 4, std::uint32_t,
                                                  std::conditional_t<sizeof(T) == 2, std::uint16_t, std::uint8_t>>>;
        std::size_t n = src.size() / sizeof(T);
        for (std::size_t i{0}; i < n; ++i) {
            U raw{};
            std::memcpy(&raw, src.data() + i * sizeof(T), sizeof(raw));
            if constexpr (std::endian::native == std::endian::little)
                raw = std::byteswap(raw);
            // for float/double
            dst[i] = std::bit_cast<T>(raw);
        }
    }

    Task<void> handle_connection(int fd, const int& reactor_id) {

        // allocate buffer

        std::array<std::byte, READ_BUFFER_SIZE> buf{};
        std::size_t filled{};

        while (true) {
            // we read into the buffer from the last filled spot
            auto res = co_await AsyncRead{fd, this->epoll_, std::span{buf}.subspan(filled)};
            if (!res) [[unlikely]] {
                int saved_errno = res.error().err;
                switch (res.error().type) {
                case ReadError::Type::PeerClosed:
                    // client hung up, clean up.
                    co_return;

                case ReadError::Type::Recv:
                    std::println("Socket Error: {}: {}", fd, strerror(saved_errno));
                    co_return;

                case ReadError::Type::EpollRegister:
                    std::println("Epoll Registration Error: {}: {}", fd, strerror(saved_errno));
                    co_return;
                }
            }

            filled += *res;

            // try to extract complete messages from what we have, and then dispatch
            while (auto message = try_extract(std::span{buf}.first(filled))) {
                auto job = parse_job(*message);
                std::variant<std::monostate, PinnedBuffer<std::int32_t>, PinnedBuffer<float>, PinnedBuffer<double>>
                    output_storage;

                PayloadVariant response{};
                switch (job.type) {
                case (DataType::I32): {
                    auto& output = output_storage.emplace<PinnedBuffer<std::int32_t>>(job.count);
                    PinnedBuffer<std::int32_t, PinnedType::DefaultPinned> input(job.count);
                    decode_bytes(input.span(), job.payload);

                    std::expected<std::span<std::int32_t>, DispatchError> result = std::unexpected(DispatchError::Full);
                    int atempts{};
                    do {
                        result = co_await DispatchAwaitable<std::int32_t, SPSC>{this->dispatch_queue_, output.span(),
                                                                                input.span(), this->reactor_id};
                        if (!result) {
                            switch (result.error()) {
                            case (DispatchError::Full): {
                                if (!co_await YieldAwaitable<MPSC>{}) {
                                    std::println(stderr, "Error 503: Excessive Traffic");
                                    break;
                                }
                                if (++atempts > MAX_DISPATCH_RETIRES) {
                                    std::println(stderr, "Error 503: Dispatch Queue Saturated");
                                    break;
                                }
                            }
                            }
                        }
                    } while (!result);

                    response = *result;
                    break;
                }
                }
            }
        }

        void on_new_connection() {

            while (true) { // EPOLLET: drain all connections
                int newfd = accept4(this->listener_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

                if (newfd == -1) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
                        break;
                    if (errno == EINTR)
                        continue;

                    perror("accept4 error");
                    break;
                }

                // REMOVED BUSYPOLL: TEST WITH GPU PCIE OVERHEAD

                const int enable = 1;
                const int cpu    = sched_getcpu();
                setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
                setsockopt(newfd, IPPROTO_TCP, TCP_QUICKACK, &enable, sizeof(enable));
                setsockopt(newfd, SOL_SOCKET, SO_INCOMING_CPU, &cpu, sizeof(cpu));

                struct epoll_event ev{};
                ev.events   = EPOLLIN | EPOLLONESHOT | EPOLLET;
                ev.data.ptr = nullptr; // set on AsyncRead MOD

                if (epoll_ctl(this->epoll_, EPOLL_CTL_ADD, newfd, &ev) == -1) {
                    close(newfd);
                    continue;
                }

                this->tasks_[newfd].emplace(handle_connection(newfd, cpu));

                this->event_loop_.push(tasks_[newfd]->get_handle());
            }
        }

        ~Reactor() {
            close(this->listener_);
            close(this->epoll_);
        }

      private:
        int listener_;
        int epoll_;
        int reactor_id;
        LockFreeMPSC<std::coroutine_handle<>, EVENT_LOOP_NUM> event_loop_;
        LockFreeSPSC<Job, DISPATCH_QUEUE_SIZE> dispatch_queue_;
        std::vector<std::optional<Task<void>>> tasks_{};

      public:
        Reactor(const Reactor&)            = delete;
        Reactor(Reactor&&)                 = delete;
        Reactor& operator=(const Reactor&) = delete;
        Reactor& operator=(Reactor&&)      = delete;
    };
