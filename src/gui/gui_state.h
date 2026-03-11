#pragma once

/// @file gui_state.h
/// Runtime GUI state enums and aggregate.

#include <cstdint>
#include <string>

namespace canmatik {

enum class DataSource : uint8_t {
    LIVE = 0,
    FILE = 1,
};

enum class PlaybackState : uint8_t {
    STOPPED = 0,
    PLAYING = 1,
    PAUSED  = 2,
};

enum class ObdDisplayMode : uint8_t {
    OBD_AND_BROADCAST = 0,
    OBD_ONLY          = 1,
    BROADCAST_ONLY    = 2,
};

/// Actions produced by the playback toolbar.
/// Color scheme for the GUI.
enum class ColorScheme : uint8_t {
    DARK      = 0,
    LIGHT     = 1,
    RETRO     = 2, // old-school green-on-black
};

/// ID filter mode for the CAN messages table.
enum class IdFilterMode : uint8_t {
    EXCLUDE = 0,  // hide listed IDs (nothing listed = show all)
    INCLUDE = 1,  // show only listed IDs (nothing listed = show all)
};

enum class PlaybackAction : uint8_t {
    NONE        = 0,
    PLAY        = 1,
    PAUSE       = 2,
    STOP        = 3,
    REWIND      = 4,
    FAST_FORWARD = 5,
    TOGGLE_LOOP = 6,
};

/// Mutable runtime state (not persisted).
struct GuiState {
    DataSource    data_source   = DataSource::LIVE;
    PlaybackState playback      = PlaybackState::STOPPED;
    bool          connected     = false;
    bool          loop          = false;
    float         speed_mult    = 1.0f;
    int           selected_tab  = 0;           // 0=CAN, 1=OBD, 2=Settings
    uint32_t      selected_row  = 0xFFFFFFFF;  // 0xFFFFFFFF = none
    std::string   open_file;                   // path for FILE data source
    std::string   error_message;               // shown in popup if non-empty
    uint64_t      total_frames  = 0;
    uint64_t      total_errors  = 0;
    uint64_t      buffer_used   = 0;
};

} // namespace canmatik
