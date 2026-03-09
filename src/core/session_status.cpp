/// @file session_status.cpp
/// SessionStatus and OperatingMode implementations.

#include "core/session_status.h"

namespace canmatik {

const char* operating_mode_to_string(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Passive:      return "PASSIVE";
        case OperatingMode::ActiveQuery:  return "ACTIVE-QUERY";
        case OperatingMode::ActiveInject: return "ACTIVE-INJECT";
    }
    return "UNKNOWN";
}

void SessionStatus::reset() {
    mode = OperatingMode::Passive;
    provider_name.clear();
    adapter_name.clear();
    bitrate = 0;
    channel_open = false;
    recording = false;
    recording_file.clear();
    frames_received = 0;
    frames_transmitted = 0;
    errors = 0;
    dropped = 0;
    session_start = std::chrono::steady_clock::now();
    active_filters.clear();
}

double SessionStatus::elapsed_seconds() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - session_start).count();
}

} // namespace canmatik
