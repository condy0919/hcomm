// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_UB_JETTY_HPP_
#define HCOMM_TRANSPORT_UB_JETTY_HPP_


// #include "foo/server/fooserver.h"

// #include <sys/types.h>
// #include <unistd.h>

// #include <string>
// #include <vector>

// #include "base/basictypes.h"
// #include "foo/server/bar.h"
// #include "third_party/absl/flags/flag.h"

#include <cstdint>

#include "urma_api.h"

namespace hcomm {
namespace ub {
enum class UBJettyState : std::uint8_t {
    Reset, ///< The initial state when jetty is created
    Ready, ///< Be able to send, recv, read, write and etc
    Error, ///< Unable to send, recv, ...
};

///
class Jetty {
public:

private:
};

} // namespace ub
} // namespace hcomm

#endif // HCOMM_TRANSPORT_UB_JETTY_HPP_
