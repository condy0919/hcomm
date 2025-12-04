// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_JETTY_HPP_
#define HCOMM_TRANSPORT_UB_JETTY_HPP_

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>

#include "hcomm/transport/ub/error.hpp"
#include "hcomm/transport/ub/internal/deleter.hpp"

namespace hcomm {
namespace ub {
enum class JettyState : std::uint8_t {
    Reset = 0, ///< The initial state when jetty is created
    Ready,     ///< Being able to send, recv, ...
    Error,     ///< No SQE dispatch and scheduling
};

struct JettyCreateOptions {
    std::uint32_t tx_depth;
    std::uint32_t rx_depth;
    std::uint32_t seg_size;
    std::uint32_t seg_count;
};

/// Jetty is a queue used to manage submitted IO tasks and received messages.
class Jetty {
public:
    static std::expected<Jetty, Error> create(urma_context_t& urma_ctx, urma_jfc_t& jfc,
                                              const JettyCreateOptions& opts);

    Jetty(Jetty&& rhs) noexcept
        : id_(rhs.id_), urma_ctx_(rhs.urma_ctx_), jfr_(std::move(rhs.jfr_)), jetty_(std::move(rhs.jetty_)),
          tjetty_(std::move(rhs.tjetty_)) {}

    ~Jetty();

private:
    Jetty(std::uint32_t id, urma_context_t& urma_ctx, urma_jfr_t* jfr, urma_jetty_t* jetty)
        : id_(id), urma_ctx_(urma_ctx), jfr_(jfr), jetty_(jetty) {}

    std::atomic<JettyState> state_{JettyState::Reset};
    // 3B reserved
    std::uint32_t id_ = 0;

    urma_context_t& urma_ctx_;
    std::unique_ptr<urma_jfr_t, internal::JfrDeleter> jfr_;
    std::unique_ptr<urma_jetty_t, internal::JettyDeleter> jetty_;
    std::unique_ptr<urma_target_jetty_t, internal::TargetJettyDeleter> tjetty_;

    //
    //     urpc_list_t qctx_node;
    //     // queue param
    //     umq_ub_ctx_t *dev_ctx;
    //     struct ub_bind_ctx *bind_ctx;
    //     volatile uint32_t ref_cnt;
    //     atomic_uint require_rx_count;
    //     volatile uint32_t tx_outstanding;
    //     urma_target_seg_t *imported_tseg_list[UMQ_MAX_TSEG_NUM];
    //     pthread_mutex_t imported_tseg_list_mutex;
    //     uint64_t addr_list[UMQ_MAX_ID_NUM];
    //
    //     // config param
    //     ub_flow_control_t flow_control;
    //     char name[UMQ_NAME_MAX_LEN];
    //     uint32_t remote_rx_buf_size;

    //
    //     std::string mName;
    //     std::string mPeerIpPort;
    //     uint64_t mUpId = 0;
    //     std::mutex mStopMutex;
    //
    //     UBContext *mUBContext = nullptr;
    //     UBJfc *mSendJfc = nullptr;
    //     UBJfc *mRecvJfc = nullptr;
    //     urma_jfr_t *mJfr = nullptr;
    //     JettyOptions mJettyOptions{};
    //     std::unique_ptr<UBJettyExchangeInfo> mRemoteJettyInfo; // 对端建链时交换信息
    //     uintptr_t mUpContext = 0;
    //     uintptr_t mUpContext1 = 0;
    //     NetSpinLock mLock;
    //     UBOpContextInfo mCtxPosted{};
    //     uint32_t mCtxPostedCount{ 0 };
    //     UBMemoryRegionFixedBuffer *mJettyMr = nullptr;
    //
    //     int32_t mOneSideMaxWr = JETTY_MAX_SEND_WR - NN_NO64;
    //     int32_t mOneSideRef = JETTY_MAX_SEND_WR - NN_NO64;
    //     int32_t mPostSendMaxWr = NN_NO64;
    //     uint32_t mPostSendMaxSize = NN_NO1024;
    //     int32_t mPostSendRef = NN_NO64;
    //
    //     UBSHcomNetMemoryRegionPtr mHBLocalMr = nullptr;
    //     UBSHcomNetMemoryRegionPtr mHBRemoteMr = nullptr;
    //     uint64_t mLocalNextOffset = 0;
    //     uint64_t mRemoteNextOffset = 0;
    //
    //     friend class NetDriverUBWithOob;
    //     friend class NetHeartbeat;
    //
    //     DEFINE_RDMA_REF_COUNT_VARIABLE;
    //
    //     static uint32_t G_INDEX;
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_JETTY_HPP_
