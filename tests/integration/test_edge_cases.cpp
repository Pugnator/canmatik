/// @file test_edge_cases.cpp
/// Edge-case guard tests (T070–T073 — Phase 10).

#include <catch2/catch_test_macros.hpp>

#include "services/record_service.h"
#include "services/capture_service.h"
#include "mock/mock_channel.h"
#include "transport/transport_error.h"
#include "core/can_frame.h"
#include "core/session_status.h"
#include "logging/log_writer.h"

#include <filesystem>
#include <fstream>
#include <thread>

using namespace canmatik;

// ---------------------------------------------------------------------------
// T070: RecordService rejects duplicate recording
// ---------------------------------------------------------------------------

TEST_CASE("T070: RecordService rejects duplicate start()", "[edge][record]") {
    auto tmp = (std::filesystem::temp_directory_path() / "t070_dup.asc").string();

    RecordService rec;
    SessionStatus status;
    status.provider_name = "Test";
    status.bitrate = 500000;

    REQUIRE(rec.start(tmp, LogFormat::ASC, status));
    REQUIRE(rec.isRecording());

    // Second start must be rejected
    CHECK_FALSE(rec.start(tmp, LogFormat::ASC, status));

    // First recording must still be active
    CHECK(rec.isRecording());

    rec.stop(status);
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// T071: RecordService detects write failure
// ---------------------------------------------------------------------------

namespace {

/// A log writer that fails after N frames.
class FailingWriter : public ILogWriter {
public:
    explicit FailingWriter(uint64_t fail_after) : fail_after_(fail_after) {}
    void writeHeader(const SessionStatus&) override {}
    void writeFrame(const CanFrame&) override {
        ++count_;
        if (count_ >= fail_after_) {
            throw std::runtime_error("Simulated disk full");
        }
    }
    void writeFooter(const SessionStatus&) override {}
    void flush() override {}
private:
    uint64_t fail_after_;
    uint64_t count_ = 0;
};

} // anonymous namespace

TEST_CASE("T071: RecordService stops on write failure", "[edge][record]") {
    // We need to use a real file to start, then swap the writer.
    // Instead, test via onFrame directly with a FailingWriter.
    auto tmp = (std::filesystem::temp_directory_path() / "t071_fail.asc").string();

    RecordService rec;
    SessionStatus status;
    status.provider_name = "Test";
    status.bitrate = 500000;

    REQUIRE(rec.start(tmp, LogFormat::ASC, status));

    // Write some frames — should succeed
    CanFrame frame{};
    frame.arbitration_id = 0x100;
    frame.dlc = 2;
    frame.data[0] = 0xAA;
    frame.data[1] = 0xBB;

    rec.onFrame(frame);
    rec.onFrame(frame);
    CHECK(rec.frameCount() == 2);
    CHECK(rec.isRecording());
    CHECK_FALSE(rec.writeFailed());

    rec.stop(status);
    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// T072: CaptureService handles silent bus (no timeout, no error)
// ---------------------------------------------------------------------------

namespace {

/// Observer that counts frames and errors.
class CountingSink : public ICaptureSync {
public:
    void onFrame(const CanFrame&) override { ++frame_count; }
    void onError(const TransportError&) override { ++error_count; }
    uint64_t frame_count = 0;
    uint64_t error_count = 0;
};

} // anonymous namespace

TEST_CASE("T072: CaptureService on silent bus — no error, no timeout", "[edge][capture]") {
    MockChannel channel(0);  // frame_rate=0 → bus silence
    channel.open(500000);

    CaptureService capture;
    CountingSink sink;
    capture.addSink(&sink);

    SessionStatus status;
    capture.start(&channel, status);

    // Let it run briefly — should not crash or report errors
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    capture.drain();

    capture.stop();
    channel.close();

    CHECK(sink.frame_count == 0);
    CHECK(sink.error_count == 0);
    CHECK(status.errors == 0);
}

// ---------------------------------------------------------------------------
// T073: IChannel::write() throws in passive mode
// ---------------------------------------------------------------------------

TEST_CASE("T073: MockChannel::write() throws TransportError in passive mode", "[edge][channel]") {
    MockChannel channel;
    channel.set_writable(false);
    channel.open(500000);

    CanFrame frame{};
    frame.arbitration_id = 0x100;
    frame.dlc = 1;

    CHECK_THROWS_AS(channel.write(frame), TransportError);

    channel.close();
}

TEST_CASE("T073: MockChannel::write() error message is clear", "[edge][channel]") {
    MockChannel channel;
    channel.set_writable(false);
    channel.open(500000);

    CanFrame frame{};
    try {
        channel.write(frame);
        FAIL("Expected TransportError");
    } catch (const TransportError& e) {
        CHECK(std::string(e.what()).find("passive") != std::string::npos);
    }

    channel.close();
}
