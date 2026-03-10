/// @file jsonl_reader.cpp
/// JsonlReader — parse JSON Lines format (T049 — US5).

#include "logging/jsonl_reader.h"
#include "core/log_macros.h"

#include <nlohmann/json.hpp>
#include <charconv>
#include <string_view>

namespace canmatik {

namespace {

/// Parse space-separated hex byte string into data array, return count.
uint8_t parse_hex_data(const std::string& hex_str, std::array<uint8_t, 64>& data) {
    uint8_t count = 0;
    size_t pos = 0;
    while (pos < hex_str.size() && count < 64) {
        // Skip spaces
        while (pos < hex_str.size() && hex_str[pos] == ' ') ++pos;
        if (pos >= hex_str.size()) break;

        // Read two hex chars
        size_t end = pos;
        while (end < hex_str.size() && hex_str[end] != ' ') ++end;

        uint8_t byte = 0;
        std::from_chars(hex_str.data() + pos, hex_str.data() + end, byte, 16);
        data[count++] = byte;
        pos = end;
    }
    return count;
}

} // anonymous namespace

bool JsonlReader::open(const std::string& path) {
    file_.close();
    file_.open(path, std::ios::in);
    if (!file_.is_open()) return false;

    path_ = path;
    meta_ = {};
    frame_count_ = 0;

    // Try to read the first line as metadata header
    std::string line;
    if (std::getline(file_, line)) {
        try {
            auto j = nlohmann::json::parse(line);
            if (j.contains("_meta") && j["_meta"].get<bool>() &&
                !j.contains("type")) {
                // This is a header metadata line
                if (j.contains("adapter")) meta_.adapter_name = j["adapter"].get<std::string>();
                if (j.contains("bitrate")) meta_.bitrate = j["bitrate"].get<uint32_t>();
            } else {
                // Not a metadata line — rewind to beginning
                file_.clear();
                file_.seekg(0);
            }
        } catch (...) {
            // Not valid JSON — rewind
            file_.clear();
            file_.seekg(0);
        }
    }

    return true;
}

std::optional<CanFrame> JsonlReader::nextFrame() {
    if (!file_.is_open()) return std::nullopt;

    std::string line;
    while (std::getline(file_, line)) {
        if (line.empty()) continue;

        try {
            auto j = nlohmann::json::parse(line);

            // Skip metadata / summary lines
            if (j.contains("_meta") && j["_meta"].get<bool>()) {
                continue;
            }

            // Parse frame fields
            if (!j.contains("id") || !j.contains("dlc")) continue;

            CanFrame frame;

            // Parse ID from hex string
            std::string id_str = j["id"].get<std::string>();
            std::from_chars(id_str.data(), id_str.data() + id_str.size(),
                           frame.arbitration_id, 16);

            // Extended flag
            bool ext = j.value("ext", false);
            frame.type = ext ? FrameType::Extended : FrameType::Standard;

            // DLC
            frame.dlc = static_cast<uint8_t>(j["dlc"].get<int>());

            // Timestamps
            if (j.contains("ts")) {
                double ts = j["ts"].get<double>();
                frame.host_timestamp_us = static_cast<uint64_t>(ts * 1'000'000.0);
            }
            if (j.contains("ats")) {
                frame.adapter_timestamp_us = j["ats"].get<uint64_t>();
            }

            // Data
            if (j.contains("data")) {
                std::string data_str = j["data"].get<std::string>();
                parse_hex_data(data_str, frame.data);
            }

            frame.channel_id = 1;
            ++frame_count_;
            return frame;
        } catch (...) {
            // Skip malformed lines
            continue;
        }
    }

    return std::nullopt;
}

SessionStatus JsonlReader::metadata() const {
    auto meta = meta_;
    meta.frames_received = frame_count_;
    return meta;
}

void JsonlReader::reset() {
    if (!path_.empty()) {
        file_.close();
        frame_count_ = 0;
        open(path_);
    }
}

} // namespace canmatik
