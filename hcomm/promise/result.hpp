// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_PROMISE_RESULT_HPP_
#define HCOMM_PROMISE_RESULT_HPP_

#include <concepts>
#include <cstdint>
#include <utility>
#include <variant>

namespace hcomm {
/// The pending variant of `Result`.
struct Pending {};

/// The ok variant of `Result`.
template <typename T>
struct Ok {
    using ValueType = T;

    Ok(const T& x) : value(x) {}
    Ok(T&& x) noexcept : value(std::move(x)) {}

    T value;
};

template <>
struct Ok<void> {
    using ValueType = void;
};

Ok() -> Ok<void>;

template <typename E>
struct Err {
    using ErrorType = E;

    Err(const E& err) : error(err) {}
    Err(E&& err) noexcept : error(std::move(err)) {}

    E error;
};

template <>
struct Err<void> {
    using ErrorType = void;
};

Err() -> Err<void>;

/// The state of `Result`.
enum class ResultState : std::uint8_t {
    /// The task is still in progress.
    Pending = 0,
    /// The task completes successfully.
    Ok = 1,
    /// The task failed.
    Error = 2,
};

/// `Result` is a vocabulary type representing the outcome of an operation that may succeed, fail, or be pending.
/// It serves as a sophisticated alternative to error codes or exceptions, enabling robust error handling and
/// state propagation.
///
/// Inspiration & Origin:
/// This class is heavily inspired by Rust's `Result` enum and the C++23 `std::expected` utility. It extends
/// these concepts by introducing a `Pending` state, making it particularly suitable for asynchronous programming,
/// state machines, or scenarios where an operation's completion is deferred.
///
/// Key Variants:
/// - `Ok`: Represents a successful computation and holds the resulting value.
/// - `Err`: Represents a failure and holds the error details.
/// - `Pending`: Represents an incomplete operation (e.g., waiting for I/O).
///
/// Functional Composition:
/// `Result` supports monadic operations such as `map`, `andThen`, and `orElse`, allowing for clean and expressive
/// chaining of operations without explicit control flow checks at every step.
template <typename T = void, typename E = void>
class Result final {
    static_assert(!std::is_reference_v<T>, "Result cannot be used with reference type");
    static_assert(!std::is_reference_v<E>, "Result cannot be used with reference type");

public:
    using ValueType = T;
    using ErrorType = E;

    /// Constructs an `Pending` variant of `Result`.
    Result() = default;
    Result(Pending) {}

    /// Constructs from `Ok` variant.
    Result(const Ok<T>& rhs) : state_(rhs) {}
    Result(Ok<T>&& rhs) noexcept : state_(std::move(rhs)) {}

    template <typename U>
        requires std::constructible_from<T, U>
    Result(const Ok<U>& rhs) : state_(std::in_place_index<1>, T(rhs.value)) {}
    template <typename U>
        requires std::constructible_from<T, U&&>
    Result(Ok<U>&& rhs) noexcept : state_(std::in_place_index<1>, T(std::move(rhs.value))) {}

    /// Constructs from `Err` variant.
    Result(const Err<E>& rhs) : state_(rhs) {}
    Result(Err<E>&& rhs) noexcept : state_(std::move(rhs)) {}

    template <typename U>
        requires std::constructible_from<E, U>
    Result(const Err<U>& rhs) : state_(std::in_place_index<2>, E(rhs.error)) {}
    template <typename U>
        requires std::constructible_from<E, U&&>
    Result(Err<U>&& rhs) noexcept : state_(std::in_place_index<2>, E(std::move(rhs.error))) {}

    /// Copies/Assigns if copyable/assignable.
    Result(const Result& rhs) = default;
    Result& operator=(const Result& rhs) = default;

    /// Moves/Assigns from another `Result`, leaving it in pending state.
    Result(Result&& rhs) noexcept : state_(std::move(rhs.state_)) {
        rhs.reset();
    }
    Result& operator=(Result&& rhs) noexcept {
        state_ = std::move(rhs.state_);
        rhs.reset();
        return *this;
    }

    ~Result() = default;

    void swap(Result& rhs) noexcept {
        std::swap(state_, rhs.state_);
    }

