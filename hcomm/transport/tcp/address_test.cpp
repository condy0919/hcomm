// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/tcp/address.hpp"

#include <gtest/gtest.h>

namespace {
TEST(AddressTest, IPv4ConstructFromOctets) {
    hcomm::tcp::IPv4Address addr(127, 0, 0, 1);
    const auto& octets = addr.octets();
    EXPECT_EQ(octets[0], 127);
    EXPECT_EQ(octets[1], 0);
    EXPECT_EQ(octets[2], 0);
    EXPECT_EQ(octets[3], 1);

    struct in_addr native_addr = addr;
    EXPECT_EQ(ntohl(native_addr.s_addr), 0x7F000001);
}

TEST(AddressTest, IPv4ConstructFromUint32) {
    hcomm::tcp::IPv4Address addr(0xC0A80101); // 192.168.1.1
    const auto& octets = addr.octets();
    EXPECT_EQ(octets[0], 192);
    EXPECT_EQ(octets[1], 168);
    EXPECT_EQ(octets[2], 1);
    EXPECT_EQ(octets[3], 1);
    EXPECT_EQ(addr.as_uint32(), 0xC0A80101);
}

TEST(AddressTest, IPv4AsUint32) {
    hcomm::tcp::IPv4Address addr(1, 2, 3, 4);
    EXPECT_EQ(addr.as_uint32(), 0x01020304);
}

TEST(AddressTest, IPv4ConstructFromInAddr) {
    struct in_addr native_addr;
    native_addr.s_addr = htonl(0x08080808); // 8.8.8.8
    hcomm::tcp::IPv4Address addr(native_addr);
    const auto& octets = addr.octets();
    EXPECT_EQ(octets[0], 8);
    EXPECT_EQ(octets[1], 8);
    EXPECT_EQ(octets[2], 8);
    EXPECT_EQ(octets[3], 8);
    EXPECT_EQ(addr.as_uint32(), 0x08080808);
}

TEST(AddressTest, IPv6DefaultConstruct) {
    hcomm::tcp::IPv6Address addr; // Should be ::
    const auto& octets = addr.octets();
    for (uint8_t octet : octets) {
        EXPECT_EQ(octet, 0);
    }
}

TEST(AddressTest, IPv6ConstructFromOctets) {
    hcomm::tcp::IPv6Address addr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1); // ::1
    const auto& octets = addr.octets();
    for (size_t i = 0; i < 15; ++i) {
        EXPECT_EQ(octets[i], 0);
    }
    EXPECT_EQ(octets[15], 1);

    const auto& segments = addr.segments();
    for (size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(segments[i], 0);
    }
    EXPECT_EQ(segments[7], 1);
}

TEST(AddressTest, IPv6ConstructFromSegments) {
    hcomm::tcp::IPv6Address addr(0x2001, 0x0db8, 0x85a3, 0x0000, 0x0000, 0x8a2e, 0x0370, 0x7334);
    const auto& segments = addr.segments();
    EXPECT_EQ(segments[0], 0x2001);
    EXPECT_EQ(segments[1], 0x0db8);
    EXPECT_EQ(segments[2], 0x85a3);
    EXPECT_EQ(segments[5], 0x8a2e);
    EXPECT_EQ(segments[7], 0x7334);

    const auto& octets = addr.octets();
    EXPECT_EQ(octets[0], 0x20);
    EXPECT_EQ(octets[1], 0x01);
    EXPECT_EQ(octets[2], 0x0d);
    EXPECT_EQ(octets[3], 0xb8);
}

TEST(AddressTest, IPv6ConstructFromUint32s) {
    hcomm::tcp::IPv6Address addr(0x20010db8, 0x85a30000, 0x00008a2e, 0x03707334);
    const auto& segments = addr.segments();
    EXPECT_EQ(segments[0], 0x2001);
    EXPECT_EQ(segments[1], 0x0db8);
    EXPECT_EQ(segments[2], 0x85a3);
    EXPECT_EQ(segments[3], 0x0000);
    EXPECT_EQ(segments[4], 0x0000);
    EXPECT_EQ(segments[5], 0x8a2e);
    EXPECT_EQ(segments[6], 0x0370);
    EXPECT_EQ(segments[7], 0x7334);
}

TEST(AddressTest, IPv6ConstructFromIn6Addr) {
    struct in6_addr native_addr = IN6ADDR_LOOPBACK_INIT;
    hcomm::tcp::IPv6Address addr(native_addr);
    const auto& octets = addr.octets();
    EXPECT_EQ(octets[15], 1);
    const auto& segments = addr.segments();
    EXPECT_EQ(segments[7], 1);
}

TEST(AddressTest, IPAddressDefaultConstruct) {
    hcomm::tcp::IPAddress addr;
    EXPECT_TRUE(addr.is_empty());
    EXPECT_FALSE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv6());
    EXPECT_EQ(addr.family(), AF_UNSPEC);
}

TEST(AddressTest, IPAddressConstructWithIPv4) {
    hcomm::tcp::IPv4Address v4(192, 168, 0, 1);
    hcomm::tcp::IPAddress addr(v4);
    EXPECT_FALSE(addr.is_empty());
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv6());
    EXPECT_EQ(addr.family(), AF_INET);
    EXPECT_EQ(addr.as_ipv4().octets()[0], 192);
}

