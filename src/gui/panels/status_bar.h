#pragma once

/// @file status_bar.h
/// Bottom status bar rendering.

#include "gui/gui_state.h"
#include "gui/frame_collector.h"
#include "gui/controllers/capture_controller.h"

namespace canmatik {

void render_status_bar(const GuiState& state,
                       const CaptureController& capture,
                       const FrameCollector& collector);

} // namespace canmatik
