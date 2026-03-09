#pragma once

/// @file record_service.h
/// RecordService — writes captured frames to a log file (T044 — US4).

#include "core/capture_sink.h"
#include "core/session_status.h"
#include "config/config.h"
#include "logging/log_writer.h"

#include <memory>
#include <string>
#include <cstdint>

namespace canmatik {

/// RecordService acts as an ICaptureSync observer: each received frame is
/// forwarded to the active ILogWriter.  The caller manages the capture
/// lifecycle; RecordService only owns the writer and file.
class RecordService : public ICaptureSync {
public:
    RecordService() = default;
    ~RecordService() override;

    // Non-copyable
    RecordService(const RecordService&) = delete;
    RecordService& operator=(const RecordService&) = delete;

    /// Start recording to the given path in the specified format.
    /// Writes the file header immediately.
    /// @return true on success, false if the file cannot be opened.
    bool start(const std::string& path, LogFormat format, const SessionStatus& status);

    /// Stop recording: write footer and close the file.
    void stop(const SessionStatus& status);

    /// Whether recording is currently active.
    [[nodiscard]] bool isRecording() const { return recording_; }

    /// The file path being recorded to.
    [[nodiscard]] const std::string& path() const { return path_; }

    /// Number of frames written so far.
    [[nodiscard]] uint64_t frameCount() const { return frame_count_; }

    /// Whether recording stopped due to a write failure (e.g., disk full).
    [[nodiscard]] bool writeFailed() const { return write_failed_; }

    // ICaptureSync overrides
    void onFrame(const CanFrame& frame) override;
    void onError(const TransportError& error) override;

private:
    std::unique_ptr<ILogWriter> writer_;
    std::string path_;
    bool recording_ = false;
    bool write_failed_ = false;
    uint64_t frame_count_ = 0;
};

} // namespace canmatik
