// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_ERROR_HPP_
#define HCOMM_TRANSPORT_UB_ERROR_HPP_

#include <cstdint>

namespace hcomm {
namespace ub {
enum class Error : std::uint32_t {
    Syscall = 1,

    // URMA
    CreateJetty,
    CreateJfr,
    CreateJfce,
    CreateJfc,
    RearmJfc,
    ImportJetty,
    BindJetty,
    DeviceNotFound,
    QueryDevice,

    // hcomm
    AlreadyBind,
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_ERROR_HPP_
