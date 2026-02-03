// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/ub/context.hpp"

#include <cstring>
#include <optional>

#include "hcomm/base/logging.hpp"
#include "hcomm/base/scope_exit.hpp"
#include <urma_api.h>

namespace hcomm {
namespace ub {
/// Searches for the `eid_index` associated with a specific `urma_eid_t` on a given device.
///
/// This function queries the device for a list of all its available entity IDs (EIDs) and iterates through them to
/// find a match. Resource cleanup for the EID list is automatically handled by `ScopeExit`.
static std::optional<std::uint32_t> get_eid_index_by_eid(urma_device_t* dev, urma_eid_t eid) {
    std::uint32_t total = 0;
    urma_eid_info_t* eid_list = urma_get_eid_list(dev, &total);
    if (eid_list == nullptr) {
        return std::nullopt;
    }

    // Ensure the EID list is freed when the function exits.
    ScopeExit eid_list_guard = [eid_list] { urma_free_eid_list(eid_list); };

    for (std::uint32_t i = 0; i < total; ++i) {
        if (std::memcmp(&eid_list[i].eid, &eid, sizeof(urma_eid_t)) == 0) {
            return eid_list[i].eid_index;
        }
    }
    return std::nullopt;
}

/// Searches for the `urma_eid_t` associated with a specific `eid_index` on a given device.
///
/// This is the reverse operation of `get_eid_index_by_eid`. It queries the device's EID list to find the entity ID
/// that corresponds to the given index. Resource cleanup for the EID list is automatically handled by `ScopeExit`.
static std::optional<urma_eid_t> get_eid_by_eid_index(urma_device* dev, std::uint32_t eid_idx) {
    std::uint32_t total = 0;
    urma_eid_info_t* eid_list = urma_get_eid_list(dev, &total);
    if (eid_list == nullptr) {
        return std::nullopt;
    }

    // Ensure the EID list is freed when the function exits.
    ScopeExit eid_list_guard = [eid_list] { urma_free_eid_list(eid_list); };

    for (std::uint32_t i = 0; i < total; ++i) {
        if (eid_list[i].eid_index == eid_idx) {
            return eid_list[i].eid;
        }
    }
    return std::nullopt;
}

Context::~Context() {
    if (urma_delete_context(urma_ctx_) != URMA_SUCCESS) {
        // Log an error if cleanup fails, as there's no other way to report it from a destructor.
        HCOMM_LOG_ERROR("Failed to delete urma context");
    }
}

std::expected<RefPtr<Context>, Error> Context::create(const ContextCreateOptions& opts) {
    urma_device_t* urma_device = nullptr;
    urma_eid_t eid;
    std::uint32_t eid_idx = 0;

    // The creation process depends on whether the user provided an entity ID (EID) or a DeviceIndex.
    switch (opts.dev_info.index()) {
    case 0:
        // Identification by EID
        eid = opts.dev_info.eid();
        urma_device = urma_get_device_by_eid(eid, URMA_TRANSPORT_UB);
        if (urma_device == nullptr) {
            HCOMM_LOG_ERROR("Failed to get urma_device by eid");
            return std::unexpected(Error::DeviceNotFound);
        }

        // Attempt to find the corresponding eid_index for the given EID. A fallback to index 0 is used if the lookup
        // fails, which may be a system default but could lead to unexpected behavior.
        eid_idx = get_eid_index_by_eid(urma_device, eid)
                      .or_else([] -> std::optional<std::uint32_t> {
                          HCOMM_LOG_ERROR("Unable to get eid_idx by eid, set eid_idx=0");
                          return 0;
                      })
                      .value();
        break;

    case 1: {
        // Identification by name and index
        auto di = opts.dev_info.di();
        urma_device = urma_get_device_by_name(di.name);
        if (urma_device == nullptr) {
            HCOMM_LOG_ERROR("Failed to get urma_device by name");
            return std::unexpected(Error::DeviceNotFound);
        }

        eid_idx = di.idx;
        // Perform the reverse lookup to find the EID associated with the index. A zeroed-out EID is used as a fallback
        // if the lookup fails, which is likely to be an invalid address.
        eid = get_eid_by_eid_index(urma_device, eid_idx)
                  .or_else([] -> std::optional<urma_eid_t> {
                      HCOMM_LOG_ERROR("Unable to get eid by eid_idx, set eid=00:00...:00");
                      return urma_eid_t{};
                  })
                  .value();
        break;
    }

    default:
        std::unreachable();
    }

    // After identifying the device, query its capabilities and limits.
    urma_device_attr_t attr;
    if (urma_query_device(urma_device, &attr) != URMA_SUCCESS) {
        HCOMM_LOG_ERROR("Failed to query device, device name: {}", urma_device->name);
        return std::unexpected(Error::QueryDevice);
    }

    // With the device and index confirmed, create the underlying URMA context.
    urma_context_t* urma_ctx = urma_create_context(urma_device, eid_idx);
    if (urma_ctx == nullptr) {
        HCOMM_LOG_ERROR("Failed to create urma context");
        return std::unexpected(Error::DeviceNotFound);
    }

    // If all steps succeed, construct the Context object and return it.
    return makeRef<Context>(PrivateTag{}, urma_ctx, attr);
}
} // namespace ub
} // namespace hcomm
