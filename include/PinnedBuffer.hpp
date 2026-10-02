#include <cstdint>
#include <cuda_runtime.h>
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <new>
#include <print>
#include <span>
#include <type_traits>

enum class PinnedType : std::uint8_t { DefaultPinned, MappedPinned };

template <typename T, PinnedType TYPE = PinnedType::DefaultPinned>
    requires std::is_trivially_copyable_v<T>
struct PinnedBuffer {

    explicit PinnedBuffer(std::size_t count) : count_{count} {
        if (count > SIZE_MAX / sizeof(T))
            throw std::bad_alloc{};
        void* p{};

        constexpr unsigned flag = (TYPE == PinnedType::DefaultPinned) ? cudaHostAllocDefault : cudaHostAllocMapped;

        if (cudaError_t err = cudaHostAlloc(&p, count * sizeof(T), flag); err != cudaSuccess) {
            std::println(stderr, "cudaHostAlloc: {}", cudaGetErrorString(err));
            throw std::bad_alloc{};
        }
        this->data_ = static_cast<T*>(p);

        if constexpr (TYPE == PinnedType::MappedPinned) {
            void* dp{};
            if (cudaError_t err = cudaHostGetDevicePointer(&dp, p, 0); err != cudaSuccess) {
                std::println(stderr, "cudaHostGetDevicePointer: {}", cudaGetErrorString(err));
                cudaFreeHost(p);
                throw std::bad_alloc{};
            }
            device_data_ = static_cast<T*>(dp);
        }
    }

    ~PinnedBuffer() {
        if (data_)
            cudaFreeHost(data_);
    }

    PinnedBuffer(PinnedBuffer&& o) noexcept = default;

    PinnedBuffer& operator=(PinnedBuffer&& o) noexcept {
        if (this != &o) {
            if (data_)
                cudaFreeHost(data_);
            data_          = o.data_;
            count_         = o.count_;
            device_data_   = o.device_data_;
            o.data_        = nullptr;
            o.device_data_ = nullptr;
            o.count_       = 0;
        }
        return *this;
    }

    // since copying would have double free
    PinnedBuffer(const PinnedBuffer&)            = delete;
    PinnedBuffer& operator=(const PinnedBuffer&) = delete;

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return sizeof(T) * count_; }

    std::span<T> span() noexcept { return {data_, count_}; }
    std::span<const T> span() const noexcept { return {data_, count_}; }

    T& operator[](std::size_t index) noexcept { return data_[index]; }
    const T& operator[](std::size_t index) const noexcept { return data_[index]; }

    [[nodiscard]] T* device() noexcept
        requires(TYPE == PinnedType::MappedPinned)
    {
        return device_data_;
    }

  private:
    std::size_t count_{};
    T* data_{};
    T* device_data_{};
};
