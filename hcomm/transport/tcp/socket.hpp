// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_SOCKET_HPP_
#define HCOMM_TRANSPORT_TCP_SOCKET_HPP_

#include <cstdint>
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

/// Continuation for a single asynchronous read operation.
///
/// It attempts to read data from the socket into the provided buffer. If the socket is not ready for reading, it
/// registers a waker and returns `Pending`.
class ReadContinuation {
public:
    ReadContinuation(RefPtr<Socket> sk, std::span<char> buf) : sk_(std::move(sk)), buf_(buf) {}

    Result<ssize_t, NetworkError> operator()(Context& ctx);

private:
    RefPtr<Socket> sk_;
    std::span<char> buf_;
};

/// Continuation for an asynchronous read-exact operation.
///
/// It continues to read data until the buffer is completely filled. It handles partial reads and `EAGAIN` by storing
/// the current offset and registering a waker to be resumed when more data is available.
class ReadExactContinuation {
public:
    ReadExactContinuation(RefPtr<Socket> sk, std::span<char> buf) : sk_(std::move(sk)), buf_(buf) {}

    Result<void, NetworkError> operator()(Context& ctx);

private:
    RefPtr<Socket> sk_;
    std::span<char> buf_;
    std::uint32_t offset_ = 0;
};

/// Continuation for a single asynchronous write operation.
///
/// It attempts to write data from the buffer to the socket. If the socket's send buffer is full, it registers a waker
/// and returns `Pending`.
class WriteContinuation {
public:
    WriteContinuation(RefPtr<Socket> sk, std::span<char> buf) : sk_(std::move(sk)), buf_(buf) {}

    Result<ssize_t, NetworkError> operator()(Context& ctx);

private:
    RefPtr<Socket> sk_;
    std::span<char> buf_;
};

/// Continuation for an asynchronous write-all operation.
///
/// It continues to write data until the entire buffer is sent. It handles partial writes and `EAGAIN` by storing the
/// current offset and registering a waker to be resumed when the socket is writable again.
class WriteAllContinuation {
public:
    WriteAllContinuation(RefPtr<Socket> sk, std::span<char> buf) : sk_(std::move(sk)), buf_(buf) {}

    Result<void, NetworkError> operator()(Context& ctx);

private:
    RefPtr<Socket> sk_;
    std::span<char> buf_;
    std::uint32_t offset_ = 0;
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
        return PromiseImpl(internal::ReadContinuation(shared_from_this(), buf));
    }

    /// Asynchronously reads exactly the number of bytes required to fill the buffer.
    ///
    /// This method returns a promise that resolves only when the entire buffer is filled. If the peer closes the
    /// connection before the buffer is full, the promise resolves to `Err(NetworkError::kUnexpectedEOF)`.
    auto readExact(std::span<char> buf) {
        return PromiseImpl(internal::ReadExactContinuation(shared_from_this(), buf));
    }

    /// Asynchronously writes data from the provided buffer to the socket.
    ///
    /// This method returns a promise that resolves to the number of bytes written, or an error code (errno).
    ///
    /// If the write operation cannot complete immediately (returns `EAGAIN`), it registers a waker with the
    /// `IOExecutor` and returns `Pending`. The promise will be resumed when the socket becomes writable again. It is
    /// the caller's responsibility to handle partial writes by calling `write` again with the remaining data.
    auto write(std::span<char> buf) {
        return PromiseImpl(internal::WriteContinuation(shared_from_this(), buf));
    }

    /// Asynchronously writes the entire contents of the buffer to the socket.
    ///
    /// This method returns a promise that resolves only when all data in the buffer has been successfully sent. It
    /// handles partial writes and `EAGAIN` internally.
    auto writeAll(std::span<char> buf) {
        return PromiseImpl(internal::WriteAllContinuation(shared_from_this(), buf));
    }

    /// Shuts down part of a full-duplex connection.
    ///
    /// This method provides a mechanism for graceful termination of the TCP connection. Unlike closing the socket,
    /// `shutdown` can close one direction of the connection while keeping the other open.
    ///
    /// To perform a graceful close:
    /// 1. Call `shutdown(SHUT_WR)` to signal EOF to the peer, indicating that no more data will be sent.
    /// 2. Continue to `read` from the socket until `NetworkError::kEOF` is received. This confirms the peer has
    ///    received all data and has also finished its transmission.
    /// 3. Destroy the `Socket` object to release system resources.
    ///
    /// This procedure prevents potential data loss and TCP Reset (RST) errors that can occur when a socket is
    /// closed while unread data remains in the receive buffer.
    ///
    /// The `how` parameter (typically `SHUT_RD`, `SHUT_WR`, or `SHUT_RDWR` from `<sys/socket.h>`) specifies which part
    /// of the connection to shut down. `SHUT_WR` is the most common choice for initiating a graceful close as it
    /// sends a FIN packet to the peer.
    std::expected<void, NetworkError> shutdown(int how);

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
    auto accept() {
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
