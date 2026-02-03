// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_CONTEXT_HPP_
#define HCOMM_TRANSPORT_UB_CONTEXT_HPP_

#include <expected>

#include "hcomm/base/refptr.hpp"
#include "hcomm/transport/ub/device.hpp"
#include "hcomm/transport/ub/error.hpp"

namespace hcomm {
namespace ub {
/// Defines the configuration for creating a URMA context.
struct ContextCreateOptions {
    DeviceInfo dev_info;
};

/// Represents an active communication context for a specific URMA device.
///
/// A `Context` encapsulates a `urma_context_t`, which is the fundamental object for managing URMA resources, such as
/// memory registrations and communication endpoints (jetties). It also stores the attributes of the associated device.
/// This class is reference-counted and should be managed via `RefPtr`. Creation is handled exclusively through the
/// static `create` factory method to ensure proper initialization and error handling.
class Context : public RefCounted<Context> {
    struct PrivateTag {};

public:
    /// Constructs a `Context` instance. This constructor is intentionally made public to be used with `makeRef` but
    /// can only be called with the `PrivateTag`, effectively restricting its direct use and enforcing object creation
    /// through the static `create` method.
    Context(PrivateTag, urma_context_t* urma_ctx, urma_device_attr_t attr) : urma_ctx_(urma_ctx), dev_attr_(attr) {}

    Context(Context&& rhs) noexcept = delete;
    Context& operator=(Context&& rhs) noexcept = delete;

    /// Destructor that cleans up the underlying URMA context.
    /// It ensures that `urma_delete_context` is called to release all associated driver resources.
    ~Context();

    /// Factory method to create and initialize a new `Context`.
    ///
    /// This function handles the multi-step process of identifying a URMA device based on the provided options,
    /// querying its attributes, and creating a URMA context. It returns a `RefPtr` to the new `Context` on success or
    /// an `Error` code if any step in the process fails.
    static std::expected<RefPtr<Context>, Error> create(const ContextCreateOptions& opts);

    /// Retrieves the raw pointer to the underlying `urma_context_t`.
    ///
    /// The lifetime of the pointer is managed by the `Context` object.
    urma_context_t* getContext() {
        return urma_ctx_;
    }

    /// Retrieves a pointer to the device attributes associated with this context.
    ///
    /// The attributes contain detailed information about the URMA device's capabilities, such as maximum message size,
    /// queue depths, and supported features. The lifetime of the pointer is managed by the `Context` object.
    urma_device_attr_t* getDeviceAttr() {
        return &dev_attr_;
    }

private:
    urma_context_t* urma_ctx_;
    urma_device_attr_t dev_attr_;
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_CONTEXT_HPP_
