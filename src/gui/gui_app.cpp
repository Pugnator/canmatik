/// @file gui_app.cpp
/// Top-level GUI application implementation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include "gui/gui_app.h"
#include "gui/panels/can_messages_panel.h"
#include "gui/panels/obd_data_panel.h"
#include "gui/panels/dtc_panel.h"
#include "gui/panels/settings_panel.h"
#include "gui/panels/status_bar.h"
#include "gui/panels/watchdog_panel.h"
#include "gui/panels/menu_bar.h"
#include "gui/widgets/playback_toolbar.h"
#include "logging/asc_writer.h"
#include "logging/jsonl_writer.h"
#include "core/session_status.h"

#include "imgui.h"

#include <algorithm>
#include <format>

namespace canmatik {

GuiApp::GuiApp()
    : collector_(host_timestamp_us(), 100000) {}

void GuiApp::init(const std::string& settings_path) {
    settings_path_ = settings_path;
    settings_.load(settings_path_);
    collector_.resize_buffer(settings_.buffer_capacity);
    last_tick_us_ = host_timestamp_us();
}

void GuiApp::apply_color_scheme() {
    switch (settings_.color_scheme) {
    case ColorScheme::LIGHT: ImGui::StyleColorsLight(); break;
    case ColorScheme::RETRO: ImGui::StyleColorsClassic(); break;
    default:                 ImGui::StyleColorsDark();  break;
    }
}

void GuiApp::shutdown() {
    obd_.stop_streaming();
    capture_.stop();
    capture_.disconnect();
    settings_.save(settings_path_);
}

void GuiApp::render() {
    // Time delta for replay tick
    uint64_t now = host_timestamp_us();
    uint64_t delta = now - last_tick_us_;
    last_tick_us_ = now;

    // Drain live capture frames
    if (state_.data_source == DataSource::LIVE && capture_.is_capturing())
        capture_.drain();

    // Advance replay
    if (state_.data_source == DataSource::FILE && replay_.is_playing())
        replay_.tick(delta, collector_);

    // Sync playback state when replay finishes on its own
    if (state_.data_source == DataSource::FILE &&
        state_.playback == PlaybackState::PLAYING &&
        !replay_.is_playing()) {
        state_.playback = PlaybackState::STOPPED;
    }

    // Update state counters
    state_.buffer_used  = collector_.buffer_count();
    state_.total_frames = collector_.buffer_count();

    handle_keyboard_shortcuts();

    // Menu bar
    render_menu_bar(state_, show_watchdog_, [this]{ open_file_dialog(); },
                    [this]{ save_buffer_dialog(); });

    // Fullscreen dockspace
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - 28.0f));
    ImGui::Begin("##MainArea", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("CAN Messages")) {
            state_.selected_tab = 0;
            render_can_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("OBD Data")) {
            state_.selected_tab = 1;
            render_obd_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("DTC")) {
            state_.selected_tab = 2;
            render_dtc_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            state_.selected_tab = 3;
            render_settings_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    // Status bar
    render_status_bar(state_, capture_, collector_);

    // Error popups
    show_error_popup();
}

