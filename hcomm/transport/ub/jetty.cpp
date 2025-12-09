// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/ub/jetty.hpp"

#include <random>

#include "hcomm/base/logging.hpp"
#include "urma_api.h"

namespace hcomm {
namespace ub {
static std::uint32_t generateJettyToken() {
    static std::random_device entropy;
    static std::mt19937 gen(entropy());

    std::uniform_int_distribution<std::uint32_t> dist(1);
    return dist(gen);
}

std::expected<Jetty, Error> Jetty::create(urma_context_t& urma_ctx, urma_jfc_t& jfc, const JettyCreateOptions& opts) {
    // Create JFR
    urma_jfr_cfg_t jfr_cfg = {
        // clang-format off
        .id = 0, // auto
        .depth = opts.rx_depth,
        .flag = {
            .bs = {
                .token_policy = URMA_TOKEN_PLAIN_TEXT,
                .tag_matching = URMA_NO_TAG_MATCHING,
                .lock_free = 0, // Jetty can be shared between threads
                .order_type = URMA_DEF_ORDER,
                .reserved = 0,
            },
        },
        .trans_mode = URMA_TM_RC,
        .max_sge = 4, // TODO opts
        .min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER,
        .jfc = &jfc,
        .token_value = {.token = generateJettyToken()},
        .user_ctx = 0, // TODO
        // clang-format on
    };
    std::unique_ptr<urma_jfr_t, Deleter> jfr(urma_create_jfr(&urma_ctx, &jfr_cfg));
    if (!jfr) {
        return std::unexpected(Error::CreateJfrFailed);
    }

    // Create Jetty
    urma_jetty_cfg_t jetty_cfg = {
        // clang-format off
        .id = 0, // auto
        .flag = {
            .bs = {
                .share_jfr = URMA_SHARE_JFR,
                .reserved = 0,
            },
        },
        .jfs_cfg = {
            .depth = opts.tx_depth,
            .flag = {
                .bs = {
                    .lock_free = 0,
                    .error_suspend = 0, // error continue
                    .outorder_comp = 0,
                    .order_type = 0,
                    .multi_path = 0, // single path
                    .ctp_rc_mul_path_mode = 0,
                    .reserved = 0,
                },
            },
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
        .shared = {
            .jfr = jfr.get(),
            .jfc = &jfc,
        },
        .jetty_grp = nullptr,
        .user_ctx = 0, // TODO
        // clang-format on
    };
    std::unique_ptr<urma_jetty_t, Deleter> jetty(urma_create_jetty(&urma_ctx, &jetty_cfg));
    if (!jetty) {
        return std::unexpected(Error::CreateJettyFailed);
    }

    return Jetty(jetty->jetty_id.id, urma_ctx, jfr.release(), jetty.release());
}

Jetty::~Jetty() {
    if (tjetty_) {
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

std::expected<void, Error> Jetty::bind(const JettyBindInfo& info) {
    if (tjetty_bind_info_) {
        HCOMM_LOG_ERROR("The jetty has already been binded");
        return std::unexpected(Error::AlreadyBind);
    }

    urma_rjetty_t rjetty = {
        // clang-format off
        .jetty_id = info.jetty_id,
        .trans_mode = info.trans_mode,
        .policy = URMA_JETTY_GRP_POLICY_RR, // TODO jetty group
        .type = info.type,
        .flag = {
            .bs = {
                .token_policy = URMA_TOKEN_PLAIN_TEXT,
                .order_type = info.order_type,
                .share_tp = 1,
                .reserved = 0,
            },
        },
        .tp_type = URMA_RTP,
        // clang-format on
    };
    std::unique_ptr<urma_target_jetty, Deleter> tjetty(
        urma_import_jetty(&urma_ctx_, &rjetty, const_cast<urma_token_t*>(&info.token)));
    if (!tjetty) {
        HCOMM_LOG_ERROR("import jetty failed, remote jetty id {}", info.jetty_id.id);
        return std::unexpected(Error::ImportJettyFailed);
    }

    urma_status_t status = urma_bind_jetty(jetty_.get(), tjetty.get());
    if (status != URMA_SUCCESS && status != URMA_EEXIST) {
        HCOMM_LOG_ERROR("bind jetty failed, status {}", status);
        return std::unexpected(Error::BindJettyFailed);
    }

    tjetty_ = std::move(tjetty);
    tjetty_bind_info_ = std::make_unique<JettyBindInfo>(info);
    return {};

    //     uint32_t max_msg_size = queue->dev_ctx->dev_attr.dev_cap.max_msg_size;
    //     queue->remote_rx_buf_size = (max_msg_size > info->rx_buf_size) ? info->rx_buf_size : max_msg_size;
    //     return UMQ_SUCCESS;
    //

    // TODO seg size, seg count, window
}

} // namespace ub
} // namespace hcomm
