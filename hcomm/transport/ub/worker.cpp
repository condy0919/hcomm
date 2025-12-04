// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/ub/worker.hpp"

#include <thread>

#include <fcntl.h>
#include <sched.h>

#include "hcomm/base/logging.hpp"

namespace hcomm {
namespace ub {
std::expected<Worker, Error> Worker::create(urma_context_t& urma_ctx, const WorkerCreateOptions& opts) {
    // Create JFCE
    std::unique_ptr<urma_jfce_t, internal::JfceDeleter> jfce(urma_create_jfce(&urma_ctx));
    if (!jfce) {
        return std::unexpected(Error::CreateJfce);
    }

    // Create JFC
    urma_jfc_cfg_t jfc_cfg = {
        .depth = opts.cq_depth,
        .flag = {.value = 0}, // TODO lockfree = true
        .ceqn = 0,
        .jfce = jfce.get(),
        .user_ctx = 0,
    };

    std::unique_ptr<urma_jfc_t, internal::JfcDeleter> jfc(urma_create_jfc(&urma_ctx, &jfc_cfg));
    if (!jfc) {
        return std::unexpected(Error::CreateJfc);
    }

    urma_status_t ret = urma_rearm_jfc(jfc.get(), /*solicited_only=*/false);
    if (ret != URMA_SUCCESS) {
        return std::unexpected(Error::RearmJfc);
    }

    int flags = fcntl(jfce->fd, F_GETFL);
    if (fcntl(jfce->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        HCOMM_LOG_ERROR("Failed to set nonblocking for JFC");
        return std::unexpected(Error::Syscall);
    }

    return Worker(urma_ctx, jfce.release(), jfc.release(), opts);
}

Worker::~Worker() {
    ;
}

void Worker::start(std::optional<std::uint32_t> cpuid) {
    // TODO check
    // mNewRequestHandler;
    // mSendPostedHandler;
    // mOneSideDoneHandler;

    std::thread th(&Worker::runInThread, this);

    // Set human-friendly name
    const std::string name = "UBWkr-" + std::to_string(index_);
    if (pthread_setname_np(th.native_handle(), name.c_str()) != 0) {
        HCOMM_LOG_WARN("Unable to set name of UBWkr-{} thread", index_);
    }

    // Set thread affinity if user requested
    if (cpuid) {
        cpu_set_t set;

        CPU_ZERO(&set);
        CPU_SET(cpuid.value(), &set);
        if (pthread_setaffinity_np(th.native_handle(), sizeof(set), &set) != 0) {
            HCOMM_LOG_WARN("Unable to bind UBWkr-{} to CPU {}", index_, cpuid.value());
        }
    }

    rthread_ = std::move(th);
}

void Worker::runInThread() {
    //
    //     if (mOptions.threadPriority != 0) {
    //         if (NN_UNLIKELY(setpriority(PRIO_PROCESS, 0, mOptions.threadPriority) != 0)) {
    //             char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
    //             NN_LOG_WARN("Unable to set worker thread priority in ub worker " << mName << ", errno:" <<
    //                 NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
    //         }
    //     }
    //
    //     mProgressThreadStarted.store(true);
    //     NN_LOG_INFO("UBWorker " << DetailName() << ", cpuId: " << mProgressCpuId << ", cq count: " <<
    //         ((mUBJfc != nullptr) ? mUBJfc->GetCQCount() : 0) << ", polling batch size: " << mProgressBatchSize <<
    //         ", more " << mOptions.ToString() << "] working thread started");
    //
    //     if (mOptions.workerMode == UB_BUSY_POLLING) {
    //         DoWithBusyPolling();
    //     } else if (mOptions.workerMode == UB_EVENT_POLLING) {
    //         DoWithCQEventPolling();
    //     } else {
    //         NN_LOG_ERROR("Un-reachable");
    //     }
    //
    //     NN_LOG_INFO("UBWorker " << DetailName() << " working thread exiting");
}
} // namespace ub
} // namespace hcomm
