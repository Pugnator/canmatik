/// @file test_timestamp.cpp
/// Unit tests for timestamp rollover handling and conversion (T022).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/timestamp.h"

using namespace canmatik;

// ---------------------------------------------------------------------------
// extend_timestamp — no rollover
// ---------------------------------------------------------------------------
TEST_CASE("extend_timestamp — monotonic increase, no rollover", "[timestamp]") {
    uint32_t rollover = 0;
    uint64_t ts1 = extend_timestamp(1000, 0, rollover);
    CHECK(ts1 == 1000);
    CHECK(rollover == 0);

    uint64_t ts2 = extend_timestamp(2000, 1000, rollover);
    CHECK(ts2 == 2000);
    CHECK(rollover == 0);
}

TEST_CASE("extend_timestamp — zero timestamps", "[timestamp]") {
    uint32_t rollover = 0;
    uint64_t ts = extend_timestamp(0, 0, rollover);
    CHECK(ts == 0);
    CHECK(rollover == 0);
}

// ---------------------------------------------------------------------------
// extend_timestamp — rollover detection
// ---------------------------------------------------------------------------
TEST_CASE("extend_timestamp — single rollover", "[timestamp]") {
    uint32_t rollover = 0;

    // Just before max
    uint64_t ts1 = extend_timestamp(0xFFFFFFF0, 0, rollover);
    CHECK(ts1 == 0xFFFFFFF0ULL);
    CHECK(rollover == 0);

    // After rollover: raw_ts < prev_ts → rollover incremented
    uint64_t ts2 = extend_timestamp(100, 0xFFFFFFF0, rollover);
    CHECK(rollover == 1);
    CHECK(ts2 == 0x1'00000064ULL); // 0x100000000 + 100
    CHECK(ts2 > ts1); // Must be monotonically increasing
}

TEST_CASE("extend_timestamp — double rollover", "[timestamp]") {
    uint32_t rollover = 0;

    extend_timestamp(0xFFFFFF00, 0, rollover);
    extend_timestamp(50, 0xFFFFFF00, rollover);
    CHECK(rollover == 1);

    // Second rollover
    extend_timestamp(0xFFFFFFF0, 50, rollover);
    uint64_t ts3 = extend_timestamp(10, 0xFFFFFFF0, rollover);
    CHECK(rollover == 2);
    CHECK(ts3 == 2 * 0x1'00000000ULL + 10);
}

// ---------------------------------------------------------------------------
// extend_timestamp — typical J2534 scenario (~71 min rollover)
// ---------------------------------------------------------------------------
TEST_CASE("extend_timestamp — realistic microsecond values", "[timestamp]") {
    uint32_t rollover = 0;
    // Adapter running for ~60 seconds: 60M µs
    uint64_t ts1 = extend_timestamp(60'000'000, 0, rollover);
    CHECK(ts1 == 60'000'000ULL);
    CHECK(rollover == 0);

    // ~70 minutes: near rollover at ~4295 seconds
    uint64_t ts2 = extend_timestamp(4'294'000'000U, 60'000'000, rollover);
    CHECK(ts2 == 4'294'000'000ULL);
    CHECK(rollover == 0);

    // Rollover after ~71.6 minutes
    uint64_t ts3 = extend_timestamp(1'000'000, 4'294'000'000U, rollover);
    CHECK(rollover == 1);
    CHECK(ts3 > ts2);
}

// ---------------------------------------------------------------------------
// host_timestamp_us
// ---------------------------------------------------------------------------
TEST_CASE("host_timestamp_us returns non-zero value", "[timestamp]") {
    uint64_t t1 = host_timestamp_us();
    CHECK(t1 > 0);

    // Second call should be >= first (monotonic)
    uint64_t t2 = host_timestamp_us();
    CHECK(t2 >= t1);
}

// ---------------------------------------------------------------------------
// session_relative_seconds
// ---------------------------------------------------------------------------
TEST_CASE("session_relative_seconds — basic conversion", "[timestamp]") {
    // 1.5 seconds elapsed
    double rel = session_relative_seconds(1'500'000, 0);
    CHECK_THAT(rel, Catch::Matchers::WithinAbs(1.5, 1e-9));
}

TEST_CASE("session_relative_seconds — zero elapsed", "[timestamp]") {
    double rel = session_relative_seconds(1000, 1000);
    CHECK_THAT(rel, Catch::Matchers::WithinAbs(0.0, 1e-9));
}

TEST_CASE("session_relative_seconds — frame before session start returns 0", "[timestamp]") {
    double rel = session_relative_seconds(500, 1000);
    CHECK_THAT(rel, Catch::Matchers::WithinAbs(0.0, 1e-9));
}

TEST_CASE("session_relative_seconds — microsecond precision", "[timestamp]") {
    double rel = session_relative_seconds(1'000'001, 1'000'000);
    CHECK_THAT(rel, Catch::Matchers::WithinAbs(0.000001, 1e-12));
}

TEST_CASE("session_relative_seconds — large values", "[timestamp]") {
    // 1 hour session
    uint64_t start = 0;
    uint64_t frame = 3'600'000'000ULL; // 3600 seconds in µs
    double rel = session_relative_seconds(frame, start);
    CHECK_THAT(rel, Catch::Matchers::WithinAbs(3600.0, 1e-6));
}
