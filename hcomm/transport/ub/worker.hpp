// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_WORKER_HPP_
#define HCOMM_TRANSPORT_UB_WORKER_HPP_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <thread>

#include "hcomm/transport/ub/error.hpp"
#include "hcomm/transport/ub/internal/deleter.hpp"
#include "urma_types.h"

namespace hcomm {
namespace ub {
struct WorkerCreateOptions {
    std::uint16_t index;
    std::uint32_t cq_depth;
};

class Worker {
public:
    static std::expected<Worker, Error> create(urma_context_t& urma_ctx, const WorkerCreateOptions& opts);

    Worker(Worker&& rhs) noexcept
        : index_(rhs.index_), urma_ctx_(rhs.urma_ctx_), jfce_(std::move(rhs.jfce_)), jfc_(std::move(rhs.jfc_)) {}

    ~Worker();

    void start(std::optional<std::uint32_t> cpuid = std::nullopt);

private:
    Worker(urma_context_t& urma_ctx, urma_jfce_t* jfce, urma_jfc_t* jfc, const WorkerCreateOptions& opts)
        : index_(opts.index), urma_ctx_(urma_ctx), jfce_(jfce), jfc_(jfc) {}

    void runInThread();

    std::uint16_t index_; ///< The index in a worker group
    bool stop_ = false;   ///< The stop flag
    // 5B padding

    std::thread rthread_;

    urma_context_t& urma_ctx_;
    std::unique_ptr<urma_jfce_t, internal::JfceDeleter> jfce_;
    std::unique_ptr<urma_jfc_t, internal::JfcDeleter> jfc_;
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_WORKER_HPP_