TEST(AddressTest, IPAddressConstructWithIPv6) {
    hcomm::tcp::IPv6Address v6(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
    hcomm::tcp::IPAddress addr(v6);
    EXPECT_FALSE(addr.is_empty());
    EXPECT_FALSE(addr.is_ipv4());
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_EQ(addr.family(), AF_INET6);
    EXPECT_EQ(addr.as_ipv6().octets()[15], 1);
}

TEST(AddressTest, IPAddressToSockaddrIPv4) {
    hcomm::tcp::IPAddress addr(hcomm::tcp::IPv4Address(127, 0, 0, 1));
    uint16_t port = 8080;
    struct sockaddr_storage ss = addr.to_sockaddr(port);

    EXPECT_EQ(ss.ss_family, AF_INET);
    auto* sin = reinterpret_cast<struct sockaddr_in*>(&ss);
    EXPECT_EQ(sin->sin_port, htons(port));
    EXPECT_EQ(ntohl(sin->sin_addr.s_addr), 0x7F000001);
}

TEST(AddressTest, IPAddressToSockaddrIPv6) {
    hcomm::tcp::IPAddress addr(hcomm::tcp::IPv6Address(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1));
    uint16_t port = 9090;
    struct sockaddr_storage ss = addr.to_sockaddr(port);

    EXPECT_EQ(ss.ss_family, AF_INET6);
    auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(&ss);
    EXPECT_EQ(sin6->sin6_port, htons(port));
    EXPECT_EQ(sin6->sin6_addr.s6_addr[15], 1);
}

TEST(AddressTest, SocketAddressDefaultConstructor) {
    hcomm::tcp::SocketAddress addr;
    EXPECT_TRUE(addr.ip().is_empty());
    EXPECT_EQ(addr.port(), 0);
}

TEST(AddressTest, SocketAddressIPAddressAndPortConstructor) {
    hcomm::tcp::IPv4Address ipv4(127, 0, 0, 1);
    hcomm::tcp::IPAddress ip(ipv4);
    hcomm::tcp::SocketAddress addr(ip, 8080);

    EXPECT_TRUE(addr.ip().is_ipv4());
    EXPECT_EQ(addr.port(), 8080);
}

TEST(AddressTest, SocketAddressFromSockAddrIPv4) {
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(1234);
    sin.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1

    hcomm::tcp::SocketAddress addr(reinterpret_cast<const struct sockaddr&>(sin));

    EXPECT_TRUE(addr.ip().is_ipv4());
    EXPECT_EQ(addr.port(), 1234);
    EXPECT_EQ(addr.ip().as_ipv4().as_uint32(), 0x7F000001);
}

TEST(AddressTest, SocketAddressFromSockAddrIPv6) {
    struct sockaddr_in6 sin6;
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(5678);
    // ::1
    sin6.sin6_addr = {{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}};

    hcomm::tcp::SocketAddress addr(reinterpret_cast<const struct sockaddr&>(sin6));

    EXPECT_TRUE(addr.ip().is_ipv6());
    EXPECT_EQ(addr.port(), 5678);
    auto segments = addr.ip().as_ipv6().segments();
    EXPECT_EQ(segments[0], 0);
    EXPECT_EQ(segments[1], 0);
    EXPECT_EQ(segments[2], 0);
    EXPECT_EQ(segments[3], 0);
    EXPECT_EQ(segments[4], 0);
    EXPECT_EQ(segments[5], 0);
    EXPECT_EQ(segments[6], 0);
    EXPECT_EQ(segments[7], 1);
}

TEST(AddressTest, SocketAddressToSockAddrStorageIPv4) {
    hcomm::tcp::IPAddress ip(hcomm::tcp::IPv4Address(192, 168, 1, 100));
    hcomm::tcp::SocketAddress addr(ip, 9999);

    struct sockaddr_storage ss = addr;
    EXPECT_EQ(ss.ss_family, AF_INET);

    const auto& sin = reinterpret_cast<const struct sockaddr_in&>(ss);
    EXPECT_EQ(sin.sin_port, htons(9999));
    EXPECT_EQ(ntohl(sin.sin_addr.s_addr), 0xC0A80164); // 192.168.1.100
}

TEST(AddressTest, SocketAddressToSockAddrStorageIPv6) {
    hcomm::tcp::IPAddress ip(hcomm::tcp::IPv6Address(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1));
    hcomm::tcp::SocketAddress addr(ip, 10000);

    struct sockaddr_storage ss = addr;
    EXPECT_EQ(ss.ss_family, AF_INET6);

    const auto& sin6 = reinterpret_cast<const struct sockaddr_in6&>(ss);
    EXPECT_EQ(sin6.sin6_port, htons(10000));
    auto segments = hcomm::tcp::IPv6Address(sin6.sin6_addr).segments();
    EXPECT_EQ(segments[0], 0x2001);
    EXPECT_EQ(segments[1], 0xdb8);
    EXPECT_EQ(segments[7], 1);
}

TEST(AddressTest, IPAddressToSockAddrUnspecified) {
    hcomm::tcp::IPAddress ip; // AF_UNSPEC by default
    std::uint16_t port = 12345;
    struct sockaddr_storage ss = ip.to_sockaddr(port);

    EXPECT_EQ(ss.ss_family, AF_UNSPEC);
    // Other fields should be zeroed out
    struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(&ss);
    EXPECT_EQ(sin->sin_port, 0);
    EXPECT_EQ(sin->sin_addr.s_addr, 0);
}

TEST(AddressTest, SocketAddressFromSockAddrUnspecified) {
    struct sockaddr generic_addr;
    generic_addr.sa_family = AF_UNSPEC; // Or any other family not AF_INET/AF_INET6

    hcomm::tcp::SocketAddress addr(generic_addr);

    EXPECT_TRUE(addr.ip().is_empty());
    EXPECT_EQ(addr.port(), 0);
}
} // namespace
