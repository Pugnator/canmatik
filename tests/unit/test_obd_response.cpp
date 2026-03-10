/// @file test_obd_response.cpp
/// Unit tests for OBD response parsing and multi-frame assembly (T120, T121).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "obd/obd_response.h"
#include "obd/iso15765.h"

#include <cstring>

using namespace canmatik;

namespace {

CanFrame make_response(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2,
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

TEST_CASE("ObdResponse: single frame parse", "[obd][response]") {
    // RPM response: mode 0x41, PID 0x0C, data A=0x1A B=0xF8
    auto frame = make_response(0x7E8, 0x04, 0x41, 0x0C, 0x1A, 0xF8);
    auto resp = parse_obd_response(frame, 0x01, 0x0C);

    REQUIRE(resp.has_value());
    CHECK(resp->rx_id == 0x7E8);
    CHECK(resp->mode == 0x41);
    CHECK(resp->pid == 0x0C);
    CHECK(resp->data_length == 2);
    CHECK(resp->data[0] == 0x1A);
    CHECK(resp->data[1] == 0xF8);
    CHECK_FALSE(resp->is_negative);
}

TEST_CASE("ObdResponse: mode validation fails", "[obd][response]") {
    // Wrong mode in response (0x42 instead of 0x41)
    auto frame = make_response(0x7E8, 0x04, 0x42, 0x0C, 0x1A, 0xF8);
    auto resp = parse_obd_response(frame, 0x01, 0x0C);
    REQUIRE_FALSE(resp.has_value());
}

TEST_CASE("ObdResponse: PID validation fails", "[obd][response]") {
    // Wrong PID echo (0x0D instead of 0x0C)
    auto frame = make_response(0x7E8, 0x04, 0x41, 0x0D, 0x1A, 0xF8);
    auto resp = parse_obd_response(frame, 0x01, 0x0C);
    REQUIRE_FALSE(resp.has_value());
}

TEST_CASE("ObdResponse: non-response ID rejected", "[obd][response]") {
    auto frame = make_response(0x100, 0x04, 0x41, 0x0C, 0x1A, 0xF8);
    auto resp = parse_obd_response(frame, 0x01, 0x0C);
    REQUIRE_FALSE(resp.has_value());
}

TEST_CASE("ObdResponse: negative response (0x7F)", "[obd][response]") {
    // 0x7F, rejected service 0x01, NRC 0x12 (subfunction not supported)
    auto frame = make_response(0x7E8, 0x03, 0x7F, 0x01, 0x12);
    auto resp = parse_obd_response(frame, 0x01, 0x0C);

    REQUIRE(resp.has_value());
    CHECK(resp->is_negative);
    CHECK(resp->mode == 0x01);
    CHECK(resp->negative_code == 0x12);
}

TEST_CASE("ObdResponse: parse without validation", "[obd][response]") {
    auto frame = make_response(0x7E8, 0x04, 0x41, 0x0C, 0x1A, 0xF8);
    auto resp = parse_obd_response(frame);

    REQUIRE(resp.has_value());
    CHECK(resp->mode == 0x41);
    CHECK(resp->pid == 0x0C);
}

TEST_CASE("ObdResponse: coolant temp (1 data byte)", "[obd][response]") {
    // Mode $41, PID $05, A=0xA0 → 3 payload bytes
    auto frame = make_response(0x7E8, 0x03, 0x41, 0x05, 0xA0);
    auto resp = parse_obd_response(frame, 0x01, 0x05);

    REQUIRE(resp.has_value());
    CHECK(resp->data_length == 1);
    CHECK(resp->data[0] == 0xA0);
}

// ---------------------------------------------------------------------------
// Multi-frame assembly tests (ISO-TP for VIN)
// ---------------------------------------------------------------------------

TEST_CASE("ObdResponse: multi-frame VIN assembly", "[obd][response][multiframe]") {
    // VIN = "WVWZZZ3CZWE123456" (17 chars)
    // Mode $09 PID $02 response:
    // Total payload = 1 (number of data items) + 17 (VIN) = 20 bytes
    // But following ISO-TP convention, after mode+pid, total = 20 bytes

    // First Frame: PCI=0x10|0x14 (total=20), data[2..7] = first 6 bytes
    // Actually, First Frame data[0-1] = PCI+length, data[2-7] = first 6 data bytes
    // For Mode $09 response, data starts with mode_resp(0x49), pid(0x02), count(0x01), then VIN
    // Total ISO-TP payload: 3 + 17 = 20

    std::vector<CanFrame> frames;

    // First Frame: 10 14 49 02 01 57 56 57  (length=20)
    CanFrame ff{};
    ff.arbitration_id = 0x7E8;
    ff.dlc = 8;
    ff.data[0] = 0x10; ff.data[1] = 0x14; // FF, length = 20
    ff.data[2] = 0x49; ff.data[3] = 0x02; ff.data[4] = 0x01; // resp mode, pid, count
    ff.data[5] = 'W'; ff.data[6] = 'V'; ff.data[7] = 'W';
    frames.push_back(ff);

    // CF1: 21 5A 5A 5A 33 43 5A 57  (seq=1)
    CanFrame cf1{};
    cf1.arbitration_id = 0x7E8;
    cf1.dlc = 8;
    cf1.data[0] = 0x21; // CF seq=1
    cf1.data[1] = 'Z'; cf1.data[2] = 'Z'; cf1.data[3] = 'Z';
    cf1.data[4] = '3'; cf1.data[5] = 'C'; cf1.data[6] = 'Z'; cf1.data[7] = 'W';
    frames.push_back(cf1);

    // CF2: 22 45 31 32 33 34 35 36  (seq=2)
    CanFrame cf2{};
    cf2.arbitration_id = 0x7E8;
    cf2.dlc = 8;
    cf2.data[0] = 0x22; // CF seq=2
    cf2.data[1] = 'E'; cf2.data[2] = '1'; cf2.data[3] = '2';
    cf2.data[4] = '3'; cf2.data[5] = '4'; cf2.data[6] = '5'; cf2.data[7] = '6';
    frames.push_back(cf2);

    auto result = assemble_multiframe(frames);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 20);

    // Verify payload: 49 02 01 W V W Z Z Z 3 C Z W E 1 2 3 4 5 6
    CHECK((*result)[0] == 0x49); // mode resp
    CHECK((*result)[1] == 0x02); // pid
    CHECK((*result)[2] == 0x01); // data item count

    std::string vin(result->begin() + 3, result->begin() + 20);
    CHECK(vin == "WVWZZZ3CZWE123456");
}

TEST_CASE("ObdResponse: multi-frame empty input", "[obd][response][multiframe]") {
    auto result = assemble_multiframe({});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ObdResponse: multi-frame wrong first PCI", "[obd][response][multiframe]") {
    CanFrame f{};
    f.data[0] = 0x04; // Single frame, not first frame
    auto result = assemble_multiframe({f});
    REQUIRE_FALSE(result.has_value());
}
