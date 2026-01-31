// SPDX-License-Identifier: MulanPSL-2.0

#include <cassert>
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
///
///                   -- producer drops --> [ Abandoned ]
///                  /
///                 /--- consumer drops --> [ Cancelled ]
///                /
/// [ Pending ] ---+--------------------------------------- producer completes ----------+
///                \                                                                     |
///                 \                                                                    v
///                  +--- consumer waits --> [ Waiting ] -- producer completes --> [ Completed ] --+
///                                                                                                |
///                                                                                                v
///                                                                                           [ Consumed ]
enum class SharedState : std::uint8_t {
    /// The initial state. The `Completer` has not yet provided a result, and the `Consumer` has not yet dropped.
    Pending,
    /// The `Completer` was dropped without producing a result.
    Abandoned,
    /// The `Completer` has produced a result, which is now available.
    Completed,
    /// The `Consumer` was dropped without consuming the result.
    Cancelled,
    /// The `Consumer` was waiting for the result.
    Waiting,
    /// The `Consumer` has taken the result. This is a conceptual terminal state; the state variable is not set to this
    /// value. It is implied when the result is moved out of the `SharedCore`.
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
        return state_.load(std::memory_order_relaxed) == SharedState::Cancelled;
    }

    /// Returns `true` if the completer has abandoned the operation.
    bool abandoned() const {
        return state_.load(std::memory_order_relaxed) == SharedState::Abandoned;
    }

    /// Abandons the operation from the completer's side.
    ///
    /// This is called when the `Completer` is destroyed without providing a result. The state transitions from
    /// `Pending` to `Abandoned`.
    void abandon() {
        auto expected = SharedState::Pending;
        state_.compare_exchange_strong(expected, SharedState::Abandoned, std::memory_order_acq_rel);
    }

    /// Completes the operation with a result.
    ///
    /// Stores the result and wakes the waiting consumer. The state transitions from `Pending/Waiting` to `Completed`.
    /// The consumer.promise() may have already determined the state as Waiting, making waker_ valid and requiring
    /// waker_.wake() to notify the consumer upon completion.
    void complete(Result<T, E> result) {
        const auto current = state_.load(std::memory_order_relaxed);
        assert(current == SharedState::Pending || current == SharedState::Waiting || current == SharedState::Cancelled);

        // The completion logic is carefully ordered to prevent race conditions
        // with the consumer in `await`.

        // 1. Store the result. This write is not atomic but is safe. The consumer is guaranteed not to read `result_`
        //    until it has observed the `Completed` state, which happens after the release fence in step 2.
        result_ = std::move(result);

        // 2. Atomically update the state to `Completed`. This operation uses an acquire-release memory ordering to
        //    create a synchronization point with `await`.
        //
        //    - `release`: Ensures that the write to `result_` (step 1) is visible to any thread that performs an
        //      `acquire` operation on `state_` and sees the value `Completed`.
        //
        //    - `acquire`: Ensures that if we see a `Waiting` state, we also see the write to `waker_` that happened in
        //      `await` before it set the `Waiting` state.
        SharedState old = state_.exchange(SharedState::Completed, std::memory_order_acq_rel);

        // 3. Wake the consumer only if it was actually waiting. If the previous state was `Waiting`, it means the
        //    consumer has registered its waker and is expecting to be woken up. If the state was `Pending`, the
        //    consumer hasn't yet reached the point of waiting, and it will observe the `Completed` state itself in
        //    `await`.
        if (old == SharedState::Waiting) {
            // The `acquire` on the `exchange` above ensures that we have visibility of the `waker_` that was written
            // by the consumer thread before it transitioned to `Waiting`.
            waker_.wake();
        }
    }

    /// Cancels the operation from the consumer's side.
    ///
    /// This is called when the `Consumer` is destroyed without taking the result. The state transitions from `Pending`
    /// to `Cancelled`.
    void cancel() {
        auto expected = SharedState::Pending;
        state_.compare_exchange_strong(expected, SharedState::Cancelled, std::memory_order_acq_rel);
    }

    /// Sets a default result if the completer abandons the operation (without providing a result).
    ///
    /// This is used by `Consumer::promiseOr()`.
    /// If the state is `Pending`, this result acts as a default and can be overridden by a subsequent completion
    /// from the completer. If the state is `Abandoned`, this result will be returned by `await()`.
    void setDefaultResult(Result<T, E> result) {
        auto current = state_.load(std::memory_order_relaxed);
        if (current == SharedState::Pending || current == SharedState::Abandoned) {
            result_ = std::move(result);
        }
    }

    /// Awaits the result from the consumer's side.
    ///
    /// This is the polling function for the `Promise` implementation. If the result is pending, it registers the
    /// current context's waker. If the result is available (`Completed` or `Abandoned`), it returns the result and
    /// transitions the state to `Consumed`.
    ///
    /// If the `Completer` was abandoned, this will return the default `Result` (which is `Pending{}`) unless
    /// `promiseOr` was used to provide a default result.
    Result<T, E> await(Context& ctx) {
        auto current = state_.load(std::memory_order_relaxed);
        assert(current == SharedState::Abandoned || current == SharedState::Pending ||
               current == SharedState::Completed);

        if (current == SharedState::Pending) {
            // State is Pending, so the result is not ready yet. We need to prepare to go to sleep (by returning
            // Pending{}), but we must first register our waker so the producer can wake us up.

            // 1. Register the waker from the current async context.
            waker_ = ctx.waker();

            // 2. Try to transition from `Pending` to `Waiting`. This is the critical step. `acq_rel` is used:
            //
            //    - `release`: Makes our write to `waker_` visible to the producer if it reads `Waiting`.
            //
            //    - `acquire`: If the CAS fails because the state is now `Completed`, we need to see the producer's
            //      write to `result_`.
            if (state_.compare_exchange_strong(current, SharedState::Waiting, std::memory_order_acq_rel)) {
                // --- Success Case ---
                //
                // The state is now `Waiting`. We can safely return `Pending` and go to sleep. The producer is now
                // responsible for waking us.
                return Pending{};
            }

            // --- Failure Case (the interesting part) ---
            //
            // The CAS failed! This means `state_` was NOT `Pending` when we tried to change it.
            // `compare_exchange_strong` has updated our `current` variable with the new state that caused the failure.
            // This happens if the producer completed the promise *just* after we loaded `Pending` but *before* we
            // could set it to `Waiting`.
            //
            // Timeline of this race:
            //
            //   Consumer (`await`)                   Producer (`complete`)
            //   -------------------                  ---------------------
            //   load() sees `Pending`
            //   waker_ = ...
            //                                        result_ = ...
            //                                        exchange() sets `Completed`
            //                                        sees old state `Pending`, doesn't wake
            //   cas(Pending->Waiting) fails
            //   `current` is now `Completed`
            //
            //   We have successfully avoided a lost wakeup.

            // 3. Check the new state.
            //
            //    If it's `Completed` or `Abandoned`, the result is now available. The `acquire` part of our failed
            //    `compare_exchange_strong` ensures we can safely read `result_`.
            if (current == SharedState::Completed || current == SharedState::Abandoned) {
                // The producer finished its work in the small window before we could go to sleep. No need to be woken
                // up, just take the result.
                return std::move(result_);
            }
        }

        assert(current == SharedState::Abandoned || current == SharedState::Completed);
        // The result was already `Completed` or `Abandoned` when we first checked. We can just take the result and
        // move on.
        // The state is `Completed` or `Abandoned`. We take ownership of the result and transition to a terminal
        // `Consumed` state is implied by moving the result. Note: The actual state variable is not changed to
        // `Consumed` here, this is a conceptual final state.
        return std::move(result_);
    }

