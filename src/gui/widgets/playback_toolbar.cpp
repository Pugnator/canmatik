/// @file playback_toolbar.cpp
/// Playback toolbar widget implementation.

#include "gui/widgets/playback_toolbar.h"
#include "imgui.h"

#include <format>

namespace canmatik {

PlaybackAction render_playback_toolbar(PlaybackState playback,
                                       DataSource data_source,
                                       bool loop,
                                       float speed) {
    PlaybackAction action = PlaybackAction::NONE;
    bool is_file = (data_source == DataSource::FILE);

    // Play
    bool can_play = (playback == PlaybackState::STOPPED || playback == PlaybackState::PAUSED);
    ImGui::BeginDisabled(!can_play);
    if (ImGui::Button("Play"))
        action = PlaybackAction::PLAY;
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Pause
    bool can_pause = (playback == PlaybackState::PLAYING);
    ImGui::BeginDisabled(!can_pause);
    if (ImGui::Button("Pause"))
        action = PlaybackAction::PAUSE;
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Stop
    bool can_stop = (playback != PlaybackState::STOPPED);
    ImGui::BeginDisabled(!can_stop);
    if (ImGui::Button("Stop"))
        action = PlaybackAction::STOP;
    ImGui::EndDisabled();

    if (is_file) {
        ImGui::SameLine();
        // Rewind
        if (ImGui::Button("Rewind"))
            action = PlaybackAction::REWIND;

        ImGui::SameLine();
        // Fast-forward
        std::string ff_label = std::format("FF ({}x)", static_cast<int>(speed));
        if (ImGui::Button(ff_label.c_str()))
            action = PlaybackAction::FAST_FORWARD;

        ImGui::SameLine();
        // Loop toggle
        bool lp = loop;
        if (ImGui::Checkbox("Loop", &lp))
            action = PlaybackAction::TOGGLE_LOOP;
    }

    return action;
}

} // namespace canmatik
