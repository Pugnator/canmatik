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

void J2534Channel::open(uint32_t bitrate, BusProtocol protocol) {
    if (open_) {
        LOG_WARNING("J2534Channel::open() — channel already open, closing first");
        close();
    }

    bitrate_ = bitrate;
    protocol_ = protocol;
    unsigned long channel_id = 0;

    // Select J2534 protocol ID and connect flags based on BusProtocol
    unsigned long j2534_proto = j2534::PROTOCOL_CAN;
    unsigned long flags = 0;
    const char* proto_label = "CAN";

    switch (protocol) {
    case BusProtocol::J1850_VPW:
        j2534_proto = j2534::PROTOCOL_J1850_VPW;
        proto_label = "J1850_VPW";
        break;
    case BusProtocol::J1850_PWM:
        j2534_proto = j2534::PROTOCOL_J1850_PWM;
        proto_label = "J1850_PWM";
        break;
    default: // CAN
        flags = j2534::CAN_ID_BOTH; // Accept both 11-bit and 29-bit IDs
        break;
    }

    LOG_DEBUG("Calling PassThruConnect(device={}, protocol={}(0x{:X}), flags=0x{:X}, baudrate={})",
              device_id_, proto_label, j2534_proto, flags, bitrate);
    long ret = dll_.PassThruConnect(device_id_, j2534_proto, flags, bitrate, &channel_id);
    LOG_DEBUG("PassThruConnect returned {} ({})", ret, j2534::status_to_string(ret));

    if (ret != j2534::STATUS_NOERROR) {
        throw TransportError(static_cast<int32_t>(ret),
                             "PassThruConnect failed: " + get_last_error(),
                             "PassThruConnect");
    }

    channel_id_ = channel_id;

    // Set a pass-all filter so we receive all messages.
    // For CAN: 4-byte mask/pattern (arbitration ID). For J1850: 1-byte (match all).
    j2534::PASSTHRU_MSG mask_msg = {};
    mask_msg.ProtocolID = j2534_proto;
    j2534::PASSTHRU_MSG pattern_msg = {};
    pattern_msg.ProtocolID = j2534_proto;

    if (is_j1850()) {
        mask_msg.DataSize = 1;
        mask_msg.Data[0] = 0x00;        // match any priority byte
        pattern_msg.DataSize = 1;
        pattern_msg.Data[0] = 0x00;
    } else {
        mask_msg.DataSize = 4;
        std::memset(mask_msg.Data, 0x00, 4);     // Mask = 0x00000000: match all IDs
        pattern_msg.DataSize = 4;
        std::memset(pattern_msg.Data, 0x00, 4);  // Pattern = 0x00000000
    }

    unsigned long filter_id = 0;
    LOG_DEBUG("Calling PassThruStartMsgFilter(channel={}, PASS, dataSize={})",
              channel_id_, mask_msg.DataSize);
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

    LOG_INFO("{} channel opened: channel_id={}, bitrate={} bps", proto_label, channel_id_, bitrate);
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
    if (!open_) {
        throw TransportError(0, "Cannot write — channel not open",
                             "J2534Channel::write", false);
    }

    j2534::PASSTHRU_MSG msg = {};

    if (is_j1850()) {
        // J1850: Data[0..2] = 3-byte header (from arbitration_id), Data[3..] = payload
        msg.ProtocolID = (protocol_ == BusProtocol::J1850_PWM)
                         ? j2534::PROTOCOL_J1850_PWM : j2534::PROTOCOL_J1850_VPW;
        msg.Data[0] = static_cast<unsigned char>((frame.arbitration_id >> 16) & 0xFF);
        msg.Data[1] = static_cast<unsigned char>((frame.arbitration_id >>  8) & 0xFF);
        msg.Data[2] = static_cast<unsigned char>((frame.arbitration_id >>  0) & 0xFF);

        uint8_t payload_len = (frame.dlc <= 8) ? frame.dlc : 8;
        for (uint8_t i = 0; i < payload_len; ++i) {
            msg.Data[3 + i] = frame.data[i];
        }
        msg.DataSize = 3 + payload_len;
    } else {
        // CAN: Data[0..3] = arbitration ID (big-endian), Data[4..] = payload
        msg.ProtocolID = j2534::PROTOCOL_CAN;
        msg.TxFlags = (frame.type == FrameType::Extended) ? j2534::CAN_29BIT_ID : 0;

        msg.Data[0] = static_cast<unsigned char>((frame.arbitration_id >> 24) & 0xFF);
        msg.Data[1] = static_cast<unsigned char>((frame.arbitration_id >> 16) & 0xFF);
        msg.Data[2] = static_cast<unsigned char>((frame.arbitration_id >>  8) & 0xFF);
        msg.Data[3] = static_cast<unsigned char>((frame.arbitration_id >>  0) & 0xFF);

        uint8_t payload_len = (frame.dlc <= 8) ? frame.dlc : 8;
        for (uint8_t i = 0; i < payload_len; ++i) {
            msg.Data[4 + i] = frame.data[i];
        }
        msg.DataSize = 4 + payload_len;
    }

    unsigned long num_msgs = 1;
    constexpr unsigned long kWriteTimeoutMs = 100;

    LOG_DEBUG("Calling PassThruWriteMsgs(channel={}, id=0x{:X}, dlc={}, timeout={}ms)",
              channel_id_, frame.arbitration_id, frame.dlc, kWriteTimeoutMs);
    long ret = dll_.PassThruWriteMsgs(channel_id_, &msg, &num_msgs, kWriteTimeoutMs);
    LOG_DEBUG("PassThruWriteMsgs returned {} ({}), num_msgs={}",
              ret, j2534::status_to_string(ret), num_msgs);

    if (ret != j2534::STATUS_NOERROR) {
        throw TransportError(static_cast<int32_t>(ret),
                             "PassThruWriteMsgs failed: " + get_last_error(),
                             "PassThruWriteMsgs",
                             ret == j2534::ERR_TIMEOUT || ret == j2534::ERR_BUFFER_FULL);
    }
}

