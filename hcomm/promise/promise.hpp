// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_PROMISE_HPP_
#define HCOMM_PROMISE_PROMISE_HPP_

#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <type_traits>
#include <variant>

#include "hcomm/base/refptr.hpp"
#include "hcomm/promise/result.hpp"
#include <boost/callable_traits.hpp>

namespace hcomm {
// forward
class Context;
class Executor;
class Waker;

/// A concept representing a continuation for a promise.
///
/// A continuation is a callable that:
/// 1. Is move-constructible and move-assignable (to be carried by the `PromiseImpl`).
/// 2. Accepts a `Context&` as an argument.
/// 3. Returns a type that satisfies `IsResult` (i.e., `Result<T, E>` or similar).
template <typename C>
concept Continuation = requires {
    requires IsResult<std::invoke_result_t<C, Context&>>;
    requires std::is_move_constructible_v<C> && std::is_move_assignable_v<C>;
};

template <Continuation C>
class PromiseImpl;

template <typename P>
class FutureImpl;

namespace internal {
/// A wrapper that provides uniform move construction and move assignment for callable types,
/// enabling move semantics even for types that are not natively move-assignable (e.g., lambdas).
///
/// While lambdas are typically move-constructible, they are often not move-assignable.
/// This class bridges that gap by managing the lifetime of the handler via `std::optional`,
/// allowing for reconstruction upon assignment.
///
/// This type maintains a well-defined empty state. Instances that have been moved from
/// are left in this empty state and must not be accessed.
template <typename Handler>
    requires std::is_move_constructible_v<Handler>
class MoveOnlyHandler {
public:
    MoveOnlyHandler() = default;

    MoveOnlyHandler(Handler&& h) noexcept : handler_(std::move(h)) {}

    MoveOnlyHandler(MoveOnlyHandler&& rhs) noexcept : handler_(std::move(rhs.handler_)) {
        rhs.handler_.reset();
    }

    MoveOnlyHandler& operator=(Handler&& h) noexcept {
        handler_.emplace(std::move(h));
        return *this;
    }

    MoveOnlyHandler& operator=(MoveOnlyHandler&& rhs) noexcept {
        handler_.emplace(std::move(rhs.handler_));
        rhs.handler_.reset();
        return *this;
    }

    /// Invokes the wrapped handler.
    template <typename... Args>
    auto operator()(Args&&... args) -> std::invoke_result_t<Handler, Args...> {
        assert(handler_.has_value());
        return (*handler_)(std::forward<Args>(args)...);
    }

    /// Checks if the handler is valid (not reset).
    explicit operator bool() const {
        return handler_.has_value();
    }

    /// Resets the handler.
    void reset() {
        handler_.reset();
    }

private:
    std::optional<Handler> handler_;
};

/// Adapts a handler to produce a Result or Continuation.
template <typename Handler, typename R = boost::callable_traits::return_type_t<Handler>>
class ResultAdaptor;

/// Specialization for handlers returning Result<T, E>.
template <typename Handler, typename T, typename E>
class ResultAdaptor<Handler, Result<T, E>> {
public:
    using ResultType = Result<T, E>;

    ResultAdaptor(MoveOnlyHandler<Handler> h) : handler_(std::move(h)) {}

    ResultAdaptor(ResultAdaptor&& rhs) noexcept = default;
    ResultAdaptor& operator=(ResultAdaptor&& rhs) noexcept = default;

    /// Invokes the handler and returns its Result.
    template <typename... Args>
    ResultType operator()([[maybe_unused]] Context& ctx, Args&&... args) {
        return handler_(std::forward<Args>(args)...);
    }

private:
    MoveOnlyHandler<Handler> handler_;
};

/// Specialization for handlers returning a Continuation.
template <typename Handler, Continuation C>
class ResultAdaptor<Handler, C> {
public:
    using ResultType = std::invoke_result_t<C, Context&>;

    ResultAdaptor(MoveOnlyHandler<Handler> h) : handler_(std::move(h)) {}

    ResultAdaptor(ResultAdaptor&& rhs) noexcept = default;
    ResultAdaptor& operator=(ResultAdaptor&& rhs) noexcept = default;

