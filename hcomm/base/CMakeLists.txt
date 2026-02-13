# SPDX-License-Identifier: MulanPSL-2.0

target_sources(hcomm
  PUBLIC
    status.cpp
)

if(HCOMM_BUILD_BENCHMARKS)
  add_executable(hcomm_base_benchmark
    status_benchmark.cpp
  )
  target_link_libraries(hcomm_base_benchmark
    PRIVATE
      hcomm::hcomm
      benchmark::benchmark_main
  )
  add_test(NAME hcomm_base_benchmark COMMAND hcomm_base_benchmark)
endif()

if(HCOMM_BUILD_TESTS)
  add_executable(hcomm_base_test
    status_test.cpp
    refptr_test.cpp
    weakptr_test.cpp
    scope_exit_test.cpp
  )
  target_link_libraries(hcomm_base_test
    PRIVATE
      hcomm::hcomm
      GTest::gtest_main
  )
  add_test(NAME hcomm_base_test COMMAND hcomm_base_test)
endif()
