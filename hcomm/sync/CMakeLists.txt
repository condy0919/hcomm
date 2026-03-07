# SPDX-License-Identifier: MulanPSL-2.0

if(HCOMM_BUILD_TESTS)
  add_executable(hcomm_sync_test
    bridge_test.cpp
  )
  target_link_libraries(hcomm_sync_test
    PRIVATE
      hcomm::hcomm
      GTest::gtest_main
  )
  add_test(NAME hcomm_sync_test COMMAND hcomm_sync_test)
endif()
