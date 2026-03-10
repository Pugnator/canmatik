/// @file asc_writer.cpp
/// AscWriter — Vector ASC format output (T040 — US4).

#include "logging/asc_writer.h"
#include "core/timestamp.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <format>
#include <iomanip>
#include <sstream>

namespace canmatik {

namespace {

std::string utc_date_string() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%a %b %d %H:%M:%S.000 %Y");
    return oss.str();
}

} // anonymous namespace

AscWriter::AscWriter(std::ostream& os)
    : os_(&os)
{}

AscWriter::AscWriter(const std::string& path) {
    auto fs = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
    if (!fs->is_open()) {
        throw std::runtime_error("Failed to open ASC file: " + path);
    }
    os_ = fs.get();
    owned_os_ = std::move(fs);
}

void AscWriter::writeHeader(const SessionStatus& status) {
    session_start_us_ = host_timestamp_us();
    auto date = utc_date_string();

    *os_ << "; CANmatik ASC v1.0\n";
    *os_ << "date " << date << '\n';
    *os_ << "base hex  timestamps absolute\n";
    *os_ << "; adapter: " << (status.adapter_name.empty() ? status.provider_name : status.adapter_name) << '\n';
    *os_ << "; bitrate: " << status.bitrate << '\n';
    *os_ << "internal events logged\n";
    *os_ << "Begin Triggerblock " << date << '\n';
}

void AscWriter::writeFrame(const CanFrame& frame) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us_);

    // ASC format: <timestamp> <channel> <id> Rx d <dlc> <data bytes...>
    std::string id_str;
    if (frame.type == FrameType::Extended || frame.type == FrameType::FD) {
        id_str = std::format("{:08X}x", frame.arbitration_id);
    } else {
        id_str = std::format("{:03X}", frame.arbitration_id);
    }

    std::string data_str;
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        if (i > 0) data_str += ' ';
        data_str += std::format("{:02X}", frame.data[i]);
    }

    *os_ << std::format("   {:.6f} {}  {:15s} Rx   d {} {}\n",
                        rel,
                        frame.channel_id > 0 ? frame.channel_id : 1,
                        id_str,
                        frame.dlc,
                        data_str);
    ++frame_count_;
}

void AscWriter::writeFooter(const SessionStatus& /*status*/) {
    *os_ << "End TriggerBlock\n";
}

void AscWriter::flush() {
    if (os_) os_->flush();
}

} // namespace canmatik
