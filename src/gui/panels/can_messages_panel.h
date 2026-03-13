#pragma once

/// @file can_messages_panel.h
/// CAN Messages table rendering with byte-level diff highlighting.

#include "gui/frame_collector.h"
#include "gui/gui_settings.h"
#include "gui/gui_state.h"

#include <vector>

namespace canmatik {

/// Render the CAN messages table. Handles row selection and right-click context menu.
/// When raw_stream is true, rows are displayed as a chronological log instead of
/// grouped-by-ID with diff highlighting.
void render_can_messages_panel(const std::vector<MessageRow>& rows,
                               GuiState& state,
                               FrameCollector& collector,
                               GuiSettings& settings,
                               bool show_graph = true,
                               bool raw_stream = false);

} // namespace canmatik
