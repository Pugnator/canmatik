#pragma once

/// @file watchdog_panel.h
/// Watchdog sub-panel showing only watched CAN IDs.

#include "gui/frame_collector.h"

#include <vector>

namespace canmatik {

void render_watchdog_panel(const std::vector<MessageRow>& watched_rows,
                           FrameCollector& collector);

} // namespace canmatik
