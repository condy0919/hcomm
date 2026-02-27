// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/transport/tcp/error.hpp"

#include <cerrno>
#include <string>

#include <gtest/gtest.h>

namespace {

using hcomm::tcp::network_error_category;
using hcomm::tcp::NetworkError;

TEST(NetworkErrorTest, DefaultConstructor) {
    NetworkError err;
    EXPECT_EQ(static_cast<int>(err), 0);
}

TEST(NetworkErrorTest, ConstructorWithErrorCode) {
    NetworkError err(EADDRINUSE);
    EXPECT_EQ(static_cast<int>(err), EADDRINUSE);
}

TEST(NetworkErrorTest, CategoryName) {
    const auto& category = network_error_category();
    EXPECT_STREQ(category.name(), "TCPNetworkCategory");
}

TEST(NetworkErrorTest, Messages) {
    const auto& category = network_error_category();

    // Test some standard errno values
    EXPECT_EQ(category.message(EPERM), "Operation not permitted");
    EXPECT_EQ(category.message(EAGAIN), "Resource temporarily unavailable");
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    EXPECT_EQ(category.message(EWOULDBLOCK), "Resource temporarily unavailable");
#endif
    EXPECT_EQ(category.message(ECONNREFUSED), "Connection refused");
    EXPECT_EQ(category.message(ETIMEDOUT), "Connection timed out");

    // Test custom error codes
    EXPECT_EQ(category.message(NetworkError::kEOF), "EOF");
    EXPECT_EQ(category.message(NetworkError::kUnexpectedEOF), "Unexpected EOF");

    // Test unknown error code
    int unknown_code = 9999;
    std::string expected_unknown = "Unknown error (9999)";
    EXPECT_EQ(category.message(unknown_code), expected_unknown);
}

TEST(NetworkErrorTest, MakeErrorCode) {
    std::error_code ec = NetworkError(ECONNRESET);
    EXPECT_EQ(ec.value(), ECONNRESET);
    EXPECT_EQ(&ec.category(), &network_error_category());
    EXPECT_EQ(ec.message(), "Connection reset by peer");
}

TEST(NetworkErrorTest, StdErrorCodeIntegration) {
    std::error_code ec = NetworkError(NetworkError::kEOF);
    EXPECT_EQ(ec.value(), NetworkError::kEOF);
    EXPECT_EQ(ec.message(), "EOF");
}

TEST(NetworkErrorTest, Comparison) {
    std::error_code ec1 = NetworkError(EACCES);
    std::error_code ec2 = NetworkError(EACCES);
    std::error_code ec3 = NetworkError(EBADF);

    EXPECT_EQ(ec1, ec2);
    EXPECT_NE(ec1, ec3);
}

} // namespace
