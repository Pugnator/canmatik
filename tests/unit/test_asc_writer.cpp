/// @file test_asc_writer.cpp
/// Unit tests for AscWriter (T042 — US4).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "logging/asc_writer.h"
#include "core/can_frame.h"
#include "core/session_status.h"
#include "core/timestamp.h"

#include <sstream>
#include <string>

using namespace canmatik;
using Catch::Matchers::ContainsSubstring;

namespace {

CanFrame make_frame(uint32_t id, uint8_t dlc, std::initializer_list<uint8_t> data,
                    uint64_t host_ts = 0) {
    CanFrame f;
    f.arbitration_id = id;
    f.dlc = dlc;
    f.type = FrameType::Standard;
    f.channel_id = 1;
    f.host_timestamp_us = host_ts;
    uint8_t i = 0;
    for (auto b : data) {
        f.data[i++] = b;
    }
    return f;
}

} // anonymous namespace

TEST_CASE("AscWriter: header contains version marker", "[asc_writer]") {
    std::ostringstream ss;
    AscWriter writer(ss);

    SessionStatus status;
    status.adapter_name = "Test Adapter";
    status.bitrate = 500000;
    writer.writeHeader(status);

    std::string output = ss.str();
    CHECK_THAT(output, ContainsSubstring("; CANmatik ASC v1.0"));
    CHECK_THAT(output, ContainsSubstring("date "));
    CHECK_THAT(output, ContainsSubstring("base hex  timestamps absolute"));
    CHECK_THAT(output, ContainsSubstring("; adapter: Test Adapter"));
    CHECK_THAT(output, ContainsSubstring("; bitrate: 500000"));
    CHECK_THAT(output, ContainsSubstring("Begin Triggerblock"));
}

TEST_CASE("AscWriter: frame line format", "[asc_writer]") {
    std::ostringstream ss;
    AscWriter writer(ss);

    SessionStatus status;
    status.bitrate = 500000;
    writer.writeHeader(status);

    // Clear the header output
    ss.str("");

    auto frame = make_frame(0x7E8, 8, {0x02, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00});
    writer.writeFrame(frame);

    std::string line = ss.str();
    CHECK_THAT(line, ContainsSubstring("7E8"));
    CHECK_THAT(line, ContainsSubstring("Rx"));
    CHECK_THAT(line, ContainsSubstring("d 8"));
    CHECK_THAT(line, ContainsSubstring("02 41 0C 1A F8 00 00 00"));
}

TEST_CASE("AscWriter: extended ID has 'x' suffix", "[asc_writer]") {
    std::ostringstream ss;
    AscWriter writer(ss);

    SessionStatus status;
    writer.writeHeader(status);
    ss.str("");

    CanFrame frame;
    frame.arbitration_id = 0x18DAF110;
    frame.type = FrameType::Extended;
    frame.dlc = 2;
    frame.data[0] = 0xAA;
    frame.data[1] = 0xBB;
    frame.channel_id = 1;
    writer.writeFrame(frame);

    std::string line = ss.str();
    CHECK_THAT(line, ContainsSubstring("18DAF110x"));
}

TEST_CASE("AscWriter: footer contains End TriggerBlock", "[asc_writer]") {
    std::ostringstream ss;
    AscWriter writer(ss);

    SessionStatus status;
    writer.writeFooter(status);

    CHECK_THAT(ss.str(), ContainsSubstring("End TriggerBlock"));
}

TEST_CASE("AscWriter: frame count tracks written frames", "[asc_writer]") {
    std::ostringstream ss;
    AscWriter writer(ss);

    SessionStatus status;
    writer.writeHeader(status);

    CHECK(writer.frameCount() == 0);

    writer.writeFrame(make_frame(0x100, 2, {0x01, 0x02}));
    writer.writeFrame(make_frame(0x200, 1, {0xFF}));

    CHECK(writer.frameCount() == 2);
}
