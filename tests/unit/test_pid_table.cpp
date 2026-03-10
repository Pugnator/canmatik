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
