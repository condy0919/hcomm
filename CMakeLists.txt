# SPDX-License-Identifier: MulanPSL-2.0

cmake_minimum_required(VERSION 3.16)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(hcomm
  VERSION 0.1.0
  LANGUAGES C CXX
  DESCRIPTION "Modern hcom"
  HOMEPAGE_URL "https://github.com/condy0919/mhcom"
)

include(CTest)
include(FetchContent)

option(HCOMM_BUILD_TESTS "Build tests" OFF)
option(HCOMM_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(HCOMM_ENABLE_COVERAGE "Enable code coverage" OFF)

# Sources for the library are specified at the end
add_library(hcomm "")

# Alias to avoid name conflicts
add_library(hcomm::hcomm ALIAS hcomm)

### Compile options
# Enable C++23 (Required)
target_compile_features(hcomm
  PUBLIC
    cxx_std_23
)

# Common GCC/Clang options
target_compile_options(hcomm
  PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

### Libraries
# Enable threading support
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(hcomm PUBLIC Threads::Threads)

# umdk::urma
include(${hcomm_SOURCE_DIR}/cmake/urma.cmake)
target_link_libraries(hcomm PUBLIC umdk::urma)

# absl
include(${hcomm_SOURCE_DIR}/cmake/absl.cmake)

# spdlog
include(${hcomm_SOURCE_DIR}/cmake/spdlog.cmake)
target_link_libraries(hcomm PUBLIC spdlog::spdlog)

# boost
include(${hcomm_SOURCE_DIR}/cmake/boost.cmake)
target_link_libraries(hcomm PUBLIC Boost::callable_traits)

# Benchmark
if(HCOMM_BUILD_BENCHMARKS)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.4
    GIT_SHALLOW    true
    GIT_PROGRESS   true
  )
  FetchContent_MakeAvailable(benchmark)
endif()

# Test
if(HCOMM_BUILD_TESTS)
  # libgtest.a and libgtest_main.a will be built
  FetchContent_Declare(
    gtest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG        v1.17.0
    GIT_SHALLOW    true
    GIT_PROGRESS   true
  )
  FetchContent_MakeAvailable(gtest)
endif()

### Definitions

### Includes

target_include_directories(hcomm PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

### Install

### Sources
add_subdirectory(hcomm)

### Coverage
if(HCOMM_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Building with coverage support")
    target_compile_options(hcomm PUBLIC --coverage -g -O0)
    target_link_options(hcomm PUBLIC --coverage)
  else()
    message(FATAL_ERROR "Coverage is only supported for GCC and Clang")
  endif()

  find_program(LCOV_PATH lcov)
  find_program(GENHTML_PATH genhtml)

  if(LCOV_PATH AND GENHTML_PATH)
    add_custom_target(coverage
      # 1. Zero counters
      COMMAND ${LCOV_PATH} --directory . --zerocounters

      # 2. Run tests
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure

      # 3. Capture coverage
      COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage.info --rc branch_coverage=1 --ignore-errors inconsistent,mismatch

      # 4. Filter coverage (remove system headers, dependencies, and build artifacts)
      COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/_deps/*' '*/build/*' --output-file coverage_filtered.info --rc branch_coverage=1 --ignore-errors inconsistent,mismatch,unused

      # 5. Generate HTML report
      COMMAND ${GENHTML_PATH} coverage_filtered.info --output-directory coverage_report --rc branch_coverage=1 --ignore-errors inconsistent,mismatch,unused

      # 6. Print success message
      COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in: ${CMAKE_BINARY_DIR}/coverage_report/index.html"

      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating code coverage report..."
    )
  else()
    message(WARNING "lcov or genhtml not found. Coverage target will not be available.")
  endif()
endif()