void J2534Channel::setFilter(uint32_t mask, uint32_t pattern) {
    if (!open_) return;

    // Remove existing filter first
    clearFilters();

    // Determine protocol ID for filter messages
    unsigned long proto = j2534::PROTOCOL_CAN;
    if (protocol_ == BusProtocol::J1850_VPW) proto = j2534::PROTOCOL_J1850_VPW;
    else if (protocol_ == BusProtocol::J1850_PWM) proto = j2534::PROTOCOL_J1850_PWM;

    j2534::PASSTHRU_MSG mask_msg = {};
    mask_msg.ProtocolID = proto;
    j2534::PASSTHRU_MSG pattern_msg = {};
    pattern_msg.ProtocolID = proto;

    if (is_j1850()) {
        // J1850: 3-byte mask/pattern (header bytes)
        mask_msg.DataSize = 3;
        mask_msg.Data[0] = static_cast<uint8_t>((mask >> 16) & 0xFF);
        mask_msg.Data[1] = static_cast<uint8_t>((mask >>  8) & 0xFF);
        mask_msg.Data[2] = static_cast<uint8_t>((mask >>  0) & 0xFF);
        pattern_msg.DataSize = 3;
        pattern_msg.Data[0] = static_cast<uint8_t>((pattern >> 16) & 0xFF);
        pattern_msg.Data[1] = static_cast<uint8_t>((pattern >>  8) & 0xFF);
        pattern_msg.Data[2] = static_cast<uint8_t>((pattern >>  0) & 0xFF);
    } else {
        // CAN: 4-byte mask/pattern (arbitration ID)
        mask_msg.DataSize = 4;
        mask_msg.Data[0] = static_cast<uint8_t>((mask >> 24) & 0xFF);
        mask_msg.Data[1] = static_cast<uint8_t>((mask >> 16) & 0xFF);
        mask_msg.Data[2] = static_cast<uint8_t>((mask >>  8) & 0xFF);
        mask_msg.Data[3] = static_cast<uint8_t>((mask >>  0) & 0xFF);
        pattern_msg.DataSize = 4;
        pattern_msg.Data[0] = static_cast<uint8_t>((pattern >> 24) & 0xFF);
        pattern_msg.Data[1] = static_cast<uint8_t>((pattern >> 16) & 0xFF);
        pattern_msg.Data[2] = static_cast<uint8_t>((pattern >>  8) & 0xFF);
        pattern_msg.Data[3] = static_cast<uint8_t>((pattern >>  0) & 0xFF);
    }

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
    unsigned long proto = j2534::PROTOCOL_CAN;
    if (protocol_ == BusProtocol::J1850_VPW) proto = j2534::PROTOCOL_J1850_VPW;
    else if (protocol_ == BusProtocol::J1850_PWM) proto = j2534::PROTOCOL_J1850_PWM;

    j2534::PASSTHRU_MSG mask_msg = {};
    mask_msg.ProtocolID = proto;
    j2534::PASSTHRU_MSG pattern_msg = {};
    pattern_msg.ProtocolID = proto;

    if (is_j1850()) {
        mask_msg.DataSize = 1;
        pattern_msg.DataSize = 1;
    } else {
        mask_msg.DataSize = 4;
        pattern_msg.DataSize = 4;
    }

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

    if (is_j1850()) {
        // J1850: Data[0..2] = header (priority, target, source), Data[3..] = payload
        frame.type = FrameType::J1850;
        if (msg.DataSize >= 3) {
            frame.arbitration_id = (static_cast<uint32_t>(msg.Data[0]) << 16) |
                                   (static_cast<uint32_t>(msg.Data[1]) <<  8) |
                                   (static_cast<uint32_t>(msg.Data[2]));
        }
        uint32_t payload_size = (msg.DataSize > 3) ? (msg.DataSize - 3) : 0;
        if (payload_size > 8) payload_size = 8;
        frame.dlc = static_cast<uint8_t>(payload_size);
        for (uint32_t i = 0; i < payload_size; ++i) {
            frame.data[i] = msg.Data[3 + i];
        }
    } else {
        // CAN: Data[0..3] = arbitration ID (big-endian), Data[4..] = payload
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
