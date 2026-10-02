#include <array>
#include <atomic>
#include <bit>
#include <type_traits>

template <typename T>
concept ConstrainedType = std::is_trivially_copy_constructible_v<T> && std::is_trivially_move_assignable_v<T>;

template <ConstrainedType T, std::size_t N>
    requires(std::has_single_bit(N))
class LockFreeSPSC {

    [[nodiscard]] bool push(T value) {
        std::size_t localTail = this->tail.load(std::memory_order_relaxed);
        std::size_t localHead = this->head.load(std::memory_order_acquire);

        if (localTail - localHead >= N) {
            return false;
        }

        this->storage[localTail & MASK] = std::move(value);
        this->tail.store(localTail + 1, std::memory_order_release);

        return true;
    }

    [[nodiscard]] bool pop(T& value) {
        std::size_t localHead = this->head.load(std::memory_order_relaxed);
        std::size_t localTail = this->tail.load(std::memory_order_acquire);

        if (localTail == localHead) {
            return false;
        }

        value = std::move(this->storage[localHead & MASK]);
        this->head.store(localHead + 1, std::memory_order_release);

        return true;
    }

  private:
    static constexpr std::size_t MASK = N - 1;
    std::array<T, N> storage{};
    alignas(64) std::atomic<std::size_t> head{};
    alignas(64) std::atomic<std::size_t> tail{};
};
