// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_TRANSPORT_TCP_ADDRESS_HPP_
#define HCOMM_TRANSPORT_TCP_ADDRESS_HPP_

#include <netinet/in.h>

#include <array>
#include <concepts>
#include <cstdint>

namespace hcomm {
namespace tcp {
/// Represents a 128-bit IPv6 address, providing a safer and more modern C++ interface around the underlying `in6_addr`
/// struct. It simplifies the creation and manipulation of IPv6 addresses by allowing construction from various integer
/// types and providing easy access to its components.
///
/// Example:
/// ```c++
/// // Create an IPv6 address for localhost (::1)
/// hcomm::tcp::IPv6Address loopback(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
///
/// // Create from 16-bit segments
/// hcomm::tcp::IPv6Address addr(0x2001, 0x0db8, 0x85a3, 0, 0, 0x8a2e, 0x0370, 0x7334);
///
/// // Access octets
/// const auto& octets = addr.octets();
/// for(std::uint8_t octet : octets) {
///   // ...
/// }
/// ```
class IPv6Address {
public:
    IPv6Address() : IPv6Address(0u, 0u, 0u, 0u) {}

    IPv6Address(struct in6_addr addr) : raw_(addr) {}

    template <std::convertible_to<std::uint8_t>... Ts>
        requires(sizeof...(Ts) == 16)
    IPv6Address(Ts... u8s) : addr8_{static_cast<std::uint8_t>(u8s)...} {}

    template <std::convertible_to<std::uint16_t>... Ts>
        requires(sizeof...(Ts) == 8)
    IPv6Address(Ts... u16s) : addr16_{::htons(static_cast<std::uint16_t>(u16s))...} {}

    template <std::convertible_to<std::uint32_t>... Ts>
        requires(sizeof...(Ts) == 4)
    IPv6Address(Ts... u32s) : addr32_{::htonl(static_cast<std::uint32_t>(u32s))...} {}

    std::array<std::uint16_t, 8> segments() const {
        std::array<std::uint16_t, 8> result;
        for (size_t i = 0; i < 8; ++i) {
            result[i] = ::ntohs(addr16_[i]);
        }
        return result;
    }

    const std::array<std::uint8_t, 16> octets() const {
        return reinterpret_cast<const std::array<std::uint8_t, 16>&>(addr8_);
    }

    operator struct in6_addr() const {
        return raw_;
    }

private:
    static_assert(sizeof(struct in6_addr) == sizeof(std::uint32_t) * 4);

    union {
        struct in6_addr raw_;
        std::uint8_t addr8_[16];
        std::uint16_t addr16_[8];
        std::uint32_t addr32_[4];
    };
};

/// Represents a 32-bit IPv4 address, acting as a C++-friendly wrapper for the traditional `in_addr` struct. This class
/// facilitates creating IPv4 addresses from common representations like individual octets or a single 32-bit integer,
/// handling network byte order conversions internally.
///
/// Example:
/// ```c++
/// // Create the localhost address (127.0.0.1)
/// hcomm::tcp::IPv4Address loopback(127, 0, 0, 1);
///
/// // Create from a 32-bit integer in host byte order
/// hcomm::tcp::IPv4Address from_int(0x7F000001); // 127.0.0.1
///
/// // Access octets
/// const auto& octets = loopback.octets();
/// // octets[0] == 127
/// ```
class IPv4Address {
public:
    IPv4Address(struct in_addr addr) : raw_(addr) {}

    IPv4Address(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)
        : IPv4Address((std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) |
                      std::uint32_t(d)) {}

    explicit IPv4Address(std::uint32_t host_order_ip) : addr32_(::htonl(host_order_ip)) {}

    const std::array<std::uint8_t, 4> octets() const {
        return reinterpret_cast<const std::array<std::uint8_t, 4>&>(addr8_);
    }

    std::uint32_t as_uint32() const {
        return ::ntohl(addr32_);
    }

    operator struct in_addr() const {
        return raw_;
    }

private:
    static_assert(sizeof(struct in_addr) == sizeof(std::uint32_t));

