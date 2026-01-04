// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/promise.hpp"

#include <gtest/gtest.h>

namespace {
TEST(Promise, box) {
    hcomm::Promise<> f;

    auto b = f.box();
}
}
