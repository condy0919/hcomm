// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_SOCKET_HPP_
#define HCOMM_TRANSPORT_TCP_SOCKET_HPP_

#include <expected>
#include <span>

#include "hcomm/base/refptr.hpp"
#include "hcomm/base/unique_fd.hpp"
#include "hcomm/promise/promise.hpp"
#include "hcomm/transport/tcp/address.hpp"
#include "hcomm/transport/tcp/executor.hpp"

namespace hcomm {
namespace tcp {
// forward
class Socket;
class IOExecutor;
class Listener;

namespace internal {
class AcceptContinuation {
public:
    AcceptContinuation(Listener* listener) : listener_(listener) {}

    Result<RefPtr<Socket>, int> operator()(Context& ctx);

private:
    Listener* listener_;
};
} // namespace internal

/// A non-blocking TCP socket for asynchronous I/O operations.
///
/// `Socket` is a move-only type that wraps a file descriptor. It provides promise-based methods for reading and
/// writing data, which integrate with an `IOExecutor`. All operations are non-blocking. When an operation cannot
/// complete immediately (e.g., reading from an empty sk_buf, writing to a full sk_buf), it returns a promise that will
/// be resolved when the socket is ready again.
///
/// The socket's lifetime is tied to the `UniqueFd` it holds. When a `Socket` object is destroyed, its file descriptor
/// is automatically closed.
class Socket final : public RefCounted<Socket> {
public:
    Socket(int fd, IOExecutor* exec) : fd_(fd), executor_(exec) {}

    Socket(Socket&& rhs) noexcept = default;
    Socket& operator=(Socket&& rhs) noexcept = default;

    ~Socket();

    int fd() const {
        return fd_.get();
    }

    /// Asynchronously reads data from the socket into the provided buffer.
    ///
    /// This method returns a promise that resolves to the number of bytes read, or an error code (errno).
    ///
    /// If the read operation cannot complete immediately (returns `EAGAIN`), it registers a waker with the
    /// `IOExecutor` and returns `Pending`. The promise will be resumed when the socket becomes readable again.
    ///
    /// A result of `Ok(0)` typically indicates that the remote peer has closed the connection (EOF).
    auto read(std::span<char> buf) {
        return makePromise([buf, this, self = shared_from_this()](Context& ctx) -> Result<ssize_t, int> {
            auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());

            ssize_t nread = ::read(fd_.get(), buf.data(), buf.size());
            if (nread < 0) {
                if (errno == EAGAIN) {
                    exec->waitForRead(fd_.get(), ctx.waker());
                    return Pending{};
                }
                return Err(errno);
            }
            return Ok(nread);
        });
    }

    /// Asynchronously writes data from the provided buffer to the socket.
    ///
    /// This method returns a promise that resolves to the number of bytes written, or an error code (errno).
    ///
    /// If the write operation cannot complete immediately (returns `EAGAIN`), it registers a waker with the
    /// `IOExecutor` and returns `Pending`. The promise will be resumed when the socket becomes writable again. It is
    /// the caller's responsibility to handle partial writes by calling `write` again with the remaining data.
    auto write(std::span<char> buf) {
        return makePromise([buf, this, self = shared_from_this()](Context& ctx) -> Result<ssize_t, int> {
            auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());

            ssize_t nwrite = ::write(fd_.get(), buf.data(), buf.size());
            if (nwrite < 0) {
                if (errno == EAGAIN) {
                    exec->waitForWrite(fd_.get(), ctx.waker());
                    return Pending{};
                }
                return Err(errno);
            }
            return Ok(nwrite);
        });
    }

private:
    UniqueFd fd_;
    IOExecutor* executor_;
};

/// A non-blocking TCP listener for accepting incoming connections.
///
/// `Listener` wraps a listening socket file descriptor. It is used to bind to a specific address and port and to
/// accept new `Socket` connections asynchronously.
class Listener final {
public:
    Listener(Listener&& rhs) noexcept = default;
    Listener& operator=(Listener&& rhs) noexcept = default;

    ~Listener();

    /// Creates a `Listener` by binding to a socket address and listening for incoming connections.
    ///
    /// This static method handles the creation of a non-blocking socket, sets reuse address and port options, binds it
    /// to the given `SocketAddress`, and puts it into the listening state.
    static std::expected<Listener, int> bind(IOExecutor* exec, const SocketAddress& sa);

    int fd() const {
        return fd_.get();
    }

    /// Asynchronously accepts a new incoming connection.
    ///
    /// This method returns a promise that resolves to a new `Socket` for the accepted connection, or an error code on
    /// failure.
    ///
    /// If no pending connections are available, it registers a waker with the `IOExecutor` and the promise will be
    /// resumed when a new connection arrives.
    ///
    /// The `Listener`'s lifetime is expected to be managed by the server and should outlive any pending `accept`
    /// operations.
    auto accept() {
        // The lifetime of a Listener is typically the same as the server's.
        return PromiseImpl(internal::AcceptContinuation(this));
    }

private:
    Listener(int fd, IOExecutor* exec) : fd_(fd), executor_(exec) {}

    UniqueFd fd_;
    IOExecutor* executor_;
};

} // namespace tcp
} // namespace hcomm

#endif // HCOMM_TRANSPORT_TCP_SOCKET_HPP_
