#pragma once

/// @file channel.h
/// IChannel interface — abstract CAN channel for read/write/filter operations.

#include <cstdint>
#include <vector>

#include "core/can_frame.h"

namespace canmatik {

/// Abstract CAN channel — wraps one J2534 channel or mock channel.
/// Implementations: J2534Channel, MockChannel.
class IChannel {
public:
    virtual ~IChannel() = default;

    /// Open the channel at the specified bitrate.
    virtual void open(uint32_t bitrate) = 0;

    /// Close the channel. Safe to call if already closed.
    virtual void close() = 0;

    /// Read received CAN frames (blocking with timeout).
    /// @param timeout_ms Maximum wait time in milliseconds.
    /// @return Vector of received frames (may be empty on timeout).
    [[nodiscard]] virtual std::vector<CanFrame> read(uint32_t timeout_ms) = 0;

    /// Write a frame to the bus.
    virtual void write(const CanFrame& frame) = 0;

    /// Set a hardware-level CAN filter (mask/pattern).
    virtual void setFilter(uint32_t mask, uint32_t pattern) = 0;

    /// Remove all hardware-level filters.
    virtual void clearFilters() = 0;

    /// Check whether the channel is currently open.
    [[nodiscard]] virtual bool isOpen() const = 0;
};

} // namespace canmatik
