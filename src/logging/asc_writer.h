#pragma once

/// @file asc_writer.h
/// AscWriter — writes CAN frames in Vector ASC text format (T040 — US4).

#include "logging/log_writer.h"

#include <ostream>
#include <memory>
#include <string>
#include <cstdint>

namespace canmatik {

/// Writes CAN frames in Vector ASC format.
/// The ASC file starts with a version comment and metadata header,
/// followed by one frame per line with timestamps, IDs, DLC, and payload.
class AscWriter : public ILogWriter {
public:
    /// Construct with an existing output stream (caller owns the stream).
    explicit AscWriter(std::ostream& os);

    /// Construct and open a file at the given path.
    explicit AscWriter(const std::string& path);

    void writeHeader(const SessionStatus& status) override;
    void writeFrame(const CanFrame& frame) override;
    void writeFooter(const SessionStatus& status) override;
    void flush() override;

    [[nodiscard]] uint64_t frameCount() const { return frame_count_; }

private:
    std::ostream* os_ = nullptr;
    std::unique_ptr<std::ostream> owned_os_;
    uint64_t frame_count_ = 0;
    uint64_t session_start_us_ = 0;
};

} // namespace canmatik
