// SPDX-License-Identifier: MulanPSL-2.0

#ifndef MHCOM_BASE_STATUS_HPP_
#define MHCOM_BASE_STATUS_HPP_

#include <atomic>
#include <cstdint>
#include <string>

namespace mhcom {
///
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

///
class [[nodiscard]] Status final {
public:
    /// Create an Ok `Status` with no message.
    Status() : Status(StatusCode::Ok) {}

    /// Create a `Status` with no message.
    Status(StatusCode code) : rep_(toInlined(code)) {}

    /// Create a `Status` with specified `code` and `msg`.
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
    const std::string& message() const;

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

private:
    /// `Status` supports two different representations:
    /// - inlined. There is no message, code can be fetched via `rep_ >> 1`.
    /// - general. `rep_` is a pointer to `internal::StatusRep`.
    std::uintptr_t rep_;
};
} // namespace mhcom

#endif // MHCOM_BASE_STATUS_HPP_
