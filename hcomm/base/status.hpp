// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_STATUS_HPP_
#define HCOMM_BASE_STATUS_HPP_

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <exception>
#include <string>

namespace hcomm {
/// A `StatusCode` is an enumerated type indicating either no error ("OK") or an
/// error condition. In most cases, a `Status` indicates a recoverable error.
///
/// The errors listed below are the canonical errors associated with `Status`
/// and are used throughout codebase. In general, try to return the most
/// specific error that applies if more than one error may pertain. e.g., prefer
/// `Cancelled` over `Error` with "Cancelled xxx" message.
///
/// If certain errors occur frequently, please add a dedicated code for them.
enum class StatusCode : int {
    Ok = 0,
    Error,
    Cancelled,
    OutOfMemory,
    OutOfRange,
    IOError,
    InvalidArgument,
    NotSupported,
    Internal,
    Unknown,
};

namespace internal {
class StatusRep {
public:
    StatusRep(StatusCode code, std::string msg) : refcount_(1), code_(code), message_(std::move(msg)) {}

    StatusCode code() const {
        return code_;
    }

    const std::string& message() const {
        return message_;
    }

    void ref() const {
        refcount_.fetch_add(1, std::memory_order_relaxed);
    }

    void unref() const {
        if (refcount_.load(std::memory_order_acquire) == 1 ||
            refcount_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0) {
            delete this;
        }
    }

private:
    mutable std::atomic<int> refcount_;
    StatusCode code_;
    std::string message_;
};
} // namespace internal

/// `hcomm::Status`
///
/// The `hcomm::Status` class is designed for graceful error handling across API
/// boundaries. It can represent both recoverable and non-recoverable errors.
/// Functions that may produce recoverable errors should typically return either
/// a `Status` or a `StatusOr<T>` (which contains either a value of type `T` or
/// an error status).
///
/// Example usage:
///
/// ```c++
/// Status result = parseString();
/// if (result.ok()) {
///     // Processing successful
/// } else {
///     // Handle error
///     std::cout << result.message() << "\n";
/// }
/// ```
///
/// When handling status codes, prefer checking for success using the `ok()`
/// method. For handling multiple error conditions, a switch statement can be
/// used, but only match against error codes you know how to handle, avoid
/// exhaustive matching of all canonical error codes. Errors that cannot be
/// handled should be logged and propagated to higher levels.
///
/// Example:
///
/// ```c++
/// Status result = CallMethod();
/// if (!result.ok()) {
///     LOG(ERROR) << result;
/// }
///
/// switch (result.code()) {
/// case StatusCode::InvalidArgument:
///     // Handle specific error
///     break;
///
/// default:
///     // Propagate unhandled errors
///     return result;
/// }
/// ```
///
/// Since the underlying implementation of `Status` uses reference counting,
/// copying is inexpensive. Note that move constructor/assignment is not
/// provided.
///
/// Example:
///
/// ```c++
/// Status result1{Unknown, "unknown error"};
///
/// Status result2 = std::move(result1); // Behaves as if std::move was not used
/// EXPECT_FALSE(result2.ok());
/// EXPECT_EQ(result2.code(), Unknown);
/// ```
///
/// See also: `StatusOr<T>`.
class [[nodiscard]] Status final {
public:
    /// Create an ok `Status`.
    explicit Status() : Status(StatusCode::Ok) {}

    /// Create a `Status` with no message.
    explicit Status(StatusCode code) : rep_(toInlined(code)) {}

    /// Create a `Status` with specified `code` and `msg`. If `code` is
    /// `StatusCode::Ok`, the msg is ignored.
    Status(StatusCode code, std::string msg);

    Status(const Status& rhs);
    Status& operator=(const Status& rhs);

    ~Status();

    /// Returns true if it's ok.
    bool ok() const {
        return code() == StatusCode::Ok;
    }

    /// Returns the canonical error code of type `StatusCode` of this status.
    StatusCode code() const;

    /// Returns the message associated with the error code, if available.
    std::string_view message() const {
        return isInlined(rep_) ? std::string_view() : ptr(rep_)->message();
    }

