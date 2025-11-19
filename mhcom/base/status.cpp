// SPDX-License-Identifier: MulanPSL-2.0

#include "mhcom/base/status.hpp"

namespace mhcom {
Status::Status(const Status& rhs) : rep_(rhs.rep_) {
    if (!isInlined(rep_)) {
        ptr(rep_)->ref();
    }
}

Status& Status::operator=(const Status& rhs) {
    Status status_copy(rhs);
    swap(status_copy, *this);
    return *this;
}

Status::~Status() {
    if (!isInlined(rep_)) {
        ptr(rep_)->unref();
    }
}

StatusCode Status::code() const {
    auto raw = static_cast<StatusCode>(rawCode());
    switch (raw) {
    case StatusCode::Ok:
    case StatusCode::Error:
    case StatusCode::Cancelled:
    case StatusCode::OutOfMemory:
    case StatusCode::OutOfRange:
    case StatusCode::IOError:
    case StatusCode::InvalidArgument:
    case StatusCode::NotSupported:
    case StatusCode::Unknown:
        return raw;

    default:
        return StatusCode::Unknown;
    }
}
} // namespace mhcom
