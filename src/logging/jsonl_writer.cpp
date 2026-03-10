/// @file jsonl_writer.cpp
/// JsonlWriter — JSON Lines output (T041 — US4).

#include "logging/jsonl_writer.h"
#include "core/timestamp.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <format>
#include <iomanip>
#include <sstream>

namespace canmatik {

namespace {

std::string utc_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "Z";
    return oss.str();
}

} // anonymous namespace

JsonlWriter::JsonlWriter(std::ostream& os)
    : os_(&os)
{}

JsonlWriter::JsonlWriter(const std::string& path) {
    auto fs = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (!fs->is_open()) {
        throw std::runtime_error("Failed to open JSONL file: " + path);
    }
    os_ = fs.get();
    owned_os_ = std::move(fs);
}

void JsonlWriter::writeHeader(const SessionStatus& status) {
    session_start_us_ = host_timestamp_us();

    nlohmann::json meta;
    meta["_meta"] = true;
    meta["format"] = "canmatik-jsonl";
    meta["version"] = "1.0";
    meta["tool"] = "canmatik 0.1.0";
    meta["adapter"] = status.adapter_name.empty() ? status.provider_name : status.adapter_name;
    meta["bitrate"] = status.bitrate;
    meta["created_utc"] = utc_iso8601();

    *os_ << meta.dump() << '\n';
}

void JsonlWriter::writeFrame(const CanFrame& frame) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us_);
    bool ext = (frame.type == FrameType::Extended || frame.type == FrameType::FD);

    std::string id_str;
    if (ext) {
        id_str = std::format("{:08X}", frame.arbitration_id);
    } else {
        id_str = std::format("{:03X}", frame.arbitration_id);
    }

    std::string payload;
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        if (i > 0) payload += ' ';
        payload += std::format("{:02X}", frame.data[i]);
    }

    nlohmann::json j;
    j["ts"] = rel;
    j["ats"] = frame.adapter_timestamp_us;
    j["id"] = id_str;
    j["ext"] = ext;
    j["dlc"] = frame.dlc;
    j["data"] = payload;

    *os_ << j.dump() << '\n';
    ++frame_count_;
}

void JsonlWriter::writeFooter(const SessionStatus& status) {
    nlohmann::json footer;
    footer["_meta"] = true;
    footer["type"] = "session_summary";
    footer["frames"] = status.frames_received;
    footer["errors"] = status.errors;
    footer["dropped"] = status.dropped;
    footer["duration"] = status.elapsed_seconds();
    *os_ << footer.dump() << '\n';
}

void JsonlWriter::flush() {
    if (os_) os_->flush();
}

} // namespace canmatik
