// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_PROMISE_HPP_
#define HCOMM_PROMISE_PROMISE_HPP_

#include <cassert>
#include <functional>
#include <optional>
#include <type_traits>
#include <variant>

#include "hcomm/promise/result.hpp"
#include <boost/callable_traits.hpp>

namespace hcomm {
// forward
class Context;

template <typename C>
concept Continuation = requires { requires IsResult<std::invoke_result_t<C, Context&>>; };

template <Continuation C>
class PromiseImpl;

template <typename P>
class FutureImpl;

template <Continuation C>
PromiseImpl<C> withContinuation(C);

namespace internal {
// MoveOnlyHandler
template <typename Handler>
class MoveOnlyHandler {
public:
    MoveOnlyHandler() = default;

    MoveOnlyHandler(MoveOnlyHandler&& rhs) noexcept = default;
    MoveOnlyHandler& operator=(MoveOnlyHandler&& rhs) noexcept = default;

    ~MoveOnlyHandler() = default;

    template <typename... Args>
        requires std::invocable<Handler, Args...>
    auto operator()(Args&&... args) {
        return (handler_.value())(std::forward<Args>(args)...);
    }

    explicit operator bool() const {
        return handler_.has_value();
    }

    void reset() {
        handler_.reset();
    }

private:
    std::optional<Handler> handler_;
};

// The continuation produced by `Promise::then()`.
template <typename P, typename ResultHandler>
class ThenContinuation {
public:
private:
};

// The continuation produced by `Promise::andThen()`.
template <typename P, typename ValueHandler>
class AndThenContinuation {
public:
private:
};

// The continuation produced by `Promise::orElse()`.
template <typename P, typename ErrorHandler>
class OrElseContinuation {
public:
private:
};

// The continuation produced by `Promise::inspect()`.
template <typename P, typename InspectHandler>
class InspectContinuation {
public:
    explicit InspectContinuation(P promise, InspectHandler handler)
        : promise_(std::move(promise)), handler_(std::move(handler)) {}

    auto operator()(Context& ctx) {
        auto result = promise_(ctx);
        if (result) {
            if constexpr (std::is_invocable_v<InspectHandler, std::add_lvalue_reference_t<typename P::ResultType>>) {
                handler_(result);
            } else {
                handler_(ctx, result);
            }
        }
        return result;
    }

private:
    P promise_;
    InspectHandler handler_;
};

// The continuation produced by `Promise::discard()`.
template <typename P>
class DiscardContinuation {
public:
    explicit DiscardContinuation(P promise) : promise_(std::move(promise)) {}

    Result<void, void> operator()(Context& ctx) {
        return promise_(ctx).isPending() ? Pending{} : Ok();
    }

private:
    P promise_;
};
} // namespace internal

/// # Brief
///
/// A `Promise` is a building block for asynchronous control flow that wraps an asynchronous task in the form of a
/// continuation that is repeatedly invoked by an executor until it produces a result.
///
/// Additionally asynchronous tasks can be chained onto the promise using a variety of combinators such as `then()`.
///
/// And some helpful functions/classes:
/// - `makePromise` creates a promise with a continuation.
/// - `makeOkPromise` creates a promise that immediately returns a value
/// - `makeErrPromise` creates a promise that immediately returns an error
/// - `makeReusltPromise` creates a promise tat immediately returns a result
/// - `Future` more conveniently holds a promise or its result
/// - `PendingTask` wraps a promise as a pending task for execution
/// - `Executor` executes a pending task
///
/// Always look to the future; never look back.
///
/// # Chaining promises using combinators
///
/// `Promise`s can be chained together using combinators suchs as `then()` which consume the original promise and
/// return a new combined promise.
///
/// For example, the `then()` combinator returns a promise that has the effect of asynchronously awaiting completion
/// of the prior promise (the instance upon which `then()` was called) then delivering its result to a handle
/// function.
///
/// Available combinators:
///
/// - `then()`
/// - `andThen()`
///
/// # Continuations and handlers
///
/// # Boxed and unboxed promises
///
/// # Single ownership model
///
/// # Threading model
///
/// # Result retention and Result
///
/// # Clarification of nomenclature
template <typename T = void, typename E = void>
using Promise = PromiseImpl<std::move_only_function<Result<T, E>(Context&)>>;

/// See documentation of `Promise` for more information.
template <Continuation C>
class PromiseImpl final {
    template <Continuation>
    friend class PromiseImpl;

public:
    /// The promise's result type.
    /// Equivalent to `Result<T, E>`.
    using ResultType = std::invoke_result_t<C, Context&>;

