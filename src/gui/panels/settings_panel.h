#pragma once

/// @file settings_panel.h
/// Settings tab rendering.

#include "gui/gui_settings.h"
#include "gui/gui_state.h"
#include "gui/frame_collector.h"
#include "gui/controllers/capture_controller.h"
#include "gui/controllers/obd_controller.h"

namespace canmatik {

/// Render the Settings tab.
/// @param scheme_changed set to true if the user changed the color scheme this frame.
void render_settings_panel(GuiSettings& settings, GuiState& state,
                           CaptureController& capture,
                           FrameCollector& collector,
                           ObdController& obd,
                           bool& scheme_changed);

} // namespace canmatik
