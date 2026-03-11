#pragma once

/// @file menu_bar.h
/// Main menu bar: File, View, Help.

#include "gui/gui_state.h"

#include <functional>

namespace canmatik {

/// Render the main menu bar. Callbacks for file open and save.
void render_menu_bar(GuiState& state, bool& show_watchdog,
                     std::function<void()> on_open_file,
                     std::function<void()> on_save_buffer);

} // namespace canmatik
