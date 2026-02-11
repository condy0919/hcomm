// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/tcp/socket.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>

#include <cstdint>

#include "hcomm/transport/tcp/executor.hpp"

namespace hcomm {
namespace tcp {
namespace internal {
Result<RefPtr<Socket>, int> AcceptContinuation::operator()(Context& ctx) {
    IOExecutor* exec = static_cast<IOExecutor*>(ctx.executor());

    // accept
    int cfd = ::accept4(listener_->fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd >= 0) {
        return Ok(makeRef<Socket>(cfd, exec));
    }

    if (errno == EAGAIN) {
        // wakeup later
        exec->waitForRead(listener_->fd(), ctx.waker());
        return Pending{};
    }

    return Err(errno);
}
} // namespace internal

Socket::~Socket() {
    executor_->deregister(fd_.get());
}

Listener::~Listener() {
    executor_->deregister(fd_.get());
}

std::expected<Listener, int> Listener::bind(IOExecutor* exec, const SocketAddress& sa) {
    // nonblocking by default
    int fd = ::socket(sa.ip().family(), SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return std::unexpected(errno);
    }
    UniqueFd ufd(fd);

    const int enable = 1;

    // Enable reuse addr
    int ret = ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (ret < 0) {
        return std::unexpected(errno);
    }

    // Enable reuse port
    ret = ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    if (ret < 0) {
        return std::unexpected(errno);
    }

    const struct sockaddr_storage addr = sa;
    const socklen_t addr_len = sa.ip().is_ipv4() ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    ret = ::bind(fd, reinterpret_cast<const struct sockaddr*>(&addr), addr_len);
    if (ret < 0) {
        return std::unexpected(errno);
    }

    // Sets the backlog with INT32_MAX.
    // `net.core.somaxconn` is the only soft constraint.
    ret = ::listen(fd, INT32_MAX);
    if (ret < 0) {
        return std::unexpected(errno);
    }

    // register events
    return exec->registerEvent(ufd.get(), EPOLLIN | EPOLLET | EPOLLEXCLUSIVE, 0).transform([&ufd, &exec]() {
        return Listener(ufd.release(), exec);
    });
}

} // namespace tcp
} // namespace hcomm
