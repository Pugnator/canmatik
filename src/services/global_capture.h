#pragma once

/// @file global_capture.h
/// Small helper wrapper exposing a shared CaptureService instance so multiple
/// components (GUI, bridge, recorders) can register sinks and reuse a single
/// reader thread.

#include "services/capture_service.h"

namespace canmatik {

/// Add a sink to the global capture service.
void AddGlobalSink(ICaptureSync* sink);

/// Remove a previously added sink.
void RemoveGlobalSink(ICaptureSync* sink);

/// Start the global capture service reading from the given channel. If the
/// service is already running this is a no-op.
void StartGlobalCapture(IChannel* channel, SessionStatus& status);

/// Stop the global capture service (if running).
void StopGlobalCapture();

/// Force-stop the global capture service even if sinks remain. Use with care.
void StopGlobalCaptureForced();

/// Drain the global capture queue (dispatch to sinks). Call from the
/// consumer thread when appropriate.
void DrainGlobalCapture();

/// Set the display filter used by the global capture service.
void SetGlobalFilter(const FilterEngine& filter);

/// Returns whether the global capture service is running.
bool IsGlobalCaptureRunning();

} // namespace canmatik
