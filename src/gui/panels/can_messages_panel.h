#pragma once

/// @file can_messages_panel.h
/// CAN Messages table rendering with byte-level diff highlighting.

#include "gui/frame_collector.h"
#include "gui/gui_settings.h"
#include "gui/gui_state.h"

#include <vector>

namespace canmatik {

/// Render the CAN messages table. Handles row selection and right-click context menu.
void render_can_messages_panel(const std::vector<MessageRow>& rows,
                               GuiState& state,
                               FrameCollector& collector,
                               GuiSettings& settings,
                               bool show_graph = true);

} // namespace canmatik
