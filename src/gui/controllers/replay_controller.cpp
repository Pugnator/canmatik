/// @file replay_controller.cpp
/// Log file replay implementation.

#include "gui/controllers/replay_controller.h"
#include "logging/asc_reader.h"
#include "logging/jsonl_reader.h"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace canmatik {

std::string ReplayController::load(const std::string& path) {
    stop();
    frames_.clear();

    std::unique_ptr<ILogReader> reader;
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".asc")
        reader = std::make_unique<AscReader>();
    else if (ext == ".jsonl")
        reader = std::make_unique<JsonlReader>();
    else
        return "Unsupported file format: " + ext;

    if (!reader->open(path))
        return "Cannot open file: " + path;

    while (auto f = reader->nextFrame())
        frames_.push_back(*f);

    if (frames_.empty())
        return "File contains no frames: " + path;

    current_    = 0;
    elapsed_us_ = 0.0;
    return {};
}

void ReplayController::play() {
    if (!frames_.empty()) playing_ = true;
}

void ReplayController::pause() {
    playing_ = false;
}

void ReplayController::stop() {
    playing_    = false;
    current_    = 0;
    elapsed_us_ = 0.0;
    speed_mult_ = 1.0f;
}

void ReplayController::rewind() {
    current_    = 0;
    elapsed_us_ = 0.0;
}

void ReplayController::fast_forward() {
    if (speed_mult_ >= 8.0f)
        speed_mult_ = 1.0f;
    else
        speed_mult_ *= 2.0f;
}

void ReplayController::tick(uint64_t delta_us, FrameCollector& collector) {
    if (!playing_ || frames_.empty()) return;

    elapsed_us_ += static_cast<double>(delta_us) * speed_mult_;

    // Use host_timestamp_us as the timeline reference.
    // Frame times are relative to first frame.
    uint64_t base = frames_[0].host_timestamp_us;

    while (current_ < frames_.size()) {
        double frame_time_us = static_cast<double>(
            frames_[current_].host_timestamp_us - base);
        if (frame_time_us > elapsed_us_) break;

        collector.pushFrame(frames_[current_]);
        ++current_;
    }

    // End of file
    if (current_ >= frames_.size()) {
        if (loop_) {
            current_    = 0;
            elapsed_us_ = 0.0;
        } else {
            playing_ = false;
        }
    }
}

} // namespace canmatik
