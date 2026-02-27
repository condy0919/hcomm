// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_SOCKET_HPP_
#define HCOMM_TRANSPORT_TCP_SOCKET_HPP_

#include <expected>
#include <span>

#include "hcomm/base/refptr.hpp"
#include "hcomm/base/unique_fd.hpp"
#include "hcomm/memory/paged_resource_pool.hpp"
#include "hcomm/promise/promise.hpp"
#include "hcomm/transport/tcp/address.hpp"
#include "hcomm/transport/tcp/error.hpp"
#include "hcomm/transport/tcp/executor.hpp"

namespace hcomm {
namespace tcp {
// forward
class Socket;
class IOExecutor;
class Listener;

namespace internal {
/// The continuation part of the `accept` promise.
///
/// This class encapsulates the logic for a single `accept` attempt. If `accept4` would block, it registers a waker to
/// be resumed later. If it succeeds, it creates and returns a new `Socket`.
class AcceptContinuation {
public:
    AcceptContinuation(RefPtr<Listener> listener) : listener_(std::move(listener)) {}

    Result<RefPtr<Socket>, NetworkError> operator()(Context& ctx);

private:
    RefPtr<Listener> listener_;
};
} // namespace internal

/// A non-blocking TCP socket for asynchronous I/O operations.
///
/// `Socket` is a reference-counted object that wraps a file descriptor. It provides promise-based methods for reading
/// and writing data that integrate with an `IOExecutor`. All operations are non-blocking. When an operation cannot
/// complete immediately (e.g., reading from an empty `sk_buf`, or writing to a full `sk_buf`), it returns a promise
/// that will be resolved when the socket is ready again.
///
/// The socket's lifetime is managed by `RefPtr`. When the last `RefPtr` to a `Socket` is destroyed, its file descriptor
/// is automatically closed and deregistered from the executor.
class Socket final : public RefCounted<Socket> {
public:
    Socket(int fd, ResourceId id, IOExecutor* exec) : fd_(fd), res_id_(id), executor_(exec) {}

    Socket(Socket&& rhs) noexcept = default;
    Socket& operator=(Socket&& rhs) noexcept = default;

    ~Socket();

    int fd() const {
        return fd_.get();
    }

    ResourceId id() const {
        return res_id_;
    }

    /// Asynchronously reads data from the socket into the provided buffer.
    ///
    /// This method returns a promise that resolves to the number of bytes read, or an error code. When the peer closes
    /// the connection (EOF), the promise resolves to `Err(NetworkError::kEOF)`.
    ///
    /// If the read operation cannot complete immediately (returns `EAGAIN`), it registers a waker with the
    /// `IOExecutor` and returns `Pending`. The promise will be resumed when the socket becomes readable again.
    auto read(std::span<char> buf) {
        return makePromise([buf, this, self = shared_from_this()](Context& ctx) -> Result<ssize_t, NetworkError> {
            auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());

            ssize_t nread = ::read(fd_.get(), buf.data(), buf.size());
            if (nread < 0) {
                if (errno == EAGAIN) {
                    exec->waitForRead(id(), ctx.waker());
                    return Pending{};
                }
                return Err(errno);
            } else if (nread == 0) {
                return Err(NetworkError::kEOF);
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
        return makePromise([buf, this, self = shared_from_this()](Context& ctx) -> Result<ssize_t, NetworkError> {
            auto* exec = reinterpret_cast<IOExecutor*>(ctx.executor());

            ssize_t nwrite = ::write(fd_.get(), buf.data(), buf.size());
            if (nwrite < 0) {
                if (errno == EAGAIN) {
                    exec->waitForWrite(id(), ctx.waker());
                    return Pending{};
                }
                return Err(errno);
            }
            return Ok(nwrite);
        });
    }

private:
    UniqueFd fd_;
    ResourceId res_id_;
    IOExecutor* executor_;
};

/// A non-blocking TCP listener for accepting incoming connections.
///
/// `Listener` wraps a listening socket file descriptor. It is used to bind to a specific address and port and to
/// accept new `Socket` connections asynchronously.
class Listener final : public RefCounted<Listener> {
public:
    Listener(int fd, ResourceId id, IOExecutor* exec) : fd_(fd), res_id_(id), executor_(exec) {}

    Listener(Listener&& rhs) noexcept = default;
    Listener& operator=(Listener&& rhs) noexcept = default;

    ~Listener();

    /// Creates a `Listener` by binding to a socket address and listening for incoming connections.
    ///
    /// This static method handles the creation of a non-blocking socket, sets reuse address and port options, binds it
    /// to the given `SocketAddress`, and puts it into the listening state.
    static std::expected<RefPtr<Listener>, NetworkError> bind(IOExecutor* exec, const SocketAddress& sa);

    int fd() const {
        return fd_.get();
    }

    ResourceId id() const {
        return res_id_;
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
        return PromiseImpl(internal::AcceptContinuation(shared_from_this()));
    }

private:
    UniqueFd fd_;
    ResourceId res_id_;
    IOExecutor* executor_;
};

} // namespace tcp
} // namespace hcomm

#endif // HCOMM_TRANSPORT_TCP_SOCKET_HPP_
