/// @file record_service.cpp
/// RecordService implementation (T044 — US4).

#include "services/record_service.h"
#include "logging/asc_writer.h"
#include "logging/jsonl_writer.h"
#include "core/log_macros.h"

namespace canmatik {

RecordService::~RecordService() {
    // If still recording, write footer with default status.
    if (recording_) {
        SessionStatus empty;
        stop(empty);
    }
}

bool RecordService::start(const std::string& path, LogFormat format,
                          const SessionStatus& status) {
    if (recording_) {
        LOG_WARNING("RecordService::start called while already recording to {}", path_);
        return false;
    }

    try {
        switch (format) {
            case LogFormat::ASC:
                writer_ = std::make_unique<AscWriter>(path);
                break;
            case LogFormat::JSONL:
                writer_ = std::make_unique<JsonlWriter>(path);
                break;
            default:
                LOG_ERROR("Unsupported log format for recording");
                return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to open recording file {}: {}", path, e.what());
        return false;
    }

    path_ = path;
    frame_count_ = 0;
    write_failed_ = false;
    recording_ = true;

    writer_->writeHeader(status);
    writer_->flush();

    LOG_INFO("Recording started: {} (format={})", path,
             log_format_extension(format));
    return true;
}

void RecordService::stop(const SessionStatus& status) {
    if (!recording_) return;

    writer_->writeFooter(status);
    writer_->flush();
    writer_.reset();
    recording_ = false;

    LOG_INFO("Recording stopped: {} ({} frames)", path_, frame_count_);
}

void RecordService::onFrame(const CanFrame& frame) {
    if (!recording_ || !writer_) return;
    try {
        writer_->writeFrame(frame);
        ++frame_count_;
    } catch (const std::exception& e) {
        LOG_ERROR("Write failure during recording: {}", e.what());
        // Stop recording gracefully — finalize the log so data is not lost
        write_failed_ = true;
        recording_ = false;
    }
}

void RecordService::onError(const TransportError& error) {
    if (!recording_) return;
    LOG_WARNING("RecordService transport error during recording: {}", error.what());
}

} // namespace canmatik
