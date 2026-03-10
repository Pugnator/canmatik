#pragma once

/// @file jsonl_reader.h
/// JsonlReader — parse JSON Lines format back into CanFrame stream (T049 — US5).

#include "logging/log_reader.h"

#include <fstream>
#include <string>

namespace canmatik {

/// Reads CAN frames from a JSON Lines (.jsonl) file.
/// First line with `_meta` is parsed as header metadata.
/// Lines with `type: session_summary` are parsed as footer.
/// All other lines are parsed as frame objects.
class JsonlReader : public ILogReader {
public:
    bool open(const std::string& path) override;
    std::optional<CanFrame> nextFrame() override;
    SessionStatus metadata() const override;
    void reset() override;

private:
    std::ifstream file_;
    std::string path_;
    SessionStatus meta_;
    uint64_t frame_count_ = 0;
};

} // namespace canmatik
