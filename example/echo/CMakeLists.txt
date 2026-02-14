# SPDX-License-Identifier: MulanPSL-2.0

add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server hcomm::hcomm)
