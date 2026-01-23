// SPDX-License-Identifier: MulanPSL-2.0

#include <mutex>
#include <utility>

#include "hcomm/base/refptr.hpp"
#include "hcomm/promise/promise.hpp"

namespace hcomm {
template <typename T, typename E>
struct Bridge;

template <typename T, typename E>
Bridge<T, E> makeBridge();

namespace internal {
/// Represents the shared state between a `Completer` and a `Consumer`.
enum class SharedState : std::uint8_t {
    /// The initial state. The `Completer` has not yet provided a result, and the `Consumer` has not yet dropped.
    Pending,
    /// The `Completer` was dropped without producing a result.
    Abandoned,
    /// The `Completer` has produced a result, which is now available.
    Completed,
    /// The `Consumer` was dropped without consuming the result.
    Cancelled,
    /// The `Consumer` has taken the result. This is a terminal state.
    Consumed,
};

/// The shared state between one completer and one consumer.
///
/// This class is the heart of the bridge, managing the state transitions, storing the result, and handling
/// synchronization between the producer (`Completer`) and consumer (`Consumer`). It is reference-counted to ensure its
/// lifetime persists until both parties are done with it.
template <typename T, typename E>
class SharedCore : public RefCounted<SharedCore<T, E>> {
public:
    using ResultType = Result<T, E>;

    /// Returns `true` if the consumer has cancelled the operation.
    bool cancelled() const {
        std::lock_guard lock(mtx_);
        return state_ == SharedState::Cancelled;
    }

    /// Returns `true` if the completer has abandoned the operation.
    bool abandoned() const {
        std::lock_guard lock(mtx_);
        return state_ == SharedState::Abandoned;
    }

    /// Abandons the operation from the completer's side.
    ///
    /// This is called when the `Completer` is destroyed without providing a result. The state transitions from
    /// `Pending` to `Abandoned`.
    void abandon() {
        std::lock_guard lock(mtx_);
        if (state_ == SharedState::Pending) {
            state_ = SharedState::Abandoned;
        }
    }

    /// Completes the operation with a result.
    ///
    /// Stores the result and wakes the waiting consumer. The state transitions from `Pending` to `Completed`.
    /// The consumer.promise() may have already determined the state as pending, making waker_ valid and requiring
    /// waker_.wake() to notify the consumer upon completion.
    void complete(Result<T, E> result) {
        Waker waker;
        bool should_wake = false;
        {
            std::lock_guard lock(mtx_);
            if (state_ == SharedState::Pending) {
                state_ = SharedState::Completed;

                should_wake = true;
                result_ = std::move(result);
                waker = std::move(waker_);
            }
        }

        if (should_wake) {
            waker.wake();
        }
    }

    /// Cancels the operation from the consumer's side.
    ///
    /// This is called when the `Consumer` is destroyed without taking the result. The state transitions from `Pending`
    /// to `Cancelled`.
    void cancel() {
        std::lock_guard lock(mtx_);
        if (state_ == SharedState::Pending) {
            state_ = SharedState::Cancelled;
        }
    }

    /// Sets a default result if the completer abandons the operation (without providing a result).
    ///
    /// This is used by `Consumer::promiseOr()`.
    /// If the state is `Pending`, this result acts as a default and can be overridden by a subsequent completion
    /// from the completer. If the state is `Abandoned`, this result will be returned by `await()`.
    void setDefaultResult(Result<T, E> result) {
        std::lock_guard lock(mtx_);
        if (state_ == SharedState::Pending || state_ == SharedState::Abandoned) {
            result_.swap(result);
        }
    }

    /// Awaits the result from the consumer's side.
    ///
    /// This is the polling function for the `Promise` implementation. If the result is pending, it registers the
    /// current context's waker. If the result is available (`Completed` or `Abandoned`), it returns the result and
    /// transitions the state to `Consumed`.
    ///
    /// If the `Completer` was abandoned, this will return the default `Result` (which is `Pending{}`) unless
    /// `promiseOr` was used to provide a different result.
    Result<T, E> await(Context& ctx) {
        std::lock_guard lock(mtx_);
        if (state_ == SharedState::Pending) {
            waker_ = ctx.waker();
            return Pending{};
        }

        state_ = SharedState::Consumed;
        return std::move(result_);
    }

private:
    mutable std::mutex mtx_;

    SharedState state_ = SharedState::Pending;

