#pragma once

/// @file formatters.h
/// Text and JSON output formatters for CLI frame display, session headers,
/// and session footers per contracts/cli-commands.md output format contract.

#include "core/can_frame.h"
#include "core/session_status.h"
#include "core/timestamp.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <string>
#include <iostream>

namespace canmatik {

// ---------------------------------------------------------------------------
// Frame formatters
// ---------------------------------------------------------------------------

/// Format a single CAN frame as a human-readable text line.
/// Contract: "   +<seconds.microseconds>  <ID>  <Std|Ext>  [<DLC>]  <hex payload bytes>"
/// @param frame         The CAN frame to format.
/// @param session_start Host timestamp (µs) of session start for relative time.
/// @param label         Optional human-readable label for the arb ID (empty = none).
/// @return Formatted line (no trailing newline).
inline std::string format_frame_text(const CanFrame& frame, uint64_t session_start_us,
                                     const std::string& label = {}) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us);

    // Arbitration ID: 3 chars for standard (11-bit), 8 chars for extended (29-bit)
    std::string id_str;
    if (frame.type == FrameType::Extended || frame.type == FrameType::FD) {
        id_str = std::format("{:08X}", frame.arbitration_id);
    } else {
        id_str = std::format("{:03X}", frame.arbitration_id);
    }

    // Label suffix
    std::string label_part;
    if (!label.empty()) {
        label_part = std::format(" [{}]", label);
    }

    // Payload hex bytes
    std::string payload;
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        if (i > 0) payload += ' ';
        payload += std::format("{:02X}", frame.data[i]);
    }

    return std::format("   +{:.6f}  {}{}  {}  [{}]  {}",
                       rel, id_str, label_part,
                       frame_type_to_string(frame.type),
                       frame.dlc, payload);
}

/// Format a single CAN frame as a JSON line (JSONL).
/// Contract: {"ts":<float>,"ats":<int>,"id":"<hex>","ext":<bool>,"dlc":<int>,"data":"<hex>"}
/// @param frame         The CAN frame to format.
/// @param session_start Host timestamp (µs) of session start for relative time.
/// @param label         Optional label (adds "label" field when non-empty).
/// @return JSON string (no trailing newline).
inline std::string format_frame_json(const CanFrame& frame, uint64_t session_start_us,
                                     const std::string& label = {}) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us);
    bool ext = (frame.type == FrameType::Extended || frame.type == FrameType::FD);

    std::string id_str;
    if (ext) {
        id_str = std::format("{:08X}", frame.arbitration_id);
    } else {
        id_str = std::format("{:03X}", frame.arbitration_id);
    }

    // Payload as space-separated hex pairs
    std::string payload;
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        if (i > 0) payload += ' ';
        payload += std::format("{:02X}", frame.data[i]);
    }

    nlohmann::json j;
    j["ts"]  = rel;
    j["ats"] = frame.adapter_timestamp_us;
    j["id"]  = id_str;
    if (!label.empty()) {
        j["label"] = label;
    }
    j["ext"] = ext;
    j["dlc"] = frame.dlc;
    j["data"] = payload;

    return j.dump();
}

// ---------------------------------------------------------------------------
// Session header/footer
// ---------------------------------------------------------------------------

/// Print session header.
/// @param status   Current session status.
/// @param os       Output stream (default: stdout).
inline void print_session_header(const SessionStatus& status, bool json_mode,
                                 std::ostream& os = std::cout) {
    if (json_mode) {
        // JSON mode: no header, frames are self-describing
        return;
    }

    // Mode badge
    std::string mode_badge = std::format("[{}]", operating_mode_to_string(status.mode));

    // Mock indicator
    std::string mock_badge;
    if (status.provider_name == "MockProvider" ||
        status.provider_name.find("mock") != std::string::npos) {
        mock_badge = " [MOCK]";
    }

    // Recording indicator
    std::string rec_badge;
    if (status.recording) {
        rec_badge = std::format(" [REC {}]", status.recording_file);
    }

    os << std::format("{}{}{} Connected to {} @ {} kbps\n",
                      mode_badge, mock_badge, rec_badge,
                      status.adapter_name.empty() ? status.provider_name : status.adapter_name,
                      status.bitrate / 1000);
}

/// Print session summary footer.
/// @param status   Final session status.
/// @param os       Output stream (default: stdout).
inline void print_session_footer(const SessionStatus& status, bool json_mode,
                                 std::ostream& os = std::cout) {
    if (json_mode) {
        nlohmann::json j;
        j["type"]     = "session_summary";
        j["frames"]   = status.frames_received;
        j["errors"]   = status.errors;
        j["dropped"]  = status.dropped;
        j["duration"] = status.elapsed_seconds();
        os << j.dump() << '\n';
        return;
    }

    auto duration = status.elapsed_seconds();
    std::string duration_str;
    if (duration < 1.0) {
        duration_str = std::format("{:.1f} ms", duration * 1000.0);
    } else {
        duration_str = std::format("{:.1f} s", duration);
    }

    os << std::format("Session ended: {} frames | {} errors | {} dropped | {}\n",
                      status.frames_received,
                      status.errors,
                      status.dropped,
                      duration_str);
}

/// Print recording saved message.
inline void print_recording_saved(const std::string& path, uint64_t frame_count,
                                  bool json_mode, std::ostream& os = std::cout) {
    if (json_mode) {
        nlohmann::json j;
        j["type"]   = "recording_saved";
        j["path"]   = path;
        j["frames"] = frame_count;
        os << j.dump() << '\n';
        return;
    }
    os << std::format("Recording saved: {} ({} frames)\n", path, frame_count);
}

} // namespace canmatik
