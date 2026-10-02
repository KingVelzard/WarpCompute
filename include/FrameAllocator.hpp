#include <cstddef>
#include <cstring>
#include <memory>

template <std::size_t SLOT_SIZE, std::size_t SLAB_SIZE> struct FrameAllocator {
    // TODO turn restaint into concept
    static_assert(SLOT_SIZE >= sizeof(std::byte*), "SLOT_SIZE must be at least pointer-sized for the free list");

    struct alignas(std::max_align_t) Slab {
        alignas(std::max_align_t) std::array<std::byte, SLOT_SIZE * SLAB_SIZE> storage;
        std::unique_ptr<Slab> next;
        std::size_t bump_index{0};

        [[nodiscard]] bool full() const { return bump_index >= SLAB_SIZE; }

        std::byte* bump() {
            std::byte* slot = &storage[bump_index * SLOT_SIZE];
            ++bump_index;
            return slot;
        }
    };

    std::unique_ptr<Slab> head_;
    std::byte* free_head_{nullptr};

  public:
    std::byte* allocate() noexcept {
        if (free_head_) {
            std::byte* slot = free_head_;
            // read the next free pointer stored inside
            std::byte* next{};
            std::memcpy(&next, slot, sizeof(next));
            free_head_ = next;
            return slot;
        }
        if (!head_ || head_->full()) {
            auto slab  = std::make_unique<Slab>();
            slab->next = std::move(head_);
            head_      = std::move(slab);
        }
        return head_->bump();
    }

    void deallocate(std::byte* slot) {
        // write current free head into slot
        std::memcpy(slot, &free_head_, sizeof(free_head_));
        free_head_ = slot;
    }

    // no dtor, uniqueptr cleans up slabs, and coroutines are deleted by its own machinery
};
