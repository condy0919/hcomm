// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_SCOPE_HPP_
#define HCOMM_PROMISE_SCOPE_HPP_

#include <atomic>
#include <memory>

#include "hcomm/base/logging.hpp"
#include "hcomm/promise/promise.hpp"

namespace hcomm {
namespace internal {
template <typename Promise>
class ScopedContinuation {
public:
    ScopedContinuation(Promise promise, std::shared_ptr<std::atomic<bool>> alive)
        : promise_(std::move(promise)), alive_(std::move(alive)) {}

    auto operator()(Context& ctx) -> typename Promise::ResultType {
        // The scope is exited, prevent "use-after-free"
        if (!alive_->load(std::memory_order_acquire)) [[unlikely]] {
            HCOMM_LOG_ERROR(
                "CRITICAL BUG: Scope destroyed while promise is still running. Fix your lifecycle management.");
            return Pending{};
        }

        return promise_(ctx);
    }

private:
    Promise promise_;
    std::shared_ptr<std::atomic<bool>> alive_;
};
} // namespace internal

/// Scope, a helper for managing the lifecycle of asynchronous operations (Promises).
///
/// `Scope` is designed for high-performance, low-latency environments (e.g., RDMA data paths) to safely manage
/// asynchronous tasks that are tied to the lifecycle of a specific scope, typically a class instance.
///
/// Unlike other implementations such as `fpromise::scope`, `hcomm::Scope` does not own or destroy promises. When a
/// `Scope` is destructed, it does not automatically cancel promises associated. Instead, it employs a lightweight
/// sentinel mechanism:
///
/// 1. A `Scope` maintains a liveness flag throughout its lifetime.
/// 2. A promise wrapped by `scope.wrap()` checks this flag before execution.
/// 3. If the `Scope` is destroyed but the wrapped promise is still running, a critical error is logged. This indicates
///    a lifecycle bug where a promise has outlived the context it depends on.
///
/// The key advantage of this design is its performance. By using `std::atomic` instead of `std::mutex`, the hot path
/// of a promise execution only requires a single, cheap atomic read, making it ideal for latency-sensitive
/// applications.
///
/// `Scope` is non-copyable and non-movable to bind its lifetime strictly to the lexical scope in which it is defined.
///
/// # Example
///
/// ```cpp
/// class MyWorker {
/// public:
///     void Start() {
///         // some_async_op() returns a Promise.
///         // `scope_.wrap()` binds the promise's execution to the lifecycle of
///         // the MyWorker instance.
///         auto p = some_async_op().then([this](const SomeType& result) {
///             // ...
///             value_ = 10;
///         }).with(scope_);
///         // Execute the promise.
///         executor_.schedule(std::move(p));
///     }
///
/// private:
///     int value_;
///
///     TheExecType executor_;
///     hcomm::Scope scope_; // It must be the last member.
/// };
/// ```
///
/// If the `MyWorker` instance is destroyed while the promise from `some_async_op()` is still in-flight, the `scope_`
/// destructor will set the liveness flag to false. When the promise continuation is next scheduled, it will detect
/// this and log an error instead of attempting to access dangling members of `MyWorker`.
class Scope final {
public:
    Scope() : alive_(std::make_shared<std::atomic<bool>>(true)) {}

    // A Scope is non-copyable and non-movable to enforce that its lifetime is bound to the lexical scope in which it
    // is defined.
    Scope(Scope&& rhs) noexcept = delete;
    Scope& operator=(Scope&& rhs) noexcept = delete;

    ~Scope() {
        exit();
    }

    bool exited() const {
        return !alive_->load(std::memory_order_relaxed);
    }

    void exit() {
        alive_->store(false, std::memory_order_release);
    }

    /// Wraps a promise, binding its execution to the lifetime of this Scope.
    template <typename Promise>
    auto wrap(Promise promise) {
        return PromiseImpl(internal::ScopedContinuation(std::move(promise), alive_));
    }

private:
    std::shared_ptr<std::atomic<bool>> alive_;
};
} // namespace hcomm

#endif // HCOMM_PROMISE_SCOPE_HPP_
