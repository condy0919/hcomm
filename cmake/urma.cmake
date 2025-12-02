include(FetchContent)

FetchContent_Declare(
  umdk
  GIT_REPOSITORY https://gitee.com/openeuler/umdk.git
  GIT_TAG        br_openEuler_24.03_LTS_SP3
  GIT_SHALLOW    true
  GIT_PROGRESS   true
)
FetchContent_MakeAvailable(umdk)

# The UMDK project lacks the necessary CMake target exports for FetchContent.
# Providing a local CMake wrapper.
add_library(urma SHARED "")
add_library(umdk::urma ALIAS urma)

target_include_directories(urma
  PUBLIC
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/include/
  PRIVATE
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/
    ${umdk_SOURCE_DIR}/src/urma/common/include/
)

# pthread_mutex_t, dlopen
find_package(Threads REQUIRED)
target_link_libraries(urma PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

target_sources(urma
  PRIVATE
    ${umdk_SOURCE_DIR}/src/urma/common/ub_util.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_cp_api.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_dp_api.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_main.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_cmd.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_cmd_tlv.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_device.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_log.c
    ${umdk_SOURCE_DIR}/src/urma/lib/urma/core/urma_format_convert.c
)
