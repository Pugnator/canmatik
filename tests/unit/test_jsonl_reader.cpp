/// @file test_jsonl_reader.cpp
/// Unit tests for JsonlReader (T051 — US5).

#include <catch2/catch_test_macros.hpp>

#include "logging/jsonl_reader.h"
#include "core/can_frame.h"
#include "core/session_status.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <string>

using namespace canmatik;

namespace {

std::string make_temp_file(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "canmatik_test_jsonl_reader.jsonl";
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

const char* kValidJsonl =
    "{\"_meta\":true,\"format\":\"canmatik-jsonl\",\"version\":\"1.0\",\"adapter\":\"TestAdapter\",\"bitrate\":500000,\"created_utc\":\"2025-01-01T00:00:00Z\"}\n"
    "{\"ts\":0.0,\"ats\":0,\"id\":\"7E0\",\"ext\":false,\"dlc\":8,\"data\":\"02 01 00 00 00 00 00 00\"}\n"
    "{\"ts\":0.016,\"ats\":16000,\"id\":\"7E8\",\"ext\":false,\"dlc\":3,\"data\":\"06 41 00\"}\n"
    "{\"ts\":0.1,\"ats\":100000,\"id\":\"18DAF110\",\"ext\":true,\"dlc\":2,\"data\":\"AA BB\"}\n"
    "{\"_meta\":true,\"type\":\"session_summary\",\"frames\":3,\"errors\":0,\"dropped\":0,\"duration\":0.1}\n";

} // anonymous namespace

TEST_CASE("JsonlReader: open valid file", "[jsonl_reader]") {
    auto path = make_temp_file(kValidJsonl);
    JsonlReader reader;
    REQUIRE(reader.open(path));
    cleanup_temp(path);
}

TEST_CASE("JsonlReader: open non-existent file returns false", "[jsonl_reader]") {
    JsonlReader reader;
    CHECK_FALSE(reader.open("nonexistent_file_12345.jsonl"));
}

TEST_CASE("JsonlReader: parse standard frames", "[jsonl_reader]") {
    auto path = make_temp_file(kValidJsonl);
    JsonlReader reader;
    REQUIRE(reader.open(path));

    // Frame 1: 7E0
    auto f1 = reader.nextFrame();
    REQUIRE(f1.has_value());
    CHECK(f1->arbitration_id == 0x7E0);
    CHECK(f1->type == FrameType::Standard);
    CHECK(f1->dlc == 8);
    CHECK(f1->data[0] == 0x02);
    CHECK(f1->data[1] == 0x01);
    CHECK(f1->adapter_timestamp_us == 0);

    // Frame 2: 7E8
    auto f2 = reader.nextFrame();
    REQUIRE(f2.has_value());
    CHECK(f2->arbitration_id == 0x7E8);
    CHECK(f2->dlc == 3);
    CHECK(f2->adapter_timestamp_us == 16000);

    // Frame 3: extended
    auto f3 = reader.nextFrame();
    REQUIRE(f3.has_value());
    CHECK(f3->arbitration_id == 0x18DAF110);
    CHECK(f3->type == FrameType::Extended);
    CHECK(f3->dlc == 2);
    CHECK(f3->data[0] == 0xAA);
    CHECK(f3->data[1] == 0xBB);

    // Session summary line is skipped, no more frames
    CHECK_FALSE(reader.nextFrame().has_value());

    cleanup_temp(path);
}

TEST_CASE("JsonlReader: metadata extracts adapter and bitrate", "[jsonl_reader]") {
    auto path = make_temp_file(kValidJsonl);
    JsonlReader reader;
    REQUIRE(reader.open(path));

    while (reader.nextFrame()) {}

    auto meta = reader.metadata();
    CHECK(meta.adapter_name == "TestAdapter");
    CHECK(meta.bitrate == 500000);
    CHECK(meta.frames_received == 3);

    cleanup_temp(path);
}

TEST_CASE("JsonlReader: empty file", "[jsonl_reader]") {
    auto path = make_temp_file("");
    JsonlReader reader;
    REQUIRE(reader.open(path));
    CHECK_FALSE(reader.nextFrame().has_value());
    cleanup_temp(path);
}

TEST_CASE("JsonlReader: malformed JSON lines are skipped", "[jsonl_reader]") {
    const char* content =
        "{\"_meta\":true,\"format\":\"canmatik-jsonl\",\"version\":\"1.0\",\"adapter\":\"Test\",\"bitrate\":500000}\n"
        "NOT VALID JSON\n"
        "{\"ts\":0.1,\"ats\":100000,\"id\":\"100\",\"ext\":false,\"dlc\":2,\"data\":\"AA BB\"}\n";

    auto path = make_temp_file(content);
    JsonlReader reader;
    REQUIRE(reader.open(path));

    auto f = reader.nextFrame();
    REQUIRE(f.has_value());
    CHECK(f->arbitration_id == 0x100);

    CHECK_FALSE(reader.nextFrame().has_value());
    cleanup_temp(path);
}

TEST_CASE("JsonlReader: missing fields skip line", "[jsonl_reader]") {
    const char* content =
        "{\"ts\":0.0,\"data\":\"AA\"}\n"
        "{\"ts\":0.1,\"ats\":0,\"id\":\"200\",\"ext\":false,\"dlc\":1,\"data\":\"FF\"}\n";

    auto path = make_temp_file(content);
    JsonlReader reader;
    REQUIRE(reader.open(path));

    // First line missing 'id' and 'dlc' — skipped
    auto f = reader.nextFrame();
    REQUIRE(f.has_value());
    CHECK(f->arbitration_id == 0x200);

    cleanup_temp(path);
}

TEST_CASE("JsonlReader: reset re-reads from beginning", "[jsonl_reader]") {
    auto path = make_temp_file(kValidJsonl);
    JsonlReader reader;
    REQUIRE(reader.open(path));

    int count = 0;
    while (reader.nextFrame()) ++count;
    CHECK(count == 3);

    reader.reset();
    int count2 = 0;
    while (reader.nextFrame()) ++count2;
    CHECK(count2 == 3);

    cleanup_temp(path);
}
