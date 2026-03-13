/// @file obd_session.cpp
/// ObdSession implementation — query orchestration over IChannel.

#include "obd/obd_session.h"
#include "core/timestamp.h"

#include <chrono>
#include <cstring>
#include <format>

namespace canmatik {

ObdSession::ObdSession(IChannel& channel, uint32_t tx_id, uint32_t rx_base,
                       ICaptureSync* frame_sink)
    : channel_(channel), tx_id_(tx_id), rx_base_(rx_base), frame_sink_(frame_sink) {}

Result<std::vector<SupportedPids>> ObdSession::query_supported_pids() {
    std::vector<SupportedPids> result;

    // Query PID $00, $20, $40 to discover supported PIDs
    for (uint8_t base_pid : {0x00, 0x20, 0x40}) {
        auto resp = send_and_receive(0x01, base_pid);
        if (!resp) break; // No more ranges supported

        if (resp->is_negative) break;

        // Parse 4-byte bitmap
        SupportedPids sp;
        sp.ecu_id = resp->rx_id;

        uint32_t bitmap = 0;
        for (int i = 0; i < 4 && i < resp->data_length; ++i) {
            bitmap = (bitmap << 8) | resp->data[i];
        }

        for (int bit = 0; bit < 32; ++bit) {
            if (bitmap & (1u << (31 - bit))) {
                sp.pids.push_back(static_cast<uint8_t>(base_pid + bit + 1));
            }
        }

        result.push_back(std::move(sp));

        // If bit 32 (next range support) is not set, stop
        if (!(bitmap & 0x01)) break;
    }

    return result;
}

Result<DecodedPid> ObdSession::query_pid(uint8_t mode, uint8_t pid) {
    auto resp = send_and_receive(mode, pid);
    if (!resp) return Result<DecodedPid>::error(resp.error());

    if (resp->is_negative) {
        return Result<DecodedPid>::error(std::format(
            "ECU rejected request: NRC 0x{:02X}", resp->negative_code));
    }

    const auto* def = pid_lookup(mode, pid);
    if (!def) {
        // Unknown PID — return raw value
        DecodedPid decoded;
        decoded.ecu_id = resp->rx_id;
        decoded.mode = mode;
        decoded.pid = pid;
        decoded.name = "Unknown PID";
        decoded.value = 0;
        decoded.raw_bytes.assign(resp->data, resp->data + resp->data_length);
        decoded.timestamp_us = resp->timestamp_us;
        return decoded;
    }

    return decode_pid(resp->rx_id, resp->data, resp->data_length, *def, resp->timestamp_us);
}

Result<std::vector<DtcCode>> ObdSession::read_dtcs() {
    // First try: functional/standard behavior
    auto resp = send_and_receive(0x03, 0x00);
    if (resp && !resp->is_negative) {
        auto decoded = decode_dtcs(resp->data, resp->data_length, resp->rx_id, false);
        if (decoded && !decoded->empty()) return decoded;
        // If empty result, fall through to conservative fallback
    }

    // Conservative fallback: try physical addressing (0x7E0) with repeated requests
    std::vector<uint8_t> agg;
    uint32_t found_ecu = 0;
    auto save_tx = tx_id_;
    auto save_rx = rx_base_;
    tx_id_ = iso15765::kPhysicalTxBase; // 0x7E0
    rx_base_ = 0x760; // Forscan-observed response base

    for (int attempt = 0; attempt < 3; ++attempt) {
        // Try pending DTCs (Mode 07)
        auto p = send_and_receive(0x07, 0x00);
        if (p && !p->is_negative && p->data_length > 0) {
            found_ecu = p->rx_id;
            for (uint8_t i = 0; i < p->data_length; ++i) agg.push_back(p->data[i]);
        }

        // Try stored DTCs (Mode 03)
        auto s = send_and_receive(0x03, 0x00);
        if (s && !s->is_negative && s->data_length > 0) {
            found_ecu = s->rx_id;
            for (uint8_t i = 0; i < s->data_length; ++i) agg.push_back(s->data[i]);
        }
    }

    // Restore
    tx_id_ = save_tx;
    rx_base_ = save_rx;

    if (agg.empty())
        return Result<std::vector<DtcCode>>::error("No DTCs found (functional and physical attempts failed)");

    return decode_dtcs(agg.data(), agg.size(), found_ecu, false);
}

Result<std::vector<DtcCode>> ObdSession::read_pending_dtcs() {
    // Try standard single-attempt first
    auto resp = send_and_receive(0x07, 0x00);
    if (resp && !resp->is_negative) {
        auto decoded = decode_dtcs(resp->data, resp->data_length, resp->rx_id, true);
        if (decoded && !decoded->empty()) return decoded;
    }

    // Fallback: physical addressing with repeats
    std::vector<uint8_t> agg;
    uint32_t found_ecu = 0;
    auto save_tx = tx_id_;
    auto save_rx = rx_base_;
    tx_id_ = iso15765::kPhysicalTxBase;
    rx_base_ = 0x760;

    for (int attempt = 0; attempt < 3; ++attempt) {
        auto p = send_and_receive(0x07, 0x00);
        if (p && !p->is_negative && p->data_length > 0) {
            found_ecu = p->rx_id;
            for (uint8_t i = 0; i < p->data_length; ++i) agg.push_back(p->data[i]);
        }
    }

    tx_id_ = save_tx;
    rx_base_ = save_rx;

    if (agg.empty())
        return Result<std::vector<DtcCode>>::error("No pending DTCs found (functional and physical attempts failed)");

    return decode_dtcs(agg.data(), agg.size(), found_ecu, true);
}

Result<void> ObdSession::clear_dtcs(bool force) {
    if (!force) {
        return Result<void>::error("DTC clear requires --force flag (Mode $04 is destructive)");
    }

    // Mode $04 has no PID — send 1 payload byte
    auto do_clear = [&]() -> Result<void> {
        CanFrame frame{};
        frame.arbitration_id = tx_id_;
        frame.type = FrameType::Standard;
        frame.dlc = iso15765::kStandardDlc;
        frame.data.fill(iso15765::kPaddingByte);
        frame.data[0] = 0x01; // Single frame, 1 payload byte
        frame.data[1] = 0x04; // Mode $04

        frame.host_timestamp_us = host_timestamp_us();
        if (frame_sink_) frame_sink_->onFrame(frame);
        channel_.write(frame);

        // Wait for positive response (0x44)
        auto resp_frame = read_obd_frame(iso15765::kP2CanTimeout);
        if (!resp_frame) return Result<void>::error(resp_frame.error());

        if (resp_frame->data[1] == iso15765::kNegativeResponse) {
            return Result<void>::error(std::format(
                "ECU rejected DTC clear: NRC 0x{:02X}", resp_frame->data[3]));
        }

        if (resp_frame->data[1] != 0x44) {
            return Result<void>::error(std::format(
                "unexpected response to Mode $04: 0x{:02X}", resp_frame->data[1]));
        }

        return {};
    };

    // First, try standard behavior
    auto res = do_clear();
    if (res) return res;

    // Fallback: try physical addressing with a few retries
    auto save_tx = tx_id_;
    auto save_rx = rx_base_;
    tx_id_ = iso15765::kPhysicalTxBase;
    rx_base_ = 0x760;

    for (int attempt = 0; attempt < 3; ++attempt) {
        auto r = do_clear();
        if (r) {
            tx_id_ = save_tx;
            rx_base_ = save_rx;
            return r;
        }
    }

    tx_id_ = save_tx;
    rx_base_ = save_rx;

    return Result<void>::error("DTC clear failed after functional and physical attempts");
}

Result<VehicleInfo> ObdSession::read_vehicle_info() {
    VehicleInfo info;

    // VIN (Mode $09, PID $02) — multi-frame
    auto vin_data = send_and_receive_multiframe(0x09, 0x02);
    if (vin_data) {
        // Skip first byte (number of data items) if present
        size_t start = 0;
        if (vin_data->size() > 17) start = 1;
        auto end = std::min(start + 17, vin_data->size());
        info.vin.assign(vin_data->begin() + start, vin_data->begin() + end);
    }

    // Calibration ID (Mode $09, PID $04) — may be multi-frame
    auto cal_data = send_and_receive_multiframe(0x09, 0x04);
    if (cal_data) {
        size_t start = 0;
        if (cal_data->size() > 16) start = 1;
        // Trim trailing nulls/padding
        auto end = cal_data->size();
        while (end > start && ((*cal_data)[end - 1] == 0 || (*cal_data)[end - 1] == 0x55)) {
            --end;
        }
        info.calibration_id.assign(cal_data->begin() + start, cal_data->begin() + end);
    }

    // ECU name (Mode $09, PID $0A) — may be multi-frame
    auto ecu_data = send_and_receive_multiframe(0x09, 0x0A);
    if (ecu_data) {
        size_t start = 0;
        if (ecu_data->size() > 20) start = 1;
        auto end = ecu_data->size();
        while (end > start && ((*ecu_data)[end - 1] == 0 || (*ecu_data)[end - 1] == 0x55)) {
            --end;
        }
        info.ecu_name.assign(ecu_data->begin() + start, ecu_data->begin() + end);
    }

    return info;
}

Result<ObdResponse>
ObdSession::send_and_receive(uint8_t mode, uint8_t pid) {
    auto request = build_obd_request(mode, pid, tx_id_);
    request.host_timestamp_us = host_timestamp_us();
    if (frame_sink_) frame_sink_->onFrame(request);
    channel_.write(request);

    auto frame = read_obd_frame(iso15765::kP2CanTimeout);
    if (!frame) return Result<ObdResponse>::error(frame.error());

    return parse_obd_response(*frame, mode, pid, rx_base_);
}

Result<std::vector<uint8_t>>
ObdSession::send_and_receive_multiframe(uint8_t mode, uint8_t pid) {
    auto request = build_obd_request(mode, pid, tx_id_);
    request.host_timestamp_us = host_timestamp_us();
    if (frame_sink_) frame_sink_->onFrame(request);
    channel_.write(request);

    // Read first response frame
    auto first = read_obd_frame(iso15765::kP2CanTimeout);
    if (!first) return Result<std::vector<uint8_t>>::error(first.error());

    uint8_t pci_type = first->data[0] & 0xF0;

    // Single frame response
    if (pci_type == iso15765::kPciSingleFrame) {
        uint8_t len = first->data[0] & 0x0F;
        std::vector<uint8_t> payload;
        // Skip mode+pid bytes (data[1] and data[2])
        for (int i = 3; i < 1 + len && i < 8; ++i) {
            payload.push_back(first->data[i]);
        }
        return payload;
    }

    // Multi-frame: First Frame
    if (pci_type != iso15765::kPciFirstFrame) {
        return Result<std::vector<uint8_t>>::error("unexpected PCI type in response");
    }

    std::vector<CanFrame> frames;
    frames.push_back(*first);

    // Send flow control
    auto fc = build_flow_control(tx_id_);
    fc.host_timestamp_us = host_timestamp_us();
    if (frame_sink_) frame_sink_->onFrame(fc);
    channel_.write(fc);

    // Read consecutive frames
    uint16_t total_len = static_cast<uint16_t>(((first->data[0] & 0x0F) << 8) | first->data[1]);
    size_t received = 6; // First frame carries 6 bytes

    while (received < total_len) {
        auto cf = read_obd_frame(iso15765::kP2CanTimeout);
        if (!cf) return Result<std::vector<uint8_t>>::error(cf.error());
        frames.push_back(*cf);
        received += 7; // Each consecutive frame carries 7 bytes
    }

    return assemble_multiframe(frames);
}

Result<CanFrame>
ObdSession::read_obd_frame(uint32_t timeout_ms) {
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;

        auto frames = channel_.read(static_cast<uint32_t>(remaining));
        for (const auto& f : frames) {
            if (f.arbitration_id >= rx_base_ && f.arbitration_id < rx_base_ + 8) {
                if (frame_sink_) frame_sink_->onFrame(f);

                // NRC 0x78: responsePending — ECU needs more time.
                // Extend deadline to P2* and keep reading for the real response.
                uint8_t pci_type = f.data[0] & 0xF0;
                if (pci_type == iso15765::kPciSingleFrame &&
                    f.data[1] == iso15765::kNegativeResponse &&
                    f.data[3] == iso15765::kNrcResponsePending) {
                    deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds(iso15765::kP2StarTimeout);
                    continue;
                }

                return f;
            }
        }
    }

    return Result<CanFrame>::error("timeout waiting for OBD response");
}

} // namespace canmatik
