#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// gateway_log.h — Structured logging macros for the CTP gateway module.
//
// All gateway logs are prefixed with [ISO8601-timestamp][LEVEL][CtpGateway]
// to enable automated log parsing and monitoring.
//
// Usage:
//   LOG_INFO("Login OK, TradingDay=%s", day.c_str());
//   LOG_WARN("Queue full, dropped %d ticks", count);
//   LOG_ERROR("Login failed, ErrorID=%d", err_id);

namespace gateway_log {

// Format current time as ISO8601: YYYY-MM-DDTHH:MM:SS.mmm
inline std::string iso8601_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

// ── Logging helpers ─────────────────────────────────────────────

inline void log_info(const std::string& msg) {
    std::cout << "[" << iso8601_timestamp()
              << "][INFO][CtpGateway] " << msg << "\n";
}
inline void log_warn(const std::string& msg) {
    std::cerr << "[" << iso8601_timestamp()
              << "][WARN][CtpGateway] " << msg << "\n";
}
inline void log_error(const std::string& msg) {
    std::cerr << "[" << iso8601_timestamp()
              << "][ERROR][CtpGateway] " << msg << "\n";
}

} // namespace gateway_log

// ── Logging macros ──────────────────────────────────────────────

#define LOG_INFO(fmt, ...)                                                     \
    do {                                                                       \
        char _log_buf[1024];                                                   \
        std::snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__);         \
        gateway_log::log_info(_log_buf);                                       \
    } while (0)

#define LOG_WARN(fmt, ...)                                                     \
    do {                                                                       \
        char _log_buf[1024];                                                   \
        std::snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__);         \
        gateway_log::log_warn(_log_buf);                                       \
    } while (0)

#define LOG_ERROR(fmt, ...)                                                    \
    do {                                                                       \
        char _log_buf[1024];                                                   \
        std::snprintf(_log_buf, sizeof(_log_buf), fmt, ##__VA_ARGS__);         \
        gateway_log::log_error(_log_buf);                                      \
    } while (0)
