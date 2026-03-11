#pragma once

/// @file replay_controller.h
/// Log file replay with timed playback, rewind, ff, loop.

#include "gui/frame_collector.h"
#include "core/can_frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace canmatik {

class ReplayController {
public:
    ReplayController() = default;

    /// Load a log file (.asc or .jsonl). Returns empty on success.
    std::string load(const std::string& path);

    /// Start / resume playback.
    void play();

    /// Pause playback at current position.
    void pause();

    /// Stop and reset to frame 0.
    void stop();

    /// Jump to beginning.
    void rewind();

    /// Cycle speed multiplier: 1→2→4→8→1.
    void fast_forward();

    void set_loop(bool loop) { loop_ = loop; }
    bool is_loop() const { return loop_; }
    float speed() const { return speed_mult_; }

    bool is_playing() const { return playing_; }
    bool is_loaded() const { return !frames_.empty(); }

    size_t frame_count() const { return frames_.size(); }
    size_t current_index() const { return current_; }

    /// Advance playback by real delta microseconds; push frames to collector.
    void tick(uint64_t delta_us, FrameCollector& collector);

private:
    std::vector<CanFrame> frames_;
    size_t current_     = 0;
    bool   playing_     = false;
    bool   loop_        = false;
    float  speed_mult_  = 1.0f;
    double elapsed_us_  = 0.0; // logical playback time
};

} // namespace canmatik
