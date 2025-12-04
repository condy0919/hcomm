# SPDX-License-Identifier: MulanPSL-2.0

include(FetchContent)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.16.0
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(spdlog)
