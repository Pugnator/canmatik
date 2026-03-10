/// @file test_query_scheduler.cpp
/// Unit tests for QueryScheduler (T127).

#include <catch2/catch_test_macros.hpp>

#include "obd/query_scheduler.h"
#include "obd/obd_session.h"
#include "mock/mock_channel.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace canmatik;

namespace {

/// Build a canned RPM response frame (0x7E8: 04 41 0C 1A F8 55 55 55).
CanFrame make_rpm_response() {
    CanFrame f{};
    f.arbitration_id = 0x7E8;
    f.type = FrameType::Standard;
    f.dlc = 8;
    f.data[0] = 0x04; f.data[1] = 0x41; f.data[2] = 0x0C;
    f.data[3] = 0x1A; f.data[4] = 0xF8;
    f.data[5] = 0x55; f.data[6] = 0x55; f.data[7] = 0x55;
    return f;
}

CanFrame make_speed_response() {
    CanFrame f{};
    f.arbitration_id = 0x7E8;
    f.type = FrameType::Standard;
    f.dlc = 8;
    f.data[0] = 0x03; f.data[1] = 0x41; f.data[2] = 0x0D;
    f.data[3] = 0x78; // 120 km/h
    f.data[4] = 0x55; f.data[5] = 0x55; f.data[6] = 0x55; f.data[7] = 0x55;
    return f;
}

} // namespace

TEST_CASE("QueryScheduler: stop flag terminates", "[obd][scheduler]") {
    MockChannel channel(100); // 100 fps so read() returns frames
    channel.open(500000);
    channel.set_writable(true);

    // Provide responses in sequence
    channel.set_frame_sequence({make_rpm_response()});

    ObdSession session(channel);
    ObdConfig config;
    config.default_interval = {100}; // 100ms
    config.groups.push_back({"test", {100}, true, {0x0C}});

    std::vector<DecodedPid> results;
    std::atomic<bool> stop{false};

    // Start scheduler in a thread, stop after a short time
    std::thread t([&] {
        QueryScheduler scheduler(session, config, [&](const DecodedPid& d) {
            results.push_back(d);
        });
        scheduler.run(stop);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop.store(true);
    t.join();

    // Should have received at least one decoded PID
    CHECK(results.size() >= 1);
}

TEST_CASE("QueryScheduler: callback receives decoded values", "[obd][scheduler]") {
    MockChannel channel(100);
    channel.open(500000);
    channel.set_writable(true);

    // Alternate RPM and speed responses
    channel.set_frame_sequence({make_rpm_response(), make_speed_response()});

    ObdSession session(channel);
    ObdConfig config;
    config.default_interval = {50};
    config.groups.push_back({"engine", {50}, true, {0x0C, 0x0D}});

    std::vector<DecodedPid> results;
    std::atomic<bool> stop{false};

    std::thread t([&] {
        QueryScheduler scheduler(session, config, [&](const DecodedPid& d) {
            results.push_back(d);
            if (results.size() >= 2) stop.store(true);
        });
        scheduler.run(stop);
    });

    // Safety timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!stop.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stop.store(true);
    t.join();

    REQUIRE(results.size() >= 2);
    // First query is PID 0x0C (RPM), response is RPM
    CHECK(results[0].pid == 0x0C);
}
