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
#include "gui/panels/logs_panel.h"
#include "gui/panels/settings_panel.h"
#include "gui/panels/status_bar.h"
#include "gui/panels/watchdog_panel.h"
#include "gui/panels/menu_bar.h"
#include "gui/widgets/playback_toolbar.h"
#include "logging/asc_writer.h"
#include "logging/jsonl_writer.h"
#include "core/session_status.h"
#include "core/log_macros.h"
#include "obd/pid_decoder.h"
#include "obd/pid_table.h"
#include "obd/dtc_decoder.h"

#include "imgui.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace canmatik {

GuiApp::GuiApp()
    : collector_(host_timestamp_us(), 100000) {}

void GuiApp::init(const std::string& settings_path) {
    settings_path_ = settings_path;
    auto load_err = settings_.load(settings_path_);
    if (load_err.empty())
        GUI_LOG_INFO("Settings loaded from {}", settings_path_);
    else
        GUI_LOG_WARNING("Settings: {} (using defaults)", load_err);
    collector_.resize_buffer(settings_.buffer_capacity);
    collector_.set_overwrite(settings_.buffer_overwrite);
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
    proxy_server_.stop();
    if (proxy_loader_.is_loaded()) proxy_loader_.unload();
    settings_.save(settings_path_);
}

