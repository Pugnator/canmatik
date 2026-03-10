#pragma once

/// @file capture_sink.h
/// ICaptureSync observer interface for frame dispatch.

#include "core/can_frame.h"
#include "transport/transport_error.h"

namespace canmatik {

/// Observer interface: consumers register to receive frames and errors.
class ICaptureSync {
public:
    virtual ~ICaptureSync() = default;

    /// Called when a new frame is received from the transport layer.
    virtual void onFrame(const CanFrame& frame) = 0;

    /// Called when the transport layer reports an error.
    virtual void onError(const TransportError& error) = 0;
};

} // namespace canmatik