    /// Invokes the handler to get the continuation, then drives the continuation.
    template <typename... Args>
    ResultType operator()(Context& ctx, Args&&... args) {
        if (handler_) {
            cont_ = handler_(std::forward<Args>(args)...);
            handler_.reset();
        }
        if (!cont_) {
            return Pending{};
        }
        return cont_(ctx);
    }

private:
    MoveOnlyHandler<Handler> handler_;
    MoveOnlyHandler<C> cont_;
};

/// Helper to extract the first type of a tuple or return an empty tuple.
template <typename>
struct FirstTypeOrEmpty;

template <>
struct FirstTypeOrEmpty<std::tuple<>> {
    using Type = std::tuple<>;
};

template <typename T, typename... Ts>
struct FirstTypeOrEmpty<std::tuple<T, Ts...>> {
    using Type = std::tuple<T>;
};

/// Helper to remove Context& from the beginning of a tuple type.
template <typename>
struct TypeExceptFirstContextRef;

template <typename... Ts>
struct TypeExceptFirstContextRef<std::tuple<Context&, Ts...>> {
    using Type = std::tuple<Ts...>;
};

template <typename... Ts>
struct TypeExceptFirstContextRef<std::tuple<Ts...>> {
    using Type = std::tuple<Ts...>;
};

/// Adapts a handler to be invoked with or without Context& as the first argument.
template <typename Handler>
class ContextAdaptor {
    using AdaptorType = ResultAdaptor<Handler>;

    static constexpr bool HasContextRefFirst =
        std::is_same_v<typename FirstTypeOrEmpty<boost::callable_traits::args_t<Handler>>::Type, std::tuple<Context&>>;

public:
    using ResultType = typename AdaptorType::ResultType;

    ContextAdaptor(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler, automatically passing Context& if required by the handler signature.
    template <typename... Args>
    ResultType operator()(Context& ctx, Args&&... args) {
        if constexpr (HasContextRefFirst) {
            return adaptor_(ctx, ctx, std::forward<Args>(args)...);
        } else {
            return adaptor_(ctx, std::forward<Args>(args)...);
        }
    }

private:
    AdaptorType adaptor_;
};

/// Invoker for handlers taking just Context.
template <typename Handler>
class ContextHandlerInvoker {
    using AdaptorType = ContextAdaptor<Handler>;

public:
    using ResultType = typename AdaptorType::ResultType;

    ContextHandlerInvoker(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler with Context.
    ResultType operator()(Context& ctx) {
        return adaptor_(ctx);
    }

private:
    AdaptorType adaptor_;
};

/// Invoker for handlers taking Context and Result.
template <typename Handler, typename R>
class ResultHandlerInvoker {
    using AdaptorType = ContextAdaptor<Handler>;

    using Args = boost::callable_traits::args_t<Handler>;
    static_assert(std::tuple_size_v<Args> == 1 || std::tuple_size_v<Args> == 2,
                  "The provided handler has wrong arguments number");

    static_assert(
        std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<R&>> ||
            std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<const R&>>,
        "The provided handler's last argument was expected to be of type 'Result<T, E>&' or 'const Result<T, E>&'");

public:
    using ResultType = typename AdaptorType::ResultType;

    ResultHandlerInvoker(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler with Context and Result.
    ResultType operator()(Context& ctx, R& result) {
        return adaptor_(ctx, result);
    }

private:
    AdaptorType adaptor_;
};

/// Invoker for handlers taking Context and Value.
template <typename Handler, typename R, typename T = typename R::ValueType>
class ValueHandlerInvoker {
    using AdaptorType = ContextAdaptor<Handler>;

    using Args = boost::callable_traits::args_t<Handler>;
    static_assert(std::tuple_size_v<Args> == 1 || std::tuple_size_v<Args> == 2,
                  "The provided handler has wrong arguments number");

    static_assert(std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<T&>> ||
                      std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<const T&>>,
                  "The provided handler's last argument was expected to be of type 'T&' or 'const T&'");

public:
    using ResultType = typename AdaptorType::ResultType;

    ValueHandlerInvoker(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler with Context and Value.
    ResultType operator()(Context& ctx, R& result) {
        return adaptor_(ctx, result.value());
    }

private:
    AdaptorType adaptor_;
};

/// Specialization for void ValueType.
template <typename Handler, typename R>
class ValueHandlerInvoker<Handler, R, void> {
    using AdaptorType = ContextAdaptor<Handler>;

