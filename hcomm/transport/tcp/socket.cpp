// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/tcp/socket.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>

#include <cstdint>

#include "hcomm/transport/tcp/executor.hpp"

namespace hcomm {
namespace tcp {
namespace internal {
/// Attempts to accept a new connection.
///
/// If a connection is pending, it is accepted, and a new non-blocking `Socket` is created for it. The new socket is
/// registered with the executor.
/// If no connections are pending (`EAGAIN` or `EWOULDBLOCK`), it registers a waker to be notified when a new
/// connection arrives.
/// Returns a `Socket` on success, an error code on failure, or `Pending` if the operation would block.
Result<RefPtr<Socket>, NetworkError> AcceptContinuation::operator()(Context& ctx) {
    IOExecutor* exec = static_cast<IOExecutor*>(ctx.executor());

    // accept
    int cfd = ::accept4(listener_->fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // wakeup later
            exec->waitForRead(listener_->id(), ctx.waker());
            return Pending{};
        }
        return Err(errno);
    }

    UniqueFd ufd(cfd);
    return exec->registerEvent(ufd.get(), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET)
        .transform([&ufd, &exec](ResourceId id) {
            auto sock = makeRef<Socket>(ufd.get(), id, exec);
            ufd.release();
            return sock;
        });
}

/// Attempts to read data from the socket.
///
/// Returns the number of bytes read, or an error. If the operation would block, it registers for read events and
/// returns `Pending`.
Result<ssize_t, NetworkError> ReadContinuation::operator()(Context& ctx) {
    auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());

    ssize_t nread = ::read(sk_->fd(), buf_.data(), buf_.size());
    if (nread < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Socket is not ready, register a waker to be notified when it is.
            exec->waitForRead(sk_->id(), ctx.waker());
            return Pending{};
        }
        return Err(errno);
    } else if (nread == 0) {
        return Err(NetworkError::kEOF);
    }
    return Ok(nread);
}

/// Repeatedly attempts to read data until the entire buffer is full.
///
/// This stateful continuation tracks the `offset_` of data already read. If a read would block, it suspends and waits
/// for more data.
Result<void, NetworkError> ReadExactContinuation::operator()(Context& ctx) {
    auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());
    if (buf_.empty()) {
        return Ok();
    }

    while (offset_ < buf_.size()) {
        ssize_t nread = ::recv(sk_->fd(), buf_.data() + offset_, buf_.size() - offset_, 0);
        if (nread > 0) {
            offset_ += static_cast<std::uint32_t>(nread);
        } else if (nread == 0) {
            // Peer closed the connection while we were expecting more data.
            return Err(NetworkError::kUnexpectedEOF);
        } else {
            if (errno == EINTR) {
                // Interrupted by signal, retry immediately.
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer empty, register for more read events.
                exec->waitForRead(sk_->id(), ctx.waker());
                return Pending{};
            }
            return Err(errno);
        }
    }

    return Ok();
}

/// Attempts to write data from the buffer to the socket.
///
/// Returns the number of bytes written, or an error. If the operation would block, it registers for write events and
/// returns `Pending`.
Result<ssize_t, NetworkError> WriteContinuation::operator()(Context& ctx) {
    auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());

    ssize_t nwrite = ::write(sk_->fd(), buf_.data(), buf_.size());
    if (nwrite < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Socket buffer is full, register a waker to be notified when space is available.
            exec->waitForWrite(sk_->id(), ctx.waker());
            return Pending{};
        }
        return Err(errno);
    }
    return Ok(nwrite);
}

/// Repeatedly attempts to write data until the entire buffer is sent.
///
/// This stateful continuation tracks the `offset_` of data already written. If a write would block, it suspends and
/// waits for the socket to become writable again.
Result<void, NetworkError> WriteAllContinuation::operator()(Context& ctx) {
    auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());
    if (buf_.empty()) {
        return Ok();
    }

    while (offset_ < buf_.size()) {
        // MSG_NOSIGNAL prevents SIGPIPE if the peer closes the connection.
        ssize_t nwrite = ::send(sk_->fd(), buf_.data() + offset_, buf_.size() - offset_, MSG_NOSIGNAL);
        if (nwrite >= 0) {
            offset_ += nwrite;
        } else {
            if (errno == EINTR) {
                // Interrupted by signal, retry immediately.
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, register for more write events.
                exec->waitForWrite(sk_->id(), ctx.waker());
                return Pending{};
            } else {
                return Err(errno);
            }
        }
    }
    return Ok();
}
} // namespace internal

/// Deregisters the socket from the executor upon destruction.
Socket::~Socket() {
    executor_->deregister(fd_.get(), res_id_);
}

/// Deregisters the listener from the executor upon destruction.
Listener::~Listener() {
    executor_->deregister(fd_.get(), res_id_);
}

/// Creates a `Listener` by binding to a socket address and preparing it to accept connections.
///
/// This function performs all the necessary steps to set up a listening socket:
/// 1. Creates a non-blocking TCP socket (`SOCK_NONBLOCK | SOCK_CLOEXEC`).
/// 2. Sets `SO_REUSEADDR` and `SO_REUSEPORT` to allow quick restarts and better load distribution.
/// 3. Binds the socket to the specified `SocketAddress`.
/// 4. Puts the socket in listening mode with a large backlog queue.
/// 5. Registers the listening socket with the `IOExecutor` to monitor for incoming connections (`EPOLLIN`).
///
/// On success, it returns a `RefPtr` to a new `Listener` object. On failure, it returns an `unexpected` value
/// containing the error code (errno).
std::expected<RefPtr<Listener>, NetworkError> Listener::bind(IOExecutor* exec, const SocketAddress& sa) {
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
    return exec->registerEvent(ufd.get(), EPOLLIN | EPOLLET | EPOLLEXCLUSIVE).transform([&ufd, &exec](ResourceId id) {
        auto listener = makeRef<Listener>(ufd.get(), id, exec);
        ufd.release();
        return listener;
    });
}

} // namespace tcp
} // namespace hcomm
