/// @file test_asc_reader.cpp
/// Unit tests for AscReader (T050 — US5).

#include <catch2/catch_test_macros.hpp>

#include "logging/asc_reader.h"
#include "logging/asc_writer.h"
#include "core/can_frame.h"
#include "core/session_status.h"

#include <sstream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

using namespace canmatik;

namespace {

std::string make_temp_file(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "canmatik_test_asc_reader.asc";
    {
        std::ofstream f(path, std::ios::out | std::ios::trunc);
        f << content;
    }
    return path.string();
}

void cleanup_temp(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

const char* kValidAsc =
    "; CANmatik ASC v1.0\n"
    "date Mon Jan 01 00:00:00.000 2025\n"
    "base hex  timestamps absolute\n"
    "; adapter: TestAdapter\n"
    "; bitrate: 500000\n"
    "internal events logged\n"
    "Begin Triggerblock Mon Jan 01 00:00:00.000 2025\n"
    "   0.000000 1  7E0             Rx   d 8 02 01 00 00 00 00 00 00\n"
    "   0.016000 1  7E8             Rx   d 3 06 41 00\n"
    "   0.100000 1  18DAF110x       Rx   d 2 AA BB\n"
    "End TriggerBlock\n";

} // anonymous namespace

TEST_CASE("AscReader: open valid file", "[asc_reader]") {
    auto path = make_temp_file(kValidAsc);
    AscReader reader;
    REQUIRE(reader.open(path));
    cleanup_temp(path);
}

TEST_CASE("AscReader: open non-existent file returns false", "[asc_reader]") {
    AscReader reader;
    CHECK_FALSE(reader.open("nonexistent_file_12345.asc"));
}

TEST_CASE("AscReader: parse standard frames", "[asc_reader]") {
    auto path = make_temp_file(kValidAsc);
    AscReader reader;
    REQUIRE(reader.open(path));

    // Frame 1: 7E0, 8 bytes
    auto f1 = reader.nextFrame();
    REQUIRE(f1.has_value());
    CHECK(f1->arbitration_id == 0x7E0);
    CHECK(f1->type == FrameType::Standard);
    CHECK(f1->dlc == 8);
    CHECK(f1->data[0] == 0x02);
    CHECK(f1->data[1] == 0x01);
    CHECK(f1->host_timestamp_us == 0);

    // Frame 2: 7E8, 3 bytes
    auto f2 = reader.nextFrame();
    REQUIRE(f2.has_value());
    CHECK(f2->arbitration_id == 0x7E8);
    CHECK(f2->dlc == 3);
    CHECK(f2->data[0] == 0x06);
    CHECK(f2->host_timestamp_us == 16000);

    // Frame 3: extended ID
    auto f3 = reader.nextFrame();
    REQUIRE(f3.has_value());
    CHECK(f3->arbitration_id == 0x18DAF110);
    CHECK(f3->type == FrameType::Extended);
    CHECK(f3->dlc == 2);
    CHECK(f3->data[0] == 0xAA);
    CHECK(f3->data[1] == 0xBB);

    // No more frames
    CHECK_FALSE(reader.nextFrame().has_value());

    cleanup_temp(path);
}

TEST_CASE("AscReader: metadata extracts adapter and bitrate", "[asc_reader]") {
    auto path = make_temp_file(kValidAsc);
    AscReader reader;
    REQUIRE(reader.open(path));

    // Read all frames
    while (reader.nextFrame()) {}

    auto meta = reader.metadata();
    CHECK(meta.adapter_name == "TestAdapter");
    CHECK(meta.bitrate == 500000);
    CHECK(meta.frames_received == 3);

    cleanup_temp(path);
}

TEST_CASE("AscReader: empty ASC file", "[asc_reader]") {
    auto path = make_temp_file("");
    AscReader reader;
    REQUIRE(reader.open(path));
    CHECK_FALSE(reader.nextFrame().has_value());
    cleanup_temp(path);
}

TEST_CASE("AscReader: corrupt data lines are skipped", "[asc_reader]") {
    const char* content =
        "; CANmatik ASC v1.0\n"
        "Begin Triggerblock\n"
        "NOT_A_FRAME\n"
        "   0.100000 1  100             Rx   d 2 AA BB\n"
        "End TriggerBlock\n";

    auto path = make_temp_file(content);
    AscReader reader;
    REQUIRE(reader.open(path));

    auto f = reader.nextFrame();
    REQUIRE(f.has_value());
    CHECK(f->arbitration_id == 0x100);

    CHECK_FALSE(reader.nextFrame().has_value());
    cleanup_temp(path);
}

TEST_CASE("AscReader: reset re-reads from beginning", "[asc_reader]") {
    auto path = make_temp_file(kValidAsc);
    AscReader reader;
    REQUIRE(reader.open(path));

    // Read all
    int count = 0;
    while (reader.nextFrame()) ++count;
    CHECK(count == 3);

    // Reset and re-read
    reader.reset();
    int count2 = 0;
    while (reader.nextFrame()) ++count2;
    CHECK(count2 == 3);

    cleanup_temp(path);
}
