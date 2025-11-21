// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/status.hpp"

#include <gtest/gtest.h>

namespace {
TEST(Status, CheckOk) {
    hcomm::Status status(hcomm::StatusCode::Ok);
    EXPECT_TRUE(status.ok());
}

TEST(Status, CheckCode) {
    hcomm::Status status(hcomm::StatusCode::InvalidArgument, "invalid argument");
    EXPECT_EQ(status.code(), hcomm::StatusCode::InvalidArgument);
}

} // namespace
