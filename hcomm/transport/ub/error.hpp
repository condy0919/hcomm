// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_ERROR_HPP_
#define HCOMM_TRANSPORT_UB_ERROR_HPP_

namespace hcomm {
namespace ub {
enum class Error {
    SyscallError = 1,

    // URMA
    CreateJettyFailed,
    CreateJfrFailed,
    CreateJfceFailed,
    CreateJfcFailed,
    RearmJfcFailed,
    ImportJettyFailed,
    BindJettyFailed,

    // hcomm
    AlreadyBind,
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_ERROR_HPP_