    /// The type of value produced when the promise completes successfully.
    using ValueType = typename ResultType::ValueType;

    /// The type of value produced when the promise completes with an error.
    using ErrorType = typename ResultType::ErrorType;

    /// Creates an empty promise without a continuation. A continuation must be assigned before the promise can be
    /// used.
    PromiseImpl() = default;
    PromiseImpl(std::nullptr_t) {}

    /// Creates a promise with a continuation.
    PromiseImpl(C continuation) noexcept : cont_(std::move(continuation)) {}

    /// Constructs/Assigns the promise by taking the continuation from another promise, leaving the other promise
    /// empty.
    PromiseImpl(PromiseImpl&& rhs) noexcept = default;
    PromiseImpl& operator=(PromiseImpl&& rhs) noexcept = default;

    /// Discards the promise's continuation, leaving it empty.
    PromiseImpl& operator=(std::nullptr_t) {
        cont_.reset();
        return *this;
    }

    /// Assigns the promise's continuation.
    PromiseImpl& operator=(C continuation) {
        cont_.emplace(std::move(continuation));
        return *this;
    }

    /// Destroys the promise, releasing its continuation.
    ~PromiseImpl() = default;

    /// Returns true if the promise is non-empty (has a valid continuation).
    explicit operator bool() const {
        return cont_.has_value();
    }

    void swap(PromiseImpl& rhs) noexcept {
        std::swap(cont_, rhs.cont_);
    }

    /// Invokes the promise's continuation.
    ///
    /// This method should be called by an executor to evaluate the promise. If the result's state is unresolved
    /// then the executor is responsible for arranging to invoke the promise's continuation again once it determines
    /// that it is possible to make progress.
    ///
    /// Once the continuation returns a ready result, the promise is assigned to an empty continuation.
    ///
    /// It will throw `std::bad_optional_access` when the promise is empty.
    ResultType operator()(Context& ctx) {
        auto result = (cont_.value())(ctx);
        if (result) {
            cont_.reset();
        }
        return result;
    }

    /// Takes the promise's continuation, leaving it in an empty state.
    ///
    /// It will throw `std::bad_optional_access` when the promise is empty.
    C takeContinuation() {
        auto c = std::move(cont_.value());
        cont_.reset();
        return c;
    }

    template <typename ResultHandler>
    auto then(ResultHandler handler) {}

    template <typename ValueHandler>
    auto andThen(ValueHandler handler) {}

    template <typename ErrorHandler>
    auto orElse(ErrorHandler handler) {}

    /// Returns an unboxed promise which invokes the specified handler function after this promise completes
    /// (successfully or unsuccessfully), passing it the promise's result then delivering the result onwards to the next
    /// promise once the handler returns.
    ///
    /// The handler receives a const reference, or non-const reference depending on the signature of the handler's last
    /// argument.
    ///
    /// - const reference are especially useful for inspect a result mid-stream without modification, such as print it
    ///   for debugging.
    /// - non-const reference are especially useful for synchronously modify a result mid-stream, such as clamping its
    ///   bounds or inject a default value.
    ///
    /// `InspectHandler` is a callback object (such as a lambda) which can examine or modify the incoming result. Unlike
    /// `then()`, the handler does not need to propagate the result onwards.
    ///
    /// This method consumes the promise's continuation, leaving it empty.
    template <typename InspectHandler>
    auto inspect(InspectHandler handler) {
        return withContinuation(
            internal::InspectContinuation<PromiseImpl, InspectHandler>(std::move(*this), std::move(handler)));
    }

    /// Returns an unboxed promise which discards the result of this promise once it completes, thereby always
    /// producing a successful result of type `Result<void, void>` regardless of whether this promise succeed or
    /// failed.
    auto discard() {
        return withContinuation(internal::DiscardContinuation<PromiseImpl>(std::move(*this)));
    }

    // TODO
    template <typename Wrapper, typename... Args>
    auto wrapWith(Wrapper& wrapper, Args&&... args) {
        return wrapper.wrap(std::move(*this), std::forward<Args>(args)...);
    }