    using Args = boost::callable_traits::args_t<Handler>;
    static_assert(std::tuple_size_v<Args> == 0 || std::tuple_size_v<Args> == 1,
                  "The provided handler has wrong arguments number");

    static_assert(std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<>>,
                  "The provided handler's should only accept a 'Context&' or nothing");

public:
    using ResultType = typename AdaptorType::ResultType;

    ValueHandlerInvoker(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler with just Context (ignoring void value).
    ResultType operator()(Context& ctx, [[maybe_unused]] R& result) {
        return adaptor_(ctx);
    }

private:
    AdaptorType adaptor_;
};

/// Invoker for handlers taking Context and Error.
template <typename Handler, typename R, typename E = typename R::ErrorType>
class ErrorHandlerInvoker {
    using AdaptorType = ContextAdaptor<Handler>;

    using Args = boost::callable_traits::args_t<Handler>;
    static_assert(std::tuple_size_v<Args> == 1 || std::tuple_size_v<Args> == 2,
                  "The provided handler has wrong arguments number");

    static_assert(std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<E&>> ||
                      std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<const E&>>,
                  "The provided handler's last argument was expected to be of type 'E&' or 'const E&'");

public:
    using ResultType = typename AdaptorType::ResultType;

    ErrorHandlerInvoker(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler with Context and Error.
    ResultType operator()(Context& ctx, R& result) {
        return adaptor_(ctx, result.error());
    }

private:
    AdaptorType adaptor_;
};

/// Specialization for void ErrorType.
template <typename Handler, typename R>
class ErrorHandlerInvoker<Handler, R, void> {
    using AdaptorType = ContextAdaptor<Handler>;

    using Args = boost::callable_traits::args_t<Handler>;
    static_assert(std::tuple_size_v<Args> == 0 || std::tuple_size_v<Args> == 1,
                  "The provided handler has wrong arguments number");

    static_assert(std::is_same_v<typename TypeExceptFirstContextRef<Args>::Type, std::tuple<>>,
                  "The provided handler should only accept a 'Context&' or nothing");

public:
    using ResultType = typename AdaptorType::ResultType;

    ErrorHandlerInvoker(Handler h) : adaptor_(std::move(h)) {}

    /// Invokes the handler with just Context (ignoring void error).
    ResultType operator()(Context& ctx, [[maybe_unused]] R& result) {
        return adaptor_(ctx);
    }

private:
    AdaptorType adaptor_;
};

/// Continuation logic for `then()`.
template <typename P, typename Handler>
class ThenContinuation {
    using Invoker = ResultHandlerInvoker<Handler, typename P::ResultType>;

public:
    ThenContinuation(P promise, Handler h) : future_(std::move(promise)), invoker_(std::move(h)) {}

    /// Drives the future and invokes the handler on completion.
    auto operator()(Context& ctx) -> typename Invoker::ResultType {
        if (!future_(ctx)) {
            return Pending{};
        }
        return invoker_(ctx, future_.result());
    }

private:
    FutureImpl<P> future_;
    Invoker invoker_;
};

/// Continuation logic for `andThen()`.
template <typename P, typename ValueHandler>
class AndThenContinuation {
    using Invoker = ValueHandlerInvoker<ValueHandler, typename P::ResultType>;

    static_assert(std::is_same_v<typename Invoker::ResultType::ErrorType, typename P::ErrorType>,
                  "The new result should be with the same error type as before");

public:
    AndThenContinuation(P promise, ValueHandler h) : future_(std::move(promise)), invoker_(std::move(h)) {}

    /// Drives the future, propagates error, or invokes handler on success.
    auto operator()(Context& ctx) -> typename Invoker::ResultType {
        if (!future_(ctx)) {
            return Pending{};
        } else if (future_.isErr()) {
            if constexpr (std::is_void_v<typename P::ErrorType>) {
                return Err();
            } else {
                return Err(future_.takeError());
            }
        }
        return invoker_(ctx, future_.result());
    }

private:
    FutureImpl<P> future_;
    Invoker invoker_;
};

/// Continuation logic for `orElse()`.
template <typename P, typename ErrorHandler>
class OrElseContinuation {
    using Invoker = ErrorHandlerInvoker<ErrorHandler, typename P::ResultType>;

