#include "FrameAllocator.hpp"
#include "Job.hpp"
#include "LockFreeSPSC.hpp"

#include <coroutine>
#include <expected>
#include <span>

inline constexpr int QUEUE_SIZE     = 1'024;
inline constexpr int COROUTINE_SIZE = 512;
inline constexpr int SLAB_SIZE      = 1'024;

inline thread_local FrameAllocator<COROUTINE_SIZE, SLAB_SIZE> frame_pool;

template <typename T> struct Task {

    struct promise_type final {
        Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }

        auto initial_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() noexcept { std::terminate(); }

        void return_void() noexcept {}

        auto final_suspend() noexcept { return std::suspend_always{}; }

        static void* operator new(std::size_t size) {
            assert(size <= COROUTINE_SIZE);
            return frame_pool.allocate();
        } // NOLINT
        static void operator delete(void* ptr) { frame_pool.deallocate(static_cast<std::byte*>(ptr)); }
    };

  private:
    explicit Task(std::coroutine_handle<> h) : handle_{h} {}
    std::coroutine_handle<promise_type> handle_{};
};

// TODO GPU ERROR, CANCELLED, OTHERS
enum class DispatchError { Full };

template <typename T, typename Queue> struct DispatchAwaitable final {
  public:
    DispatchAwaitable(Queue& queue, std::span<T> output, std::span<const T> input, uint8_t id) noexcept
        : queue_{queue}, result_{output}, payload_{input}, reactor_id_{id} {}

    //*
    // always suspend
    //*
    bool await_ready() noexcept {}

    //*
    // if queue is full, resumes immediately and returns error, then is requeued in event loop
    // if queue is partial, suspends as normal
    //*
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        if (queue_.push(Job{this->payload_, this->result_, h, this->reactor_id_})) {
            this->failed_ = false;
            return true;
        }
        this->failed_ = true;
        return false;
    }

    std::expected<std::span<T>, DispatchError> await_resume() {
        if (failed_) {
            return std::unexpected(DispatchError::Full);
        }
        return result_;
    }

  private:
    Queue& queue_;
    TypedPayload<T> payload_;
    std::span<T> result_;
    uint8_t reactor_id_;

    bool failed_{false};
};
