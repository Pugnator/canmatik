#pragma once

/// @file mock_channel.h
/// MockChannel : IChannel — generates simulated CAN frames for testing.

#include "transport/channel.h"
#include "core/can_frame.h"

#include <vector>
#include <cstdint>
#include <chrono>
#include <random>
#include <atomic>

namespace canmatik {

/// Mock CAN channel that generates simulated traffic.
/// Supports configurable frame generation, timing, and error injection.
class MockChannel : public IChannel {
public:
    /// @param frame_rate Simulated frames per second.
    explicit MockChannel(uint32_t frame_rate = 100);

    void open(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN) override;
    void close() override;
    std::vector<CanFrame> read(uint32_t timeout_ms) override;
    void write(const CanFrame& frame) override;
    void setFilter(uint32_t mask, uint32_t pattern) override;
    void clearFilters() override;
    [[nodiscard]] bool isOpen() const override;

    /// Set a predefined sequence of frames to emit instead of random generation.
    void set_frame_sequence(std::vector<CanFrame> frames);

    /// Configure error injection: throw TransportError after N frames.
    void set_error_after(uint64_t frame_count);

    /// Enable or disable write acceptance (default: writable).
    void set_writable(bool writable);

    /// Get frames written to the channel (for test verification).
    [[nodiscard]] const std::vector<CanFrame>& written_frames() const;

private:
    CanFrame generate_frame();

    uint32_t bitrate_ = 0;
    uint32_t frame_rate_ = 100;
    std::atomic<bool> open_{false};
    uint64_t frame_counter_ = 0;
    uint64_t error_after_ = 0;  ///< 0 = no error injection
    std::chrono::steady_clock::time_point start_time_;
    std::mt19937 rng_{42};      ///< Deterministic seed for reproducibility
    std::vector<CanFrame> frame_sequence_;
    size_t sequence_index_ = 0;
    uint32_t filter_mask_ = 0;
    uint32_t filter_pattern_ = 0;
    bool has_filter_ = false;
    bool writable_ = true;
    std::vector<CanFrame> written_frames_;
};

} // namespace canmatik
