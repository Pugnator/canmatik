/// @file obd_controller.cpp
/// OBD-II streaming worker implementation.

#include "gui/controllers/obd_controller.h"
#include "core/timestamp.h"
#include "obd/obd_session.h"
#include "obd/pid_table.h"

#include <chrono>
#include <format>

namespace canmatik {

ObdController::~ObdController() {
    stop_streaming();
}

void ObdController::start_streaming(IChannel* channel,
                                     const std::vector<uint8_t>& pids,
                                     uint32_t interval_ms,
                                     ICaptureSync* frame_sink) {
    stop_streaming();
    start_time_ = static_cast<double>(host_timestamp_us()) / 1'000'000.0;

    // Initialize rows
    {
        std::lock_guard lock(mu_);
        active_pids_ = pids;
        active_interval_ms_ = interval_ms;
        rows_.clear();
        for (auto pid : pids) {
            ObdPidRow r;
            r.pid  = pid;
            auto* def = pid_lookup(0x01, pid);
            r.name = def ? def->name : std::format("PID 0x{:02X}", pid);
            r.unit = def ? def->unit : "";
            rows_.push_back(std::move(r));
        }
    }

    running_.store(true);
    thread_ = std::thread(&ObdController::worker, this, channel, pids, interval_ms, frame_sink);
}

void ObdController::stop_streaming() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

std::vector<ObdPidRow> ObdController::snapshot() {
    std::lock_guard lock(mu_);
    return rows_;
}

void ObdController::worker(IChannel* channel,
                            std::vector<uint8_t> pids,
                            uint32_t interval_ms,
                            ICaptureSync* frame_sink) {
    ObdSession session(*channel, iso15765::kFunctionalTxId, iso15765::kResponseBase, frame_sink);

    uint32_t per_pid_ms = (pids.size() > 0)
        ? std::max(1u, interval_ms / static_cast<uint32_t>(pids.size()))
        : interval_ms;

    while (running_.load()) {
        for (size_t i = 0; i < pids.size() && running_.load(); ++i) {
            auto result = session.query_pid(0x01, pids[i]);
            if (result) {
                auto& decoded = result.value();
                double now_s = static_cast<double>(host_timestamp_us()) / 1'000'000.0 - start_time_;

                std::string raw;
                for (size_t b = 0; b < decoded.raw_bytes.size(); ++b) {
                    if (b > 0) raw += ' ';
                    raw += std::format("{:02X}", decoded.raw_bytes[b]);
                }

                std::lock_guard lock(mu_);
                if (i < rows_.size()) {
                    auto& row = rows_[i];
                    row.value_changed = (row.value != decoded.value);
                    row.value    = decoded.value;
                    row.raw_hex  = raw;
                    row.history.emplace_back(now_s, decoded.value);
                    if (row.history.size() > 600) row.history.pop_front(); // ~60s @ 10Hz
                }
            }

            // Sleep per-PID so full cycle approximates interval_ms
            auto wake = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(per_pid_ms);
            while (running_.load() && std::chrono::steady_clock::now() < wake)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

} // namespace canmatik
