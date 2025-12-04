// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_ERROR_HPP_
#define HCOMM_TRANSPORT_UB_ERROR_HPP_

namespace hcomm {
namespace ub {
enum class Error {
    Syscall = 1,

    // URMA
    CreateJetty,
    CreateJfr,
    CreateJfce,
    CreateJfc,
    RearmJfc,
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_ERROR_HPP_
