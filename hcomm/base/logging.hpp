// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_LOGGING_HPP_
#define HCOMM_BASE_LOGGING_HPP_

#include <spdlog/spdlog.h>

#define HCOMM_LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define HCOMM_LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define HCOMM_LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define HCOMM_LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define HCOMM_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define HCOMM_LOG_FATAL(...) spdlog::critical(__VA_ARGS__)

#endif // HCOMM_BASE_LOGGING_HPP_
