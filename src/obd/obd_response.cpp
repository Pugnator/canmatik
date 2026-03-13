/// @file obd_response.cpp
/// OBD response parsing and multi-frame assembly.

#include "obd/obd_response.h"
#include "obd/iso15765.h"

#include <cstring>
#include <format>

namespace canmatik {

Result<ObdResponse>
parse_obd_response(const CanFrame& frame, uint8_t expected_mode, uint8_t expected_pid,
                   uint32_t resp_base) {
    if (!(frame.arbitration_id >= resp_base && frame.arbitration_id < resp_base + 8)) {
        return Result<ObdResponse>::error(std::format(
            "CAN ID 0x{:03X} is not a valid OBD response ID for base 0x{:03X}",
            frame.arbitration_id, resp_base));
    }

    // Check for single frame PCI
    uint8_t pci = frame.data[0];
    uint8_t pci_type = pci & 0xF0;
    uint8_t payload_len = pci & 0x0F;

    if (pci_type != iso15765::kPciSingleFrame) {
        return Result<ObdResponse>::error(std::format(
            "expected single frame (PCI=0x0), got PCI=0x{:X}0", pci_type >> 4));
    }

    if (payload_len < 2 || payload_len > 7) {
        return Result<ObdResponse>::error("invalid single frame payload length: "
                               + std::to_string(payload_len));
    }

    uint8_t resp_mode = frame.data[1];
    uint8_t resp_pid = frame.data[2];

    // Check for negative response (0x7F)
    if (resp_mode == iso15765::kNegativeResponse) {
        ObdResponse resp;
        resp.rx_id = frame.arbitration_id;
        resp.is_negative = true;
        resp.mode = frame.data[2];       // Rejected service
        resp.negative_code = frame.data[3]; // NRC
        resp.timestamp_us = frame.host_timestamp_us;
        resp.raw_frame = frame;
        return resp;
    }

    // Validate mode + 0x40
    if (resp_mode != expected_mode + iso15765::kPositiveResponseOffset) {
        return Result<ObdResponse>::error(std::format(
            "response mode 0x{:02X} does not match expected 0x{:02X}",
            resp_mode, expected_mode + iso15765::kPositiveResponseOffset));
    }

    // Validate PID echo
    if (resp_pid != expected_pid) {
        return Result<ObdResponse>::error(std::format(
            "response PID 0x{:02X} does not match expected 0x{:02X}", resp_pid, expected_pid));
    }

    ObdResponse resp;
    resp.rx_id = frame.arbitration_id;
    resp.mode = resp_mode;
    resp.pid = resp_pid;
    resp.data_length = payload_len - 2; // Subtract mode + PID bytes
    std::memset(resp.data, 0, sizeof(resp.data));
    for (uint8_t i = 0; i < resp.data_length && i < 4; ++i) {
        resp.data[i] = frame.data[3 + i];
    }
    resp.timestamp_us = frame.host_timestamp_us;
    resp.raw_frame = frame;
    return resp;
}

Result<ObdResponse>
parse_obd_response(const CanFrame& frame, uint32_t resp_base) {
    if (!(frame.arbitration_id >= resp_base && frame.arbitration_id < resp_base + 8)) {
        return Result<ObdResponse>::error(std::format(
            "CAN ID 0x{:03X} is not a valid OBD response ID for base 0x{:03X}",
            frame.arbitration_id, resp_base));
    }

    uint8_t pci = frame.data[0];
    uint8_t pci_type = pci & 0xF0;
    uint8_t payload_len = pci & 0x0F;

    if (pci_type != iso15765::kPciSingleFrame) {
        return Result<ObdResponse>::error("not a single frame");
    }

    if (payload_len < 2 || payload_len > 7) {
        return Result<ObdResponse>::error("invalid payload length");
    }

    uint8_t resp_mode = frame.data[1];

    // Negative response
    if (resp_mode == iso15765::kNegativeResponse) {
        ObdResponse resp;
        resp.rx_id = frame.arbitration_id;
        resp.is_negative = true;
        resp.mode = frame.data[2];
        resp.negative_code = frame.data[3];
        resp.timestamp_us = frame.host_timestamp_us;
        resp.raw_frame = frame;
        return resp;
    }

    ObdResponse resp;
    resp.rx_id = frame.arbitration_id;
    resp.mode = resp_mode;
    resp.pid = frame.data[2];
    resp.data_length = payload_len - 2;
    std::memset(resp.data, 0, sizeof(resp.data));
    for (uint8_t i = 0; i < resp.data_length && i < 4; ++i) {
        resp.data[i] = frame.data[3 + i];
    }
    resp.timestamp_us = frame.host_timestamp_us;
    resp.raw_frame = frame;
    return resp;
}

Result<std::vector<uint8_t>>
assemble_multiframe(const std::vector<CanFrame>& frames) {
    if (frames.empty()) {
        return Result<std::vector<uint8_t>>::error("no frames to assemble");
    }

    const auto& ff = frames[0];
    uint8_t pci_type = ff.data[0] & 0xF0;

    if (pci_type != iso15765::kPciFirstFrame) {
        return Result<std::vector<uint8_t>>::error("first frame has wrong PCI type");
    }

    // First Frame: Data[0] high nibble = 1, low nibble + Data[1] = total length
    uint16_t total_len = static_cast<uint16_t>(((ff.data[0] & 0x0F) << 8) | ff.data[1]);

    std::vector<uint8_t> payload;
    payload.reserve(total_len);

    // First frame carries 6 data bytes (bytes 2..7)
    for (int i = 2; i < 8 && payload.size() < total_len; ++i) {
        payload.push_back(ff.data[i]);
    }

    // Consecutive frames
    uint8_t expected_seq = 1;
    for (size_t f = 1; f < frames.size() && payload.size() < total_len; ++f) {
        const auto& cf = frames[f];
        uint8_t cf_pci = cf.data[0];

        if ((cf_pci & 0xF0) != iso15765::kPciConsecutiveFrame) {
            return Result<std::vector<uint8_t>>::error("expected consecutive frame at index " + std::to_string(f));
        }

        uint8_t seq = cf_pci & 0x0F;
        if (seq != (expected_seq & 0x0F)) {
            return Result<std::vector<uint8_t>>::error("sequence number mismatch: expected "
                                   + std::to_string(expected_seq & 0x0F)
                                   + ", got " + std::to_string(seq));
        }

        // Consecutive frame carries 7 data bytes (bytes 1..7)
        for (int i = 1; i < 8 && payload.size() < total_len; ++i) {
            payload.push_back(cf.data[i]);
        }

        ++expected_seq;
    }

    if (payload.size() < total_len) {
        return Result<std::vector<uint8_t>>::error("incomplete multi-frame: got " + std::to_string(payload.size())
                               + " bytes, expected " + std::to_string(total_len));
    }

    return payload;
}

} // namespace canmatik
