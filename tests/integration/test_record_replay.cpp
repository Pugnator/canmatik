/// @file test_record_replay.cpp
/// Integration tests for record/replay pipeline (T054 — US5).
/// Tests AscWriter→AscReader and JsonlWriter→JsonlReader roundtrip,
/// verifying frame-level equality and SC-006 determinism.

#include <catch2/catch_test_macros.hpp>

#include "logging/asc_writer.h"
#include "logging/asc_reader.h"
#include "logging/jsonl_writer.h"
#include "logging/jsonl_reader.h"
#include "core/can_frame.h"
#include "core/session_status.h"
#include "core/timestamp.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace canmatik;

namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

void cleanup(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

/// Build a set of known test frames with fixed timestamps.
std::vector<CanFrame> make_test_frames() {
    std::vector<CanFrame> frames;

    {
        CanFrame f;
        f.arbitration_id = 0x7E0;
        f.type = FrameType::Standard;
        f.dlc = 8;
        f.data = {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        f.host_timestamp_us = 1000000;  // 1.0s
        f.adapter_timestamp_us = 1000000;
        f.channel_id = 1;
        frames.push_back(f);
    }
    {
        CanFrame f;
        f.arbitration_id = 0x7E8;
        f.type = FrameType::Standard;
        f.dlc = 4;
        f.data = {0x06, 0x41, 0x0C, 0x1A};
        f.host_timestamp_us = 1016000;
        f.adapter_timestamp_us = 1016000;
        f.channel_id = 1;
        frames.push_back(f);
    }
    {
        CanFrame f;
        f.arbitration_id = 0x18DAF110;
        f.type = FrameType::Extended;
        f.dlc = 2;
        f.data = {0xAA, 0xBB};
        f.host_timestamp_us = 1100000;
        f.adapter_timestamp_us = 1100000;
        f.channel_id = 1;
        frames.push_back(f);
    }
    {
        CanFrame f;
        f.arbitration_id = 0x100;
        f.type = FrameType::Standard;
        f.dlc = 0;
        f.host_timestamp_us = 2000000;
        f.adapter_timestamp_us = 2000000;
        f.channel_id = 1;
        frames.push_back(f);
    }

    return frames;
}

SessionStatus make_test_status() {
    SessionStatus s;
    s.adapter_name = "TestAdapter";
    s.bitrate = 500000;
    s.frames_received = 4;
    return s;
}

void verify_frame_match(const CanFrame& written, const CanFrame& read, bool check_timestamps = false) {
    CHECK(read.arbitration_id == written.arbitration_id);
    CHECK(read.type == written.type);
    CHECK(read.dlc == written.dlc);
    for (uint8_t i = 0; i < written.dlc; ++i) {
        CHECK(read.data[i] == written.data[i]);
    }
    if (check_timestamps) {
        CHECK(read.adapter_timestamp_us == written.adapter_timestamp_us);
    }
}

/// Read entire file content as string for byte comparison.
std::string read_file_content(const std::string& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

} // anonymous namespace

TEST_CASE("Roundtrip: AscWriter → AscReader", "[integration][record_replay]") {
    auto path = temp_path("canmatik_roundtrip_test.asc");
    auto frames = make_test_frames();
    auto status = make_test_status();

    // Write
    {
        AscWriter writer(path);
        writer.writeHeader(status);
        for (const auto& f : frames) {
            writer.writeFrame(f);
        }
        writer.writeFooter(status);
        writer.flush();
    }

    // Read back
    AscReader reader;
    REQUIRE(reader.open(path));

    std::vector<CanFrame> read_frames;
    while (auto f = reader.nextFrame()) {
        read_frames.push_back(*f);
    }

    REQUIRE(read_frames.size() == frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        INFO("Frame index " << i);
        verify_frame_match(frames[i], read_frames[i]);
    }

    // Verify metadata
    auto meta = reader.metadata();
    CHECK(meta.adapter_name == "TestAdapter");
    CHECK(meta.bitrate == 500000);
    CHECK(meta.frames_received == 4);

    cleanup(path);
}

TEST_CASE("Roundtrip: JsonlWriter → JsonlReader", "[integration][record_replay]") {
    auto path = temp_path("canmatik_roundtrip_test.jsonl");
    auto frames = make_test_frames();
    auto status = make_test_status();

    // Write
    {
        JsonlWriter writer(path);
        writer.writeHeader(status);
        for (const auto& f : frames) {
            writer.writeFrame(f);
        }
        writer.writeFooter(status);
        writer.flush();
    }

    // Read back
    JsonlReader reader;
    REQUIRE(reader.open(path));

    std::vector<CanFrame> read_frames;
    while (auto f = reader.nextFrame()) {
        read_frames.push_back(*f);
    }

    REQUIRE(read_frames.size() == frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        INFO("Frame index " << i);
        verify_frame_match(frames[i], read_frames[i], true);
    }

    // Verify metadata
    auto meta = reader.metadata();
    CHECK(meta.adapter_name == "TestAdapter");
    CHECK(meta.bitrate == 500000);
    CHECK(meta.frames_received == 4);

    cleanup(path);
}

TEST_CASE("SC-006 Determinism: identical input produces identical output (ASC)", "[integration][record_replay]") {
    auto path1 = temp_path("canmatik_determ_1.asc");
    auto path2 = temp_path("canmatik_determ_2.asc");
    auto frames = make_test_frames();

    // Use a fixed status so headers are identical (except date, which varies)
    SessionStatus status;
    status.adapter_name = "DetermTest";
    status.bitrate = 250000;

    // Write file 1
    {
        AscWriter writer(path1);
        writer.writeHeader(status);
        for (const auto& f : frames) writer.writeFrame(f);
        writer.writeFooter(status);
        writer.flush();
    }

    // Write file 2 with same input
    {
        AscWriter writer(path2);
        writer.writeHeader(status);
        for (const auto& f : frames) writer.writeFrame(f);
        writer.writeFooter(status);
        writer.flush();
    }

    // Compare frame lines (skip header which contains timestamp)
    // Both files should have identical frame content sections
    AscReader r1, r2;
    REQUIRE(r1.open(path1));
    REQUIRE(r2.open(path2));

    while (true) {
        auto f1 = r1.nextFrame();
        auto f2 = r2.nextFrame();
        CHECK(f1.has_value() == f2.has_value());
        if (!f1 || !f2) break;
        verify_frame_match(*f1, *f2);
    }

    cleanup(path1);
    cleanup(path2);
}

TEST_CASE("SC-006 Determinism: identical input produces identical output (JSONL)", "[integration][record_replay]") {
    auto path1 = temp_path("canmatik_determ_1.jsonl");
    auto path2 = temp_path("canmatik_determ_2.jsonl");
    auto frames = make_test_frames();

    SessionStatus status;
    status.adapter_name = "DetermTest";
    status.bitrate = 250000;
    status.frames_received = 4;

    // Write file 1
    {
        JsonlWriter writer(path1);
        writer.writeHeader(status);
        for (const auto& f : frames) writer.writeFrame(f);
        writer.writeFooter(status);
        writer.flush();
    }

    // Write file 2
    {
        JsonlWriter writer(path2);
        writer.writeHeader(status);
        for (const auto& f : frames) writer.writeFrame(f);
        writer.writeFooter(status);
        writer.flush();
    }

    // Compare all frames
    JsonlReader r1, r2;
    REQUIRE(r1.open(path1));
    REQUIRE(r2.open(path2));

    while (true) {
        auto f1 = r1.nextFrame();
        auto f2 = r2.nextFrame();
        CHECK(f1.has_value() == f2.has_value());
        if (!f1 || !f2) break;
        verify_frame_match(*f1, *f2, true);
    }

    cleanup(path1);
    cleanup(path2);
}