    /// Wraps the promise's continuation into a `std::move_only_function`.
    ///
    /// A boxed promise is easier to store and pass around than unboxed promise produced by `makePromise` and
    /// combinators, though boxing may incur a heap allocation.
    ///
    /// It's good idea to defer boxing the promise until after all desired combinators have been applied to prevent
    /// unnecessary heap allocation during intermediate states of the promise's construction.
    ///
    /// Returns an empty promise if the promise is empty. This method consumes the promise's continuation, leaving
    /// it empty.
    PromiseImpl<std::move_only_function<ResultType(Context&)>> box() {
        if (cont_.has_value()) {
            std::move_only_function<ResultType(Context&)> f(std::move(cont_.value()));
            cont_.reset();
            return f;
        } else {
            return nullptr;
        }
    }

private:
    std::optional<C> cont_;
};

template <Continuation C>
inline void swap(PromiseImpl<C>& lhs, PromiseImpl<C>& rhs) noexcept {
    lhs.swap(rhs);
}

/// The state of a future.
enum class FutureState : std::uint8_t {
    /// Empty
    Empty,
    /// The future holds a promise that may eventually produce a result.
    Pending,
    /// The future holds a successful result.
    Ok,
    /// The future holds a failed result.
    Error,
};

/// # Synopsis
///
/// `Future` is a flexible container that represents the state of an asynchronous operation. It acts as a discriminated
/// union (sum type) that can hold one of three states:
///
/// 1. **Empty**: Holds nothing.
/// 2. **Pending**: Holds a `Promise` that is working towards a result.
/// 3. **Ready**: Holds a `Result` (Value or Error) produced by the promise.
///
/// `Future` is a type alias for `FutureImpl<Promise<T, E>>`.
///
/// # Theory of Operations
///
/// The `Future` class facilitates the lifecycle management of asynchronous tasks.
///
/// - **Initialization**: A future is typically initialized with a `Promise` (Pending state) or a `Result` (Ready
///   state).
/// - **Execution**: When in the Pending state, the future can be "polled" or "driven" using `operator()(Context&)`.
///   This invokes the underlying promise's continuation.
///   - If the continuation returns `Pending`, the future remains in the Pending state.
///   - If the continuation returns a `Result` (Ok or Error), the future transitions to the Ready state, storing the
///   result.
/// - **Consumption**: Once in the Ready state, the result (or specific value/error) can be inspected or extracted.
///   Extracting the result (`takeResult`, `takeValue`, `takeError`) moves the data out and leaves the future in the
///   Empty state.
///
/// # States and Allowed Operations
///
/// ## 1. Empty State
///
/// The future contains no data. This is the default state after default construction or after data has been moved out.
///
/// - **Allowed Operations**:
///   - `state()`: Returns `FutureState::Empty`.
///   - `isEmpty()`: Returns `true`.
///   - `operator bool()`: Returns `false`.
///   - Assignment operators: Can assign a new `Promise` or `Result` to transition to Pending or Ready.
/// - **Forbidden Operations** (will trigger assertions):
///   - `operator()(Context&)`: Cannot poll an empty future.
///   - `promise()`, `takePromise()`: No promise to access.
///   - `result()`, `takeResult()`: No result to access.
///   - `value()`, `takeValue()`: No value to access.
///   - `error()`, `takeError()`: No error to access.
///
/// ## 2. Pending State
///
/// The future holds a `Promise` object. The task is not yet complete.
///
/// - **Allowed Operations**:
///   - `state()`: Returns `FutureState::Pending`.
///   - `isPending()`: Returns `true`.
///   - `operator bool()`: Returns `true`.
///   - `operator()(Context&)`: Polls the internal promise. May transition to Ready state.
///   - `promise()`: Accesses the internal promise.
///   - `takePromise()`: Moves the promise out, transitioning to Empty state.
/// - **Forbidden Operations**:
///   - `result()`, `takeResult()`: Result not yet available.
///   - `value()`, `takeValue()`: Value not yet available.
///
/// ## 3. Ready State (Ok or Error)
///
/// The future holds a `Result` object. The task has completed.
///
/// - **Allowed Operations**:
///   - `state()`: Returns `FutureState::Ok` or `FutureState::Error`.
///   - `isReady()`: Returns `true`.
///   - `isOk()` / `isErr()`: Checks success or failure.
///   - `operator bool()`: Returns `true`.
///   - `operator()(Context&)`: Returns `true` immediately (idempotent).
///   - `result()`: Accesses the stored result.
///   - `takeResult()`: Moves the result out, transitioning to Empty state.
///   - `value()` / `takeValue()`: Accesses/moves the value (only if `isOk()`).
///   - `error()` / `takeError()`: Accesses/moves the error (only if `isErr()`).
/// - **Forbidden Operations**:
///   - `promise()`, `takePromise()`: Promise has already been consumed/converted to a result.
///
/// # Example
///
/// @code
/// #include "hcomm/promise/promise.hpp"
///
/// void example(hcomm::Context& ctx) {
///     // 1. Create a promise (Pending)
///     auto promise = hcomm::makePromise([](hcomm::Context& c) -> hcomm::Result<int> {
///         return hcomm::Ok(123);
///     });
///
///     // 2. Wrap in Future
///     hcomm::Future<int> future(std::move(promise));
///     assert(future.isPending());
///
///     // 3. Drive the future
///     bool done = future(ctx);
///     if (done) {
///         assert(future.isReady());
///         assert(future.isOk());
///
///         // 4. Extract value
///         int val = future.takeValue();
///         assert(val == 123);
///         assert(future.isEmpty());
///     }
/// }
/// @endcode
template <typename T = void, typename E = void>
using Future = FutureImpl<Promise<T, E>>;

template <typename P>
class FutureImpl final {
public:
    /// The type of the promise held by this future.
    using PromiseType = P;
    /// The type of the result produced by the promise.
    using ResultType = typename PromiseType::ResultType;
    /// The success value type.
    using ValueType = typename ResultType::ValueType;
    /// The error value type.
    using ErrorType = typename ResultType::ErrorType;

