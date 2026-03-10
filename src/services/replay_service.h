#pragma once

/// @file replay_service.h
/// ReplayService — load, iterate, search, and summarize captured logs (T052 — US5).

#include "core/can_frame.h"
#include "core/session_status.h"
#include "logging/log_reader.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace canmatik {

/// Summary statistics for a loaded log file.
struct ReplaySummary {
    uint64_t total_frames = 0;
    uint64_t unique_ids = 0;
    double duration_seconds = 0.0;
    std::string adapter_name;
    uint32_t bitrate = 0;
    std::map<uint32_t, uint64_t> id_distribution; ///< arb ID -> count
};

/// ReplayService — loads a log file, iterates frames, searches by ID,
/// and provides session summary with ID distribution.
class ReplayService {
public:
    ReplayService() = default;

    /// Load a log file. Detects format by extension (.asc or .jsonl).
    /// @return true on success.
    bool load(const std::string& path);

    /// Get all frames loaded from the file.
    [[nodiscard]] const std::vector<CanFrame>& frames() const { return frames_; }

    /// Search frames by arbitration ID.
    [[nodiscard]] std::vector<CanFrame> search(uint32_t arb_id) const;

    /// Get replay summary (unique IDs, distribution, duration).
    [[nodiscard]] ReplaySummary summary() const;

    /// Get metadata from the file header.
    [[nodiscard]] SessionStatus metadata() const;

    /// Get the path of the currently loaded file.
    [[nodiscard]] const std::string& path() const { return path_; }

private:
    std::string path_;
    std::unique_ptr<ILogReader> reader_;
    std::vector<CanFrame> frames_;
    SessionStatus metadata_;
};

} // namespace canmatik
