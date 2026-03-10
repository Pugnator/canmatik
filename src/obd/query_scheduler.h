#pragma once

/// @file query_scheduler.h
/// Multi-group OBD query scheduler with independent intervals.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "obd/obd_config.h"
#include "obd/obd_session.h"
#include "obd/pid_decoder.h"

namespace canmatik {

class QueryScheduler {
public:
    using Callback = std::function<void(const DecodedPid&)>;

    /// @param session OBD session for issuing queries.
    /// @param config OBD configuration with groups and intervals.
    /// @param callback Called for each decoded PID value.
    QueryScheduler(ObdSession& session, const ObdConfig& config, Callback callback);

    /// Run the scheduler loop (blocking). Call from a dedicated thread.
    /// @param stop Set to true to terminate the loop.
    void run(std::atomic<bool>& stop);

private:
    struct ScheduledGroup {
        std::string name;
        std::vector<uint8_t> pids;
        uint32_t interval_ms;
        size_t next_pid_index = 0;
        std::chrono::steady_clock::time_point next_query_time;
    };

    ObdSession& session_;
    Callback callback_;
    std::vector<ScheduledGroup> groups_;
};

} // namespace canmatik
