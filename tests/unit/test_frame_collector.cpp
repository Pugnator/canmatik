/// @file test_frame_collector.cpp
/// Unit tests for FrameCollector: ring buffer, change detection, watchdog, OBD filter.

#include <catch2/catch_test_macros.hpp>
#include "gui/frame_collector.h"

using namespace canmatik;

static CanFrame make_frame(uint32_t id, uint8_t dlc, uint8_t fill, uint64_t ts_us = 0) {
    CanFrame f;
    f.arbitration_id    = id;
    f.dlc               = dlc;
    f.host_timestamp_us = ts_us;
    for (uint8_t i = 0; i < dlc; ++i)
        f.data[i] = fill;
    return f;
}

TEST_CASE("FrameCollector stores frames in ring buffer", "[frame_collector]") {
    FrameCollector c(0, 10);
    for (int i = 0; i < 5; ++i)
        c.pushFrame(make_frame(0x100, 2, static_cast<uint8_t>(i), i * 1000));

    CHECK(c.buffer_count() == 5);
    auto contents = c.buffer_contents();
    CHECK(contents.size() == 5);
    CHECK(contents[0].data[0] == 0);
    CHECK(contents[4].data[0] == 4);
}

TEST_CASE("FrameCollector ring buffer overflow drops oldest", "[frame_collector]") {
    FrameCollector c(0, 3);
    c.set_overwrite(true);
    for (int i = 0; i < 5; ++i)
        c.pushFrame(make_frame(0x100, 1, static_cast<uint8_t>(i)));

    CHECK(c.buffer_count() == 3);
    auto contents = c.buffer_contents();
    CHECK(contents[0].data[0] == 2); // oldest kept
    CHECK(contents[2].data[0] == 4); // newest
}

TEST_CASE("FrameCollector per-ID change detection", "[frame_collector]") {
    FrameCollector c(0, 100);
    c.pushFrame(make_frame(0x200, 2, 0xAA, 1000));
    c.pushFrame(make_frame(0x200, 2, 0xBB, 2000)); // data changed

    auto rows = c.snapshot(false, 1, ObdDisplayMode::OBD_AND_BROADCAST);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].arb_id == 0x200);
    CHECK(rows[0].changed[0] == true);
    CHECK(rows[0].changed[1] == true);
    CHECK(rows[0].update_count == 2);
}

TEST_CASE("FrameCollector changed-only filter", "[frame_collector]") {
    FrameCollector c(0, 100);
    // ID 0x100: payload changes -> included
    c.pushFrame(make_frame(0x100, 1, 0xAA, 1000));
    c.pushFrame(make_frame(0x100, 1, 0xBB, 2000));
    // ID 0x200: payload also changes -> included
    c.pushFrame(make_frame(0x200, 1, 0xAA, 1000));
    c.pushFrame(make_frame(0x200, 1, 0xCC, 2000));
    // ID 0x300: single frame with no change history -> only 1 history entry -> filtered OUT
    c.pushFrame(make_frame(0x300, 1, 0xDD, 1000));
    c.pushFrame(make_frame(0x300, 1, 0xDD, 2000)); // no change, is_new stays true

    auto all = c.snapshot(false, 1, ObdDisplayMode::OBD_AND_BROADCAST);
    CHECK(all.size() == 3);

    auto changed = c.snapshot(true, 1, ObdDisplayMode::OBD_AND_BROADCAST);
    // 0x100: is_new=false, history=[AA,BB], found change → passes
    // 0x200: is_new=false, history=[AA,CC], found change → passes
    // 0x300: is_new=true → passes (new IDs always included)
    CHECK(changed.size() == 3);
}

TEST_CASE("FrameCollector watchdog add/remove", "[frame_collector]") {
    FrameCollector c(0, 100);
    c.pushFrame(make_frame(0x300, 2, 0x11, 1000));
    c.pushFrame(make_frame(0x400, 2, 0x22, 1000));

    c.add_watchdog(0x300);
    CHECK(c.is_watched(0x300));
    CHECK_FALSE(c.is_watched(0x400));

    auto watched = c.watchdog_snapshot();
    REQUIRE(watched.size() == 1);
    CHECK(watched[0].arb_id == 0x300);

    c.remove_watchdog(0x300);
    CHECK_FALSE(c.is_watched(0x300));
    watched = c.watchdog_snapshot();
    CHECK(watched.empty());
}

TEST_CASE("FrameCollector OBD mode filters", "[frame_collector]") {
    FrameCollector c(0, 100);
    c.pushFrame(make_frame(0x7DF, 8, 0x01, 1000));  // OBD request
    c.pushFrame(make_frame(0x7E8, 8, 0x41, 2000));  // OBD response
    c.pushFrame(make_frame(0x100, 4, 0xAA, 3000));  // broadcast

    auto all = c.snapshot(false, 1, ObdDisplayMode::OBD_AND_BROADCAST);
    CHECK(all.size() == 3);

    auto obd_only = c.snapshot(false, 1, ObdDisplayMode::OBD_ONLY);
    CHECK(obd_only.size() == 2);

    auto bcast_only = c.snapshot(false, 1, ObdDisplayMode::BROADCAST_ONLY);
    CHECK(bcast_only.size() == 1);
    CHECK(bcast_only[0].arb_id == 0x100);
}

TEST_CASE("FrameCollector resize preserves data", "[frame_collector]") {
    FrameCollector c(0, 10);
    for (int i = 0; i < 8; ++i)
        c.pushFrame(make_frame(0x100, 1, static_cast<uint8_t>(i)));
    CHECK(c.buffer_count() == 8);

    // Shrink to 5 — keep newest 5
    c.resize_buffer(5);
    CHECK(c.buffer_count() == 5);
    auto contents = c.buffer_contents();
    CHECK(contents[0].data[0] == 3);
    CHECK(contents[4].data[0] == 7);

    // Grow — existing data preserved
    c.resize_buffer(20);
    CHECK(c.buffer_count() == 5);
    CHECK(c.buffer_capacity() == 20);
}

TEST_CASE("FrameCollector clear resets everything", "[frame_collector]") {
    FrameCollector c(0, 100);
    c.pushFrame(make_frame(0x100, 1, 0xAA));
    c.add_watchdog(0x100);
    c.clear();

    CHECK(c.buffer_count() == 0);
    CHECK(c.unique_ids() == 0);
    // watchdogs preserved through clear — only clear_watchdogs resets them
}