    /// Swap the contents of one status with another.
    friend void swap(Status& lhs, Status& rhs) noexcept {
        std::swap(lhs.rep_, rhs.rep_);
    }

private:
    const static internal::StatusRep* ptr(std::uintptr_t rep) {
        return reinterpret_cast<const internal::StatusRep*>(rep);
    }

    static std::uintptr_t toInlined(StatusCode code) {
        return (static_cast<std::uintptr_t>(code) << 1) | 1;
    }

    static StatusCode toCode(std::uintptr_t rep) {
        return static_cast<StatusCode>(rep >> 1);
    }

    /// Returns true if rep is in inlined representation.
    static bool isInlined(std::uintptr_t rep) {
        return (rep & 1) != 0;
    }

    /// Returns a raw error code.
    int rawCode() const {
        return static_cast<int>(isInlined(rep_) ? toCode(rep_) : ptr(rep_)->code());
    }

    /// `Status` supports two different representations:
    /// - inlined. There is no message, code can be fetched via `rep_ >> 1`.
    /// - general. `rep_` is a pointer to `internal::StatusRep`.
    std::uintptr_t rep_;
};

class BadStatusOrAccess : public std::exception {
public:
    const char* what() const noexcept override {
        return "Try to access a StatusOr<T> in bad state.";
    }
};

/// `hcomm::StatusOr<T>`
///
/// The `hcomm::StatusOr<T>` class template represents a union of an
/// `hcomm::Status` object and a value of type `T`. It models an entity that is
/// either a valid object of type `T` or an error (represented by
/// `hcomm::Status`) explaining why such an object is unavailable. This type is
/// commonly used as the return value for functions that may fail.
///
/// An `hcomm::StatusOr<T>` can never contain an "OK" status (i.e.,
/// `hcomm::StatusCode::Ok`). Instead, the presence of a value of type `T`
/// indicates success. For this reason and for better code clarity, using the
/// `ok()` member function is recommended (even for `hcomm::Status` itself) over
/// explicitly checking for `Ok`.
///
/// Example:
///
/// ```c++
/// StatusOr<Foo> result = DoBigCalculationThatCouldFail();
/// if (result.ok()) {
///     result->DoSomethingCool();
/// } else {
///     LOG(ERROR) << result.status();
/// }
/// ```
///
/// To access the contained object in an `hcomm::StatusOr<T>`, use `operator*`
/// or `operator->` only after verifying that a value exists via the `ok()`
/// method.
///
/// Example:
///
/// ```c++
/// hcomm::StatusOr<int> i = GetCount();
/// if (i.ok()) {
///     updated_total += *i;
/// }
/// ```
///
/// ⚠️ Note: Invoking `hcomm::StatusOr<T>::value()` when no value is present will
/// result in an exception.
///
/// Example:
///
/// ```c++
/// StatusOr<Foo> result = DoBigCalculationThatCouldFail();
/// const Foo& foo = result.value();  // Throws an exception if no value exists
/// foo.DoSomethingCool();
/// ```
///
/// A `hcomm::StatusOr<T*>` can be constructed from a null pointer. In such
/// cases, `ok()` returns `true` and `value()` returns `nullptr`. Special care
/// should be taken when working with pointer types to ensure that a value
/// exists and is non-null:
///
/// ```c++
/// StatusOr<std::unique_ptr<Foo>> result = FooFactory::MakeNewFoo(arg);
/// if (!result.ok()) {
///     LOG(ERROR) << result.status();
/// } else if (*result == nullptr) {
///     LOG(ERROR) << "Unexpected null pointer";
/// } else {
///     (*result)->DoSomethingCool();
/// }
/// ```
///
/// Example factory implementation returning `StatusOr<T>`:
///
/// ```c++
/// StatusOr<Foo> FooFactory::MakeFoo(int arg) {
///     if (arg <= 0) {
///         return hcomm::Status(hcomm::StatusCode::InvalidArgument, "Arg must be positive");
///     }
///     return Foo(arg);
/// }
/// ```
template <typename T>
class [[nodiscard]] StatusOr final {
    static_assert(!std::is_same_v<T, Status>, "T cannot be of type Status.");
    static_assert(!std::is_reference_v<T>, "T cannot be reference, use pointer instead.");

