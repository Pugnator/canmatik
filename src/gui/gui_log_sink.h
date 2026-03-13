#pragma once

/// @file gui_log_sink.h
/// Thread-safe in-memory ring buffer for GUI log display.

#include <log.hpp>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace canmatik {

struct LogEntry {
    TraceSeverity severity;
    std::string   message;
    uint64_t      timestamp_ms;  ///< Milliseconds since session start
};

/// A singleton ring buffer that captures log messages for the GUI Logs tab.
/// Thread-safe: can be pushed from any thread, snapshotted from the GUI thread.
class GuiLogSink {
public:
    static GuiLogSink& get() {
        static GuiLogSink instance;
        return instance;
    }

    /// Push a log entry (called from logging macros).
    void push(TraceSeverity severity, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.size() >= max_entries_)
            entries_.pop_front();
        entries_.push_back({severity, msg, elapsed_ms()});
    }

    /// Return a copy of all current entries.
    std::vector<LogEntry> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {entries_.begin(), entries_.end()};
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

private:
    GuiLogSink() : start_tick_(GetTickCount64()) {}

    uint64_t elapsed_ms() const { return GetTickCount64() - start_tick_; }

    mutable std::mutex   mutex_;
    std::deque<LogEntry> entries_;
    size_t               max_entries_ = 10000;
    uint64_t             start_tick_;
};

} // namespace canmatik

// -----------------------------------------------------------------------
// Extended logging macros: write to both TinyLog and GuiLogSink
// -----------------------------------------------------------------------
#ifndef GUI_LOG_INFO
#define GUI_LOG_INFO(...) do { \
    auto _msg = std::format(__VA_ARGS__); \
    Log::get().log(TraceSeverity::info, "{}", _msg); \
    canmatik::GuiLogSink::get().push(TraceSeverity::info, _msg); \
} while(0)
#endif

#ifndef GUI_LOG_WARNING
#define GUI_LOG_WARNING(...) do { \
    auto _msg = std::format(__VA_ARGS__); \
    Log::get().log(TraceSeverity::warning, "{}", _msg); \
    canmatik::GuiLogSink::get().push(TraceSeverity::warning, _msg); \
} while(0)
#endif

#ifndef GUI_LOG_ERROR
#define GUI_LOG_ERROR(...) do { \
    auto _msg = std::format(__VA_ARGS__); \
    Log::get().log(TraceSeverity::error, "{}", _msg); \
    canmatik::GuiLogSink::get().push(TraceSeverity::error, _msg); \
} while(0)
#endif

#ifndef GUI_LOG_DEBUG
#define GUI_LOG_DEBUG(...) do { \
    auto _msg = std::format(__VA_ARGS__); \
    Log::get().log(TraceSeverity::debug, "{}", _msg); \
    canmatik::GuiLogSink::get().push(TraceSeverity::debug, _msg); \
} while(0)
#endif
