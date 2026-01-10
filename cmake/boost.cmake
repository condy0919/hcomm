# SPDX-License-Identifier: MulanPSL-2.0

include(FetchContent)

set(BOOST_VERSION_TAG "boost-1.90.0")
set(BUILD_TESTING OFF CACHE BOOL "Don't run boost tests" FORCE)

# Boost::callable_traits
FetchContent_Declare(
  boost_callable_traits
  GIT_REPOSITORY https://github.com/boostorg/callable_traits
  GIT_TAG        ${BOOST_VERSION_TAG}
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(boost_callable_traits)

# Boost::config
FetchContent_Declare(
  boost_config
  GIT_REPOSITORY https://github.com/boostorg/config
  GIT_TAG        ${BOOST_VERSION_TAG}
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(boost_config)

# Boost::assert
FetchContent_Declare(
  boost_assert
  GIT_REPOSITORY https://github.com/boostorg/assert
  GIT_TAG        ${BOOST_VERSION_TAG}
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(boost_assert)

FetchContent_Declare(
  boost_move
  GIT_REPOSITORY https://github.com/boostorg/move
  GIT_TAG        ${BOOST_VERSION_TAG}
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(boost_move)

# Boost::intrusive
FetchContent_Declare(
  boost_intrusive
  GIT_REPOSITORY https://github.com/boostorg/intrusive
  GIT_TAG        ${BOOST_VERSION_TAG}
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(boost_intrusive)

# Boost::container
FetchContent_Declare(
  boost_container
  GIT_REPOSITORY https://github.com/boostorg/container
  GIT_TAG        ${BOOST_VERSION_TAG}
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(boost_container)

set(BUILD_TESTING ON CACHE BOOL "Turn ON" FORCE)
