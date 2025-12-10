# SPDX-License-Identifier: MulanPSL-2.0

include(FetchContent)

FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp
  GIT_TAG        20250814.1
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(absl)
