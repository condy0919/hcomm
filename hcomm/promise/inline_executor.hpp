// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_INLINE_EXECUTOR_HPP_
#define HCOMM_PROMISE_INLINE_EXECUTOR_HPP_

#include <stdexcept>

#include "hcomm/promise/promise.hpp"

namespace hcomm {
class InlineExecutor : public Executor {
public:
    InlineExecutor() : ctx_(this) {}

    void schedule(PendingTask task) override {
        [[maybe_unused]] const bool done = task(ctx_);
        assert(done == !task);
    }

private:
    class ContextImpl : public Context {
    public:
        ContextImpl(InlineExecutor* exec) : executor_(exec) {}

        Waker waker() override {
            throw std::runtime_error("InlineExecutor does not support waker()");
        }

        InlineExecutor* executor() override {
            return executor_;
        }

    private:
        InlineExecutor* executor_;
    };

    ContextImpl ctx_;
};
} // namespace hcomm

#endif // HCOMM_PROMISE_INLINE_EXECUTOR_HPP_
