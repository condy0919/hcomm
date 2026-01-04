# SPDX-License-Identifier: MulanPSL-2.0

include(FetchContent)

# Boost::callable_traits
FetchContent_Declare(
  callable_traits
  GIT_REPOSITORY https://github.com/boostorg/callable_traits
  GIT_TAG        boost-1.90.0
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(callable_traits)