    [[nodiscard]] bool operator==(const Result& rhs) const {
        if (state() != rhs.state()) {
            return false;
        }

        switch (state()) {
        case ResultState::Pending:
            return true;

        case ResultState::Ok:
            if constexpr (std::is_void_v<T>) {
                return true;
            } else {
                return std::get<1>(state_).value == std::get<1>(rhs.state_).value;
            }

        case ResultState::Error:
            if constexpr (std::is_void_v<E>) {
                return true;
            } else {
                return std::get<2>(state_).error == std::get<2>(rhs.state_).error;
            }
        }

        std::unreachable();
    }

    [[nodiscard]] ResultState state() const {
        return static_cast<ResultState>(state_.index());
    }

    [[nodiscard]] bool isPending() const {
        return state() == ResultState::Pending;
    }

    [[nodiscard]] bool isOk() const {
        return state() == ResultState::Ok;
    }

    [[nodiscard]] bool isErr() const {
        return state() == ResultState::Error;
    }

    /// Accesses the contained value.
    ///
    /// Requires the `Result` to be in the `Ok` state.
    ///
    /// Example:
    /// @code
    /// Result<int> res = Ok(42);
    /// int v = res.value();
    /// @endcode
    template <typename Self>
        requires(!std::is_void_v<T>)
    decltype(auto) value(this Self&& self) {
        return std::forward_like<Self>(std::get<1>(self.state_).value);
    }

    /// Returns the contained value if `Ok`, otherwise returns `default_value`.
    ///
    /// Example:
    /// @code
    /// Result<int> res = Pending{};
    /// int v = res.valueOr(10); // v is 10
    /// @endcode
    template <typename Self, typename U>
        requires(!std::is_void_v<T>)
    T valueOr(this Self&& self, U&& default_value) {
        if (self.isOk()) {
            return std::forward_like<Self>(std::get<1>(self.state_).value);
        } else {
            return std::forward<U>(default_value);
        }
    }

    /// Extracts the value from the `Result`, leaving the `Result` in a `Pending` state.
    ///
    /// This method is useful when you need to move the value out of the `Result` without destroying the `Result` object
    /// immediately. Requires the `Result` to be in the `Ok` state.
    T takeValue()
        requires(!std::is_void_v<T>)
    {
        auto x = std::move(std::get<1>(state_).value);
        reset();
        return x;
    }

    /// Accesses the contained error.
    ///
    /// Requires the `Result` to be in the `Err` state.
    template <typename Self>
        requires(!std::is_void_v<E>)
    decltype(auto) error(this Self&& self) {
        return std::forward_like<Self>(std::get<2>(self.state_).error);
    }

    /// Extracts the error from the `Result`, leaving the `Result` in a `Pending` state.
    ///
    /// This method is useful when you need to move the error out of the `Result`.
    /// Requires the `Result` to be in the `Err` state.
    E takeError()
        requires(!std::is_void_v<E>)
    {
        auto x = std::move(std::get<2>(state_).error);
        reset();
        return x;
    }

    /// Transforms the contained value via a function `f` if the result is `Ok`.
    ///
    /// If the result is `Pending` or `Err`, the function returns the original state unchanged.
    ///
    /// Example:
    /// @code
    /// Result<int, std::string> res = Ok(10);
    /// auto s = res.map([](int i) { return std::to_string(i); });
    /// // s is Ok("10")
    /// @endcode
    template <typename Self, typename F,
              typename U =
                  typename std::conditional_t<std::is_void_v<T>, std::invoke_result<F>, std::invoke_result<F, T>>::type>
    Result<U, E> map(this Self&& self, F&& f) {
        switch (self.state()) {
        case ResultState::Pending:
            return Pending{};

        case ResultState::Ok:
            if constexpr (std::is_void_v<T>) {
                if constexpr (std::is_void_v<U>) {
                    std::forward<F>(f)();
                    return Ok();
                } else {
                    return Ok(std::forward<F>(f)());
                }
            } else {
                if constexpr (std::is_void_v<U>) {
                    std::forward<F>(f)(std::forward_like<Self>(std::get<1>(self.state_).value));
                    return Ok();
                } else {
                    return Ok(std::forward<F>(f)(std::forward_like<Self>(std::get<1>(self.state_).value)));
                }
            }

        case ResultState::Error:
            return std::forward_like<Self>(std::get<2>(self.state_));
        }

        std::unreachable();
    }

