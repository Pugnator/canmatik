/// @file test_obd_request.cpp
/// Unit tests for OBD-II request frame encoding (T119).

#include <catch2/catch_test_macros.hpp>

#include "obd/obd_request.h"

using namespace canmatik;

TEST_CASE("ObdRequest: Mode $01 PID $0C encoding", "[obd][request]") {
    auto frame = build_obd_request(0x01, 0x0C);

    CHECK(frame.arbitration_id == 0x7DF);
    CHECK(frame.dlc == 8);
    CHECK(frame.data[0] == 0x02); // Single frame, 2 payload bytes
    CHECK(frame.data[1] == 0x01); // Mode
    CHECK(frame.data[2] == 0x0C); // PID
    // Padding
    CHECK(frame.data[3] == 0x55);
    CHECK(frame.data[4] == 0x55);
    CHECK(frame.data[5] == 0x55);
    CHECK(frame.data[6] == 0x55);
    CHECK(frame.data[7] == 0x55);
}

TEST_CASE("ObdRequest: physical addressing", "[obd][request]") {
    auto frame = build_obd_request(0x01, 0x0C, 0x7E0);
    CHECK(frame.arbitration_id == 0x7E0);
}

TEST_CASE("ObdRequest: Mode $03 (DTC read)", "[obd][request]") {
    auto frame = build_obd_request(0x03, 0x00);
    CHECK(frame.data[0] == 0x02);
    CHECK(frame.data[1] == 0x03);
    CHECK(frame.data[2] == 0x00);
}

TEST_CASE("ObdRequest: flow control frame", "[obd][request]") {
    auto frame = build_flow_control(0x7E0);
    CHECK(frame.arbitration_id == 0x7E0);
    CHECK(frame.dlc == 8);
    CHECK(frame.data[0] == 0x30); // Flow control: continue
    CHECK(frame.data[1] == 0x00); // Block size
    CHECK(frame.data[2] == 0x00); // STmin
}

TEST_CASE("ObdRequest: frame type is Data", "[obd][request]") {
    auto frame = build_obd_request(0x01, 0x00);
    CHECK(frame.type == FrameType::Standard);
}
