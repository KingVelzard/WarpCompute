#include <new>
#include <print>
#include <span>
#include <vector>

template <typename T, std::size_t Alignment>
concept GoodAlignment = (Alignment % sizeof(T) == 0) && ((Alignment > 0) && ((Alignment) & (Alignment - 1)) == 0);

template <typename T, std::size_t Alignment>
    requires(GoodAlignment<T, Alignment>)
struct AlignedBuffer {

    explicit AlignedBuffer(std::size_t count)
        : count_{count}, data_{static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t{Alignment}))} {}

    ~AlignedBuffer() { ::operator delete(this->data_, std::align_val_t{Alignment}); }

    AlignedBuffer(AlignedBuffer&& o) noexcept : count_{o.count_}, data_{o.data_} {
        o.data_  = nullptr;
        o.count_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& o) noexcept {
        if (this != &o) {
            ::operator delete(data_, std::align_val_t{Alignment});
            data_    = o.data_;
            count_   = o.count_;
            o.data_  = nullptr;
            o.count_ = 0;
        }
        return *this;
    }

    // since copying would have double free
    AlignedBuffer(const AlignedBuffer&)            = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

    std::span<T> span() noexcept { return {data_, count_}; }
    std::span<const T> span() const noexcept { return {data_, count_}; }

    T& operator[](std::size_t index) noexcept { return data_[index]; }

  private:
    std::size_t count_;
    T* data_;
};
