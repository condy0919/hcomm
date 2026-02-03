// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/scope_exit.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ScopedExitTest, Basic) {
    bool executed = false;
    {
        hcomm::ScopeExit se([&] { executed = true; });
        EXPECT_FALSE(executed);
    }
    EXPECT_TRUE(executed);
}

TEST(ScopedExitTest, Cancel) {
    bool executed = false;
    {
        hcomm::ScopeExit se([&] { executed = true; });
        se.cancel();
    }
    EXPECT_FALSE(executed);
}

TEST(ScopedExitTest, MoveConstruction) {
    bool executed = false;
    {
        hcomm::ScopeExit se1([&] { executed = true; });
        {
            hcomm::ScopeExit se2(std::move(se1));
            EXPECT_FALSE(executed);
        }
        EXPECT_TRUE(executed);
    }
    EXPECT_TRUE(executed);
}

TEST(ScopedExitTest, MoveDoesNotDoubleExecute) {
    int executed_count = 0;
    {
        hcomm::ScopeExit se1([&] { executed_count++; });
        hcomm::ScopeExit se2(std::move(se1));
    }
    EXPECT_EQ(executed_count, 1);
}

} // namespace