    static_assert(std::is_same_v<typename Invoker::ResultType::ValueType, typename P::ValueType>,
                  "The new result should be with the same value type as before");

public:
    OrElseContinuation(P promise, ErrorHandler h) : future_(std::move(promise)), invoker_(std::move(h)) {}

    /// Drives the future, propagates success, or invokes handler on error.
    auto operator()(Context& ctx) -> typename Invoker::ResultType {
        if (!future_(ctx)) {
            return Pending{};
        } else if (future_.isOk()) {
            if constexpr (std::is_void_v<typename P::ValueType>) {
                return Ok();
            } else {
                return Ok(future_.takeValue());
            }
        }
        return invoker_(ctx, future_.result());
    }

private:
    FutureImpl<P> future_;
    Invoker invoker_;
};

/// Continuation logic for `inspect()`.
template <typename P, typename InspectHandler>
class InspectContinuation {
public:
    InspectContinuation(P promise, MoveOnlyHandler<InspectHandler> handler)
        : promise_(std::move(promise)), handler_(std::move(handler)) {}

    /// Drives the promise and inspects the result if ready.
    auto operator()(Context& ctx) {
        auto result = promise_(ctx);
        if (result) {
            if constexpr (std::is_invocable_v<InspectHandler, typename P::ResultType&>) {
                handler_(result);
            } else {
                handler_(ctx, result);
            }
        }
        return result;
    }

private:
    P promise_;
    MoveOnlyHandler<InspectHandler> handler_;
};

/// Continuation logic for `discard()`.
template <typename P>
class DiscardContinuation {
public:
    explicit DiscardContinuation(P promise) : promise_(std::move(promise)) {}

    /// Drives the promise and returns void success when ready.
    Result<void, void> operator()(Context& ctx) {
        if (promise_(ctx)) {
            return Ok();
        }
        return Pending{};
    }

private:
    P promise_;
};

/// Continuation that holds a ready result, produced by `makeResultPromise()`.
template <typename T, typename E>
class ResultContinuation {
public:
    explicit ResultContinuation(Result<T, E> result) : result_(std::move(result)) {}

    /// Returns the stored result.
    Result<T, E> operator()([[maybe_unused]] Context& ctx) {
        return std::move(result_);
    }

private:
    Result<T, E> result_;
};

/// Continuation wrapping a simple handler, produced by `makePromise()`.
template <typename Handler>
class PromiseContinuation {
public:
    explicit PromiseContinuation(Handler h) : handler_(std::move(h)) {}

    /// Invokes the handler.
    auto operator()(Context& ctx) {
        return handler_(ctx);
    }

private:
    ContextHandlerInvoker<Handler> handler_;
};

/// Continuation logic for `joinPromises()` (variadic version).
template <typename... Ps>
class JoinContinuation {
public:
    JoinContinuation(Ps... promises) : futures_(std::move(promises)...) {}

    /// Drives all futures until all are ready.
    auto operator()(Context& ctx) {
        return eval(ctx, std::index_sequence_for<Ps...>{});
    }

private:
    template <std::size_t... Is>
    auto eval(Context& ctx, std::index_sequence<Is...>) -> Result<std::tuple<typename Ps::ResultType...>, void> {
        bool done = (std::get<Is>(futures_)(ctx) && ...);
        if (done) {
            return Ok(std::make_tuple(std::get<Is>(futures_).takeResult()...));
        }
        return Pending{};
    }

    std::tuple<FutureImpl<Ps>...> futures_;
};

/// Continuation logic for `joinPromises()` (vector version).
template <typename Promise>
class JoinVectorContinuation {
public:
    explicit JoinVectorContinuation(std::vector<Promise> promises)
        : promises_(std::move(promises)), results_(promises_.size()) {}

