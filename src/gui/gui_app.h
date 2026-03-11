#pragma once

/// @file gui_app.h
/// Top-level GUI application class owning state, settings, controllers, and panels.

#include "gui/gui_state.h"
#include "gui/gui_settings.h"
#include "gui/frame_collector.h"
#include "gui/controllers/capture_controller.h"
#include "gui/controllers/replay_controller.h"
#include "gui/controllers/obd_controller.h"
#include "gui/panels/dtc_panel.h"
#include "core/timestamp.h"

#include <cstdint>
#include <memory>
#include <string>

namespace canmatik {

class GuiApp {
public:
    GuiApp();

    /// Load settings from disk.
    void init(const std::string& settings_path);

    /// Render one full ImGui frame (tab bar + all panels + status bar).
    void render();

    /// Save settings to disk + cleanup.
    void shutdown();

    /// Apply the current color scheme from settings.
    void apply_color_scheme();

    // Accessors for gui_main.cpp
    GuiSettings& settings() { return settings_; }
    const GuiSettings& settings() const { return settings_; }
    int window_width() const  { return settings_.window_width; }
    int window_height() const { return settings_.window_height; }

private:
    void render_can_tab();
    void render_obd_tab();
    void render_dtc_tab();
    void render_settings_tab();
    void handle_playback_action(PlaybackAction action);
    void handle_keyboard_shortcuts();
    void open_file_dialog();
    void save_buffer_dialog();
    void show_error_popup();

    GuiSettings       settings_;
    GuiState          state_;
    std::string       settings_path_;

    FrameCollector    collector_;
    CaptureController capture_;
    ReplayController  replay_;
    ObdController     obd_;
    DtcPanelState     dtc_state_;

    uint64_t          last_tick_us_ = 0;
    bool              show_watchdog_ = true;
    float             change_flash_end_ = 0.0f; // for value flash timing
};

} // namespace canmatik
