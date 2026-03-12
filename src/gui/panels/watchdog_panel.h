#pragma once

/// @file watchdog_panel.h
/// Watchdog sub-panel showing watched CAN IDs with decode configuration and history.

#include "gui/frame_collector.h"
#include "gui/gui_settings.h"

#include <vector>

namespace canmatik {

/// Render the enhanced watchdog panel.
void render_watchdog_panel(const std::vector<WatchdogSnapshot>& snapshots,
                           FrameCollector& collector,
                           const GuiSettings& settings);

} // namespace canmatik
