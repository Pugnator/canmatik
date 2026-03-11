#pragma once

/// @file playback_toolbar.h
/// Reusable playback toolbar widget: Play/Pause/Stop/Rewind/FF/Loop.

#include "gui/gui_state.h"

namespace canmatik {

/// Render playback controls. Returns the action the user triggered (NONE if no click).
PlaybackAction render_playback_toolbar(PlaybackState playback,
                                       DataSource data_source,
                                       bool loop,
                                       float speed);

} // namespace canmatik
