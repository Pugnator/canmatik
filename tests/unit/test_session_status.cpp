/// @file test_session_status.cpp
/// Unit tests for SessionStatus counters, state tracking, and OperatingMode (T023).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/session_status.h"

#include <thread>
#include <chrono>

using namespace canmatik;

// ---------------------------------------------------------------------------
// OperatingMode
// ---------------------------------------------------------------------------
TEST_CASE("OperatingMode enum values", "[session_status]") {
    CHECK(static_cast<uint8_t>(OperatingMode::Passive)      == 0);
    CHECK(static_cast<uint8_t>(OperatingMode::ActiveQuery)  == 1);
    CHECK(static_cast<uint8_t>(OperatingMode::ActiveInject) == 2);
}

TEST_CASE("operating_mode_to_string", "[session_status]") {
    CHECK(std::string(operating_mode_to_string(OperatingMode::Passive))      == "PASSIVE");
    CHECK(std::string(operating_mode_to_string(OperatingMode::ActiveQuery))  == "ACTIVE-QUERY");
    CHECK(std::string(operating_mode_to_string(OperatingMode::ActiveInject)) == "ACTIVE-INJECT");
}

// ---------------------------------------------------------------------------
// SessionStatus default construction
// ---------------------------------------------------------------------------
TEST_CASE("SessionStatus defaults", "[session_status]") {
    SessionStatus s;
    CHECK(s.mode == OperatingMode::Passive);
    CHECK(s.provider_name.empty());
    CHECK(s.adapter_name.empty());
    CHECK(s.bitrate == 0);
    CHECK(s.channel_open == false);
    CHECK(s.recording == false);
    CHECK(s.recording_file.empty());
    CHECK(s.frames_received == 0);
    CHECK(s.frames_transmitted == 0);
    CHECK(s.errors == 0);
    CHECK(s.dropped == 0);
    CHECK(s.active_filters.empty());
}

// ---------------------------------------------------------------------------
// Counter tracking
// ---------------------------------------------------------------------------
TEST_CASE("SessionStatus counter increments", "[session_status]") {
    SessionStatus s;
    s.frames_received = 100;
    s.frames_transmitted = 5;
    s.errors = 2;
    s.dropped = 1;

    CHECK(s.frames_received == 100);
    CHECK(s.frames_transmitted == 5);
    CHECK(s.errors == 2);
    CHECK(s.dropped == 1);

    // Simulate more frames
    s.frames_received += 50;
    s.errors += 3;
    CHECK(s.frames_received == 150);
    CHECK(s.errors == 5);
}

// ---------------------------------------------------------------------------
// State tracking
// ---------------------------------------------------------------------------
TEST_CASE("SessionStatus state fields", "[session_status]") {
    SessionStatus s;
    s.provider_name = "Tactrix OpenPort 2.0";
    s.adapter_name  = "Tactrix OpenPort 2.0";
    s.bitrate       = 500000;
    s.channel_open  = true;
    s.recording     = true;
    s.recording_file = "captures/test.asc";

    CHECK(s.provider_name == "Tactrix OpenPort 2.0");
    CHECK(s.bitrate == 500000);
    CHECK(s.channel_open);
    CHECK(s.recording);
    CHECK(s.recording_file == "captures/test.asc");
}

// ---------------------------------------------------------------------------
// Filter tracking
// ---------------------------------------------------------------------------
TEST_CASE("SessionStatus filter tracking", "[session_status]") {
    SessionStatus s;
    CHECK(s.active_filters.empty());

    FilterRule r;
    r.action = FilterAction::Pass;
    r.id_value = 0x7E8;
    r.id_mask = 0xFFFFFFFF;
    s.active_filters.push_back(r);

    CHECK(s.active_filters.size() == 1);
    CHECK(s.active_filters[0].id_value == 0x7E8);
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------
TEST_CASE("SessionStatus reset clears all fields", "[session_status]") {
    SessionStatus s;
    s.mode = OperatingMode::ActiveQuery;
    s.provider_name = "Test";
    s.adapter_name = "Test";
    s.bitrate = 250000;
    s.channel_open = true;
    s.recording = true;
    s.recording_file = "file.asc";
    s.frames_received = 999;
    s.frames_transmitted = 10;
    s.errors = 5;
    s.dropped = 3;
    s.active_filters.push_back({FilterAction::Block, 0, 0});

    s.reset();

    CHECK(s.mode == OperatingMode::Passive);
    CHECK(s.provider_name.empty());
    CHECK(s.adapter_name.empty());
    CHECK(s.bitrate == 0);
    CHECK(s.channel_open == false);
    CHECK(s.recording == false);
    CHECK(s.recording_file.empty());
    CHECK(s.frames_received == 0);
    CHECK(s.frames_transmitted == 0);
    CHECK(s.errors == 0);
    CHECK(s.dropped == 0);
    CHECK(s.active_filters.empty());
}

// ---------------------------------------------------------------------------
// elapsed_seconds()
// ---------------------------------------------------------------------------
TEST_CASE("SessionStatus elapsed_seconds near zero on fresh status", "[session_status]") {
    SessionStatus s;
    s.session_start = std::chrono::steady_clock::now();
    double elapsed = s.elapsed_seconds();
    // Should be very close to 0 (well under 1 second)
    CHECK(elapsed >= 0.0);
    CHECK(elapsed < 1.0);
}

TEST_CASE("SessionStatus elapsed_seconds increases over time", "[session_status]") {
    SessionStatus s;
    s.session_start = std::chrono::steady_clock::now();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    double elapsed = s.elapsed_seconds();
    // At least 40ms should have elapsed (generous for CI)
    CHECK(elapsed >= 0.04);
    CHECK(elapsed < 2.0);
}
