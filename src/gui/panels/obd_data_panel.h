#pragma once

/// @file obd_data_panel.h
/// OBD Data tab rendering: decoded PID table + history graph.

#include "gui/controllers/obd_controller.h"
#include "gui/gui_settings.h"

#include <vector>

namespace canmatik {

void render_obd_data_panel(const std::vector<ObdPidRow>& rows, const GuiSettings& settings,
                          bool show_graph = true);

} // namespace canmatik
