/// @file j2534_channel.cpp
/// J2534Channel : IChannel — real hardware CAN channel (T029 — US1).
/// Every J2534 API call is logged at debug level per Constitution Principle II.

#include "platform/win32/j2534_channel.h"
#include "platform/win32/j2534_dll_loader.h"
#include "transport/transport_error.h"
#include "core/timestamp.h"

#include "core/log_macros.h"

#include <chrono>
#include <cstring>

namespace canmatik {

J2534Channel::J2534Channel(J2534DllLoader& dll, unsigned long device_id)
    : dll_(dll), device_id_(device_id)
{
    LOG_DEBUG("J2534Channel created (device_id={})", device_id);
}

J2534Channel::~J2534Channel() {
    if (open_) {
        try { close(); } catch (...) {}
    }
}

void J2534Channel::open(uint32_t bitrate) {
    if (open_) {
        LOG_WARNING("J2534Channel::open() — channel already open, closing first");
        close();
    }

    bitrate_ = bitrate;
    unsigned long channel_id = 0;
    unsigned long flags = j2534::CAN_ID_BOTH; // Accept both 11-bit and 29-bit IDs

    LOG_DEBUG("Calling PassThruConnect(device={}, protocol=CAN(0x{:X}), flags=0x{:X}, baudrate={})",
              device_id_, j2534::PROTOCOL_CAN, flags, bitrate);
    long ret = dll_.PassThruConnect(device_id_, j2534::PROTOCOL_CAN, flags, bitrate, &channel_id);
    LOG_DEBUG("PassThruConnect returned {} ({})", ret, j2534::status_to_string(ret));

    if (ret != j2534::STATUS_NOERROR) {
        throw TransportError(static_cast<int32_t>(ret),
                             "PassThruConnect failed: " + get_last_error(),
                             "PassThruConnect");
    }

    channel_id_ = channel_id;

    // Set a pass-all filter so we receive all messages
    j2534::PASSTHRU_MSG mask_msg = {};
    mask_msg.ProtocolID = j2534::PROTOCOL_CAN;
    mask_msg.DataSize = 4;
    std::memset(mask_msg.Data, 0x00, 4); // Mask = 0x00000000: match all IDs

    j2534::PASSTHRU_MSG pattern_msg = {};
    pattern_msg.ProtocolID = j2534::PROTOCOL_CAN;
    pattern_msg.DataSize = 4;
    std::memset(pattern_msg.Data, 0x00, 4); // Pattern = 0x00000000

    unsigned long filter_id = 0;
    LOG_DEBUG("Calling PassThruStartMsgFilter(channel={}, PASS, mask=0x00000000, pattern=0x00000000)",
              channel_id_);
    ret = dll_.PassThruStartMsgFilter(channel_id_, j2534::FILTER_PASS,
                                       &mask_msg, &pattern_msg, nullptr, &filter_id);
    LOG_DEBUG("PassThruStartMsgFilter returned {} ({}), filter_id={}",
              ret, j2534::status_to_string(ret), filter_id);

    if (ret != j2534::STATUS_NOERROR) {
        LOG_WARNING("PassThruStartMsgFilter failed: {} — frames may not be received",
                   get_last_error());
        // Non-fatal: some adapters auto-pass all messages
    } else {
        pass_filter_id_ = filter_id;
    }

    // Clear the receive buffer
    LOG_DEBUG("Calling PassThruIoctl(channel={}, CLEAR_RX_BUFFER)", channel_id_);
    ret = dll_.PassThruIoctl(channel_id_, j2534::IOCTL_CLEAR_RX_BUFFER, nullptr, nullptr);
    LOG_DEBUG("PassThruIoctl(CLEAR_RX_BUFFER) returned {} ({})", ret, j2534::status_to_string(ret));

    open_ = true;
    last_raw_ts_ = 0;
    rollover_offset_ = 0;

    LOG_INFO("CAN channel opened: channel_id={}, bitrate={} bps", channel_id_, bitrate);
}

void J2534Channel::close() {
    if (!open_) return;

    LOG_DEBUG("Calling PassThruDisconnect(channel={})", channel_id_);
    long ret = dll_.PassThruDisconnect(channel_id_);
    LOG_DEBUG("PassThruDisconnect returned {} ({})", ret, j2534::status_to_string(ret));

    if (ret != j2534::STATUS_NOERROR) {
        LOG_WARNING("PassThruDisconnect failed: {}", get_last_error());
    }

    open_ = false;
    pass_filter_id_ = kInvalidFilterId;
    LOG_DEBUG("J2534Channel closed (channel_id={})", channel_id_);
}

std::vector<CanFrame> J2534Channel::read(uint32_t timeout_ms) {
    if (!open_) return {};

    // Read up to 16 messages at a time
    constexpr unsigned long kBatchSize = 16;
    j2534::PASSTHRU_MSG msgs[kBatchSize] = {};
    unsigned long num_msgs = kBatchSize;

    long ret = dll_.PassThruReadMsgs(channel_id_, msgs, &num_msgs, timeout_ms);

    // ERR_BUFFER_EMPTY and ERR_TIMEOUT are normal "no data" conditions
    if (ret == j2534::ERR_BUFFER_EMPTY || ret == j2534::ERR_TIMEOUT) {
        return {};
    }

    if (ret != j2534::STATUS_NOERROR && num_msgs == 0) {
        LOG_DEBUG("PassThruReadMsgs returned {} ({}) with 0 messages",
                  ret, j2534::status_to_string(ret));
        return {};
    }

    // Capture host timestamp immediately after successful read
    auto now = std::chrono::steady_clock::now();
    uint64_t host_ts = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());

