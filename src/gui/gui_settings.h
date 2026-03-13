#pragma once

/// @file gui_settings.h
/// Persisted GUI settings (canmatik_gui.json).

#include "gui/gui_state.h"
#include "core/can_frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace canmatik {

struct GuiSettings {
    // Connection
    std::string provider;
    uint32_t    bitrate         = 500000;
    bool        mock_enabled    = false;
    BusProtocol bus_protocol    = BusProtocol::CAN;

    // Proxy mode: expose fake J2534 DLL, forward to real adapter
    bool        proxy_enabled   = false;
    bool        proxy_terminated = false;  ///< true = terminated mode (no real adapter)
    std::string proxy_target;              ///< real J2534 provider to forward to

    // Capture buffer
    uint32_t    buffer_capacity  = 100000;
    bool        buffer_overwrite = false;   ///< true = ring-buffer wraps; false = stop when full

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

    // Watchdog
    uint32_t    watchdog_history_size = 200;  ///< Max decoded value samples per watched ID

    // Font scale (multiplier: 0.5 – 3.0, default 1.0)
    float font_scale_can = 1.0f;  ///< Bus Messages panel font scale
    float font_scale_obd = 1.0f;  ///< OBD Data panel font scale

    // Font colors (RGBA, 0.0–1.0)
    // CAN messages panel
    float color_can_new[4]      = {0.3f, 1.0f, 0.3f, 1.0f};  ///< New ID (green)
    float color_can_changed[4]  = {1.0f, 0.3f, 0.3f, 1.0f};  ///< Changed byte (red)
    float color_can_dlc[4]      = {1.0f, 0.9f, 0.2f, 1.0f};  ///< DLC changed (yellow)
    float color_can_default[4]  = {0.9f, 0.9f, 0.9f, 1.0f};  ///< Normal text
    float color_can_watched[4]  = {0.4f, 0.8f, 1.0f, 1.0f};  ///< Watched (cyan)
    // OBD data panel
    float color_obd_changed[4]  = {0.3f, 1.0f, 0.3f, 1.0f};  ///< Value changed (green)
    float color_obd_normal[4]   = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Normal text
    float color_obd_dim[4]      = {0.6f, 0.6f, 0.7f, 1.0f};  ///< Dim/raw text

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