    // valid if state is pending and `await`ed.
    Waker waker_;

    // valid if the state is Completed, or if it is Abandoned/Pending and `promiseOr` was used.
    Result<T, E> result_;
};

/// A continuation that wraps a `SharedCore` to be used within a `Promise`.
///
/// This acts as an adapter between the `Consumer` and the `Promise` machinery.
template <typename T, typename E>
class ConsumerPromiseContinuation {
public:
    using ResultType = Result<T, E>;

    /// Constructs a continuation from a shared core.
    explicit ConsumerPromiseContinuation(RefPtr<internal::SharedCore<T, E>> core) : core_(std::move(core)) {}

    /// Constructs a continuation with a default result.
    ///
    /// This is used for `promiseOr`, setting a fallback result in case the completer abandons.
    ConsumerPromiseContinuation(RefPtr<internal::SharedCore<T, E>> core, Result<T, E> result) : core_(std::move(core)) {
        core_->setDefaultResult(std::move(result));
    }

    /// The poll function called by the `Promise` executor.
    ///
    /// It calls `await` on the shared core to poll for the result.
    Result<T, E> operator()(Context& ctx) {
        return core_->await(ctx);
    }

private:
    RefPtr<internal::SharedCore<T, E>> core_;
};
} // namespace internal

/// Provides a result to complete an asynchronous task.
///
/// A `Completer` is the producer side of a single-producer, single-consumer channel. It holds the unique capability to
/// complete the associated `Consumer` with a result. This capability must be exercised at most once.
///
/// Ownership of this capability is implicitly transferred away when the completer is abandoned (either explicitly via
/// `abandon()` or by destruction) or completed (via `completeOk`, `completeErr`, or `completeResult`).
///
/// A `Completer` is invalid after the capability has been exercised.
///
/// This is a move-only type.
///
/// Both T and E default to `void`.
template <typename T = void, typename E = void>
class Completer final {
public:
    using ValueType = T;
    using ErrorType = E;
    using ResultType = Result<T, E>;

    Completer() = default;

    Completer(Completer&&) noexcept = default;
    Completer& operator=(Completer&&) noexcept = default;

    /// Destroys the `Completer`.
    ///
    /// If the completer still owns the completion capability (i.e., has not been completed or abandoned), it
    /// implicitly abandons the task.
    ~Completer() {
        if (core_) {
            core_->abandon();
        }
    }

    /// Returns `true` if this instance can be used to complete the task.
    explicit operator bool() const {
        return static_cast<bool>(core_);
    }

    /// Returns `true` if the associated `Consumer` has cancelled the task.
    ///
    /// This method returns a snapshot of the current cancellation state. Note that the task may be cancelled
    /// concurrently at any time.
    bool cancelled() const {
        return core_->cancelled();
    }

    /// Explicitly abandons the task, signaling that it will never be completed.
    ///
    /// After this call, the `Completer` becomes invalid.
    void abandon() {
        core_->abandon();
        core_.reset();
    }

    /// Completes the task with an Ok result containing no value.
    ///
    /// This is only available when `T` is `void`. After this call, the `Completer` becomes invalid.
    void completeOk()
        requires(std::is_void_v<T>)
    {
        core_->complete(Ok());
        core_.reset();
    }

    /// Completes the task with an Ok result containing a value.
    ///
    /// This is only available when `T` is not `void`. After this call, the `Completer` becomes invalid.
    template <typename U = T>
    void completeOk(U value)
        requires(!std::is_void_v<U>)
    {
        core_->complete(Ok(std::move(value)));
        core_.reset();
    }

    /// Completes the task with an Err result containing no value.
    ///
    /// This is only available when `E` is `void`. After this call, the `Completer` becomes invalid.
    void completeErr()
        requires std::is_void_v<E>
    {
        core_->complete(Err());
        core_.reset();
    }

    /// Completes the task with an Err result containing an error.
    ///
    /// This is only available when `E` is not `void`. After this call, the `Completer` becomes invalid.
    template <typename U = E>
    void completeErr(U error)
        requires(!std::is_void_v<U>)
    {
        core_->complete(Err(std::move(error)));
        core_.reset();
    }

    /// Completes the task with the given `Result`.
    ///
    /// After this call, the `Completer` becomes invalid.
    void completeResult(Result<T, E> result) {
        core_->complete(std::move(result));
        core_.reset();
    }

private:
    template <typename U, typename V>
    friend Bridge<U, V> makeBridge();