// -----------------------------------------------------------------------
// CAN Messages tab
// -----------------------------------------------------------------------
void GuiApp::render_can_tab() {
    // Connect / Disconnect button
    if (!capture_.is_connected()) {
        if (ImGui::Button("Connect")) {
            auto err = capture_.connect(settings_.provider, settings_.bitrate,
                                       settings_.mock_enabled, collector_);
            if (err.empty()) {
                state_.connected = true;
            } else {
                state_.error_message = err;
            }
        }
    } else {
        if (ImGui::Button("Disconnect")) {
            obd_.stop_streaming();
            capture_.stop();
            capture_.disconnect();
            state_.connected = false;
            state_.playback  = PlaybackState::STOPPED;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled(capture_.is_connected() ? "Connected" : "Disconnected");
    ImGui::SameLine();

    // Data source selector + playback toolbar
    int src = static_cast<int>(state_.data_source);
    ImGui::RadioButton("Live", &src, 0); ImGui::SameLine();
    ImGui::RadioButton("File", &src, 1); ImGui::SameLine();
    state_.data_source = static_cast<DataSource>(src);

    // Playback toolbar
    PlaybackAction action = render_playback_toolbar(
        state_.playback, state_.data_source, state_.loop, replay_.speed());
    if (action != PlaybackAction::NONE)
        handle_playback_action(action);

    ImGui::SameLine();
    if (ImGui::Button("Save Buffer"))
        save_buffer_dialog();

    if (state_.data_source == DataSource::FILE) {
        ImGui::SameLine();
        if (ImGui::Button("Open File"))
            open_file_dialog();
    }

    ImGui::Separator();

    // Filter controls
    ImGui::Checkbox("Changed only", &settings_.show_changed_only);
    ImGui::SameLine();
    int n = static_cast<int>(settings_.change_filter_n);
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("Last N", &n))
        settings_.change_filter_n = static_cast<uint32_t>(std::clamp(n, 1, 100));

    // ID filter controls
    ImGui::SameLine();
    {
        int fm = static_cast<int>(settings_.id_filter_mode);
        ImGui::RadioButton("Exclude IDs", &fm, 0); ImGui::SameLine();
        ImGui::RadioButton("Include IDs", &fm, 1);
        settings_.id_filter_mode = static_cast<IdFilterMode>(fm);
        if (!settings_.id_filter_list.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d IDs)", static_cast<int>(settings_.id_filter_list.size()));
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Filter"))
                settings_.id_filter_list.clear();
        }
    }

    ImGui::Separator();

    // Message table
    auto rows = collector_.snapshot(settings_.show_changed_only,
                                    settings_.change_filter_n,
                                    settings_.obd_mode,
                                    settings_.id_filter_mode,
                                    settings_.id_filter_list);
    render_can_messages_panel(rows, state_, collector_, settings_);

    // Watchdog panel
    if (show_watchdog_) {
        auto watched = collector_.watchdog_snapshot();
        if (!watched.empty()) {
            ImGui::Separator();
            render_watchdog_panel(watched, collector_);
        }
    }
}

// -----------------------------------------------------------------------
// OBD Data tab
// -----------------------------------------------------------------------
void GuiApp::render_obd_tab() {
    // OBD start / stop
    if (!capture_.is_connected()) {
        ImGui::TextDisabled("Not connected.");
    } else {
        if (!obd_.is_streaming()) {
            if (ImGui::Button("Start OBD")) {
                auto* ch = capture_.channel();
                if (ch)
                    obd_.start_streaming(ch, settings_.obd_pids,
                                         settings_.obd_interval_ms, &collector_);
            }
        } else {
            if (ImGui::Button("Stop OBD"))
                obd_.stop_streaming();
        }
    }

    ImGui::SameLine();

    // OBD display filter
    const char* obd_modes[] = { "OBD + Broadcast", "OBD Only", "Broadcast Only" };
    int obd_sel = static_cast<int>(settings_.obd_mode);
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("Display Filter", &obd_sel, obd_modes, 3))
        settings_.obd_mode = static_cast<ObdDisplayMode>(obd_sel);

    ImGui::Separator();

    auto obd_rows = obd_.snapshot();
    render_obd_data_panel(obd_rows);
}

// -----------------------------------------------------------------------
// DTC tab
// -----------------------------------------------------------------------
void GuiApp::render_dtc_tab() {
    render_dtc_panel(dtc_state_, capture_, state_);
}

// -----------------------------------------------------------------------
// Settings tab
// -----------------------------------------------------------------------
void GuiApp::render_settings_tab() {
    bool scheme_changed = false;
    render_settings_panel(settings_, state_, capture_, collector_, obd_, scheme_changed);
    if (scheme_changed) apply_color_scheme();
}

