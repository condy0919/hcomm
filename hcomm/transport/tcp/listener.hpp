// SPDX-License-Identifier: MulanPSL-2.0

// /home/condy/workspace/hcomm/hcomm/transport/tcp/listener.hpp

#ifndef HCOMM_TRANSPORT_TCP_LISTENER_HPP_
#define HCOMM_TRANSPORT_TCP_LISTENER_HPP_

#include "hcomm/base/unique_fd.hpp"

namespace hcomm {
namespace tcp {
class Listener final {
public:
    Listener(Listener&& rhs) noexcept = default;
    Listener& operator=(Listener&& rhs) noexcept = default;

    ~Listener() = default;



private:
    UniqueFd fd_;
};
} // namespace tcp
} // namespace hcomm

#endif // HCOMM_TRANSPORT_TCP_LISTENER_HPP_
