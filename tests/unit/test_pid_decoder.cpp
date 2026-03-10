/// @file test_pid_decoder.cpp
/// Unit tests for PID value decoding with known J1979 values (T113).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "obd/pid_decoder.h"
#include "obd/pid_table.h"

using namespace canmatik;
using Catch::Matchers::WithinAbs;

TEST_CASE("PidDecoder: RPM (0x1A, 0xF8) = 1726", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x0C);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x1A, 0xF8};
    double value = decode_pid_value(data, 2, def->formula);
    CHECK_THAT(value, WithinAbs(1726.0, 0.5));
}

TEST_CASE("PidDecoder: Coolant temp (0xA0) = 120°C", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x05);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0xA0};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(120.0, 0.5));
}

TEST_CASE("PidDecoder: Speed (0x78) = 120 km/h", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x0D);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x78};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(120.0, 0.5));
}

TEST_CASE("PidDecoder: Throttle (0x80) ≈ 50.2%", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x11);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x80};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(50.2, 0.5));
}

TEST_CASE("PidDecoder: MAF (0x01, 0xF4) = 5.0 g/s", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x10);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x01, 0xF4};
    double value = decode_pid_value(data, 2, def->formula);
    CHECK_THAT(value, WithinAbs(5.0, 0.01));
}

TEST_CASE("PidDecoder: Fuel trim (0x80) = 0%", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x06);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x80};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(0.0, 0.5));
}

TEST_CASE("PidDecoder: Engine load (0xFF) = 100%", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x04);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0xFF};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(100.0, 0.5));
}

TEST_CASE("PidDecoder: decode_pid produces full DecodedPid", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x0D);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x3C}; // 60 km/h
    auto decoded = decode_pid(0x7E8, data, 1, *def, 12345);

    CHECK(decoded.ecu_id == 0x7E8);
    CHECK(decoded.mode == 0x01);
    CHECK(decoded.pid == 0x0D);
    CHECK(decoded.name == "Vehicle Speed");
    CHECK(decoded.unit == "km/h");
    CHECK_THAT(decoded.value, WithinAbs(60.0, 0.5));
    CHECK(decoded.raw_bytes.size() == 1);
    CHECK(decoded.timestamp_us == 12345);
}