// -----------------------------------------------------------------------
// Playback action dispatch
// -----------------------------------------------------------------------
void GuiApp::handle_playback_action(PlaybackAction action) {
    switch (action) {
    case PlaybackAction::PLAY:
        if (state_.data_source == DataSource::LIVE) {
            if (!capture_.is_capturing()) {
                auto err = capture_.start(collector_);
                if (!err.empty()) { state_.error_message = err; return; }
            } else {
                capture_.resume();
            }
            state_.playback = PlaybackState::PLAYING;
        } else {
            replay_.play();
            state_.playback = PlaybackState::PLAYING;
        }
        break;
    case PlaybackAction::PAUSE:
        if (state_.data_source == DataSource::LIVE) capture_.pause();
        else replay_.pause();
        state_.playback = PlaybackState::PAUSED;
        break;
    case PlaybackAction::STOP:
        if (state_.data_source == DataSource::LIVE) {
            capture_.stop();
        } else {
            replay_.stop();
        }
        state_.playback = PlaybackState::STOPPED;
        collector_.clear();
        break;
    case PlaybackAction::REWIND:
        replay_.rewind();
        collector_.clear();
        break;
    case PlaybackAction::FAST_FORWARD:
        replay_.fast_forward();
        break;
    case PlaybackAction::TOGGLE_LOOP:
        state_.loop = !state_.loop;
        replay_.set_loop(state_.loop);
        break;
    default: break;
    }
}

// -----------------------------------------------------------------------
// Keyboard shortcuts
// -----------------------------------------------------------------------
void GuiApp::handle_keyboard_shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        if (state_.playback == PlaybackState::PLAYING)
            handle_playback_action(PlaybackAction::PAUSE);
        else
            handle_playback_action(PlaybackAction::PLAY);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
        save_buffer_dialog();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
        open_file_dialog();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        handle_playback_action(PlaybackAction::STOP);
    }
}

// -----------------------------------------------------------------------
// File dialogs (Win32 native)
// -----------------------------------------------------------------------
void GuiApp::open_file_dialog() {
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = "CAN Logs\0*.asc;*.jsonl\0All Files\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        auto err = replay_.load(filename);
        if (!err.empty()) {
            state_.error_message = err;
        } else {
            state_.data_source = DataSource::FILE;
            state_.playback    = PlaybackState::STOPPED;
            state_.open_file   = filename;
            settings_.last_file_path = filename;
            collector_.clear();
        }
        // Reset clock so the first tick after Play doesn't include dialog time.
        last_tick_us_ = host_timestamp_us();
    }
}

void GuiApp::save_buffer_dialog() {
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = "ASC Log\0*.asc\0JSONL Log\0*.jsonl\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt  = "asc";

    if (GetSaveFileNameA(&ofn)) {
        auto frames   = collector_.buffer_contents();
        std::string path(filename);
        std::string ext = path.substr(path.find_last_of('.') + 1);

        SessionStatus dummy_status;
        try {
            if (ext == "jsonl") {
                JsonlWriter w(path);
                w.writeHeader(dummy_status);
                for (auto& f : frames) w.writeFrame(f);
                w.writeFooter(dummy_status);
                w.flush();
            } else {
                AscWriter w(path);
                w.writeHeader(dummy_status);
                for (auto& f : frames) w.writeFrame(f);
                w.writeFooter(dummy_status);
                w.flush();
            }
        } catch (const std::exception& e) {
            state_.error_message = e.what();
        }
    }
    // Reset clock so the next tick doesn't include dialog time.
    last_tick_us_ = host_timestamp_us();
}

// -----------------------------------------------------------------------
// Error popup
// -----------------------------------------------------------------------
void GuiApp::show_error_popup() {
    if (!state_.error_message.empty()) {
        ImGui::OpenPopup("Error");
        state_.error_message.clear(); // consume — will be shown in popup
    }

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("An error occurred. Check the log for details.");
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace canmatik
