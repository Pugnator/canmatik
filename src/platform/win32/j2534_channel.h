#pragma once

/// @file j2534_channel.h
/// J2534Channel : IChannel — real hardware CAN channel (T029 — US1).
/// Every J2534 API call is wrapped with LOG_DEBUG per Constitution Principle II.

#include "transport/channel.h"
#include "platform/win32/j2534_defs.h"

#include <cstdint>
#include <atomic>

namespace canmatik {

class J2534DllLoader; // forward declaration

/// Real CAN channel backed by a loaded J2534 DLL.
/// Wraps PassThruConnect, PassThruReadMsgs, PassThruStartMsgFilter, etc.
class J2534Channel : public IChannel {
public:
    /// Construct with a reference to a loaded DLL and an open device ID.
    /// @param dll Reference to the already-loaded J2534 DLL (must outlive this channel).
    /// @param device_id The deviceID from PassThruOpen.
    J2534Channel(J2534DllLoader& dll, unsigned long device_id);
    ~J2534Channel() override;

    /// Open a CAN channel at the specified bitrate (PassThruConnect).
    void open(uint32_t bitrate) override;

    /// Close the CAN channel (PassThruDisconnect).
    void close() override;

    /// Read CAN frames (PassThruReadMsgs). Returns decoded CanFrames.
    [[nodiscard]] std::vector<CanFrame> read(uint32_t timeout_ms) override;

    /// Write a CAN frame (PassThruWriteMsgs).
    void write(const CanFrame& frame) override;

    /// Set a hardware pass filter (PassThruStartMsgFilter).
    void setFilter(uint32_t mask, uint32_t pattern) override;

    /// Clear all hardware filters (PassThruIoctl CLEAR_MSG_FILTERS).
    void clearFilters() override;

    /// Check whether the channel is open.
    [[nodiscard]] bool isOpen() const override;

private:
    /// Convert a PASSTHRU_MSG to a CanFrame.
    [[nodiscard]] CanFrame convert_msg(const j2534::PASSTHRU_MSG& msg) const;

    /// Get the last J2534 error string from the DLL.
    [[nodiscard]] std::string get_last_error() const;

    J2534DllLoader& dll_;
    unsigned long device_id_ = 0;
    unsigned long channel_id_ = 0;
    uint32_t bitrate_ = 0;
    std::atomic<bool> open_{false};

    /// Timestamp tracking for 32→64 bit rollover extension
    uint64_t last_raw_ts_ = 0;        ///< Last raw 32-bit timestamp
    uint64_t rollover_offset_ = 0;    ///< Accumulated rollover offset

    /// Filter tracking
    static constexpr unsigned long kInvalidFilterId = 0xFFFFFFFF;
    unsigned long pass_filter_id_ = kInvalidFilterId;
};

} // namespace canmatik
