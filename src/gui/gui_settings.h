#pragma once

/// @file gui_settings.h
/// Persisted GUI settings (canmatik_gui.json).

#include "gui/gui_state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace canmatik {

struct GuiSettings {
    // Connection
    std::string provider;
    uint32_t    bitrate         = 500000;
    bool        mock_enabled    = false;

    // Capture buffer
    uint32_t    buffer_capacity = 100000;

    // Change filter
    uint32_t    change_filter_n = 1;
    bool        show_changed_only = false;

    // ID filter
    IdFilterMode id_filter_mode = IdFilterMode::EXCLUDE;
    std::vector<uint32_t> id_filter_list;  // arb IDs to include/exclude

    // OBD
    ObdDisplayMode obd_mode       = ObdDisplayMode::OBD_AND_BROADCAST;
    std::vector<uint8_t> obd_pids = {0x0C, 0x0D, 0x05};
    uint32_t    obd_interval_ms   = 500;

    // Appearance
    ColorScheme color_scheme    = ColorScheme::DARK;

    // Window
    int         window_width    = 1024;
    int         window_height   = 720;
    std::string last_file_path;

    /// Load settings from a JSON file. Returns empty string on success,
    /// error description on failure (file keeps defaults).
    std::string load(const std::string& path);

    /// Save settings to a JSON file. Returns empty string on success.
    std::string save(const std::string& path) const;
};

} // namespace canmatik
