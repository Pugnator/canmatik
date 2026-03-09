#pragma once

/// @file log_writer.h
/// ILogWriter interface (Phase 6: US4 — T039).

#include "core/can_frame.h"
#include "core/session_status.h"

namespace canmatik {

class ILogWriter {
public:
    virtual ~ILogWriter() = default;
    virtual void writeHeader(const SessionStatus& status) = 0;
    virtual void writeFrame(const CanFrame& frame) = 0;
    virtual void writeFooter(const SessionStatus& status) = 0;
    virtual void flush() = 0;
};

} // namespace canmatik