    /// Default constructor. Initializes an Empty future.
    FutureImpl() = default;

    /// Constructs an Empty future from nullptr.
    FutureImpl(std::nullptr_t) {}

    /// Constructs a Pending future from a promise.
    /// If the promise is empty, the future remains Empty.
    FutureImpl(PromiseType promise) {
        if (promise) {
            state_.template emplace<1>(std::move(promise));
        }
    }

    /// Constructs a Ready future from a result.
    /// If the result is uninitialized (should not happen for valid Results), it effectively stays Empty.
    FutureImpl(ResultType result) {
        if (result) {
            state_.template emplace<2>(std::move(result));
        }
    }

    /// Move constructor. Transfers state from `rhs`. `rhs` becomes Empty.
    FutureImpl(FutureImpl&& rhs) noexcept : state_(std::move(rhs.state_)) {
        rhs.state_.template emplace<0>();
    }

    /// Assigns nullptr. Resets the future to Empty state.
    FutureImpl& operator=(std::nullptr_t) {
        state_.template emplace<0>();
        return *this;
    }

    /// Assigns a promise. Transitions to Pending state (or Empty if promise is empty).
    FutureImpl& operator=(PromiseType promise) {
        if (promise) {
            state_.template emplace<1>(std::move(promise));
        } else {
            state_.template emplace<0>();
        }
        return *this;
    }

    /// Assigns a result. Transitions to Ready state.
    FutureImpl& operator=(ResultType result) {
        if (result) {
            state_.template emplace<2>(std::move(result));
        } else {
            state_.template emplace<0>();
        }
        return *this;
    }

    /// Move assignment. Transfers state from `rhs`. `rhs` becomes Empty.
    FutureImpl& operator=(FutureImpl&& rhs) noexcept {
        state_ = std::move(rhs.state_);
        rhs.state_.template emplace<0>();
        return *this;
    }

    /// Destructor.
    ~FutureImpl() = default;

    /// Returns the current state of the future.
    FutureState state() const {
        switch (state_.index()) {
        case 0:
            return FutureState::Empty;

        case 1:
            return FutureState::Pending;

        case 2:
            return std::get<2>(state_).isOk() ? FutureState::Ok : FutureState::Error;
        }
        std::unreachable();
    }

    /// Checks if the future is not Empty.
    explicit operator bool() const {
        return !isEmpty();
    }

    /// Checks if the future is in the Empty state.
    [[nodiscard]] bool isEmpty() const {
        return state() == FutureState::Empty;
    }

    /// Checks if the future is in the Pending state.
    [[nodiscard]] bool isPending() const {
        return state() == FutureState::Pending;
    }