    template <typename U>
    friend class StatusOr;

public:
    /// Constructs a `StatusOr<T>` with `StatusCode::kUnknown` status. So that
    /// `std::vector<StatusOr<T>>` can be resize-able.
    explicit StatusOr() : status_(StatusCode::Unknown) {}

    /// Constructs the inner value `T` in-place with the given args.
    template <typename... Args>
        requires std::constructible_from<T, Args...>
    explicit StatusOr(std::in_place_t, Args&&... args) {
        makeValue(std::forward<Args>(args)...);
    }

    /// Constructs the inner value `T` in-place with the given `U`.
    template <typename U = T>
        requires std::constructible_from<T, U>
    StatusOr(U&& u) : StatusOr(std::in_place, std::forward<U>(u)) {}

    /// Constructs a `StatusOr<T>` with a non-ok status. After calling this
    /// ctor, `this->ok()` will be `false` and calls to `this->value()` will
    /// throw an exception.
    template <typename U = Status>
        requires std::constructible_from<Status, U>
    StatusOr(U&& u) : status_(std::forward<U>(u)) {
        // Users should not use an ok status to construct StatusOr.
        assert(!ok());
    }

    /// Constructs a `StatusOr<T>` from `StatusOr<T>`.
    StatusOr(const StatusOr& rhs)
        requires std::copy_constructible<T>
        : status_(rhs.status_) {
        if (ok()) {
            makeValue(rhs.value_);
        }
    }

    /// Constructs a `StatusOr<T>` from `StatusOr<T>&&`.
    StatusOr(StatusOr&& rhs) noexcept
        requires std::move_constructible<T>
        : status_(rhs.status_) {
        if (ok()) {
            makeValue(std::move(rhs.value_));
        }
    }

    /// Constructs a new `StatusOr<T>` from a `StatusOr<U>` where `T` can be
    /// constructed from `U`.
    template <typename U>
        requires std::constructible_from<T, U>
    StatusOr(const StatusOr<U>& rhs) : status_(rhs.status_) {
        if (ok()) {
            makeValue(rhs.value_);
        }
    }

    template <typename U>
        requires std::constructible_from<T, U&&>
    StatusOr(StatusOr<U>&& rhs) noexcept : status_(rhs.status_) {
        if (ok()) {
            makeValue(std::move(rhs.value_));
        }
    }

    ~StatusOr() {
        clear();
    }

    /// Creates a `Status<T>` through assignment from a `StatusOr<T>`.
    StatusOr& operator=(const StatusOr& rhs)
        requires std::assignable_from<T&, T> && std::copy_constructible<T>
    {
        assign(rhs);
        return *this;
    }

    /// Creates a `Status<T>` through assignment from a `StatusOr<T>&&`.
    StatusOr& operator=(StatusOr&& rhs) noexcept
        requires std::assignable_from<T&, T&&> && std::move_constructible<T>
    {
        assign(std::move(rhs));
        return *this;
    }

    /// Creates a `StatusOr<T>` through assignment from a `StatusOr<U>` when:
    ///
    /// - Both `StatusOr<T>` and `StatusOr<U>` are ok by assigning `U` to `T` directly
    /// - `StatusOr<T>` is ok and `StatusOr<U>` contains an error by destroying
    ///   `StatusOr<T>`'s value first and assigning `StatusOr<U>`'s status to
    ///   `StatusOr<T>`'s status directly.
    /// - `StatusOr<T>` contains an error and `StatusOr<T>` is ok by reset
    ///   status to Ok and constructing `T` from `U` directly
    /// - Both `StatusOr<T>` and `StatusOr<U>` contain an error by assigning the
    ///   `Status` in `StatusOr<U> to `StatusOr<T>`
    template <typename U>
        requires std::assignable_from<T&, U> && std::constructible_from<T, U>
    StatusOr<T>& operator=(const StatusOr<U>& rhs) {
        assign(rhs);
        return *this;
    }

    template <typename U>
        requires std::assignable_from<T&, U&&> && std::constructible_from<T, U&&>
    StatusOr<T>& operator=(StatusOr<U>&& rhs) noexcept {
        assign(std::move(rhs));
        return *this;
    }