void GuiApp::render() {
    // Time delta for replay tick
    uint64_t now = host_timestamp_us();
    uint64_t delta = now - last_tick_us_;
    last_tick_us_ = now;

    // Sync overwrite mode to collector
    collector_.set_overwrite(settings_.buffer_overwrite);

    // Drain live capture frames
    if (state_.data_source == DataSource::LIVE && capture_.is_capturing())
        capture_.drain();

    // Advance replay
    if (state_.data_source == DataSource::FILE && replay_.is_playing())
        replay_.tick(delta, collector_);

    // Auto-stop when buffer is full and overwrite is disabled
    if (!settings_.buffer_overwrite && collector_.is_buffer_full()) {
        if (state_.data_source == DataSource::LIVE && capture_.is_capturing()) {
            capture_.stop();
            state_.playback = PlaybackState::STOPPED;
        }
        if (state_.data_source == DataSource::FILE && replay_.is_playing()) {
            replay_.pause();
            state_.playback = PlaybackState::PAUSED;
        }
    }

    // Sync playback state when replay finishes on its own
    if (state_.data_source == DataSource::FILE &&
        state_.playback == PlaybackState::PLAYING &&
        !replay_.is_playing()) {
        state_.playback = PlaybackState::STOPPED;
    }

    // Update state counters
    state_.buffer_used  = collector_.buffer_count();
    state_.total_frames = collector_.buffer_count();

    // Sync proxy server state with settings
    sync_proxy_state();

    handle_keyboard_shortcuts();

    // Menu bar
    render_menu_bar(state_, show_watchdog_, show_graph_,
                    [this]{ open_file_dialog(); },
                    [this]{ save_buffer_dialog(); });

    // Fullscreen dockspace
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - 28.0f));
    ImGui::Begin("##MainArea", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // === Shared toolbar: connection, data source, playback, save/open ===
    render_shared_toolbar();

    // === Tab bar: data sub-tabs + Settings ===
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Bus Messages")) {
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
        if (ImGui::BeginTabItem("Logs")) {
            state_.selected_tab = 3;
            render_logs_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            state_.selected_tab = 4;
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
// Shared toolbar: connection, data source, playback, save/open, timeline
// -----------------------------------------------------------------------
void GuiApp::render_shared_toolbar() {
    // Connect / Disconnect button
    // In terminated proxy mode, there is no real adapter — skip connect.
    bool terminated_active = proxy_server_.is_running() && settings_.proxy_terminated;
    if (terminated_active) {
        ImGui::BeginDisabled();
        ImGui::Button("Connect");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(terminated mode)");
    } else if (!capture_.is_connected()) {
        if (ImGui::Button("Connect")) {
            // Block if the proxy is using the same (or only) adapter
            bool proxy_conflict = false;
            if (proxy_server_.is_running() && !settings_.proxy_terminated) {
                proxy_conflict = settings_.provider.empty()
                              || settings_.provider == settings_.proxy_target
                              || settings_.provider.find(settings_.proxy_target) != std::string::npos
                              || settings_.proxy_target.find(settings_.provider) != std::string::npos;
            }

            if (proxy_conflict) {
                state_.error_message = std::format(
                    "Cannot connect: the proxy server is already using '{}'.\n"
                    "Stop the proxy first, or select a different adapter.",
                    settings_.proxy_target);
                GUI_LOG_ERROR("Connect blocked: adapter '{}' in use by proxy", settings_.proxy_target);
            } else {
                auto err = capture_.connect(settings_.provider, settings_.bitrate,
                                           settings_.mock_enabled, collector_,
                                           settings_.bus_protocol);
                if (err.empty()) {
                    state_.connected = true;
                    GUI_LOG_INFO("Connected to '{}' at {} bps", settings_.provider, settings_.bitrate);
                } else {
                    state_.error_message = err;
                    GUI_LOG_ERROR("Connect failed: {}", err);
                }
            }
        }
    } else {
        if (ImGui::Button("Disconnect")) {
            obd_.stop_streaming();
            capture_.stop();
            capture_.disconnect();
            GUI_LOG_INFO("Disconnected");
            state_.connected = false;
            state_.playback  = PlaybackState::STOPPED;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled(capture_.is_connected() ? "Connected" : "Disconnected");

    // Proxy status indicator
    if (proxy_server_.is_running()) {
        ImGui::SameLine();
        if (settings_.proxy_terminated) {
            if (proxy_server_.has_client())
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[Proxy: terminated, client connected]");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "[Proxy: terminated, waiting]");
        } else {
            if (proxy_server_.has_client())
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[Proxy: client connected]");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "[Proxy: waiting]");
        }
    }

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

    // Restart capture button when buffer is full
    if (!settings_.buffer_overwrite && collector_.is_buffer_full() &&
        state_.playback == PlaybackState::STOPPED) {
        ImGui::SameLine();
        if (ImGui::Button("Restart Capture")) {
            collector_.clear();
            if (state_.data_source == DataSource::LIVE && capture_.is_connected()) {
                auto err = capture_.start(collector_);
                if (err.empty())
                    state_.playback = PlaybackState::PLAYING;
                else
                    state_.error_message = err;
            } else if (state_.data_source == DataSource::FILE && replay_.is_loaded()) {
                replay_.rewind();
                replay_.play();
                state_.playback = PlaybackState::PLAYING;
            }
        }
    }

    // Replay position slider (file mode only)
    if (state_.data_source == DataSource::FILE && replay_.is_loaded()) {
        int pos = static_cast<int>(replay_.current_index());
        int total = static_cast<int>(replay_.frame_count());
        uint64_t dur = replay_.duration_us();

        // Time label on the right
        char time_buf[32] = {};
        if (dur > 0 && total > 1) {
            double progress = static_cast<double>(pos) / static_cast<double>(total - 1);
            double cur_sec = (dur * progress) / 1000000.0;
            double total_sec = dur / 1000000.0;
            snprintf(time_buf, sizeof(time_buf), "%.1f / %.1fs", cur_sec, total_sec);
        }
        float time_w = (time_buf[0] != '\0') ? ImGui::CalcTextSize(time_buf).x + 12 : 0;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - time_w);
        if (ImGui::SliderInt("##ReplayPos", &pos, 0, total > 0 ? total - 1 : 0,
                             "Frame %d")) {
            replay_.seek(static_cast<size_t>(pos), collector_);
        }
        if (time_buf[0] != '\0') {
            ImGui::SameLine();
            ImGui::Text("%s", time_buf);
        }
    }

    ImGui::Separator();
}

// -----------------------------------------------------------------------
// CAN Messages tab
// -----------------------------------------------------------------------
void GuiApp::render_can_tab() {
    ImGui::SetWindowFontScale(settings_.font_scale_can);

    // Filter controls
    static bool raw_stream = false;
    ImGui::Checkbox("Raw Stream", &raw_stream);
    ImGui::SameLine();
    if (!raw_stream) {
        ImGui::Checkbox("Changed only", &settings_.show_changed_only);
        ImGui::SameLine();
        int n = static_cast<int>(settings_.change_filter_n);
        ImGui::SetNextItemWidth(60);
        if (ImGui::InputInt("Last N", &n))
            settings_.change_filter_n = static_cast<uint32_t>(std::clamp(n, 1, 100));
    } else {
        ImGui::TextDisabled("(chronological, no grouping)");
    }

    // OBD display filter
    ImGui::SameLine();
    {
        const char* obd_modes[] = { "OBD + Broadcast", "OBD Only", "Broadcast Only" };
        int obd_sel = static_cast<int>(settings_.obd_mode);
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Display Filter", &obd_sel, obd_modes, 3))
            settings_.obd_mode = static_cast<ObdDisplayMode>(obd_sel);
    }

    // ID filter management
    {
        static bool show_filter_popup = false;
        ImGui::SameLine();
        if (!settings_.id_filter_list.empty()) {
            const char* mode_label = (settings_.id_filter_mode == IdFilterMode::EXCLUDE)
                ? "Exclude" : "Include";
            char btn_label[64];
            snprintf(btn_label, sizeof(btn_label), "Filters: %s %d IDs###FilterBtn",
                     mode_label, static_cast<int>(settings_.id_filter_list.size()));
            if (ImGui::SmallButton(btn_label))
                show_filter_popup = true;
        } else {
            ImGui::TextDisabled("No ID filter");
        }

        if (show_filter_popup) {
            ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("ID Filter", &show_filter_popup)) {
                const char* mode_label = (settings_.id_filter_mode == IdFilterMode::EXCLUDE)
                    ? "Excluding" : "Including only";
                ImGui::Text("Mode: %s", mode_label);
                ImGui::Separator();

                if (ImGui::Button("Clear All")) {
                    settings_.id_filter_list.clear();
                    show_filter_popup = false;
                }
                ImGui::SameLine();
                if (settings_.id_filter_mode == IdFilterMode::EXCLUDE) {
                    if (ImGui::Button("Switch to Include"))
                        settings_.id_filter_mode = IdFilterMode::INCLUDE;
                } else {
                    if (ImGui::Button("Switch to Exclude"))
                        settings_.id_filter_mode = IdFilterMode::EXCLUDE;
                }
                ImGui::Separator();

                // List each filtered ID with a remove button
                int remove_idx = -1;
                for (int i = 0; i < static_cast<int>(settings_.id_filter_list.size()); ++i) {
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("X"))
                        remove_idx = i;
                    ImGui::SameLine();
                    ImGui::Text("0x%03X", settings_.id_filter_list[i]);
                    ImGui::PopID();
                }
                if (remove_idx >= 0) {
                    settings_.id_filter_list.erase(
                        settings_.id_filter_list.begin() + remove_idx);
                }
            }
            ImGui::End();
        }
    }

    ImGui::Separator();

    // Message table
    std::vector<MessageRow> rows;
    if (raw_stream) {
        rows = collector_.raw_snapshot(5000);
    } else {
        rows = collector_.snapshot(settings_.show_changed_only,
                                   settings_.change_filter_n,
                                   settings_.obd_mode,
                                   settings_.id_filter_mode,
                                   settings_.id_filter_list);
    }
    render_can_messages_panel(rows, state_, collector_, settings_, show_graph_, raw_stream);

    // Watchdog panel
    if (show_watchdog_) {
        collector_.set_watchdog_history_size(settings_.watchdog_history_size);
        auto wd_snaps = collector_.watchdog_detail_snapshot();
        if (!wd_snaps.empty()) {
            ImGui::Separator();
            render_watchdog_panel(wd_snaps, collector_, settings_);
        }
    }

    ImGui::SetWindowFontScale(1.0f);
}

// -----------------------------------------------------------------------
// Decode OBD PID rows from buffered frames (for recordings / offline view)
// -----------------------------------------------------------------------
static std::vector<ObdPidRow> decode_obd_from_buffer(const std::vector<CanFrame>& frames) {
    // Map PID -> latest decoded row
    std::unordered_map<uint8_t, ObdPidRow> pid_map;

    for (auto& f : frames) {
        // OBD response IDs: 0x7E8ÔÇô0x7EF
        if (f.arbitration_id < 0x7E8 || f.arbitration_id > 0x7EF) continue;
        if (f.dlc < 4) continue;

        // ISO-TP single frame: data[0] = length, data[1] = mode+0x40, data[2] = PID, data[3..] = value bytes
        uint8_t resp_mode = f.data[1];
        if (resp_mode != 0x41) continue; // Only Mode $01 responses

        uint8_t pid = f.data[2];
        const PidDefinition* def = pid_lookup(0x01, pid);
        if (!def) continue;

        uint8_t data_len = std::min<uint8_t>(f.data[0] - 2, static_cast<uint8_t>(f.dlc - 3));
        if (data_len == 0 || data_len > 4) continue;

        double value = decode_pid_value(&f.data[3], data_len, def->formula);

        auto& row = pid_map[pid];
        bool changed = (row.pid == pid && row.value != value);
        row.pid   = pid;
        row.name  = def->name;
        row.value = value;
        row.unit  = def->unit;
        row.value_changed = changed;

        // Raw hex
        std::string hex;
        for (uint8_t i = 0; i < data_len; ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", f.data[3 + i]);
            hex += buf;
        }
        row.raw_hex = hex;
    }

    std::vector<ObdPidRow> result;
    result.reserve(pid_map.size());
    for (auto& [pid, row] : pid_map)
        result.push_back(std::move(row));
    std::sort(result.begin(), result.end(),
              [](const ObdPidRow& a, const ObdPidRow& b) { return a.pid < b.pid; });
    return result;
}

// -----------------------------------------------------------------------
// OBD Data tab
// -----------------------------------------------------------------------
void GuiApp::render_obd_tab() {
    ImGui::SetWindowFontScale(settings_.font_scale_obd);

    // Live OBD start / stop controls
    if (state_.data_source == DataSource::LIVE) {
        if (!capture_.is_connected()) {
            ImGui::TextDisabled("Not connected.");
        } else {
            if (!obd_.is_streaming()) {
                if (ImGui::Button("Start OBD Polling")) {
                    auto* ch = capture_.channel();
                    if (ch)
                        obd_.start_streaming(ch, settings_.obd_pids,
                                             settings_.obd_interval_ms, &collector_);
                }
            } else {
                if (ImGui::Button("Stop OBD Polling"))
                    obd_.stop_streaming();

                // Auto-restart if PIDs or interval changed while streaming
                bool pids_changed = (obd_.current_pids() != settings_.obd_pids);
                bool interval_changed = (obd_.current_interval_ms() != settings_.obd_interval_ms);
                if (pids_changed || interval_changed) {
                    obd_.stop_streaming();
                    auto* ch = capture_.channel();
                    if (ch)
                        obd_.start_streaming(ch, settings_.obd_pids,
                                             settings_.obd_interval_ms, &collector_);
                }
            }
        }
    } else {
        ImGui::TextDisabled("Showing OBD data decoded from recording.");
    }

    ImGui::Separator();

    // Show OBD data: live snapshot if streaming, otherwise decode from buffer
    std::vector<ObdPidRow> obd_rows;
    if (obd_.is_streaming()) {
        obd_rows = obd_.snapshot();
    } else {
        auto frames = collector_.buffer_contents();
        obd_rows = decode_obd_from_buffer(frames);
    }
    render_obd_data_panel(obd_rows, settings_, show_graph_);

    ImGui::SetWindowFontScale(1.0f);
}

// -----------------------------------------------------------------------
// DTC tab
// -----------------------------------------------------------------------
void GuiApp::render_dtc_tab() {
    bool file_mode = (state_.data_source == DataSource::FILE);
    render_dtc_panel(dtc_state_, capture_, state_, file_mode);
}

// -----------------------------------------------------------------------
// Logs tab
// -----------------------------------------------------------------------
void GuiApp::render_logs_tab() {
    render_logs_panel(GuiLogSink::get());
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

        // Rebase frame timestamps: the writer uses host_timestamp_us() as
        // session start, so shift all frames so the first frame's relative
        // time is ~0 and inter-frame deltas are preserved.
        if (!frames.empty()) {
            uint64_t now = host_timestamp_us();
            uint64_t base = frames[0].host_timestamp_us;
            for (auto& f : frames) {
                uint64_t offset = f.host_timestamp_us - base;
                f.host_timestamp_us = now + offset;
            }
        }

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
// Proxy server lifecycle — start/stop based on settings
// -----------------------------------------------------------------------
void GuiApp::sync_proxy_state() {
    bool want = settings_.proxy_enabled;

    // Start the proxy if newly enabled
    if (want && !proxy_was_enabled_) {

        // --- Terminated mode: no real adapter needed ---
        if (settings_.proxy_terminated) {
            auto err = proxy_server_.start_terminated([this](const CanFrame& f) {
                collector_.pushFrame(f);
            });
            if (!err.empty()) {
                state_.error_message = std::format("Proxy: {}", err);
                settings_.proxy_enabled = false;
                return;
            }
            proxy_was_enabled_ = true;
            GUI_LOG_INFO("Proxy server started (terminated mode)");
            return;
        }

        // --- Normal proxy mode: forward to real adapter ---
        // Wait until user selects a target — don't error, just skip
        if (settings_.proxy_target.empty())
            return;

        // Find the DLL path for the selected target adapter
        // Ensure providers have been scanned (may be empty on fresh startup)
        if (capture_.scanned_devices().empty())
            capture_.scan_providers(false);

        const auto& devices = capture_.scanned_devices();
        std::string dll_path;
        for (auto& d : devices) {
            if (d.name == settings_.proxy_target) {
                dll_path = d.dll_path;
                break;
            }
        }
        if (dll_path.empty()) {
            state_.error_message = std::format(
                "Proxy: cannot find DLL for '{}'.\n"
                "Scan for providers in Settings first.", settings_.proxy_target);
            settings_.proxy_enabled = false;
            return;
        }

        try {
            proxy_loader_.load(dll_path);
        } catch (const std::exception& e) {
            state_.error_message = std::format("Proxy: failed to load DLL: {}", e.what());
            settings_.proxy_enabled = false;
            return;
        }

        auto err = proxy_server_.start(proxy_loader_, [this](const CanFrame& f) {
            collector_.pushFrame(f);
        });
        if (!err.empty()) {
            state_.error_message = std::format("Proxy: {}", err);
            proxy_loader_.unload();
            settings_.proxy_enabled = false;
            return;
        }

        proxy_was_enabled_ = true;
        GUI_LOG_INFO("Proxy server started (target: {})", settings_.proxy_target);
    }

    // Stop the proxy if newly disabled
    if (!want && proxy_was_enabled_) {
        proxy_server_.stop();
        if (proxy_loader_.is_loaded()) proxy_loader_.unload();
        proxy_was_enabled_ = false;
        GUI_LOG_INFO("Proxy server stopped");
    }
}

// -----------------------------------------------------------------------
// Error popup
// -----------------------------------------------------------------------
void GuiApp::show_error_popup() {
    static std::string popup_error;
    if (!state_.error_message.empty()) {
        popup_error = state_.error_message;
        state_.error_message.clear();
        ImGui::OpenPopup("Error");
    }

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", popup_error.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("See canmatik_gui.log for details.");
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            popup_error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace canmatik
