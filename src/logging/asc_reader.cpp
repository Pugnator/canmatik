/// @file asc_reader.cpp
/// AscReader — parse Vector ASC format (T048 — US5).

#include "logging/asc_reader.h"
#include "core/log_macros.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string_view>

namespace canmatik {

namespace {

/// Trim leading and trailing whitespace.
std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) sv.remove_suffix(1);
    return sv;
}

/// Parse a hex byte from a two-char string.
bool parse_hex_byte(std::string_view sv, uint8_t& out) {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, 16);
    return ec == std::errc{};
}

/// Parse a hex uint32 from a string view.
bool parse_hex_u32(std::string_view sv, uint32_t& out) {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, 16);
    return ec == std::errc{};
}

} // anonymous namespace

bool AscReader::open(const std::string& path) {
    file_.close();
    file_.open(path, std::ios::in);
    if (!file_.is_open()) return false;

    path_ = path;
    meta_ = {};
    frame_count_ = 0;
    header_parsed_ = false;

    // Parse header lines until we hit "Begin Triggerblock" or a frame line
    std::string line;
    while (std::getline(file_, line)) {
        auto sv = trim(line);
        if (sv.empty()) continue;

        // Comment lines
        if (sv.starts_with(';')) {
            // Parse metadata from comments
            if (sv.find("adapter:") != std::string_view::npos) {
                auto pos = sv.find("adapter:");
                meta_.adapter_name = std::string(trim(sv.substr(pos + 8)));
            } else if (sv.find("bitrate:") != std::string_view::npos) {
                auto pos = sv.find("bitrate:");
                auto val = trim(sv.substr(pos + 8));
                uint32_t br = 0;
                std::from_chars(val.data(), val.data() + val.size(), br);
                meta_.bitrate = br;
            }
            continue;
        }

        if (sv.starts_with("date ") || sv.starts_with("base ") ||
            sv.starts_with("internal ") || sv.starts_with("no internal")) {
            continue;
        }

        if (sv.starts_with("Begin Triggerblock") || sv.starts_with("Begin triggerblock")) {
            header_parsed_ = true;
            break;
        }

        // If it looks like a frame line, rewind and process it later
        // (files without triggerblock markers)
        if (sv[0] >= '0' && sv[0] <= '9') {
            // Save position before this line — we'll re-parse it from nextFrame
            // Since we can't easily seek back, parse it now
            auto frame = parseLine(line);
            // Store for the first nextFrame() call — not needed since we just consumed it
            header_parsed_ = true;
            break;
        }
    }

    header_parsed_ = true;
    return true;
}

std::optional<CanFrame> AscReader::nextFrame() {
    if (!file_.is_open()) return std::nullopt;

    std::string line;
    while (std::getline(file_, line)) {
        auto sv = trim(line);
        if (sv.empty()) continue;
        if (sv.starts_with(';')) continue;
        if (sv.starts_with("End ") || sv.starts_with("end ")) break;

        auto frame = parseLine(line);
        if (frame) {
            ++frame_count_;
            return frame;
        }
    }
    return std::nullopt;
}

std::optional<CanFrame> AscReader::parseLine(const std::string& line) {
    // ASC frame format:  <timestamp> <channel> <id>[x] Rx d <dlc> <byte0> <byte1> ...
    std::istringstream iss(line);
    std::string ts_str, ch_str, id_str, dir_str, d_str, dlc_str;

    if (!(iss >> ts_str >> ch_str >> id_str >> dir_str >> d_str >> dlc_str)) {
        return std::nullopt;
    }

    // Validate direction marker and data marker
    if (dir_str != "Rx" && dir_str != "Tx") return std::nullopt;
    if (d_str != "d") return std::nullopt;

    CanFrame frame;

    // Parse timestamp
    try {
        double ts = std::stod(ts_str);
        frame.host_timestamp_us = static_cast<uint64_t>(ts * 1'000'000.0);
    } catch (...) {
        return std::nullopt;
    }

    // Parse channel
    try {
        frame.channel_id = static_cast<uint8_t>(std::stoi(ch_str));
    } catch (...) {
        frame.channel_id = 1;
    }

    // Parse ID — trailing 'x' means extended frame
    bool extended = false;
    std::string_view id_sv(id_str);
    if (!id_sv.empty() && (id_sv.back() == 'x' || id_sv.back() == 'X')) {
        extended = true;
        id_sv.remove_suffix(1);
    }
    if (!parse_hex_u32(id_sv, frame.arbitration_id)) return std::nullopt;
    frame.type = extended ? FrameType::Extended : FrameType::Standard;

    // Parse DLC
    try {
        frame.dlc = static_cast<uint8_t>(std::stoi(dlc_str));
    } catch (...) {
        return std::nullopt;
    }

    // Parse data bytes
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        std::string byte_str;
        if (!(iss >> byte_str)) break;
        parse_hex_byte(byte_str, frame.data[i]);
    }

    return frame;
}

SessionStatus AscReader::metadata() const {
    auto meta = meta_;
    meta.frames_received = frame_count_;
    return meta;
}

void AscReader::reset() {
    if (!path_.empty()) {
        file_.close();
        frame_count_ = 0;
        header_parsed_ = false;
        open(path_);
    }
}

} // namespace canmatik
