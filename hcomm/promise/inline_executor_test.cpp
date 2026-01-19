// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/promise/inline_executor.hpp"

#include <string>

#include <gtest/gtest.h>

namespace {
class InlineExecutorTest : public testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}

protected:
    hcomm::InlineExecutor iexec_;
};

TEST_F(InlineExecutorTest, schedule) {
    iexec_.schedule(hcomm::makeOkPromise<std::string>("inline")
                        .andThen([](const std::string& s) -> hcomm::Result<int> { return hcomm::Ok(s.size()); })
                        .then([](const hcomm::Result<int>& result) -> hcomm::Result<> {
                            EXPECT_TRUE(result.isOk());
                            EXPECT_EQ(result.value(), 6);
                            return hcomm::Ok();
                        }));

    iexec_.schedule(hcomm::makeErrPromise(-1)
                        .andThen([]() -> hcomm::Result<void, int> {
                            EXPECT_TRUE(false);
                            return hcomm::Ok();
                        })
                        .orElse([](hcomm::Context& ctx, const int& err) -> hcomm::Result<> {
                            EXPECT_THROW(ctx.waker(), std::runtime_error);
                            return hcomm::Err();
                        }));
}

} // namespace
