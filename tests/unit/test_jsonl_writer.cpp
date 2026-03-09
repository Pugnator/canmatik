/// @file test_jsonl_writer.cpp
/// Unit tests for JsonlWriter (T043 — US4).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "logging/jsonl_writer.h"
#include "core/can_frame.h"
#include "core/session_status.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using namespace canmatik;
using Catch::Matchers::ContainsSubstring;

namespace {

CanFrame make_frame(uint32_t id, uint8_t dlc, std::initializer_list<uint8_t> data) {
    CanFrame f;
    f.arbitration_id = id;
    f.dlc = dlc;
    f.type = FrameType::Standard;
    f.channel_id = 1;
    f.host_timestamp_us = 0;
    f.adapter_timestamp_us = 0;
    uint8_t i = 0;
    for (auto b : data) {
        f.data[i++] = b;
    }
    return f;
}

std::string first_line(const std::string& s) {
    auto pos = s.find('\n');
    return pos != std::string::npos ? s.substr(0, pos) : s;
}

std::string nth_line(const std::string& s, size_t n) {
    size_t start = 0;
    for (size_t i = 0; i < n; ++i) {
        auto pos = s.find('\n', start);
        if (pos == std::string::npos) return {};
        start = pos + 1;
    }
    auto end = s.find('\n', start);
    return end != std::string::npos ? s.substr(start, end - start) : s.substr(start);
}

} // anonymous namespace

TEST_CASE("JsonlWriter: header is valid JSON with _meta", "[jsonl_writer]") {
    std::ostringstream ss;
    JsonlWriter writer(ss);

    SessionStatus status;
    status.adapter_name = "Test Adapter";
    status.bitrate = 500000;
    writer.writeHeader(status);

    auto meta = nlohmann::json::parse(first_line(ss.str()));
    CHECK(meta["_meta"] == true);
    CHECK(meta["format"] == "canmatik-jsonl");
    CHECK(meta["version"] == "1.0");
    CHECK(meta["adapter"] == "Test Adapter");
    CHECK(meta["bitrate"] == 500000);
    CHECK(meta.contains("created_utc"));
}

TEST_CASE("JsonlWriter: frame line has required fields", "[jsonl_writer]") {
    std::ostringstream ss;
    JsonlWriter writer(ss);

    SessionStatus status;
    writer.writeHeader(status);

    auto frame = make_frame(0x7E8, 3, {0x02, 0x41, 0x0C});
    writer.writeFrame(frame);

    auto j = nlohmann::json::parse(nth_line(ss.str(), 1));
    CHECK(j.contains("ts"));
    CHECK(j.contains("ats"));
    CHECK(j["id"] == "7E8");
    CHECK(j["ext"] == false);
    CHECK(j["dlc"] == 3);
    CHECK(j["data"] == "02 41 0C");
}

TEST_CASE("JsonlWriter: extended ID is 8 chars", "[jsonl_writer]") {
    std::ostringstream ss;
    JsonlWriter writer(ss);

    SessionStatus status;
    writer.writeHeader(status);

    CanFrame frame;
    frame.arbitration_id = 0x18DAF110;
    frame.type = FrameType::Extended;
    frame.dlc = 1;
    frame.data[0] = 0xFF;
    writer.writeFrame(frame);

    auto j = nlohmann::json::parse(nth_line(ss.str(), 1));
    CHECK(j["id"] == "18DAF110");
    CHECK(j["ext"] == true);
}

TEST_CASE("JsonlWriter: footer has session_summary type", "[jsonl_writer]") {
    std::ostringstream ss;
    JsonlWriter writer(ss);

    SessionStatus status;
    status.frames_received = 42;
    status.errors = 1;
    status.dropped = 0;
    status.session_start = std::chrono::steady_clock::now();
    writer.writeFooter(status);

    auto j = nlohmann::json::parse(first_line(ss.str()));
    CHECK(j["_meta"] == true);
    CHECK(j["type"] == "session_summary");
    CHECK(j["frames"] == 42);
    CHECK(j["errors"] == 1);
    CHECK(j["dropped"] == 0);
}

TEST_CASE("JsonlWriter: frame count tracks writes", "[jsonl_writer]") {
    std::ostringstream ss;
    JsonlWriter writer(ss);

    SessionStatus status;
    writer.writeHeader(status);

    CHECK(writer.frameCount() == 0);

    writer.writeFrame(make_frame(0x100, 2, {0x01, 0x02}));
    writer.writeFrame(make_frame(0x200, 1, {0xFF}));
    writer.writeFrame(make_frame(0x300, 4, {0xDE, 0xAD, 0xBE, 0xEF}));

    CHECK(writer.frameCount() == 3);
}
