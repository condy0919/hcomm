# SPDX-License-Identifier: MulanPSL-2.0

target_sources(hcomm
  PUBLIC
    single_threaded_executor.cpp
)

if(HCOMM_BUILD_TESTS)
  add_executable(hcomm_promise_test
    result_test.cpp
    promise_test.cpp
    scope_test.cpp
    single_threaded_executor_test.cpp
    inline_executor_test.cpp
  )
  target_link_libraries(hcomm_promise_test
    PRIVATE
      hcomm::hcomm
      GTest::gtest_main
  )
  add_test(NAME hcomm_promise_test COMMAND hcomm_promise_test)
endif()