    std::vector<CanFrame> frames;
    frames.reserve(num_msgs);

    for (unsigned long i = 0; i < num_msgs; ++i) {
        // Skip TX echo messages
        if (msgs[i].RxStatus & j2534::RX_MSG_TX_DONE) continue;

        frames.push_back(convert_msg(msgs[i]));
        frames.back().host_timestamp_us = host_ts;
    }

    return frames;
}

void J2534Channel::write(const CanFrame& frame) {
    // Defense in depth: reject writes in passive mode (T073, Constitution I)
    throw TransportError(0,
        "Cannot write in Passive mode — active mode not supported in MVP",
        "J2534Channel::write", false);

    // Future active-mode implementation would go here:
    // (void)frame; // suppress unused warning
}

void J2534Channel::setFilter(uint32_t mask, uint32_t pattern) {
    if (!open_) return;

    // Remove existing filter first
    clearFilters();

    // Build mask message
    j2534::PASSTHRU_MSG mask_msg = {};
    mask_msg.ProtocolID = j2534::PROTOCOL_CAN;
    mask_msg.DataSize = 4;
    mask_msg.Data[0] = static_cast<uint8_t>((mask >> 24) & 0xFF);
    mask_msg.Data[1] = static_cast<uint8_t>((mask >> 16) & 0xFF);
    mask_msg.Data[2] = static_cast<uint8_t>((mask >>  8) & 0xFF);
    mask_msg.Data[3] = static_cast<uint8_t>((mask >>  0) & 0xFF);

    // Build pattern message
    j2534::PASSTHRU_MSG pattern_msg = {};
    pattern_msg.ProtocolID = j2534::PROTOCOL_CAN;
    pattern_msg.DataSize = 4;
    pattern_msg.Data[0] = static_cast<uint8_t>((pattern >> 24) & 0xFF);
    pattern_msg.Data[1] = static_cast<uint8_t>((pattern >> 16) & 0xFF);
    pattern_msg.Data[2] = static_cast<uint8_t>((pattern >>  8) & 0xFF);
    pattern_msg.Data[3] = static_cast<uint8_t>((pattern >>  0) & 0xFF);

    unsigned long filter_id = 0;
    LOG_DEBUG("Calling PassThruStartMsgFilter(channel={}, PASS, mask=0x{:08X}, pattern=0x{:08X})",
              channel_id_, mask, pattern);
    long ret = dll_.PassThruStartMsgFilter(channel_id_, j2534::FILTER_PASS,
                                            &mask_msg, &pattern_msg, nullptr, &filter_id);
    LOG_DEBUG("PassThruStartMsgFilter returned {} ({}), filter_id={}",
              ret, j2534::status_to_string(ret), filter_id);

    if (ret != j2534::STATUS_NOERROR) {
        LOG_WARNING("Hardware filter setup failed: {} — relying on software filter",
                   get_last_error());
    } else {
        pass_filter_id_ = filter_id;
    }
}

