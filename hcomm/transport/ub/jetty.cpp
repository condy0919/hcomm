// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/ub/jetty.hpp"

#include "hcomm/base/logging.hpp"

namespace hcomm {
namespace ub {
std::expected<Jetty, Error> Jetty::create(urma_context_t& urma_ctx, urma_jfc_t& jfc, const JettyCreateOptions& opts) {
    // Create JFR
    urma_jfr_cfg_t jfr_cfg = {
        .id = 0, // auto
        .depth = opts.rx_depth,
        .flag =
            {
                .bs =
                    {
                        .token_policy = URMA_TOKEN_PLAIN_TEXT,
                        .tag_matching = URMA_NO_TAG_MATCHING,
                        .lock_free = 0,  // Jetty can be shared between threads
                        .order_type = 0, // default
                        .reserved = 0,
                    },
            },
        .trans_mode = URMA_TM_RC,
        .max_sge = 4, // TODO opts
        .min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER,
        .jfc = &jfc,
        .token_value = {.token = 0}, // TODO opts
        .user_ctx = 0,               // TODO
    };
    urma_jfr_t* jfr = urma_create_jfr(&urma_ctx, &jfr_cfg);
    if (jfr == nullptr) {
        return std::unexpected{Error::CreateJfr};
    }

    // Create Jetty
    urma_jetty_cfg_t jetty_cfg = {
        .id = 0, // auto
        .flag =
            {
                .bs = {.share_jfr = URMA_SHARE_JFR},
            },
        .jfs_cfg =
            {
                .depth = opts.tx_depth,
                .flag = {.value = 0}, // error continue, single path
                .trans_mode = URMA_TM_RC,
                .priority = 0,
                .max_sge = 4,         // TODO opts
                .max_rsge = 4,        // TODO opts
                .max_inline_data = 0, // Use device's max inline data length
                .rnr_retry = URMA_TYPICAL_RNR_RETRY,
                .err_timeout = URMA_TYPICAL_ERR_TIMEOUT,
                .jfc = &jfc,
                .user_ctx = 0, // TODO
            },
        .shared =
            {
                .jfr = jfr,
                .jfc = &jfc,
            },
        .jetty_grp = nullptr,
        .user_ctx = 0, // TODO
    };
    urma_jetty_t* jetty = urma_create_jetty(&urma_ctx, &jetty_cfg);
    if (jetty == nullptr) {
        urma_delete_jfr(jfr);
        return std::unexpected{Error::CreateJetty};
    }

    return Jetty(jetty->jetty_id.id, urma_ctx, jfr, jetty);
}

Jetty::~Jetty() {
    if (jetty_) {
        if (urma_status_t ret = urma_unbind_jetty(jetty_.get()); ret != URMA_SUCCESS) {
            HCOMM_LOG_ERROR("Unable to unbind jetty, status = {}", ret);
        }

        // Unimport target jetty
        tjetty_.reset();

        // Delete jetty
        jetty_.reset();
    }

    // Delete jfr
    jfr_.reset();

    // TODO release segment

    // TODO release hb local segment

    // TODO release hb remote segment
}
} // namespace ub
} // namespace hcomm
