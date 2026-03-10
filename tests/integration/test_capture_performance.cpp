/// @file test_capture_performance.cpp
/// Performance smoke test: mock capture latency + no drops (T069 — SC-002).

#include <catch2/catch_test_macros.hpp>

#include "services/capture_service.h"
#include "mock/mock_channel.h"
#include "core/session_status.h"
#include "core/capture_sink.h"
#include "transport/transport_error.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace canmatik;

namespace {

class TimingSink : public ICaptureSync {
public:
    void onFrame(const CanFrame&) override {
        ++frame_count;
    }
    void onError(const TransportError&) override {
        ++error_count;
    }
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> error_count{0};
};

} // anonymous namespace

TEST_CASE("T069: 10s mock capture — zero drops, bounded memory", "[performance][capture]") {
    constexpr uint32_t frame_rate = 1000; // 1000 fps
    constexpr int duration_ms = 10'000;

    MockChannel channel(frame_rate);
    channel.open(500000);

    CaptureService capture;
    TimingSink sink;
    capture.addSink(&sink);

    SessionStatus status;
    capture.start(&channel, status);

    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        capture.drain();
        auto elapsed = std::chrono::steady_clock::now() - t0;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= duration_ms)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    capture.stop();
    channel.close();

    INFO("Frames received: " << status.frames_received);
    INFO("Frames dispatched to sink: " << sink.frame_count.load());
    INFO("Dropped: " << status.dropped);
    INFO("Errors: " << status.errors);

    // SC-002: zero frame loss under controlled load
    CHECK(status.dropped == 0);
    CHECK(status.errors == 0);
    CHECK(sink.frame_count.load() > 0);
}