void J2534Channel::clearFilters() {
    if (!open_) return;

    LOG_DEBUG("Calling PassThruIoctl(channel={}, CLEAR_MSG_FILTERS)", channel_id_);
    long ret = dll_.PassThruIoctl(channel_id_, j2534::IOCTL_CLEAR_MSG_FILTERS,
                                   nullptr, nullptr);
    LOG_DEBUG("PassThruIoctl(CLEAR_MSG_FILTERS) returned {} ({})",
              ret, j2534::status_to_string(ret));

    pass_filter_id_ = kInvalidFilterId;

    // Re-install pass-all filter so we keep receiving messages
    j2534::PASSTHRU_MSG mask_msg = {};
    mask_msg.ProtocolID = j2534::PROTOCOL_CAN;
    mask_msg.DataSize = 4;

    j2534::PASSTHRU_MSG pattern_msg = {};
    pattern_msg.ProtocolID = j2534::PROTOCOL_CAN;
    pattern_msg.DataSize = 4;

    unsigned long filter_id = 0;
    LOG_DEBUG("Re-installing pass-all filter on channel {}", channel_id_);
    ret = dll_.PassThruStartMsgFilter(channel_id_, j2534::FILTER_PASS,
                                       &mask_msg, &pattern_msg, nullptr, &filter_id);
    if (ret == j2534::STATUS_NOERROR) {
        pass_filter_id_ = filter_id;
    }
}

bool J2534Channel::isOpen() const {
    return open_;
}

CanFrame J2534Channel::convert_msg(const j2534::PASSTHRU_MSG& msg) const {
    CanFrame frame;

    // Handle 32-bit timestamp rollover → 64-bit extension
    // (mutable state cached — but safe because read() is single-threaded)
    uint64_t raw_ts = msg.Timestamp;
    auto& self = const_cast<J2534Channel&>(*this);
    if (raw_ts < self.last_raw_ts_) {
        // 32-bit rollover detected
        self.rollover_offset_ += 0x100000000ULL;
        LOG_DEBUG("Timestamp rollover detected: raw={} < last={}, offset now={}",
                  raw_ts, self.last_raw_ts_, self.rollover_offset_);
    }
    self.last_raw_ts_ = raw_ts;
    frame.adapter_timestamp_us = raw_ts + self.rollover_offset_;

    // Extract arbitration ID from first 4 bytes of Data
    if (msg.DataSize >= 4) {
        uint32_t id = (static_cast<uint32_t>(msg.Data[0]) << 24) |
                      (static_cast<uint32_t>(msg.Data[1]) << 16) |
                      (static_cast<uint32_t>(msg.Data[2]) << 8)  |
                      (static_cast<uint32_t>(msg.Data[3]));
        frame.arbitration_id = id;
    }

    // Determine frame type from RxStatus
    if (msg.RxStatus & j2534::RX_MSG_CAN_29BIT_ID) {
        frame.type = FrameType::Extended;
        frame.arbitration_id &= 0x1FFFFFFF; // mask to 29 bits
    } else {
        frame.type = FrameType::Standard;
        frame.arbitration_id &= 0x7FF; // mask to 11 bits
    }

    // Payload starts at Data[4] in J2534 CAN messages
    uint32_t payload_size = (msg.DataSize > 4) ? (msg.DataSize - 4) : 0;
    if (payload_size > 8) payload_size = 8; // classic CAN max
    frame.dlc = static_cast<uint8_t>(payload_size);

    for (uint32_t i = 0; i < payload_size; ++i) {
        frame.data[i] = msg.Data[4 + i];
    }

    return frame;
}

std::string J2534Channel::get_last_error() const {
    if (!dll_.PassThruGetLastError) return "(no error info available)";
    char buf[256] = {};
    dll_.PassThruGetLastError(buf);
    return std::string(buf);
}

} // namespace canmatik
