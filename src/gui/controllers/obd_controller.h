#pragma once

/// @file obd_controller.h
/// OBD-II query session orchestration for the GUI.

#include "obd/pid_decoder.h"
#include "core/capture_sink.h"
#include "transport/channel.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace canmatik {

/// Row for the OBD Data tab.
struct ObdPidRow {
    uint8_t     pid           = 0;
    std::string name;
    double      value         = 0.0;
    std::string unit;
    std::string raw_hex;
    bool        value_changed = false;
    std::deque<std::pair<double, double>> history; // (seconds, value)
};

class ObdController {
public:
    ObdController() = default;
    ~ObdController();

    ObdController(const ObdController&) = delete;
    ObdController& operator=(const ObdController&) = delete;

    /// Start cyclic PID streaming on the given channel.
    /// @param frame_sink Optional sink to receive copies of all OBD TX/RX frames.
    void start_streaming(IChannel* channel,
                         const std::vector<uint8_t>& pids,
                         uint32_t interval_ms,
                         ICaptureSync* frame_sink = nullptr);

    /// Stop streaming.
    void stop_streaming();

    bool is_streaming() const { return running_.load(); }

    /// Get current decoded values (thread-safe snapshot).
    std::vector<ObdPidRow> snapshot();

private:
    void worker(IChannel* channel,
                std::vector<uint8_t> pids,
                uint32_t interval_ms,
                ICaptureSync* frame_sink);

    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex mu_;
    std::vector<ObdPidRow> rows_;
    double start_time_ = 0.0;
};

} // namespace canmatik
