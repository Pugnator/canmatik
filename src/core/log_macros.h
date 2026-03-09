#pragma once

/// @file log_macros.h
/// Project-level logging convenience macros wrapping TinyLog.
/// TinyLog provides LOG_INFO, LOG_DEBUG, LOG_CALL. This header adds
/// LOG_WARNING, LOG_ERROR, and LOG_CRITICAL that TinyLog omits.

#include <log.hpp>

#ifndef LOG_WARNING
#define LOG_WARNING(...) Log::get().log(TraceSeverity::warning, __VA_ARGS__)
#endif

#ifndef LOG_ERROR
#define LOG_ERROR(...) Log::get().log(TraceSeverity::error, __VA_ARGS__)
#endif

#ifndef LOG_CRITICAL
#define LOG_CRITICAL(...) Log::get().log(TraceSeverity::critical, __VA_ARGS__)
#endif
