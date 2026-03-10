/// @file test_mock_capture.cpp
/// Integration tests: full mock capture pipeline (T034 — US2).
/// MockProvider → connect → open channel → CaptureService → receive frames → verify.

#include <catch2/catch_test_macros.hpp>

#include "core/can_frame.h"
#include "core/capture_sink.h"
#include "core/filter.h"
#include "mock/mock_channel.h"
#include "mock/mock_provider.h"
#include "services/capture_service.h"
#include "services/session_service.h"
#include "transport/transport_error.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace canmatik;

namespace {

/// Test sink that collects frames and errors.
class CollectorSink : public ICaptureSync {
public:
    std::vector<CanFrame> frames;
    std::vector<std::string> errors;

    void onFrame(const CanFrame& frame) override {
        frames.push_back(frame);
    }

    void onError(const TransportError& error) override {
        errors.push_back(error.what());
    }
};

} // anonymous namespace

TEST_CASE("Mock capture: basic frame reception", "[integration][capture]") {
    SessionService session;
    session.setProvider(std::make_unique<MockProvider>());

    auto providers = session.scan();
    REQUIRE(!providers.empty());

    session.connect(providers.front());
    session.openChannel(500000);

    CaptureService capture;
    CollectorSink sink;
    capture.addSink(&sink);

    capture.start(session.channel(), session.mutableStatus());

    // Let capture run briefly to collect some frames
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Drain from the main thread
    capture.drain();
    capture.stop();

    // Should have received multiple frames
    REQUIRE(sink.frames.size() > 0);

    // Verify frame fields are populated
    for (const auto& frame : sink.frames) {
        CHECK(frame.arbitration_id <= 0x7FF);
        CHECK(frame.dlc >= 1);
        CHECK(frame.dlc <= 8);
        CHECK(frame.type == FrameType::Standard);
        CHECK(frame.host_timestamp_us > 0);
    }

    // Session status should reflect received frames
    CHECK(session.status().frames_received > 0);
    CHECK(session.status().errors == 0);

    session.closeChannel();
    session.disconnect();
}

TEST_CASE("Mock capture: frames arrive in order", "[integration][capture]") {
    // Use a predefined sequence to verify ordering
    std::vector<CanFrame> sequence;
    for (uint32_t i = 0; i < 10; ++i) {
        CanFrame f;
        f.arbitration_id = 0x100 + i;
        f.dlc = 2;
        f.data[0] = static_cast<uint8_t>(i);
        f.data[1] = static_cast<uint8_t>(i * 2);
        f.type = FrameType::Standard;
        sequence.push_back(f);
    }

    SessionService session;
    auto provider = std::make_unique<MockProvider>();
    session.setProvider(std::move(provider));

    auto providers = session.scan();
    session.connect(providers.front());
    session.openChannel(500000);

    // Set the predefined sequence on the channel
    auto* channel = dynamic_cast<MockChannel*>(session.channel());
    REQUIRE(channel != nullptr);
    channel->set_frame_sequence(sequence);

    CaptureService capture;
    CollectorSink sink;
    capture.addSink(&sink);

    capture.start(session.channel(), session.mutableStatus());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    capture.drain();
    capture.stop();

    // Should have received frames, and IDs should follow the sequence pattern
    REQUIRE(sink.frames.size() >= 5);

    // Verify first few frames match sequence order (wraps around)
    for (size_t i = 0; i + 1 < sink.frames.size(); ++i) {
        uint32_t expected_this = 0x100 + (i % 10);
        CHECK(sink.frames[i].arbitration_id == expected_this);
    }

    session.closeChannel();
    session.disconnect();
}