    /// Drives all futures until all are ready.
    auto operator()(Context& ctx) -> Result<std::vector<typename Promise::ResultType>> {
        bool done = true;
        for (std::size_t i = 0; i < results_.size(); ++i) {
            if (!results_[i]) {
                results_[i] = promises_[i](ctx);
                done &= static_cast<bool>(results_[i]);
            }
        }

        if (done) {
            return Ok(std::move(results_));
        }
        return Pending{};
    }

private:
    std::vector<Promise> promises_;
    std::vector<typename Promise::ResultType> results_;
};
} // namespace internal

/// # Brief
///
/// A `Promise` is a building block for asynchronous control flow that wraps an asynchronous task in the form of a
/// continuation that is repeatedly invoked by an executor until it produces a result.
///
/// Additionally, asynchronous tasks can be chained onto the promise using a variety of combinators such as `then()`.
///
/// Some helpful functions/classes include:
/// - `makePromise` creates a promise with a continuation.
/// - `makeOkPromise` creates a promise that immediately returns a value.
/// - `makeErrPromise` creates a promise that immediately returns an error.
/// - `makeResultPromise` creates a promise that immediately returns a result.
/// - `Future` more conveniently holds a promise or its result.
/// - `PendingTask` wraps a promise as a pending task for execution.
/// - `Waker` a handle to wake up a suspended task.
/// - `Executor` executes a pending task.
///
/// Always look to the future; never look back.
///
/// # Chaining promises using combinators
///
/// `Promise`s can be chained together using combinators such as `then()` which consume the original promise and
/// return a new combined promise.
///
/// For example, the `then()` combinator returns a promise that has the effect of asynchronously awaiting completion
/// of the prior promise (the instance upon which `then()` was called) then delivering its result to a handler
/// function.
///
/// Available combinators:
///
/// - `then()`: Invoked with the result (success or failure) of the previous promise.
/// - `andThen()`: Invoked only on success. Propagates errors automatically.
/// - `orElse()`: Invoked only on error. Propagates values automatically.
/// - `inspect()`: Invoked with the result for side effects, without consuming or modifying the result.
/// - `discard()`: Ignores the result and returns a successful void result.
///
/// # Continuations and handlers
///
/// Promises are built on "continuations". A continuation is a callable object (lambda, functor) that takes a
/// `Context&` and returns a `Result<T, E>`.
///
/// Combinators like `then()` accept "handlers". Handlers are slightly more flexible than raw continuations:
/// - They can optionally accept `Context&` as their first argument.
/// - They accept the *value*, *error*, or *result* of the previous promise, depending on the combinator.
/// - They return a value, a `Result`, or another `Promise`. The system automatically wraps values into `Ok` results.
///
/// # Boxed and unboxed promises
///
/// - **Unboxed (`PromiseImpl<SpecificType>`)**: Created by `makePromise` and combinators. The type of the promise
///   depends on the type of the lambda/functor it holds. This avoids heap allocation and virtual calls (inlineable),
///   but the types can get very complex and long.
/// - **Boxed (`Promise<T, E>`)**: The alias `Promise` uses `std::move_only_function` (via type erasure). This
///   provides a uniform type signature `Result<T, E>(Context&)` regardless of the underlying implementation.
///   Boxing (via `.box()`) incurs a heap allocation and virtual dispatch but makes it easier to store promises in
///   containers or return them from functions.
///
/// # Single ownership model
///
/// `PromiseImpl` is a move-only type. It cannot be copied. This enforces a single ownership model where a promise
/// represents a unique task. When you attach a continuation via `then()`, the original promise is *moved* into the
/// new promise. This ensures that a promise is consumed exactly once.
///
/// # Threading model
///
/// `Promise`s themselves are not thread-safe. They are designed to be driven by an `Executor` (via `Context`).
/// - **Execution**: The `operator()(Context&)` method drives the promise. This should typically be called by the
///   executor, potentially on different threads over time, but never concurrently on the same promise instance.
/// - **Synchronization**: Since promises are single-owner and move-only, data races are naturally minimized. The
///   executor is responsible for memory visibility when scheduling the task on different threads.
///
/// # Result retention and Result
///
/// A `Promise` computes a `Result`. Once computed, the `Promise` is effectively "used up".
/// The `Future` class serves as a container that holds either the pending `Promise` or the completed `Result`.
/// - When the promise completes, the `Future` transitions from `Pending` to `Ready`.
/// - The `Result` is stored inside the `Future`.
/// - You can extract the value/error using `takeValue()`/`takeError()`, which moves the data out and leaves the
///   `Future` in an `Empty` state. Results are not retained indefinitely unless you store them.
///
/// # Clarification of nomenclature
///
/// - **Promise**: In this library, `Promise` (specifically `PromiseImpl`) represents the *computation* or the *task*
///   itself (the "producer" logic). This is slightly different from `std::promise` (which is a setter) and closer
///   to the Concept of a Task or a lazy Future.
/// - **Future**: `Future` (specifically `FutureImpl`) is the *state container* (holder). It manages the state
///   transitions (Pending -> Ready) and storage.
/// - **Executor**: The entity responsible for calling the promise's continuation and scheduling suspensions.
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
    PromiseImpl(PromiseImpl&& rhs) noexcept : cont_(std::move(rhs.cont_)) {
        rhs.cont_.reset();
    }
    PromiseImpl& operator=(PromiseImpl&& rhs) noexcept {
        cont_ = std::move(rhs.cont_);
        rhs.cont_.reset();
        return *this;
    }

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