    union {
        struct in_addr raw_;
        std::uint8_t addr8_[4];
        std::uint32_t addr32_;
    };
};

/// A type-safe container for either an IPv4 or an IPv6 address. It abstracts away the details of the underlying
/// address family, providing a unified interface for handling IP addresses. This is particularly useful in
/// applications that need to support both protocol versions.
///
/// Example:
/// ```c++
/// hcomm::tcp::IPAddress v4_addr(hcomm::tcp::IPv4Address(192, 168, 1, 1));
/// hcomm::tcp::IPAddress v6_addr(hcomm::tcp::IPv6Address(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1));
///
/// if (v4_addr.is_ipv4()) {
///   const auto& ipv4 = v4_addr.as_ipv4();
///   // ...
/// }
///
/// // Convert to a generic socket address
/// std::uint16_t port = 8080;
/// struct sockaddr_storage ss = v6_addr.to_sockaddr(port);
/// ```
class IPAddress {
public:
    IPAddress() : family_(AF_UNSPEC), unused_(0) {}

    IPAddress(const IPv4Address v4) : family_(AF_INET), v4_(v4) {}

    IPAddress(const IPv6Address v6) : family_(AF_INET6), v6_(v6) {}

    int family() const {
        return family_;
    }

    bool is_empty() const {
        return family_ == AF_UNSPEC;
    }

    bool is_ipv4() const {
        return family_ == AF_INET;
    }

    bool is_ipv6() const {
        return family_ == AF_INET6;
    }

    const IPv4Address& as_ipv4() const {
        return v4_;
    }

    const IPv6Address& as_ipv6() const {
        return v6_;
    }

    struct sockaddr_storage to_sockaddr(std::uint16_t port) const {
        struct sockaddr_storage addr = {};

        addr.ss_family = static_cast<unsigned short>(family_);
        if (is_ipv4()) {
            auto& sin = reinterpret_cast<struct sockaddr_in&>(addr);
            sin.sin_addr = as_ipv4();
            sin.sin_port = ::htons(port);
        } else if (is_ipv6()) {
            auto& sin = reinterpret_cast<struct sockaddr_in6&>(addr);
            sin.sin6_addr = as_ipv6();
            sin.sin6_port = ::htons(port);
        } else {
            // AF_UNSPEC
        }

        return addr;
    }

private:
    int family_ = AF_UNSPEC;
    union {
        char unused_;
        IPv4Address v4_;
        IPv6Address v6_;
    };
};

/// Represents a complete network endpoint, combining an IP address and a port number. It provides a high-level,
/// type-safe abstraction over the underlying socket address structures (`sockaddr_in`, `sockaddr_in6`). This class
/// simplifies network programming by handling the details of different address families (IPv4/IPv6) in a unified way.
///
/// Example:
/// ```c++
/// // Create a socket address for an IPv4 endpoint
/// hcomm::tcp::IPAddress v4_addr(hcomm::tcp::IPv4Address(192, 168, 1, 1));
/// hcomm::tcp::SocketAddress socket_addr(v4_addr, 8080);
///
/// // Access address and port
/// const auto& ip = socket_addr.ip();
/// std::uint16_t port = socket_addr.port();
///
/// // It can be implicitly converted to a sockaddr_storage
/// struct sockaddr_storage ss = socket_addr;
/// ```
class SocketAddress {
public:
    SocketAddress() = default;

    SocketAddress(IPAddress ip, std::uint16_t port) : ip_(ip), port_(port) {}

    SocketAddress(const struct sockaddr& addr) {
        if (addr.sa_family == AF_INET) {
            const auto& sin = reinterpret_cast<const struct sockaddr_in&>(addr);
            ip_ = IPv4Address(sin.sin_addr);
            port_ = ::ntohs(sin.sin_port);
        } else if (addr.sa_family == AF_INET6) {
            const auto& sin6 = reinterpret_cast<const struct sockaddr_in6&>(addr);
            ip_ = IPv6Address(sin6.sin6_addr);
            port_ = ::ntohs(sin6.sin6_port);
        }
    }

    const IPAddress& ip() const {
        return ip_;
    }

    std::uint16_t port() const {
        return port_;
    }

    operator struct sockaddr_storage() const {
        return ip_.to_sockaddr(port_);
    }

private:
    IPAddress ip_;
    std::uint16_t port_ = 0;
};

} // namespace tcp
} // namespace hcomm

#endif // HCOMM_TRANSPORT_TCP_ADDRESS_HPP_
