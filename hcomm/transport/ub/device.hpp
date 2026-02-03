// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_DEVICE_HPP_
#define HCOMM_TRANSPORT_UB_DEVICE_HPP_

#include <variant>

#include <urma_types.h>

namespace hcomm {
namespace ub {
/// Represents a URMA device through its name and the eid_idx. This combination is used to uniquely designate a
/// specific physical or virtual communication device on the system, facilitating device-specific operations.
struct DeviceIndex {
    /// The textual identifier for the device.
    char name[URMA_MAX_NAME];
    /// The eid_idx
    std::uint32_t idx;
};

/// A versatile container encapsulating information pertinent to a URMA device.
///
/// Device identity can be established either by a URMA entity ID (urma_eid_t), or by a DeviceIndex (name + eid_idx).
/// This class leverages `std::variant` to abstract these distinct identification mechanisms under a unified interface.
class DeviceInfo {
public:
    DeviceInfo(urma_eid_t eid) : info_(eid) {}

    DeviceInfo(DeviceIndex idx) : info_(idx) {}

    std::size_t index() const {
        return info_.index();
    }

    /// Provides access to the encapsulated URMA entity ID.
    ///
    /// This accessor is valid only if the `DeviceInfo` instance currently holds
    /// a `urma_eid_t` (i.e., `index()` returns 0). Attempting to call this when
    /// a `DeviceIndex` is held will result in undefined behavior.
    const urma_eid_t& eid() const {
        return std::get<0>(info_);
    }

    /// Provides access to the encapsulated DeviceIndex.
    ///
    /// This accessor is valid only if the `DeviceInfo` instance currently holds
    /// a `DeviceIndex` (i.e., `index()` returns 1). Attempting to call this when
    /// a `urma_eid_t` is held will result in undefined behavior.
    const DeviceIndex& di() const {
        return std::get<1>(info_);
    }

private:
    std::variant<urma_eid_t, DeviceIndex> info_;
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_DEVICE_HPP_