TEST_CASE("Mock capture: filter integration", "[integration][capture]") {
    SessionService session;
    session.setProvider(std::make_unique<MockProvider>());

    auto providers = session.scan();
    session.connect(providers.front());
    session.openChannel(500000);

    // Set up filter to only pass ID 0x100
    FilterEngine filter;
    FilterRule rule;
    rule.action = FilterAction::Pass;
    rule.id_value = 0x100;
    rule.id_mask = 0xFFFFFFFF;
    filter.add_rule(rule);

    CaptureService capture;
    capture.setFilter(filter);

    CollectorSink sink;
    capture.addSink(&sink);

    capture.start(session.channel(), session.mutableStatus());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    capture.drain();
    capture.stop();

    // All displayed frames should match the filter
    for (const auto& frame : sink.frames) {
        CHECK(frame.arbitration_id == 0x100);
    }

    // But session counters should include all received frames (pre-filter)
    CHECK(session.status().frames_received > sink.frames.size());

    session.closeChannel();
    session.disconnect();
}

TEST_CASE("Mock capture: error injection stops capture", "[integration][capture]") {
    SessionService session;
    session.setProvider(std::make_unique<MockProvider>());

    auto providers = session.scan();
    session.connect(providers.front());
    session.openChannel(500000);

    // Inject error after 5 frames
    auto* channel = dynamic_cast<MockChannel*>(session.channel());
    REQUIRE(channel != nullptr);
    channel->set_error_after(5);

    CaptureService capture;
    CollectorSink sink;
    capture.addSink(&sink);

    capture.start(session.channel(), session.mutableStatus());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    capture.drain();
    capture.stop();

    // Should have received some frames before the error
    CHECK(session.status().frames_received > 0);
    // Error should have been counted
    CHECK(session.status().errors > 0);
    // Sink should have received the error
    CHECK(!sink.errors.empty());

    session.closeChannel();
    session.disconnect();
}

TEST_CASE("Mock capture: clean stop on request", "[integration][capture]") {
    SessionService session;
    session.setProvider(std::make_unique<MockProvider>());

    auto providers = session.scan();
    session.connect(providers.front());
    session.openChannel(500000);

    CaptureService capture;
    CollectorSink sink;
    capture.addSink(&sink);

    capture.start(session.channel(), session.mutableStatus());
    CHECK(capture.isRunning());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    capture.stop();

    CHECK(!capture.isRunning());

    // Should have collected at least some frames
    CHECK(session.status().frames_received > 0);
    CHECK(session.status().dropped == 0);

    session.closeChannel();
    session.disconnect();
}

TEST_CASE("SpscQueue: basic push/pop", "[integration][spsc]") {
    SpscQueue queue(16);

    CanFrame in;
    in.arbitration_id = 0x123;
    in.dlc = 4;

    CHECK(!queue.push(in)); // no drop
    CHECK(queue.size() == 1);

    CanFrame out;
    CHECK(queue.pop(out));
    CHECK(out.arbitration_id == 0x123);
    CHECK(out.dlc == 4);
    CHECK(queue.size() == 0);

    // Pop from empty queue
    CHECK(!queue.pop(out));
}

TEST_CASE("SpscQueue: overflow drops oldest", "[integration][spsc]") {
    SpscQueue queue(4); // capacity rounds to 4

    // Fill the queue
    for (uint32_t i = 0; i < 4; ++i) {
        CanFrame f;
        f.arbitration_id = i;
        CHECK(!queue.push(f));
    }

    // Overflow — should drop oldest
    CanFrame overflow;
    overflow.arbitration_id = 0xFF;
    CHECK(queue.push(overflow)); // returns true = dropped

    // Pop remaining — oldest (0) should have been dropped
    CanFrame out;
    CHECK(queue.pop(out));
    CHECK(out.arbitration_id == 1); // 0 was dropped

    // Continue popping
    CHECK(queue.pop(out));
    CHECK(out.arbitration_id == 2);
    CHECK(queue.pop(out));
    CHECK(out.arbitration_id == 3);
    CHECK(queue.pop(out));
    CHECK(out.arbitration_id == 0xFF);
    CHECK(!queue.pop(out)); // empty
}
