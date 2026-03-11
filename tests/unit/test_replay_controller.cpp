/// @file test_replay_controller.cpp
/// Unit tests for ReplayController: load, play, pause, rewind, ff, loop.

#include <catch2/catch_test_macros.hpp>
#include "gui/controllers/replay_controller.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace canmatik;

namespace {
    struct TempFile {
        std::string path;
        TempFile(const std::string& p) : path(p) {}
        ~TempFile() { std::remove(path.c_str()); }
    };

    void write_asc(const std::string& path) {
        std::ofstream f(path);
        f << "date Jan 1 2025 12:00:00\n";
        f << "Begin Triggerblock\n";
        f << "   0.001000 1  100       Rx   d 2 AA BB\n";
        f << "   0.002000 1  200       Rx   d 3 CC DD EE\n";
        f << "   0.010000 1  300       Rx   d 1 FF\n";
        f << "End Triggerblock\n";
    }
}

TEST_CASE("ReplayController load ASC file", "[replay_controller]") {
    TempFile tmp("_test_replay.asc");
    write_asc(tmp.path);

    ReplayController rc;
    auto err = rc.load(tmp.path);
    REQUIRE(err.empty());
    CHECK(rc.is_loaded());
    CHECK(rc.frame_count() == 3);
    CHECK(rc.current_index() == 0);
}

TEST_CASE("ReplayController load nonexistent file returns error", "[replay_controller]") {
    ReplayController rc;
    auto err = rc.load("_nonexistent_12345.asc");
    CHECK_FALSE(err.empty());
    CHECK_FALSE(rc.is_loaded());
}

TEST_CASE("ReplayController play dispatches frames via tick", "[replay_controller]") {
    TempFile tmp("_test_replay_play.asc");
    write_asc(tmp.path);

    ReplayController rc;
    rc.load(tmp.path);

    FrameCollector collector(0, 100);
    rc.play();
    CHECK(rc.is_playing());

    // Tick 15ms — should dispatch all 3 frames (at 1ms, 2ms, 10ms)
    rc.tick(15000, collector);
    CHECK(collector.buffer_count() == 3);
}

TEST_CASE("ReplayController pause preserves position", "[replay_controller]") {
    TempFile tmp("_test_replay_pause.asc");
    write_asc(tmp.path);

    ReplayController rc;
    rc.load(tmp.path);

    FrameCollector collector(0, 100);
    rc.play();
    rc.tick(1500, collector); // 1.5ms — should dispatch first frame (at 1ms)
    auto idx_before = rc.current_index();

    rc.pause();
    CHECK_FALSE(rc.is_playing());

    // Tick while paused — no progress
    rc.tick(100000, collector);
    CHECK(rc.current_index() == idx_before);
}

TEST_CASE("ReplayController rewind resets to 0", "[replay_controller]") {
    TempFile tmp("_test_replay_rw.asc");
    write_asc(tmp.path);

    ReplayController rc;
    rc.load(tmp.path);
    FrameCollector collector(0, 100);

    rc.play();
    rc.tick(15000, collector);

    rc.rewind();
    CHECK(rc.current_index() == 0);
}

TEST_CASE("ReplayController fast_forward cycles speed", "[replay_controller]") {
    ReplayController rc;
    CHECK(rc.speed() == 1.0f);
    rc.fast_forward();
    CHECK(rc.speed() == 2.0f);
    rc.fast_forward();
    CHECK(rc.speed() == 4.0f);
    rc.fast_forward();
    CHECK(rc.speed() == 8.0f);
    rc.fast_forward();
    CHECK(rc.speed() == 1.0f);
}

TEST_CASE("ReplayController stop resets", "[replay_controller]") {
    TempFile tmp("_test_replay_stop.asc");
    write_asc(tmp.path);

    ReplayController rc;
    rc.load(tmp.path);
    FrameCollector collector(0, 100);

    rc.play();
    rc.tick(15000, collector);
    rc.stop();
    CHECK_FALSE(rc.is_playing());
    CHECK(rc.current_index() == 0);
}
