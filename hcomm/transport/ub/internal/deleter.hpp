// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_INTERNAL_DELETER_HPP_
#define HCOMM_TRANSPORT_UB_INTERNAL_DELETER_HPP_

#include "urma_api.h"
#include "urma_types.h"

#include "hcomm/base/logging.hpp"

namespace hcomm {
namespace ub {
namespace internal {
struct JfceDeleter {
    void operator()(urma_jfce_t* p) noexcept {
        if (urma_status_t ret = urma_delete_jfce(p); ret != URMA_SUCCESS) {
            HCOMM_LOG_ERROR("Unable to delete jfce, status = {}", ret);
        }
    }
};

struct JfcDeleter {
    void operator()(urma_jfc_t* p) noexcept {
        if (urma_status_t ret = urma_delete_jfc(p); ret != URMA_SUCCESS) {
            HCOMM_LOG_ERROR("Unable to dejete jfc, status = {}", ret);
        }
    }
};

struct JfrDeleter {
    void operator()(urma_jfr_t* p) noexcept {
        if (urma_status_t ret = urma_delete_jfr(p); ret != URMA_SUCCESS) {
            HCOMM_LOG_ERROR("Unable to delete jfr, status = {}", ret);
        }
    }
};

struct JettyDeleter {
    void operator()(urma_jetty_t* p) noexcept {
        const std::uint32_t id = p->jetty_id.id;
        if (urma_status_t ret = urma_delete_jetty(p); ret != URMA_SUCCESS) {
            HCOMM_LOG_ERROR("Unable to delete jetty, id = {}, status = {}", id, ret);
        }
    }
};

struct TargetJettyDeleter {
    void operator()(urma_target_jetty_t* p) noexcept {
        const std::uint32_t tid = p->id.id;
        if (urma_status_t ret = urma_unimport_jetty(p); ret != URMA_SUCCESS) {
            HCOMM_LOG_ERROR("Unable to unimport target jetty, id = {}, status = {}", tid, ret);
        }
    }
};
} // namespace internal
} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_INTERNAL_DELETER_HPP_
