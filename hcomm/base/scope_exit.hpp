// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_SCOPE_EXIT_HPP_
#define HCOMM_BASE_SCOPE_EXIT_HPP_

#include <utility>

namespace hcomm {

/// A utility class that guarantees a function is executed upon leaving the current scope. This is commonly known as
/// Resource Acquisition Is Initialization (RAII).
///
/// It's a C++ equivalent of `defer` in Go or `try...finally` in other languages. `ScopeExit` is particularly useful
/// for resource cleanup, ensuring that resources are released, logs are written, or state is finalized, regardless of
/// how the scope is exited (e.g., via a normal return, a `break`, `continue`, `goto`, or an exception).
///
/// Example:
/// ```c++
/// void F() {
///     auto p = new int(1);
///     ScopeExit se([&] { delete p; });
///
///     // ... code that might throw or return early
///
/// } // `delete p` is called here.
/// ```
template <typename F>
class ScopeExit {
public:
    /// Constructs a `ScopeExit` with a callable entity (e.g., a lambda). The provided callable will be invoked when
    /// the `ScopeExit` object is destructed, unless it has been moved from or cancelled.
    ScopeExit(F&& f) : active_(true), callback_(std::move(f)) {}

    /// Move constructor. Transfer ownership of the cleanup action. After moving, the source `ScopeExit` object becomes
    /// inactive and will no longer execute the callback upon its destruction. This allows returning `ScopeExit`
    /// objects from functions.
    ScopeExit(ScopeExit&& other) noexcept : callback_(std::move(other.callback_)), active_(other.active_) {
        other.active_ = false;
    }

    /// Destructor. If the `ScopeExit` is active, it executes the callback.
    ~ScopeExit() {
        if (active_) {
            callback_();
        }
    }

    /// Prevents the callback from being executed upon destruction. This can be used when the cleanup action has been
    /// manually performed or is no longer necessary.
    void cancel() {
        active_ = false;
    }

private:
    bool active_;
    [[no_unique_address]] F callback_;
};
} // namespace hcomm

#endif // HCOMM_BASE_SCOPE_EXIT_HPP_