    /// Returns whether or not this `StatusOr<T>` holds a `T` value. This method
    /// is analogous to `Status::ok()` and should be used similarly to check the
    /// state of return values.
    ///
    /// ```c++
    /// StatusOr<Foo> result = DoBigCalculationThatCouldFail();
    /// if (result.ok()) {
    ///     result->bar();
    /// } else {
    ///     // Handle error
    /// }
    /// ```
    bool ok() const {
        return status_.ok();
    }

    /// Returns a reference to the current `Status` contained with the
    /// `StatusOr<T>`. If `StatusOr<T>` contains a `T`, then the result must be
    /// ok.
    const Status& status() const& {
        return status_;
    }
    Status status() && {
        return ok() ? Status() : std::move(status_);
    }

    /// Returns a reference to the held value if `ok()`. Otherwise, throws
    /// `BadStatusOrAccess`.
    ///
    /// If you have already checked the status using `ok()`, you probably want
    /// to use `operator*()` or `operator->()` to access the value instead of
    /// `value()`.
    T& value() & {
        return ok() ? value_ : throw BadStatusOrAccess();
    }

    const T& value() const& {
        return ok() ? value_ : throw BadStatusOrAccess();
    }

    T&& value() && {
        return ok() ? std::move(value_) : throw BadStatusOrAccess();
    }

    const T&& value() const&& {
        return ok() ? std::move(value_) : throw BadStatusOrAccess();
    }

    /// Returns a reference to the current value.
    ///
    /// NOTE: Must be `ok()` checked before, otherwise the behavior is undefined.
    T& operator*() {
        return value_;
    }

    const T& operator*() const {
        return value_;
    }

    /// Returns a pointer to the current value.
    ///
    /// NOTE: Must be `ok()` checked before, otherwise the behavior is undefined.
    T* operator->() {
        return &value_;
    }

    const T* operator->() const {
        return &value_;
    }

    /// Returns the current value if `ok()`. Otherwise constructs a value using
    /// the given `def`.
    ///
    /// Unlike `value()`, the method returns by value, copying the current value
    /// if necessary.
    template <typename U>
        requires std::convertible_to<U, T>
    T valueOr(U&& def) const& {
        return ok() ? value_ : static_cast<T>(std::forward<U>(def));
    }

    template <typename U>
        requires std::convertible_to<U, T>
    T valueOr(U&& def) && {
        return ok() ? std::move(value_) : static_cast<T>(std::forward<U>(def));
    }

    /// Reconstructs the inner value `T` in-place using the provided args, using
    /// the T(args...) constructor. Returns reference to the reconstructed `T`.
    template <typename... Args>
    T& emplace(Args&&... args) {
        if (ok()) {
            clear();
            makeValue(std::forward<Args>(args)...);
        } else {
            status_ = Status();
            makeValue(std::forward<Args>(args)...);
        }
        return value_;
    }

private:
    void clear() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            if (ok()) {
                value_.~T();
            }
        }
    }

    template <typename... Args>
    void makeValue(Args&&... args) {
        new (&value_) T(std::forward<Args>(args)...);
    }

    template <typename... Args>
    void makeStatus(Args&&... args) {
        new (&status_) Status(std::forward<Args>(args)...);
    }

    template <typename U>
    void assign(const StatusOr<U>& rhs) {
        if (ok()) {
            if (rhs.ok()) {
                value_ = rhs.value_;
            } else {
                clear();
                status_ = rhs.status_;
            }
        } else {
            if (rhs.ok()) {
                status_ = Status();
                makeValue(rhs.value_);
            } else {
                status_ = rhs.status_;
            }
        }
    }

    template <typename U>
    void assign(StatusOr<U>&& rhs) {
        if (ok()) {
            if (rhs.ok()) {
                value_ = std::move(rhs.value_);
            } else {
                clear();
                status_ = rhs.status_;
            }
        } else {
            if (rhs.ok()) {
                status_ = Status();
                makeValue(std::move(rhs.value_));
            } else {
                status_ = rhs.status_;
            }
        }
    }

    Status status_;
    union {
        std::remove_cv_t<T> value_;
    };
};
} // namespace hcomm

#endif
