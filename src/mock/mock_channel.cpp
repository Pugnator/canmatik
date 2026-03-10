/// @file mock_channel.cpp
/// MockChannel implementation — generates simulated CAN traffic.

#include "mock/mock_channel.h"
#include "transport/transport_error.h"

#include <thread>
#include <algorithm>

namespace canmatik {

MockChannel::MockChannel(uint32_t frame_rate)
    : frame_rate_(frame_rate)
{}

void MockChannel::open(uint32_t bitrate) {
    if (open_.load()) {
        throw TransportError(0, "Mock channel already open",
                             "MockChannel::open", true);
    }
    bitrate_ = bitrate;
    frame_counter_ = 0;
    sequence_index_ = 0;
    start_time_ = std::chrono::steady_clock::now();
    open_.store(true);
}

void MockChannel::close() {
    open_.store(false);
    bitrate_ = 0;
}

std::vector<CanFrame> MockChannel::read(uint32_t timeout_ms) {
    if (!open_.load()) {
        throw TransportError(0, "Mock channel not open",
                             "MockChannel::read", false);
    }

    // Simulate inter-frame timing
    if (frame_rate_ > 0) {
        auto delay_us = 1000000u / frame_rate_;
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    } else {
        // Zero frame rate = bus silence.  Wait the full timeout then return empty.
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return {};
    }

    // Error injection: throw after N frames
    if (error_after_ > 0 && frame_counter_ >= error_after_) {
        throw TransportError(0, "Mock: simulated adapter error after "
                             + std::to_string(error_after_) + " frames",
                             "MockChannel::read", true);
    }

    // Generate a batch of frames (1 frame per poll to keep timing realistic)
    std::vector<CanFrame> batch;
    batch.reserve(1);

    CanFrame frame;
    if (!frame_sequence_.empty()) {
        // Replay predefined sequence (wrap around)
        frame = frame_sequence_[sequence_index_ % frame_sequence_.size()];
        ++sequence_index_;
    } else {
        frame = generate_frame();
    }

    // Apply filter if set (simulate hardware-level filtering)
    if (has_filter_) {
        if ((frame.arbitration_id & filter_mask_) != (filter_pattern_ & filter_mask_)) {
            // Frame doesn't match hardware filter — skip it
            return {};
        }
    }

    ++frame_counter_;
    batch.push_back(frame);
    return batch;
}

void MockChannel::write(const CanFrame& frame) {
    if (!writable_) {
        throw TransportError(0, "Mock channel: write rejected (passive mode only)",
                             "MockChannel::write", true);
    }
    written_frames_.push_back(frame);
}

void MockChannel::setFilter(uint32_t mask, uint32_t pattern) {
    filter_mask_ = mask;
    filter_pattern_ = pattern;
    has_filter_ = true;
}

void MockChannel::clearFilters() {
    filter_mask_ = 0;
    filter_pattern_ = 0;
    has_filter_ = false;
}

bool MockChannel::isOpen() const {
    return open_.load();
}

void MockChannel::set_frame_sequence(std::vector<CanFrame> frames) {
    frame_sequence_ = std::move(frames);
    sequence_index_ = 0;
}

void MockChannel::set_error_after(uint64_t frame_count) {
    error_after_ = frame_count;
}

void MockChannel::set_writable(bool writable) {
    writable_ = writable;
}

const std::vector<CanFrame>& MockChannel::written_frames() const {
    return written_frames_;
}

CanFrame MockChannel::generate_frame() {
    // Deterministic random frame generation (seed = 42)
    static constexpr uint32_t common_ids[] = {
        0x100, 0x120, 0x200, 0x280, 0x300, 0x320,
        0x3B0, 0x400, 0x500, 0x600, 0x7E0, 0x7E8,
    };
    static constexpr size_t num_ids = sizeof(common_ids) / sizeof(common_ids[0]);

    std::uniform_int_distribution<size_t> id_dist(0, num_ids - 1);
    std::uniform_int_distribution<int> dlc_dist(1, 8);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    CanFrame frame;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time_).count();

    frame.adapter_timestamp_us = static_cast<uint64_t>(elapsed);
    frame.host_timestamp_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());
    frame.arbitration_id = common_ids[id_dist(rng_)];
    frame.type = FrameType::Standard;
    frame.dlc = static_cast<uint8_t>(dlc_dist(rng_));
    frame.channel_id = 1;

    for (uint8_t i = 0; i < frame.dlc; ++i) {
        frame.data[i] = static_cast<uint8_t>(byte_dist(rng_));
    }
    // Trailing bytes already zero (std::array default init)

    return frame;
}

} // namespace canmatik
