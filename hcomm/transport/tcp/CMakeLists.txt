# SPDX-License-Identifier: MulanPSL-2.0

target_sources(hcomm
  PRIVATE
    socket.cpp
    executor.cpp
)

if(HCOMM_BUILD_TESTS)
  add_executable(hcomm_tcp_test
    address_test.cpp
    error_test.cpp
  )
  target_link_libraries(hcomm_tcp_test
    PRIVATE
      hcomm::hcomm
      GTest::gtest_main
  )
  add_test(NAME hcomm_tcp_test COMMAND hcomm_tcp_test)
endif()
