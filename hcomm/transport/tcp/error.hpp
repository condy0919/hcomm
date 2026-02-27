// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_ERROR_HPP_
#define HCOMM_TRANSPORT_TCP_ERROR_HPP_

#include <cerrno>
#include <string>
#include <system_error>

namespace hcomm {
namespace tcp {
/// Represents a network-level error, wrapping standard system error codes and providing specific TCP-related constants
/// like EOF.
class NetworkError {
public:
    NetworkError() = default;
    NetworkError(int err) : err_(err) {}

    static constexpr int kEOF = 1000;
    static constexpr int kUnexpectedEOF = 1001;

    operator int() const {
        return err_;
    }

private:
    int err_ = 0;
};

/// Implementation of std::error_category for TCP network errors.
/// This class maps system-level errno values and custom NetworkError codes to human-readable strings.
class NetworkErrorCategoryImpl : public std::error_category {
public:
    const char* name() const noexcept override {
        return "TCPNetworkCategory";
    }

    std::string message(int ev) const override {
        switch (ev) {
        case EPERM:
            return "Operation not permitted";
        case ENOENT:
            return "No such file or directory";
        case ESRCH:
            return "No such process";
        case EINTR:
            return "Interrupted system call";
        case EIO:
            return "Input/output error";
        case ENXIO:
            return "No such device or address";
        case E2BIG:
            return "Argument list too long";
        case ENOEXEC:
            return "Exec format error";
        case EBADF:
            return "Bad file descriptor";
        case ECHILD:
            return "No child processes";
        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return "Resource temporarily unavailable";
        case ENOMEM:
            return "Cannot allocate memory";
        case EACCES:
            return "Permission denied";
        case EFAULT:
            return "Bad address";
        case ENOTBLK:
            return "Block device required";
        case EBUSY:
            return "Device or resource busy";
        case EEXIST:
            return "File exists";
        case EXDEV:
            return "Invalid cross-device link";
        case ENODEV:
            return "No such device";
        case ENOTDIR:
            return "Not a directory";
        case EISDIR:
            return "Is a directory";
        case EINVAL:
            return "Invalid argument";
        case ENFILE:
            return "Too many open files in system";
        case EMFILE:
            return "Too many open files";
        case ENOTTY:
            return "Inappropriate ioctl for device";
        case ETXTBSY:
            return "Text file busy";
        case EFBIG:
            return "File too large";
        case ENOSPC:
            return "No space left on device";
        case ESPIPE:
            return "Illegal seek";
        case EROFS:
            return "Read-only file system";
        case EMLINK:
            return "Too many links";
        case EPIPE:
            return "Broken pipe";
        case EDOM:
            return "Numerical argument out of domain";
        case ERANGE:
            return "Numerical result out of range";
        case EDEADLK:
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
        case EDEADLOCK:
#endif
            return "Resource deadlock avoided";
        case ENAMETOOLONG:
            return "File name too long";
        case ENOLCK:
            return "No locks available";
        case ENOSYS:
            return "Function not implemented";
        case ENOTEMPTY:
            return "Directory not empty";
        case ELOOP:
            return "Too many levels of symbolic links";
        case ENOMSG:
            return "No message of desired type";
        case EIDRM:
            return "Identifier removed";
        case ECHRNG:
            return "Channel number out of range";
        case EL2NSYNC:
            return "Level 2 not synchronized";
        case EL3HLT:
            return "Level 3 halted";
        case EL3RST:
            return "Level 3 reset";
        case ELNRNG:
            return "Link number out of range";
        case EUNATCH:
            return "Protocol driver not attached";
        case ENOCSI:
            return "No CSI structure available";
        case EL2HLT:
            return "Level 2 halted";
        case EBADE:
            return "Invalid exchange";
        case EBADR:
            return "Invalid request descriptor";
        case EXFULL:
            return "Exchange full";
        case ENOANO:
            return "No anode";
        case EBADRQC:
            return "Invalid request code";
        case EBADSLT:
            return "Invalid slot";
        case EBFONT:
            return "Bad font file format";
        case ENOSTR:
            return "Device not a stream";
        case ENODATA:
            return "No data available";
        case ETIME:
            return "Timer expired";
        case ENOSR:
            return "Out of streams resources";
        case ENONET:
            return "Machine is not on the network";
        case ENOPKG:
            return "Package not installed";
        case EREMOTE:
            return "Object is remote";
        case ENOLINK:
            return "Link has been severed";
        case EADV:
            return "Advertise error";
        case ESRMNT:
            return "Srmount error";
        case ECOMM:
            return "Communication error on send";
        case EPROTO:
            return "Protocol error";
        case EMULTIHOP:
            return "Multihop attempted";
        case EDOTDOT:
            return "RFS specific error";
        case EBADMSG:
            return "Bad message";
        case EOVERFLOW:
            return "Value too large for defined data type";
        case ENOTUNIQ:
            return "Name not unique on network";
        case EBADFD:
            return "File descriptor in bad state";
        case EREMCHG:
            return "Remote address changed";
        case ELIBACC:
            return "Can not access a needed shared library";
        case ELIBBAD:
            return "Accessing a corrupted shared library";
        case ELIBSCN:
            return ".lib section in a.out corrupted";
        case ELIBMAX:
            return "Attempting to link in too many shared libraries";
        case ELIBEXEC:
            return "Cannot exec a shared library directly";
        case EILSEQ:
            return "Invalid or incomplete multibyte or wide character";
        case ERESTART:
            return "Interrupted system call should be restarted";
        case ESTRPIPE:
            return "Streams pipe error";
        case EUSERS:
            return "Too many users";
        case ENOTSOCK:
            return "Socket operation on non-socket";
        case EDESTADDRREQ:
            return "Destination address required";
        case EMSGSIZE:
            return "Message too long";
        case EPROTOTYPE:
            return "Protocol wrong type for socket";
        case ENOPROTOOPT:
            return "Protocol not available";
        case EPROTONOSUPPORT:
            return "Protocol not supported";
        case ESOCKTNOSUPPORT:
            return "Socket type not supported";
        case EOPNOTSUPP:
#if defined(ENOTSUP) && ENOTSUP != EOPNOTSUPP
        case ENOTSUP:
#endif
            return "Operation not supported";
        case EPFNOSUPPORT:
            return "Protocol family not supported";
        case EAFNOSUPPORT:
            return "Address family not supported by protocol";
        case EADDRINUSE:
            return "Address already in use";
        case EADDRNOTAVAIL:
            return "Cannot assign requested address";
        case ENETDOWN:
            return "Network is down";
        case ENETUNREACH:
            return "Network is unreachable";
        case ENETRESET:
            return "Network dropped connection on reset";
        case ECONNABORTED:
            return "Software caused connection abort";
        case ECONNRESET:
            return "Connection reset by peer";
        case ENOBUFS:
            return "No buffer space available";
        case EISCONN:
            return "Transport endpoint is already connected";
        case ENOTCONN:
            return "Transport endpoint is not connected";
        case ESHUTDOWN:
            return "Cannot send after transport endpoint shutdown";
        case ETOOMANYREFS:
            return "Too many references: cannot splice";
        case ETIMEDOUT:
            return "Connection timed out";
        case ECONNREFUSED:
            return "Connection refused";
        case EHOSTDOWN:
            return "Host is down";
        case EHOSTUNREACH:
            return "No route to host";
        case EALREADY:
            return "Operation already in progress";
        case EINPROGRESS:
            return "Operation now in progress";
        case ESTALE:
            return "Stale file handle";
        case EUCLEAN:
            return "Structure needs cleaning";
        case ENOTNAM:
            return "Not a XENIX named type file";
        case ENAVAIL:
            return "No XENIX semaphores available";
        case EISNAM:
            return "Is a named type file";
        case EREMOTEIO:
            return "Remote I/O error";
        case EDQUOT:
            return "Disk quota exceeded";
        case ENOMEDIUM:
            return "No medium found";
        case EMEDIUMTYPE:
            return "Wrong medium type";
        case ECANCELED:
            return "Operation canceled";
        case ENOKEY:
            return "Required key not available";
        case EKEYEXPIRED:
            return "Key has expired";
        case EKEYREVOKED:
            return "Key has been revoked";
        case EKEYREJECTED:
            return "Key was rejected by service";
        case EOWNERDEAD:
            return "Owner died";
        case ENOTRECOVERABLE:
            return "State not recoverable";
        case ERFKILL:
            return "Operation not possible due to RF-kill";
        case EHWPOISON:
            return "Memory page has hardware error";
        case NetworkError::kEOF:
            return "EOF";
        case NetworkError::kUnexpectedEOF:
            return "Unexpected EOF";
        default:
            return "Unknown error (" + std::to_string(ev) + ")";
        }
    }
};

/// Returns a global instance of the TCP network error category.
/// This singleton is used to identify error codes originating from TCP transport.
inline const std::error_category& network_error_category() {
    static NetworkErrorCategoryImpl impl;
    return impl;
}

/// Creates a `std::error_code` from a `NetworkError`.
/// This allows NetworkError to integrate seamlessly with the standard C++ error handling infrastructure.
inline std::error_code make_error_code(NetworkError e) {
    return {static_cast<int>(e), network_error_category()};
}
} // namespace tcp
} // namespace hcomm

namespace std {
template <>
struct is_error_code_enum<hcomm::tcp::NetworkError> : true_type {};
} // namespace std

#endif // HCOMM_TRANSPORT_TCP_ERROR_HPP_