private:
    std::atomic<SharedState> state_{SharedState::Pending};

    // valid if state is Waiting.
    Waker waker_;

    // valid if the state is Completed/Abandoned, or if it is Abandoned/Pending and `promiseOr` was used.
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
/// A `Completer` is the "producer" half of a single-producer, single-consumer channel, paired with a `Consumer`. It
/// holds the unique capability to set the result of the operation, which can be done at most once.
///
/// Once a `Completer` is used to complete, abandon, or is destroyed, it becomes invalid and cannot be used again.
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
/// A `Consumer` is the "consumer" half of a single-producer, single-consumer channel, paired with a `Completer`. It
/// holds the unique capability to retrieve the result of the operation.
///
/// This capability is exercised by converting the `Consumer` into a `Promise` via `promise()` or `promiseOr()`. Once
/// this is done, or if the `Consumer` is cancelled or destroyed, it becomes invalid and cannot be used again.
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

/// Creates a connected `Completer` and `Consumer` pair.
///
/// This pair, known as a `Bridge`, is the fundamental mechanism for creating a new `Promise` whose result can be
/// provided asynchronously. It is the analogue to `std::promise` and `std::future`.
///
/// The producer receives the `Completer` to eventually provide a result, and the consumer receives the `Consumer`,
/// which can be turned into a `Promise` to await that result.
template <typename T = void, typename E = void>
Bridge<T, E> makeBridge() {
    auto core = makeRef<internal::SharedCore<T, E>>();
    return {Completer<T, E>(core), Consumer<T, E>(core)};
}

/// Schedules a promise to be resolved on an executor and returns a `Consumer` for its result.
///
/// This function acts as a bridge between different asynchronous contexts. It schedules the given `promise` to run on
/// the specified `Executor`. When the original promise completes, its result is used to complete the `Consumer`
/// that is returned by this function.
///
/// This is useful for transferring the result of a computation from one executor to another, or for creating a
/// `Promise` that can be awaited in the current context while the work happens elsewhere.
///
/// The function takes an `exec` executor to schedule the `promise` on. It returns a `Consumer` that will be
/// completed with the result of the input promise.
template <Continuation C, typename P = PromiseImpl<C>>
auto scheduleFor(Executor* exec, PromiseImpl<C> promise) -> Consumer<typename P::ValueType, typename P::ErrorType> {
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
