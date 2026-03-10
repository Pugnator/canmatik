/// @file replay_service.cpp
/// ReplayService implementation (T052 — US5).

#include "services/replay_service.h"
#include "logging/asc_reader.h"
#include "logging/jsonl_reader.h"
#include "core/log_macros.h"

#include <algorithm>
#include <filesystem>

namespace canmatik {

bool ReplayService::load(const std::string& path) {
    frames_.clear();
    metadata_ = {};
    path_ = path;

    // Detect format by extension
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".asc") {
        reader_ = std::make_unique<AscReader>();
    } else if (ext == ".jsonl") {
        reader_ = std::make_unique<JsonlReader>();
    } else {
        LOG_ERROR("Unsupported log file extension: {}", ext);
        return false;
    }

    if (!reader_->open(path)) {
        LOG_ERROR("Failed to open log file: {}", path);
        return false;
    }

    // Read all frames into memory
    while (auto frame = reader_->nextFrame()) {
        frames_.push_back(*frame);
    }

    metadata_ = reader_->metadata();
    LOG_INFO("Loaded {} frames from {}", frames_.size(), path);
    return true;
}

std::vector<CanFrame> ReplayService::search(uint32_t arb_id) const {
    std::vector<CanFrame> result;
    for (const auto& f : frames_) {
        if (f.arbitration_id == arb_id) {
            result.push_back(f);
        }
    }
    return result;
}

ReplaySummary ReplayService::summary() const {
    ReplaySummary s;
    s.total_frames = frames_.size();
    s.adapter_name = metadata_.adapter_name;
    s.bitrate = metadata_.bitrate;

    for (const auto& f : frames_) {
        s.id_distribution[f.arbitration_id]++;
    }
    s.unique_ids = s.id_distribution.size();

    if (!frames_.empty()) {
        double first_ts = static_cast<double>(frames_.front().host_timestamp_us) / 1'000'000.0;
        double last_ts = static_cast<double>(frames_.back().host_timestamp_us) / 1'000'000.0;
        s.duration_seconds = last_ts - first_ts;
    }

    return s;
}

SessionStatus ReplayService::metadata() const {
    return metadata_;
}

} // namespace canmatik