    /// Checks if the future is in the Ready state and holds a success value.
    [[nodiscard]] bool isOk() const {
        return state() == FutureState::Ok;
    }

    /// Checks if the future is in the Ready state and holds an error.
    [[nodiscard]] bool isErr() const {
        return state() == FutureState::Error;
    }

    /// Checks if the future is in the Ready state (Ok or Error).
    [[nodiscard]] bool isReady() const {
        return state_.index() == 2;
    }

    /// Drives the future's execution by polling the underlying promise.
    ///
    /// - If the future is Pending, it invokes the promise.
    ///   - The promise returns a Result, the future transitions to Ready and returns `true`.
    ///   - The promise returns Pending, the future remains Pending and returns `false`.
    /// - If the future is already Ready, returns `true`.
    ///
    /// @param ctx The execution context.
    /// @return `true` if the future is Ready, `false` otherwise.
    bool operator()(Context& ctx) {
        assert(!isEmpty() && "Cannot poll an empty Future");
        switch (state_.index()) {
        case 0:
            return false;

        case 1:
            if (ResultType result = std::get<1>(state_)(ctx)) {
                state_.template emplace<2>(std::move(result));
                return true;
            }
            return false;

        case 2:
            return true;
        }
        std::unreachable();
    }

    /// Accesses the underlying promise.
    /// Requires state to be Pending.
    const PromiseType& promise() const {
        assert(isPending() && "Future is not in Pending state");
        return std::get<1>(state_);
    }

    /// Extracts the underlying promise, transitioning the future to Empty.
    /// Requires state to be Pending.
    PromiseType takePromise() {
        assert(isPending() && "Future is not in Pending state");
        auto p = std::move(std::get<1>(state_));
        state_.template emplace<0>();
        return p;
    }

    /// Accesses the stored result.
    /// Requires state to be Ready.
    template <typename Self>
    decltype(auto) result(this Self& self) {
        assert(self.isReady() && "Future is not in Ready state");
        return std::get<2>(self.state_);
    }

    /// Extracts the stored result, transitioning the future to Empty.
    /// Requires state to be Ready.
    ResultType takeResult() {
        assert(isReady() && "Future is not in Ready state");
        auto result = std::move(std::get<2>(state_));
        state_.template emplace<0>();
        return result;
    }

    /// Accesses the success value.
    /// Requires state to be Ready and Ok.
    template <typename Self>
        requires(!std::is_void_v<ValueType>)
    decltype(auto) value(this Self& self) {
        assert(self.isReady() && "Future is not in Ready state");
        return std::get<2>(self.state_).value();
    }

    /// Extracts the success value, transitioning the future to Empty.
    /// Requires state to be Ready and Ok.
    ValueType takeValue()
        requires(!std::is_void_v<ValueType>)
    {
        assert(isReady() && "Future is not in Ready state");
        auto value = std::get<2>(state_).takeValue();
        state_.template emplace<0>();
        return value;
    }

    /// Accesses the error value.
    /// Requires state to be Ready and Error.
    template <typename Self>
        requires(!std::is_void_v<ErrorType>)
    decltype(auto) error(this Self& self) {
        assert(self.isReady() && "Future is not in Ready state");
        return std::get<2>(self.state_).error();
    }

    /// Extracts the error value, transitioning the future to Empty.
    /// Requires state to be Ready and Error.
    ErrorType takeError()
        requires(!std::is_void_v<ErrorType>)
    {
        assert(isReady() && "Future is not in Ready state");
        auto err = std::get<2>(state_).takeError();
        state_.template emplace<0>();
        return err;
    }

    /// Swaps the state with another future.
    void swap(FutureImpl& rhs) noexcept {
        std::swap(state_, rhs.state_);
    }

private:
    std::variant<std::monostate, PromiseType, ResultType> state_;
};

template <typename P>
inline void swap(FutureImpl<P>& lhs, FutureImpl<P>& rhs) noexcept {
    lhs.swap(rhs);
}

namespace internal {
// Make a promise containing the specified continuation.
template <Continuation C>
PromiseImpl<C> withContinuation(C continuation) {
    return PromiseImpl<C>(std::move(continuation));
}
} // namespace internal
} // namespace hcomm

#endif // HCOMM_PROMISE_PROMISE_HPP_
