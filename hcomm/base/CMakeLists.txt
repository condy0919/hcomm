# SPDX-License-Identifier: MulanPSL-2.0

target_sources(hcomm
  PRIVATE
    status.cpp
)

add_executable(hcomm_base_benchmark
  status_benchmark.cpp
)
target_link_libraries(hcomm_base_benchmark
  PRIVATE
    hcomm::hcomm
    benchmark::benchmark_main
)
add_test(NAME hcomm_base_benchmark COMMAND hcomm_base_benchmark)

add_executable(hcomm_base_test
  status_test.cpp
)
target_link_libraries(hcomm_base_test
  PRIVATE
    hcomm::hcomm
    GTest::gtest_main
)
add_test(NAME hcomm_base_test COMMAND hcomm_base_test)
