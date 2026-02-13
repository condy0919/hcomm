# SPDX-License-Identifier: MulanPSL-2.0

if(HCOMM_BUILD_TESTS)
  add_executable(hcomm_memory_test
    paged_resource_pool_test.cpp
  )
  target_link_libraries(hcomm_memory_test
    PRIVATE
      hcomm::hcomm
      GTest::gtest_main
    )
  add_test(NAME hcomm_memory_test COMMAND hcomm_memory_test)
endif()
