# SPDX-License-Identifier: MulanPSL-2.0

cmake_minimum_required(VERSION 3.16)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(mhcom
  VERSION 0.1.0
  LANGUAGES CXX
  DESCRIPTION "Modern hcom"
  HOMEPAGE_URL "https://github.com/condy0919/mhcom"
)

include(CTest)
include(FetchContent)

option(MHCOM_BUILD_TESTS "Build tests" OFF)
option(MHCOM_BUILD_BENCHMARKS "Build benchmarks" OFF)

# Sources for the library are specified at the end
add_library(mhcom "")

# Alias to avoid name conflicts
add_library(mhcom::mhcom ALIAS mhcom)

### Compile options
# Enable C++20 (Required)
target_compile_features(mhcom
  PUBLIC
    cxx_std_20
)

# Common GCC/Clang options
target_compile_options(mhcom
  PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
  PUBLIC
    -fms-extensions
)

### Libraries
# Enable threading support
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(mhcom PRIVATE Threads::Threads)

# Benchmark
if(MHCOM_BUILD_BENCHMARKS)
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
if(MHCOM_BUILD_TESTS)
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

target_include_directories(mhcom PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

### Install

### Sources
add_subdirectory(mhcom)
