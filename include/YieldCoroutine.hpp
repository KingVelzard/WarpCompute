#include "MPSC.hpp"

#include <coroutine>

template <typename T>
concept QueueType = requires(T t) {
    t.push();
    t.pop();
};

template <QueueType Queue> struct YieldAwaitable {

    explicit YieldAwaitable(Queue& event_loop) : event_loop_{event_loop} {}

    bool await_ready() { return false; }

    bool await_suspend(std::coroutine_handle<> other) {
        queued_ = this->event_loop_.push(other); // check if yield blocks
        return queued_;
    }

    // Returns whether yield actually yielded
    [[nodiscard]] bool await_resume() const { return queued_; }

  private:
    Queue& event_loop_;
    bool queued_ = false;
};
