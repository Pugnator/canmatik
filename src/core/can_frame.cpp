/// @file can_frame.cpp
/// CanFrame validation and utility implementations.

#include "core/can_frame.h"
#include <format>

namespace canmatik {

std::string validate_frame(const CanFrame& frame) {
    // ID range check
    if (frame.type == FrameType::Standard && frame.arbitration_id > kMaxStandardId) {
        return std::format("Standard frame ID 0x{:X} exceeds max 0x{:X}",
                           frame.arbitration_id, kMaxStandardId);
    }
    if (frame.type == FrameType::Extended && frame.arbitration_id > kMaxExtendedId) {
        return std::format("Extended frame ID 0x{:X} exceeds max 0x{:X}",
                           frame.arbitration_id, kMaxExtendedId);
    }
    if (frame.type == FrameType::J1850 && frame.arbitration_id > kMaxJ1850Id) {
        return std::format("J1850 frame ID 0x{:X} exceeds max 0x{:X}",
                           frame.arbitration_id, kMaxJ1850Id);
    }

    // DLC range check
    const uint8_t max_dlc = (frame.type == FrameType::FD) ? kCanFdMaxDlc : kClassicCanMaxDlc;
    if (frame.dlc > max_dlc) {
        return std::format("DLC {} exceeds maximum {} for frame type {}",
                           frame.dlc, max_dlc, frame_type_to_string(frame.type));
    }

    // Trailing bytes must be zero
    for (uint8_t i = frame.dlc; i < frame.data.size(); ++i) {
        if (frame.data[i] != 0) {
            return std::format("Non-zero byte at data[{}] beyond DLC {}", i, frame.dlc);
        }
    }

    return {};
}

bool is_valid_id(uint32_t id, FrameType type) {
    switch (type) {
        case FrameType::Standard:
            return id <= kMaxStandardId;
        case FrameType::Extended:
        case FrameType::FD:
            return id <= kMaxExtendedId;
        case FrameType::Error:
        case FrameType::Remote:
            return id <= kMaxExtendedId;  // Error/Remote can use either range
        case FrameType::J1850:
            return id <= kMaxJ1850Id;
    }
    return false;
}

const char* frame_type_to_string(FrameType type) {
    switch (type) {
        case FrameType::Standard: return "Std";
        case FrameType::Extended: return "Ext";
        case FrameType::FD:       return "FD";
        case FrameType::Error:    return "Err";
        case FrameType::Remote:   return "Rtr";
        case FrameType::J1850:    return "J1850";
    }
    return "???";
}

} // namespace canmatik
