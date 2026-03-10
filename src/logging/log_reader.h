#pragma once

/// @file log_reader.h
/// ILogReader interface (Phase 7: US5 — T047).

#include "core/can_frame.h"
#include "core/session_status.h"

#include <optional>
#include <string>

namespace canmatik {

class ILogReader {
public:
    virtual ~ILogReader() = default;
    virtual bool open(const std::string& path) = 0;
    virtual std::optional<CanFrame> nextFrame() = 0;
    virtual SessionStatus metadata() const = 0;
    virtual void reset() = 0;
};

} // namespace canmatik
