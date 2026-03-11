/// @file test_pid_table.cpp
/// Unit tests for PID table lookup (T112).

#include <catch2/catch_test_macros.hpp>

#include "obd/pid_table.h"

using namespace canmatik;

TEST_CASE("PidTable: lookup known PIDs", "[obd][pid_table]") {
    SECTION("PID $00 - Supported PIDs bitmap") {
        auto* def = pid_lookup(0x01, 0x00);
        REQUIRE(def != nullptr);
        CHECK(def->data_bytes == 4);
        CHECK(def->formula.type == PidFormula::Type::BitEncoded);
    }

    SECTION("PID $0C - Engine RPM") {
        auto* def = pid_lookup(0x01, 0x0C);
        REQUIRE(def != nullptr);
        CHECK(std::string(def->name) == "Engine RPM");
        CHECK(std::string(def->unit) == "rpm");
        CHECK(def->data_bytes == 2);
    }

    SECTION("PID $0D - Vehicle Speed") {
        auto* def = pid_lookup(0x01, 0x0D);
        REQUIRE(def != nullptr);
        CHECK(std::string(def->name) == "Vehicle Speed");
        CHECK(def->data_bytes == 1);
    }

    SECTION("PID $05 - Coolant Temperature") {
        auto* def = pid_lookup(0x01, 0x05);
        REQUIRE(def != nullptr);
        CHECK(std::string(def->name) == "Coolant Temperature");
        CHECK(def->data_bytes == 1);
    }

    SECTION("PID $11 - Throttle Position") {
        auto* def = pid_lookup(0x01, 0x11);
        REQUIRE(def != nullptr);
        CHECK(std::string(def->name) == "Throttle Position");
    }
}

TEST_CASE("PidTable: lookup unknown PIDs returns nullptr", "[obd][pid_table]") {
    CHECK(pid_lookup(0x01, 0xFF) == nullptr);
    CHECK(pid_lookup(0x02, 0x0C) == nullptr);
    CHECK(pid_lookup(0x00, 0x00) == nullptr);
}

TEST_CASE("PidTable: all registered PIDs have valid metadata", "[obd][pid_table]") {
    CHECK(pid_table_size() > 0);

    // Verify a few known PIDs have non-empty names
    for (uint8_t pid : {0x00, 0x04, 0x05, 0x0C, 0x0D, 0x0F, 0x10, 0x11}) {
        auto* def = pid_lookup(0x01, pid);
        REQUIRE(def != nullptr);
        CHECK(std::string(def->name).size() > 0);
    }
}

TEST_CASE("PidTable: newly added J1979 PIDs are present", "[obd][pid_table]") {
    // PIDs from the user's vehicle that were previously missing
    SECTION("Bank 2 fuel trims") {
        auto* d08 = pid_lookup(0x01, 0x08);
        REQUIRE(d08 != nullptr);
        CHECK(std::string(d08->name) == "Short Term Fuel Trim (Bank 2)");
        CHECK(d08->data_bytes == 1);

        auto* d09 = pid_lookup(0x01, 0x09);
        REQUIRE(d09 != nullptr);
        CHECK(std::string(d09->name) == "Long Term Fuel Trim (Bank 2)");
    }

    SECTION("Intake manifold pressure and timing advance") {
        auto* d0B = pid_lookup(0x01, 0x0B);
        REQUIRE(d0B != nullptr);
        CHECK(std::string(d0B->name) == "Intake Manifold Pressure");

        auto* d0E = pid_lookup(0x01, 0x0E);
        REQUIRE(d0E != nullptr);
        CHECK(std::string(d0E->name) == "Timing Advance");
    }

    SECTION("O2 sensors") {
        for (uint8_t pid : {0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B}) {
            auto* def = pid_lookup(0x01, pid);
            REQUIRE(def != nullptr);
            CHECK(def->data_bytes == 2);
            CHECK(std::string(def->unit) == "V");
        }
    }

    SECTION("Distance and warm-ups") {
        auto* d21 = pid_lookup(0x01, 0x21);
        REQUIRE(d21 != nullptr);
        CHECK(std::string(d21->name) == "Distance Traveled With MIL On");

        auto* d30 = pid_lookup(0x01, 0x30);
        REQUIRE(d30 != nullptr);
        CHECK(std::string(d30->name) == "Warm-ups Since Codes Cleared");
    }

    SECTION("Throttle and pedal positions") {
        for (uint8_t pid : {0x45, 0x47, 0x49, 0x4A, 0x4C}) {
            auto* def = pid_lookup(0x01, pid);
            REQUIRE(def != nullptr);
            CHECK(def->data_bytes == 1);
            CHECK(std::string(def->unit) == "%");
        }
    }

    SECTION("Absolute load and commanded AFR") {
        auto* d43 = pid_lookup(0x01, 0x43);
        REQUIRE(d43 != nullptr);
        CHECK(d43->data_bytes == 2);

        auto* d44 = pid_lookup(0x01, 0x44);
        REQUIRE(d44 != nullptr);
        CHECK(d44->data_bytes == 2);
    }
}
