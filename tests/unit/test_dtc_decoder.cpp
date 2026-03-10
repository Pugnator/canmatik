/// @file test_dtc_decoder.cpp
/// Unit tests for DTC decoding (T114).

#include <catch2/catch_test_macros.hpp>

#include "obd/dtc_decoder.h"

using namespace canmatik;

TEST_CASE("DtcDecoder: P0300", "[obd][dtc]") {
    auto dtc = decode_dtc(0x03, 0x00, 0x7E8, false);
    CHECK(dtc.code == "P0300");
    CHECK(dtc.category == DtcCategory::Powertrain);
    CHECK(dtc.ecu_id == 0x7E8);
    CHECK_FALSE(dtc.pending);
}

TEST_CASE("DtcDecoder: C0035", "[obd][dtc]") {
    // C = category 01, second digit = 0, third = 0, fourth = 3, fifth = 5
    // C0035: byte0 = 0b01_00_0000 | 0x35 → 0x40, byte1 = 0x35
    auto dtc = decode_dtc(0x40, 0x35, 0x7E8, false);
    CHECK(dtc.code == "C0035");
    CHECK(dtc.category == DtcCategory::Chassis);
}

TEST_CASE("DtcDecoder: B0001", "[obd][dtc]") {
    // B = category 10, byte0 = 0b10_00_0000 = 0x80, byte1 = 0x01
    auto dtc = decode_dtc(0x80, 0x01, 0x7E8, false);
    CHECK(dtc.code == "B0001");
    CHECK(dtc.category == DtcCategory::Body);
}

TEST_CASE("DtcDecoder: U0100", "[obd][dtc]") {
    // U = category 11, byte0 = 0b11_00_0001 = 0xC1, byte1 = 0x00
    auto dtc = decode_dtc(0xC1, 0x00, 0x7E8, false);
    CHECK(dtc.code == "U0100");
    CHECK(dtc.category == DtcCategory::Network);
}

TEST_CASE("DtcDecoder: pending flag", "[obd][dtc]") {
    auto dtc = decode_dtc(0x03, 0x00, 0x7E8, true);
    CHECK(dtc.pending);
}

TEST_CASE("DtcDecoder: decode_dtcs multiple DTCs", "[obd][dtc]") {
    uint8_t data[] = {0x03, 0x00, 0x40, 0x35}; // P0300, C0035
    auto result = decode_dtcs(data, 4, 0x7E8, false);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK((*result)[0].code == "P0300");
    CHECK((*result)[1].code == "C0035");
}

TEST_CASE("DtcDecoder: zero DTCs returns empty", "[obd][dtc]") {
    uint8_t data[] = {0x00, 0x00};
    auto result = decode_dtcs(data, 2, 0x7E8, false);
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("DtcDecoder: odd byte count returns error", "[obd][dtc]") {
    uint8_t data[] = {0x03, 0x00, 0x40};
    auto result = decode_dtcs(data, 3, 0x7E8, false);
    REQUIRE_FALSE(result.has_value());
}