    /// Swaps the promise's content with another.
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
    /// The promise must be non-empty (checked by assert).
    ResultType operator()(Context& ctx) {
        assert(cont_.has_value());
        auto result = (*cont_)(ctx);
        if (result) {
            cont_.reset();
        }
        return result;
    }

    /// Takes the promise's continuation, leaving it in an empty state.
    ///
    /// The promise must be non-empty (checked by assert).
    C takeContinuation() {
        assert(cont_.has_value());
        auto c = std::move(*cont_);
        cont_.reset();
        return c;
    }

    /// Returns a new promise that invokes the specified handler after this promise completes successfully.
    ///
    /// The handler receives the result of this promise.
    ///
    /// @code
    /// makeOkPromise(1).then([](const Result<int>& result) {
    ///     return Ok(result.value() + 1);
    /// });
    /// @endcode
    template <typename ResultHandler>
    auto then(ResultHandler handler) {
        return withContinuation(
            internal::ThenContinuation<PromiseImpl, ResultHandler>(std::move(*this), std::move(handler)));
    }

    /// Returns a new promise that invokes the specified handler after this promise completes successfully with a value.
    ///
    /// The handler receives the value of this promise. If this promise fails, the error is propagated
    /// and the handler is not invoked.
    ///
    /// @code
    /// makeOkPromise(1).andThen([](const int& value) {
    ///     return Ok(value + 1);
    /// });
    /// @endcode
    template <typename ValueHandler>
    auto andThen(ValueHandler handler) {
        return withContinuation(
            internal::AndThenContinuation<PromiseImpl, ValueHandler>(std::move(*this), std::move(handler)));
    }

    /// Returns a new promise that invokes the specified handler after this promise completes with an error.
    ///
    /// The handler receives the error of this promise. If this promise succeeds, the value is propagated
    /// and the handler is not invoked.
    ///
    /// @code
    /// makeErrPromise(404).orElse([](const int& error) {
    ///     return Ok(0); // Recover from error
    /// });
    /// @endcode
    template <typename ErrorHandler>
    auto orElse(ErrorHandler handler) {
        return withContinuation(
            internal::OrElseContinuation<PromiseImpl, ErrorHandler>(std::move(*this), std::move(handler)));
    }

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

