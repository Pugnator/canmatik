/// @file query_scheduler.cpp
/// QueryScheduler implementation — round-robin multi-group scheduling.

#include "obd/query_scheduler.h"

#include <algorithm>
#include <thread>

namespace canmatik {

QueryScheduler::QueryScheduler(ObdSession& session, const ObdConfig& config, Callback callback)
    : session_(session), callback_(std::move(callback)) {

    auto now = std::chrono::steady_clock::now();

    // Build scheduled groups from config
    for (const auto& g : config.groups) {
        ScheduledGroup sg;
        sg.name = g.name;
        sg.pids = g.pids;
        sg.interval_ms = g.has_interval ? g.interval.milliseconds : config.default_interval.milliseconds;
        sg.next_query_time = now;
        groups_.push_back(std::move(sg));
    }

    // Add standalone PIDs as an unnamed group
    if (!config.standalone_pids.empty()) {
        ScheduledGroup sg;
        sg.name = "_standalone";
        sg.pids = config.standalone_pids;
        sg.interval_ms = config.default_interval.milliseconds;
        sg.next_query_time = now;
        groups_.push_back(std::move(sg));
    }
}

void QueryScheduler::run(std::atomic<bool>& stop) {
    if (groups_.empty()) return;

    while (!stop.load(std::memory_order_relaxed)) {
        // Find group with earliest next_query_time
        auto* earliest = &groups_[0];
        for (auto& g : groups_) {
            if (g.next_query_time < earliest->next_query_time) {
                earliest = &g;
            }
        }

        // Sleep until that group is due
        auto now = std::chrono::steady_clock::now();
        if (earliest->next_query_time > now) {
            auto wait = earliest->next_query_time - now;
            // Break sleep into small chunks to check stop flag
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(wait).count();
            while (remaining > 0 && !stop.load(std::memory_order_relaxed)) {
                auto chunk = std::min<long long>(remaining, 50);
                std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                remaining -= chunk;
            }
            if (stop.load(std::memory_order_relaxed)) break;
        }

        // Query next PID in the earliest group
        uint8_t pid = earliest->pids[earliest->next_pid_index];
        auto result = session_.query_pid(0x01, pid);
        if (result) {
            callback_(*result);
        }

        // Advance round-robin
        earliest->next_pid_index = (earliest->next_pid_index + 1) % earliest->pids.size();

        // If we've cycled through all PIDs, advance the next query time
        if (earliest->next_pid_index == 0) {
            earliest->next_query_time = std::chrono::steady_clock::now()
                                        + std::chrono::milliseconds(earliest->interval_ms);
        }
    }
}

} // namespace canmatik
