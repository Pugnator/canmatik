#pragma once

/// @file obd_data_panel.h
/// OBD Data tab rendering: decoded PID table + history graph.

#include "gui/controllers/obd_controller.h"

#include <vector>

namespace canmatik {

void render_obd_data_panel(const std::vector<ObdPidRow>& rows);

} // namespace canmatik
