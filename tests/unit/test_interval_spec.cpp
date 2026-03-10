/// @file test_interval_spec.cpp
/// Unit tests for IntervalSpec parser (T104).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "obd/interval_spec.h"

using namespace canmatik;

TEST_CASE("IntervalSpec: milliseconds suffix", "[obd][interval]") {
    auto r = parse_interval("500ms");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 500);
}

TEST_CASE("IntervalSpec: seconds suffix", "[obd][interval]") {
    auto r = parse_interval("1s");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 1000);
}

TEST_CASE("IntervalSpec: fractional seconds", "[obd][interval]") {
    auto r = parse_interval("2.5s");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 2500);
}

TEST_CASE("IntervalSpec: hertz", "[obd][interval]") {
    auto r = parse_interval("2hz");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 500);
}

TEST_CASE("IntervalSpec: fractional hertz", "[obd][interval]") {
    auto r = parse_interval("0.5hz");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 2000);
}

TEST_CASE("IntervalSpec: case insensitive", "[obd][interval]") {
    auto r1 = parse_interval("500MS");
    REQUIRE(r1.has_value());
    CHECK(r1->milliseconds == 500);

    auto r2 = parse_interval("2Hz");
    REQUIRE(r2.has_value());
    CHECK(r2->milliseconds == 500);

    auto r3 = parse_interval("1S");
    REQUIRE(r3.has_value());
    CHECK(r3->milliseconds == 1000);
}

TEST_CASE("IntervalSpec: minimum boundary (10ms)", "[obd][interval]") {
    auto r = parse_interval("10ms");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 10);
}

TEST_CASE("IntervalSpec: maximum boundary (60000ms)", "[obd][interval]") {
    auto r = parse_interval("60000ms");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 60000);

    auto r2 = parse_interval("60s");
    REQUIRE(r2.has_value());
    CHECK(r2->milliseconds == 60000);
}

TEST_CASE("IntervalSpec: below minimum rejects", "[obd][interval]") {
    auto r = parse_interval("5ms");
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error(), Catch::Matchers::ContainsSubstring("below minimum"));
}

TEST_CASE("IntervalSpec: above maximum rejects", "[obd][interval]") {
    auto r = parse_interval("61s");
    REQUIRE_FALSE(r.has_value());
    CHECK_THAT(r.error(), Catch::Matchers::ContainsSubstring("above maximum"));

    // 0.001hz → 1000000ms → exceeds max
    auto r2 = parse_interval("0.001hz");
    REQUIRE_FALSE(r2.has_value());
    CHECK_THAT(r2.error(), Catch::Matchers::ContainsSubstring("above maximum"));
}

TEST_CASE("IntervalSpec: invalid input", "[obd][interval]") {
    SECTION("no number") {
        auto r = parse_interval("abc");
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("empty string") {
        auto r = parse_interval("");
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("negative") {
        auto r = parse_interval("-1s");
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("0 hertz") {
        auto r = parse_interval("0hz");
        REQUIRE_FALSE(r.has_value());
    }
    SECTION("unknown suffix") {
        auto r = parse_interval("100bps");
        REQUIRE_FALSE(r.has_value());
    }
}

TEST_CASE("IntervalSpec: 100ms", "[obd][interval]") {
    auto r = parse_interval("100ms");
    REQUIRE(r.has_value());
    CHECK(r->milliseconds == 100);
}