    /// Wraps the promise using the provided wrapper.
    ///
    /// The wrapper must provide a `wrap(PromiseImpl, Args...)` method.
    template <typename Wrapper, typename... Args>
    auto wrap(Wrapper& wrapper, Args&&... args) {
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
    // Helper function for creating other PromiseImpl<> types.
    template <Continuation Other>
    auto withContinuation(Other c) {
        return PromiseImpl<Other>{std::move(c)};
    }

    std::optional<C> cont_;
};

template <Continuation C>
inline void swap(PromiseImpl<C>& lhs, PromiseImpl<C>& rhs) noexcept {
    lhs.swap(rhs);
}

template <typename Handler>
inline auto makePromise(Handler handler) {
    return PromiseImpl{internal::PromiseContinuation<Handler>(std::move(handler))};
}

/// Creates a promise that immediately returns a result.
template <typename T = void, typename E = void>
inline auto makeResultPromise(Result<T, E> result) {
    return PromiseImpl{internal::ResultContinuation<T, E>(std::move(result))};
}

template <typename T = void, typename E = void>
inline auto makeResultPromise(Ok<T> result) {
    return PromiseImpl{internal::ResultContinuation<T, E>(std::move(result))};
}

template <typename T = void, typename E = void>
inline auto makeResultPromise(Err<E> result) {
    return PromiseImpl{internal::ResultContinuation<T, E>(std::move(result))};
}

template <typename T = void, typename E = void>
inline auto makeResultPromise(Pending result) {
    return PromiseImpl{internal::ResultContinuation<T, E>(std::move(result))};
}

/// Creates a promise that immediately returns a value.
template <typename T>
inline auto makeOkPromise(T value) {
    return makeResultPromise<T, void>(Ok(std::move(value)));
}

inline auto makeOkPromise() {
    return makeResultPromise<void, void>(Ok());
}

/// Creates a promise that immediately returns an error.
template <typename E>
inline auto makeErrPromise(E error) {
    return makeResultPromise<void, E>(Err(std::move(error)));
}

inline auto makeErrPromise() {
    return makeResultPromise<void, void>(Err());
}

/// Creates a promise that joins multiple promises.
///
/// The resulting promise completes when all input promises have completed successfully.
/// The result is a tuple containing the results of each input promise in order.
template <typename... Ps>
auto joinPromises(Ps... promises) {
    return PromiseImpl{internal::JoinContinuation<Ps...>(std::move(promises)...)};
}

/// Creates a promise that joins a vector of promises.
///
/// The resulting promise completes when all input promises have completed successfully.
/// The result is a vector containing the results of each input promise in order.
template <typename T, typename E>
auto joinPromises(std::vector<Promise<T, E>> promises) {
    return PromiseImpl{internal::JoinVectorContinuation<Promise<T, E>>(std::move(promises))};
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
    [[nodiscard]] bool operator()(Context& ctx) {
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

/// # Synopsis
///
/// `PendingTask` is a type-erased container for a top-level asynchronous task (a `Promise<void, void>`) that is
/// ready to be executed by an `Executor`.
///
/// Unlike `Promise` or `Future`, which are designed for composition and chaining, `PendingTask` is the "end of the
/// line" for a promise chain. It wraps the promise in a form that the executor can store, schedule, and invoke
/// uniformly without knowing the specific type of the underlying promise.
///
/// # Lifecycle
///
/// 1. **Creation**: A `PendingTask` is typically created from a `Promise` (or `Future`'s promise) that has been
///    finalized. The promise usually should have a result type of `void` (or the result is discarded).
/// 2. **Scheduling**: The `PendingTask` is passed to an `Executor` (e.g., via `Executor::schedule()`).
/// 3. **Execution**: The executor calls `operator()(Context&)` to drive the task.
///    - Returns `false`: Task is still pending (suspended). Executor should reschedule it when notified by its `Waker`.
///    - Returns `true`: Task completed. The `PendingTask` becomes empty.
///
/// # Thread Safety
///
/// `PendingTask` is a move-only type with single ownership. It is not thread-safe. The executor must ensure that
/// a single `PendingTask` instance is not accessed concurrently. However, it can be moved between threads (e.g.,
/// scheduled on a thread pool).
///
/// # Example
///
/// @code
/// hcomm::Promise<> my_task = hcomm::makePromise([](hcomm::Context& ctx) { ... });
/// executor.schedule(hcomm::PendingTask(std::move(my_task)));
/// @endcode
class PendingTask final {
public:
    PendingTask() = default;

    PendingTask(Promise<> promise) : promise_(std::move(promise)) {}

    template <Continuation C>
    PendingTask(PromiseImpl<C> promise) : promise_(promise.discard().box()) {}

    PendingTask(PendingTask&& rhs) noexcept = default;
    PendingTask& operator=(PendingTask&& rhs) noexcept = default;

    /// Returns if the pending task is non-empty (has a valid promise).
    explicit operator bool() const {
        return static_cast<bool>(promise_);
    }

    /// Evaluates the pending task. If the task completes (returning true), the task (actually promise) transitions to
    /// the empty state. Calling an empty task is undefined.
    [[nodiscard]] bool operator()(Context& ctx) {
        return static_cast<bool>(promise_(ctx));
    }

private:
    Promise<> promise_;
};

/// # Synopsis
///
/// `Context` acts as the execution context for a running `Promise`. It is passed as an argument to every
/// continuation invoked by the `Executor`.
///
/// The context serves two primary purposes:
/// 1. **Access to the Environment**: It provides access to the `Executor` managing the task.
/// 2. **Control Flow Management**: It provides mechanisms for the task to voluntarily suspend itself if it needs
///    to wait for an external event (like I/O or a timer) before it can complete.
///
/// # Role in the System
///
/// In the `hcomm` promise model, tasks are cooperative. They run until they either complete (return a Result) or
/// yield control (return `Pending`).
///
/// When a task needs to yield (return `Pending`), it usually needs to arrange for itself to be woken up later.
/// The `waker()` method is the mechanism to obtain a `Waker` handle, which can then be stored in an
/// event source (e.g., a reactor) to reschedule the task when ready.
class Context {
public:
    virtual ~Context() = default;

    /// Returns the executor that is currently driving this task.
    virtual Executor* executor() = 0;

    /// Creates a handle to the currently executing task that allows it to be resumed later.
    ///
    /// This is typically called just before returning `Pending` from a continuation. The returned `Waker`
    /// should be stored in a location (like an I/O reactor or timer wheel) that will trigger it when the
    /// waiting condition is met.
    virtual Waker waker() = 0;
};

/// # Synopsis
///
/// An `Executor` is responsible for scheduling and executing `PendingTask`s. It abstracts the underlying execution
/// strategy, which could be anything from a simple single-threaded loop to a complex thread pool or an event-driven
/// reactor.
///
/// The executor acts as the engine of the asynchronous system. Promises define *what* needs to be done, and the
/// executor determines *when* and *where* it is done.
///
/// # Key Responsibilities
///
/// 1.  **Scheduling**: Accepting tasks via `schedule(PendingTask task)` and storing them for execution.
/// 2.  **Execution**: Driving tasks to completion by invoking `task(context)`. If a task suspends (returns `false`),
///     the executor must ensure it is resumed later when it becomes ready (usually notified via `Waker`).
/// 3.  **Context Provision**: Providing a `Context` to running tasks, which gives them access to the executor itself
///     and mechanisms to suspend/resume.
///
/// # Thread Safety
///
/// - `schedule()`: Must be thread-safe. It can be called from any thread, including from within a running task
///   (e.g., when a new task is spawned) or from an external event source (e.g., an IO completion handler).
/// - **Execution**: The executor's run loop might be single-threaded or multi-threaded. It is the executor's
///   responsibility to ensure memory visibility and avoid data races when scheduling tasks across threads.
///
/// # Lifecycle
///
/// An executor typically exists for the lifetime of the application or a specific subsystem. When the executor is
/// destroyed, any pending tasks that have not completed are also destroyed (cancelled).
///
/// # Example (Concept)
///
/// @code
/// class SimpleExecutor : public hcomm::Executor {
/// public:
///     void schedule(hcomm::PendingTask task) override {
///         tasks_.push_back(std::move(task));
///     }
///
///     void run() {
///         while (!tasks_.empty()) {
///             auto task = std::move(tasks_.front());
///             tasks_.pop_front();
///             if (!task(ctx_)) {
///                 // Task suspended, should be rescheduled when ready...
///                 // For simplicity, we might just drop it or re-queue it here.
///             }
///         }
///     }
///     // ...
/// };
/// @endcode
class Executor {
public:
    /// Destroys the executor along with all of its remaining scheduled tasks that have yet to complete.
    virtual ~Executor() = default;

    /// Schedules a task for eventual execution by the executor. This method is thread-safe.
    virtual void schedule(PendingTask task) = 0;
};

/// Users should inherit WakerImpl to define their own Waker.
class WakerImpl : public RefCounted<WakerImpl> {
public:
    virtual ~WakerImpl() = default;
    virtual void wake() = 0;
};

/// # Synopsis
///
/// `Waker` is a lightweight, thread-safe handle to a suspended task. It allows an external event source (like an
/// I/O reactor or a timer) to notify the `Executor` that the task is ready to make progress and should be
/// rescheduled.
///
/// A `Waker` can be obtained from the `Context` using `context.waker()`.
class Waker final {
public:
    Waker() = default;
    explicit Waker(RefPtr<WakerImpl> impl) : impl_(std::move(impl)) {}

    void wake() const {
        if (impl_) {
            impl_->wake();
        }
    }

private:
    RefPtr<WakerImpl> impl_;
};
} // namespace hcomm

#endif // HCOMM_PROMISE_PROMISE_HPP_
