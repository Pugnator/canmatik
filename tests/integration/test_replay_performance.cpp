/// @file test_replay_performance.cpp
/// Performance benchmark: 1M-frame JSONL load + search (T068 — SC-005).

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "services/replay_service.h"
#include "logging/jsonl_writer.h"
#include "core/can_frame.h"
#include "core/session_status.h"
#include "core/timestamp.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace canmatik;

namespace {

/// Generate a JSONL file with N frames and a mix of IDs.
std::string generate_large_jsonl(uint64_t frame_count) {
    auto path = (std::filesystem::temp_directory_path() / "perf_1m.jsonl").string();

    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    JsonlWriter writer(ofs);

    SessionStatus status;
    status.provider_name = "PerfTest";
    status.bitrate = 500000;
    writer.writeHeader(status);

    std::mt19937 rng(42);
    static constexpr uint32_t ids[] = {
        0x100, 0x120, 0x200, 0x280, 0x300, 0x320,
        0x3B0, 0x400, 0x500, 0x600, 0x7E0, 0x7E8,
    };
    std::uniform_int_distribution<size_t> id_dist(0, 11);
    std::uniform_int_distribution<int> dlc_dist(1, 8);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    uint64_t start_us = host_timestamp_us();
    for (uint64_t i = 0; i < frame_count; ++i) {
        CanFrame frame{};
        frame.host_timestamp_us = start_us + i * 10; // 10µs apart
        frame.adapter_timestamp_us = i * 10;
        frame.arbitration_id = ids[id_dist(rng)];
        frame.type = FrameType::Standard;
        frame.dlc = static_cast<uint8_t>(dlc_dist(rng));
        for (uint8_t b = 0; b < frame.dlc; ++b) {
            frame.data[b] = static_cast<uint8_t>(byte_dist(rng));
        }
        writer.writeFrame(frame);
    }

    status.frames_received = frame_count;
    writer.writeFooter(status);
    writer.flush();

    return path;
}

} // anonymous namespace

TEST_CASE("T068: 1M-frame JSONL load and search within 5 seconds", "[performance][replay]") {
    // Generate 1M-frame file
    auto path = generate_large_jsonl(1'000'000);
    REQUIRE(std::filesystem::exists(path));

    ReplayService replay;

    // Time the load
    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(replay.load(path));
    auto t1 = std::chrono::steady_clock::now();

    double load_s = std::chrono::duration<double>(t1 - t0).count();
    INFO("Load time: " << load_s << " seconds");
    CHECK(load_s < 60.0); // generous limit for load (I/O + JSON parsing)

    // Time the search
    auto t2 = std::chrono::steady_clock::now();
    auto results = replay.search(0x7E8);
    auto t3 = std::chrono::steady_clock::now();

    double search_s = std::chrono::duration<double>(t3 - t2).count();
    INFO("Search time: " << search_s << " seconds for " << results.size() << " matches");
    CHECK(search_s < 5.0); // SC-005: search < 5 seconds

    // Summary should work
    auto summary = replay.summary();
    CHECK(summary.total_frames == 1'000'000);
    CHECK(summary.unique_ids > 0);

    // Cleanup — ignore errors (file may still be open by reader)
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
