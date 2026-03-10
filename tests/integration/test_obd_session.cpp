/// @file test_obd_session.cpp
/// Integration tests for ObdSession using MockChannel (T126).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "obd/obd_session.h"
#include "mock/mock_channel.h"

#include <cstring>

using namespace canmatik;
using Catch::Matchers::WithinAbs;

namespace {

CanFrame make_frame(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2,
                    uint8_t d3 = 0x55, uint8_t d4 = 0x55,
                    uint8_t d5 = 0x55, uint8_t d6 = 0x55, uint8_t d7 = 0x55) {
    CanFrame f{};
    f.arbitration_id = id;
    f.type = FrameType::Standard;
    f.dlc = 8;
    f.data[0] = d0; f.data[1] = d1; f.data[2] = d2; f.data[3] = d3;
    f.data[4] = d4; f.data[5] = d5; f.data[6] = d6; f.data[7] = d7;
    f.host_timestamp_us = 1000;
    return f;
}

} // namespace

TEST_CASE("ObdSession: query RPM returns decoded value", "[obd][session]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    // Inject canned RPM response: 04 41 0C 1A F8 55 55 55
    channel.set_frame_sequence({
        make_frame(0x7E8, 0x04, 0x41, 0x0C, 0x1A, 0xF8)
    });

    ObdSession session(channel);
    auto result = session.query_pid(0x01, 0x0C);

    REQUIRE(result.has_value());
    CHECK(result->pid == 0x0C);
    CHECK(result->name == "Engine RPM");
    CHECK_THAT(result->value, WithinAbs(1726.0, 0.5));
}

TEST_CASE("ObdSession: query coolant temperature", "[obd][session]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    // Coolant: 03 41 05 A0 55 55 55 55 → 120°C
    channel.set_frame_sequence({
        make_frame(0x7E8, 0x03, 0x41, 0x05, 0xA0)
    });

    ObdSession session(channel);
    auto result = session.query_pid(0x01, 0x05);

    REQUIRE(result.has_value());
    CHECK_THAT(result->value, WithinAbs(120.0, 0.5));
}

TEST_CASE("ObdSession: query supported PIDs", "[obd][session]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    // Supported PIDs $00 response: all bits set for PIDs $01-$20
    // A=0xFF, B=0xFF, C=0xFF, D=0xFF (bits 1 and 32 set → supports $01-$20)
    channel.set_frame_sequence({
        make_frame(0x7E8, 0x06, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11)
    });

    ObdSession session(channel);
    auto result = session.query_supported_pids();

    REQUIRE(result.has_value());
    CHECK_FALSE(result->empty());
}

TEST_CASE("ObdSession: read DTCs", "[obd][session]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    // Mode $03 response: 2 DTCs → P0300 (0x0300) and C0035 (0x4035)
    // PCI: 0x06 (6 payload bytes), mode 0x43, count byte=2, then DTC pairs
    channel.set_frame_sequence({
        make_frame(0x7E8, 0x06, 0x43, 0x00, 0x03, 0x00, 0x40, 0x35)
    });

    ObdSession session(channel);
    auto result = session.read_dtcs();

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK((*result)[0].code == "P0300");
    CHECK((*result)[1].code == "C0035");
}

TEST_CASE("ObdSession: clear DTCs requires force", "[obd][session]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    ObdSession session(channel);
    auto result = session.clear_dtcs(false);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("--force") != std::string::npos);
}

TEST_CASE("ObdSession: negative response", "[obd][session]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    // Negative response: 7F 01 12 (service not supported)
    channel.set_frame_sequence({
        make_frame(0x7E8, 0x03, 0x7F, 0x01, 0x12)
    });

    ObdSession session(channel);
    auto result = session.query_pid(0x01, 0x0C);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("NRC") != std::string::npos);
}
