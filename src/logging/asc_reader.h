#pragma once

/// @file asc_reader.h
/// AscReader — parse Vector ASC format back into CanFrame stream (T048 — US5).

#include "logging/log_reader.h"

#include <fstream>
#include <string>

namespace canmatik {

/// Reads CAN frames from a Vector ASC text file.
/// Skips header lines, comments, and metadata; parses frame lines.
class AscReader : public ILogReader {
public:
    bool open(const std::string& path) override;
    std::optional<CanFrame> nextFrame() override;
    SessionStatus metadata() const override;
    void reset() override;

private:
    std::optional<CanFrame> parseLine(const std::string& line);

    std::ifstream file_;
    std::string path_;
    SessionStatus meta_;
    uint64_t frame_count_ = 0;
    bool header_parsed_ = false;
};

} // namespace canmatik
