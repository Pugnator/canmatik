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

TEST_CASE("PidDecoder: Intake manifold pressure (0x64) = 100 kPa", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x0B);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x64};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(100.0, 0.5));
}

TEST_CASE("PidDecoder: Timing advance (0xC0) = 32°", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x0E);
    REQUIRE(def != nullptr);

    // A=0xC0=192, formula: (192 - 128) / 2 = 32
    uint8_t data[] = {0xC0};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(32.0, 0.5));
}

TEST_CASE("PidDecoder: O2 voltage (0x96, 0x80) = 0.75V", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x14);
    REQUIRE(def != nullptr);

    // A=0x96=150, voltage = 150/200 = 0.75
    uint8_t data[] = {0x96, 0x80};
    double value = decode_pid_value(data, 2, def->formula);
    CHECK_THAT(value, WithinAbs(0.75, 0.01));
}

TEST_CASE("PidDecoder: Distance with MIL (0x00, 0xC8) = 200 km", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x21);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x00, 0xC8};
    double value = decode_pid_value(data, 2, def->formula);
    CHECK_THAT(value, WithinAbs(200.0, 0.5));
}

TEST_CASE("PidDecoder: Commanded AFR (0x80, 0x00) ≈ 1.0", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x44);
    REQUIRE(def != nullptr);

    // 256*0x80 + 0x00 = 32768, / 32768 = 1.0
    uint8_t data[] = {0x80, 0x00};
    double value = decode_pid_value(data, 2, def->formula);
    CHECK_THAT(value, WithinAbs(1.0, 0.001));
}

TEST_CASE("PidDecoder: Absolute throttle B (0x80) ≈ 50.2%", "[obd][decoder]") {
    auto* def = pid_lookup(0x01, 0x47);
    REQUIRE(def != nullptr);

    uint8_t data[] = {0x80};
    double value = decode_pid_value(data, 1, def->formula);
    CHECK_THAT(value, WithinAbs(50.2, 0.5));
}
