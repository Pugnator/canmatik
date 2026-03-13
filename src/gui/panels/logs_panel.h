#pragma once

/// @file logs_panel.h
/// Session logs panel rendering.

#include "gui/gui_log_sink.h"

namespace canmatik {

/// Render the session logs panel (auto-scrolling table of log entries).
void render_logs_panel(GuiLogSink& sink);

} // namespace canmatik
