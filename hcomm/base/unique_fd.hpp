// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_UNIQUE_FD_HPP_
#define HCOMM_BASE_UNIQUE_FD_HPP_

#include <unistd.h>
#include <utility>

namespace hcomm {
/// `UniqueFd` is a RAII (Resource Acquisition Is Initialization) wrapper for file descriptors.
///
/// It ensures that the file descriptor is properly closed when the `UniqueFd` object goes out of scope, preventing
/// resource leaks.
class UniqueFd {
public:
    explicit UniqueFd(int fd) : fd_(fd) {}

    ~UniqueFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    UniqueFd(UniqueFd&& rhs) noexcept : fd_(rhs.fd_) {
        rhs.fd_ = -1;
    }

    UniqueFd& operator=(UniqueFd&& rhs) noexcept {
        UniqueFd(std::move(rhs)).swap(*this);
        return *this;
    }

    int get() const {
        return fd_;
    }

    void reset(int new_fd = -1) {
        UniqueFd(new_fd).swap(*this);
    }

    int release() {
        int temp = fd_;
        fd_ = -1;
        return temp;
    }

    void swap(UniqueFd& rhs) noexcept {
        std::swap(fd_, rhs.fd_);
    }

private:
    int fd_ = -1;
};
} // namespace hcomm

#endif // HCOMM_BASE_UNIQUE_FD_HPP_
