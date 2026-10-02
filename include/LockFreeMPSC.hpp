#include <array>
#include <atomic>
#include <type_traits>

template <typename T>
concept QueueRequired = std::is_default_constructible_v<T> && // array default ctor at start-up time
                        std::is_nothrow_move_assignable_v<T> && std::is_trivially_destructible_v<T>;

/**
 * MPSC LockFree Ringbuffer Queue.
 * Vyukov's Model:
 *
 * Tail and Head are monotonically increasing, modulo'ed (bitwise &) to find actual index.
 * Each slot has a sequence number, starting from their index in storage.
 *
 * For pushing:
 * If tail == seq, free slot to add
 * If tail < seq, consumed by other writer thread, move on
 * If tail > seq, queue is full, return false
 *
 * For popping:
 * If head == seq, queue is empty, return false
 * If head == seq + 1, slot if full, pop
 *
 * Pushing adds one to seq.
 * Popping adds N to seq.
 * (This is due to the monotonically increasing model)
 */

template <QueueRequired T, std::size_t N>
    requires(((N & (N - 1)) == 0) && (N > 0))
class LockFreeMPSC {

  private:
    struct Slot {
        T data;
        std::atomic<std::size_t> seq{0};
    };

  public:
    LockFreeMPSC() {
        for (std::size_t i{0}; i < N; ++i) {
            Slot& slot = storage[i];
            slot.seq.store(i, std::memory_order_relaxed);
        }
    }

    template <typename U>
        requires std::same_as<std::decay_t<U>, T>
    [[nodiscard]] bool push(U&& data) {
        while (true) {
            std::size_t local_tail = this->tail_.load(std::memory_order_relaxed);
            Slot& slot             = this->storage[local_tail & MASK];
            std::size_t local_seq  = slot.seq.load(std::memory_order_acquire); // synchronization on seq
            std::intptr_t diff     = static_cast<std::intptr_t>(local_seq) - static_cast<std::intptr_t>(local_tail);

            if (diff < 0)
                return false;

            if (diff > 0)
                continue;

            if (this->tail_.compare_exchange_weak(local_tail, local_tail + 1,
                                                  std::memory_order_relaxed,    // relaxed on success
                                                  std::memory_order_relaxed)) { // relaxed on failure
                slot.data = std::forward<U>(data);
                slot.seq.store(local_seq + 1, std::memory_order_release);
                return true;
            }
        }
    }

    [[nodiscard]] bool pop(T& data) {
        std::size_t local_head = this->head_.load(std::memory_order_relaxed);
        Slot& slot             = this->storage[local_head & MASK];
        std::size_t local_seq  = slot.seq.load(std::memory_order_acquire);
        std::intptr_t diff     = static_cast<std::intptr_t>(local_seq) - static_cast<std::intptr_t>(local_head);

        if (diff == 0)
            return false;

        data = std::move(slot.data);
        this->head_.store(local_head + 1, std::memory_order_relaxed);
        slot.seq.store(local_head + N, std::memory_order_release);

        return true;
    }

  private:
    static constexpr std::size_t MASK = N - 1;
    alignas(64) std::atomic<size_t> head_{};
    alignas(64) std::atomic<size_t> tail_{};
    alignas(64) std::array<Slot, N> storage;
};