    /// Transforms the contained error via a function `f` if the result is `Err`.
    ///
    /// If the result is `Ok` or `Pending`, the function returns the original state unchanged.
    ///
    /// Example:
    /// @code
    /// Result<int, int> res = Err(404);
    /// auto s = res.mapErr([](int e) { return "Error " + std::to_string(e); });
    /// // s is Err("Error 404")
    /// @endcode
    template <typename Self, typename F,
              typename U =
                  typename std::conditional_t<std::is_void_v<E>, std::invoke_result<F>, std::invoke_result<F, E>>::type>
    Result<T, U> mapErr(this Self&& self, F&& f) {
        switch (self.state()) {
        case ResultState::Pending:
            return Pending{};

        case ResultState::Ok:
            return std::forward_like<Self>(std::get<1>(self.state_));

        case ResultState::Error:
            if constexpr (std::is_void_v<E>) {
                if constexpr (std::is_void_v<U>) {
                    std::forward<F>(f)();
                    return Err();
                } else {
                    return Err(std::forward<F>(f)());
                }
            } else {
                if constexpr (std::is_void_v<U>) {
                    std::forward<F>(f)(std::forward_like<Self>(std::get<2>(self.state_).error));
                    return Err();
                } else {
                    return Err(std::forward<F>(f)(std::forward_like<Self>(std::get<2>(self.state_).error)));
                }
            }
        }

        std::unreachable();
    }

    /// Chains a computation that returns a `Result` if the current result is `Ok`.
    ///
    /// This allows for sequencing operations that might fail. If the current result is `Pending` or `Err`,
    /// the function is not executed, and the current state is propagated.
    ///
    /// Example:
    /// @code
    /// Result<int> res = Ok(21);
    /// auto next = res.andThen([](int val) -> Result<int> { return Ok(val * 2); });
    /// // next is Ok(42)
    /// @endcode
    template <typename Self, typename F,
              typename Ret =
                  typename std::conditional_t<std::is_void_v<T>, std::invoke_result<F>, std::invoke_result<F, T>>::type>
        requires std::same_as<typename Ret::ErrorType, E>
    Ret andThen(this Self&& self, F&& f) {
        switch (self.state()) {
        case ResultState::Pending:
            return Pending{};

        case ResultState::Ok:
            if constexpr (std::is_void_v<T>) {
                return std::forward<F>(f)();
            } else {
                return std::forward<F>(f)(std::forward_like<Self>(std::get<1>(self.state_).value));
            }

        case ResultState::Error:
            return std::forward_like<Self>(std::get<2>(self.state_));
        }

        std::unreachable();
    }

    /// Chains a computation that returns a `Result` if the current result is `Err`.
    ///
    /// This is typically used for error recovery. If the current result is `Ok` or `Pending`,
    /// the function is not executed, and the current state is propagated.
    ///
    /// Example:
    /// @code
    /// Result<int, std::string> res = Err("fail");
    /// auto recovered = res.orElse([](const std::string&) -> Result<int, std::string> { return Ok(0); });
    /// // recovered is Ok(0)
    /// @endcode
    template <typename Self, typename F,
              typename Ret =
                  typename std::conditional_t<std::is_void_v<E>, std::invoke_result<F>, std::invoke_result<F, E>>::type>
        requires std::same_as<typename Ret::ValueType, T>
    Ret orElse(this Self&& self, F&& f) {
        switch (self.state()) {
        case ResultState::Pending:
            return Pending{};

        case ResultState::Ok:
            return std::forward_like<Self>(std::get<1>(self.state_));

        case ResultState::Error:
            if constexpr (std::is_void_v<E>) {
                return std::forward<F>(f)();
            } else {
                return std::forward<F>(f)(std::forward_like<Self>(std::get<2>(self.state_).error));
            }
        }

        std::unreachable();
    }

private:
    void reset() {
        state_.template emplace<0>();
    }

    std::variant<std::monostate, Ok<T>, Err<E>> state_;
};

template <typename T, typename E>
void swap(Result<T, E>& lhs, Result<T, E>& rhs) noexcept {
    lhs.swap(rhs);
}

namespace internal {
template <typename T, typename E>
void checkResult(const Result<T, E>& ret);
}

template <typename T>
concept IsResult = requires(T ret) {
    typename T::ValueType;
    typename T::ErrorType;

    internal::checkResult(ret);
};
} // namespace hcomm

#endif // HCOMM_PROMISE_RESULT_HPP_
