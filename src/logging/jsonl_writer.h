#pragma once

/// @file jsonl_writer.h
/// JsonlWriter — writes CAN frames as JSON Lines (T041 — US4).

#include "logging/log_writer.h"

#include <ostream>
#include <memory>
#include <string>
#include <cstdint>

namespace canmatik {

/// Writes CAN frames as JSON Lines (one JSON object per line).
/// First line is a metadata header object; subsequent lines are frame objects.
class JsonlWriter : public ILogWriter {
public:
    /// Construct with an existing output stream (caller owns the stream).
    explicit JsonlWriter(std::ostream& os);

    /// Construct and open a file at the given path.
    explicit JsonlWriter(const std::string& path);

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