    explicit Completer(RefPtr<internal::SharedCore<T, E>> core) : core_(std::move(core)) {}

    RefPtr<internal::SharedCore<T, E>> core_;
};

/// Consumes the result of an asynchronous task.
///
/// A `Consumer` is the consuming side of a single-producer, single-consumer channel. It holds the unique capability to
/// receive the result from the associated `Completer`.
///
/// Ownership of this capability is implicitly transferred away when the task is cancelled (either explicitly via
/// `cancel()` or by destruction) or when the `Consumer` is converted into a `Promise`.
///
/// A `Consumer` is invalid after the capability has been exercised.
///
/// This is a move-only type.
///
/// Both T and E default to `void`.
template <typename T = void, typename E = void>
class Consumer final {
public:
    using ValueType = T;
    using ErrorType = E;
    using ResultType = Result<T, E>;

    Consumer() = default;

    Consumer(Consumer&& rhs) noexcept = default;
    Consumer& operator=(Consumer&& rhs) noexcept = default;

    /// Destroys the `Consumer`.
    ///
    /// If the consumer still owns the consumption capability (i.e., has not been cancelled or converted to a promise),
    /// it implicitly cancels the task.
    ~Consumer() {
        if (core_) {
            core_->cancel();
        }
    }

    /// Returns `true` if this instance can be used to consume the task's result.
    explicit operator bool() const {
        return static_cast<bool>(core_);
    }

    /// Explicitly cancels the task, signaling that the result will never be consumed.
    ///
    /// After this call, the `Consumer` becomes invalid.
    void cancel() {
        core_->cancel();
        core_.reset();
    }

    /// Returns `true` if the associated `Completer` has abandoned the task.
    ///
    /// This method returns a snapshot of the current abandonment state. Note that the task may be abandoned
    /// concurrently at any time.
    bool abandoned() const {
        return core_->abandoned();
    }

    /// Converts the `Consumer` into a `Promise` that resolves with the result of the task.
    ///
    /// After this call, the `Consumer` becomes invalid.
    auto promise() {
        return PromiseImpl(internal::ConsumerPromiseContinuation<T, E>(std::move(core_)));
    }

    /// Converts the `Consumer` into a `Promise`, providing a default result if the task is abandoned.
    auto promiseOr(Result<T, E> result) {
        return PromiseImpl(internal::ConsumerPromiseContinuation<T, E>(std::move(core_), std::move(result)));
    }

private:
    template <typename U, typename V>
    friend Bridge<U, V> makeBridge();

    explicit Consumer(RefPtr<internal::SharedCore<T, E>> core) : core_(std::move(core)) {}

    RefPtr<internal::SharedCore<T, E>> core_;
};

/// A connected pair of a `Completer` and a `Consumer`.
template <typename T, typename E>
struct Bridge {
    Completer<T, E> completer;
    Consumer<T, E> consumer;
};

/// Creates a bridge, which is a connected pair of `Completer` and `Consumer`.
///
/// The `Completer`/`Consumer` pair is analogous to the `std::promise`/`std::future` pair from the C++11 standard
/// library, but adapted for this promise framework.
///
/// The producer receives the `Completer` object, and the consumer receives the `Consumer` object.
///
/// The `Completer` is used to provide the result (or signal an error or abandonment) of an asynchronous operation.
/// The `Consumer` is used to retrieve the result, typically by converting it into a `Promise`.
///
/// The two are linked by a shared state, ensuring that the result is passed from the completer to the consumer
/// exactly once.
template <typename T = void, typename E = void>
Bridge<T, E> makeBridge() {
    auto core = makeRef<internal::SharedCore<T, E>>();
    return {Completer<T, E>(core), Consumer<T, E>(core)};
}

template <Continuation C, typename P = PromiseImpl<C>>
Consumer<typename P::ValueType, typename P::ErrorType> scheduleFor(Executor* exec, PromiseImpl<C> promise) {
    assert(exec);
    assert(promise);

    auto [completer, consumer] = makeBridge<typename P::ValueType, typename P::ErrorType>();
    exec->schedule(promise.then([completer = std::move(completer)](typename P::ResultType& result) mutable -> Result<> {
        completer.completeResult(std::move(result));
        return Ok();
    }));
    return std::move(consumer);
}

} // namespace hcomm
