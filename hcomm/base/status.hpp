// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_STATUS_HPP_
#define HCOMM_BASE_STATUS_HPP_

#include <atomic>
#include <cstdint>
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

/// The `Status` class is generally used to gracefully handle errors across API
/// boundaries. Some of these errors maybe recoverable, but others may not. Most
/// function which can produce a recoverable error should be designed to return
/// either a `Status` or `StatusOr<T>`, which holds either an object of type `T`
/// or an error.
///
/// ```c++
/// Status result = parseString();
/// if (result.ok()) {
///     // fine
/// } else {
///     // what error?
///     std::cout << result.message() << "\n";
/// }
/// ```
///
/// Users handling status error codes should prefer checking for an Ok status by
/// using the `ok()` method. Handling multiple error codes may use the switch
/// statement, but only check for error codes you know how to handle; do no try
/// to exhaustively match against all canonical error codes. Errors that cannot
/// be handled should be logged and propagated for higher levels to deal with.
///
/// ```c++
/// Status result = CallMetohd();
/// if (!result.ok()) {
///     LOG(ERROR) << result;
/// }
///
/// switch (result.code()) {
/// case StatusCode::InvalidArgument:
///     // Handle it.
///     break;
///
/// default:
///     // Propagate the error otherwise.
///     return result;
/// }
/// ```
///
/// See also `StatusOr<T>`.
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
} // namespace hcomm

#endif
