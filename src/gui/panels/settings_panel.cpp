/// @file settings_panel.cpp
/// Settings tab implementation.

#include "gui/panels/settings_panel.h"
#include "proxy/proxy_registry.h"
#include "obd/obd_session.h"
#include "obd/pid_table.h"
#include "imgui.h"
#include "services/elm327_bridge.h"
#include "platform/win32/serial_provider.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>
#include <thread>
#include <regex>

namespace canmatik {

void render_settings_panel(GuiSettings& settings, GuiState& state,
                           CaptureController& capture,
                           FrameCollector& collector,
                           ObdController& obd,
                           bool& scheme_changed) {
    scheme_changed = false;

    // ----- Appearance (Font sizes & Colors) -----
    ImGui::SeparatorText("Appearance (Fonts & Colors)");
    static const char* scheme_labels[] = {"Dark", "Light", "Retro (Classic)"};
    int cs_idx = static_cast<int>(settings.color_scheme);
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Color scheme", &cs_idx, scheme_labels, 3)) {
        settings.color_scheme = static_cast<ColorScheme>(cs_idx);
        scheme_changed = true;
    }

    // ----- ELM327 Bridge (GUI control) -----
    ImGui::SeparatorText("ELM327 Bridge");
    static std::vector<std::string> com_ports_display;
    static std::vector<std::string> com_ports_token;
    static int com_idx = -1;
    static bool com_scanned = false;
    if (!com_scanned) {
        canmatik::SerialProvider sprov;
        auto devs = sprov.enumerate();
        int idx = 0;
        for (auto& d : devs) {
            std::string label = d.name;
            // try to extract COM token like COM5
            std::string token;
            try {
                std::smatch m;
                std::regex re("(COM\\d+)", std::regex_constants::icase);
                if (std::regex_search(label, m, re) && m.size() > 1) token = m.str(1);
                else {
                    // fallback: find any digit sequence and prefix with COM
                    std::regex r2("(\\d+)");
                    if (std::regex_search(label, m, r2) && m.size() > 1) token = std::string("COM") + m.str(1);
                }
            } catch(...) {}
            if (token.empty()) {
                // ensure unique display if token missing
                label += std::string(" #") + std::to_string(idx);
                token = label; // fallback token will be full label
            }
            com_ports_display.push_back(label);
            com_ports_token.push_back(token);
            ++idx;
        }
        com_scanned = true;
    }

    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Serial port", (com_idx >= 0 && com_idx < static_cast<int>(com_ports_display.size())) ? com_ports_display[com_idx].c_str() : "<select port>")) {
        for (int i = 0; i < static_cast<int>(com_ports_display.size()); ++i) {
            bool sel = (i == com_idx);
            ImGui::PushID(i);
            if (ImGui::Selectable(com_ports_display[i].c_str(), sel)) com_idx = i;
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // Provider selection: query available providers now and fall back to settings.provider
    auto local_providers = capture.scan_providers(false);
    int local_idx = -1;
    for (int i = 0; i < static_cast<int>(local_providers.size()); ++i)
        if (local_providers[i] == settings.provider) { local_idx = i; break; }
    std::string prov_display = (local_idx >= 0 && local_idx < static_cast<int>(local_providers.size())) ? local_providers[local_idx] : settings.provider;
    ImGui::SetNextItemWidth(260);
    ImGui::TextDisabled("J2534 provider: %s", prov_display.c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Terminated", &settings.proxy_terminated);

    // Serial baud selector for ELM327 bridge
    static const int kBaudOptions[] = {9600, 19200, 38400, 57600, 115200};
    int baud_idx = 0;
    for (int i = 0; i < static_cast<int>(sizeof(kBaudOptions)/sizeof(kBaudOptions[0])); ++i) if (kBaudOptions[i] == static_cast<int>(settings.elm327_baud)) { baud_idx = i; break; }
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Baud rate", &baud_idx, "9600\0" "19200\0" "38400\0" "57600\0" "115200\0")) {
        settings.elm327_baud = static_cast<uint32_t>(kBaudOptions[baud_idx]);
    }

    // YAML ECU mock script (only relevant when Terminated is checked)
    static char script_buf[512] = {};
    static bool script_buf_init = false;
    if (!script_buf_init) {
        auto n = settings.mock_script_path.copy(script_buf, sizeof(script_buf) - 1);
        script_buf[n] = '\0';
        script_buf_init = true;
    }
    ImGui::SetNextItemWidth(360);
    if (ImGui::InputText("ECU script (YAML)", script_buf, sizeof(script_buf))) {
        settings.mock_script_path = script_buf;
    }
    ImGui::SameLine();
    if (ImGui::Button("...##script_browse")) {
        // Win32 file open dialog
        char fname[MAX_PATH] = {};
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "YAML files\0*.yaml;*.yml\0All files\0*.*\0";
        ofn.lpstrFile = fname;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn)) {
            settings.mock_script_path = fname;
            auto n2 = settings.mock_script_path.copy(script_buf, sizeof(script_buf) - 1);
            script_buf[n2] = '\0';
        }
    }

    // Bridge start/stop
    static std::shared_ptr<Elm327Bridge> bridge_instance;
    static std::thread bridge_thread;
    static std::atomic<bool> bridge_running{false};
    static std::atomic<int> bridge_exit_code{0};

    if (!bridge_running) {
        if (ImGui::Button("Start ELM327 Bridge") ) {
            if (com_idx < 0 || com_idx >= static_cast<int>(com_ports_display.size())) {
                // show error
                // set temporary state.error_message
                state.error_message = "Select a serial port first";
            } else {
                std::string serial = com_ports_display[com_idx];
                std::string com_token = com_ports_token[com_idx];
                std::string provider_name = prov_display;
                bridge_instance = std::make_shared<Elm327Bridge>(com_token, provider_name, &collector, settings.proxy_terminated, settings.elm327_baud, settings.mock_script_path);
                bridge_running.store(true);
                bridge_exit_code.store(0);
                // copy shared_ptr to a local variable and move into the thread to ensure
                // the thread owns the instance and we avoid capturing a static variable.
                auto bridge_copy = bridge_instance;
                bridge_thread = std::thread([bridge_copy]() {
                    int rc = bridge_copy->run();
                    bridge_exit_code.store(rc);
                    bridge_running.store(false);
                });
            }
        }
    } else {
        if (ImGui::Button("Stop ELM327 Bridge")) {
            if (bridge_instance) bridge_instance->stop();
            if (bridge_thread.joinable()) bridge_thread.join();
            bridge_instance.reset();
            bridge_running.store(false);
            int rc = bridge_exit_code.load();
            if (rc != 0) state.error_message = "ELM327 bridge exited with error";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Running on %s", (com_idx >= 0 && com_idx < static_cast<int>(com_ports_display.size())) ? com_ports_display[com_idx].c_str() : "<unknown>");
    }

    // If the bridge terminated on its own, join the thread and show error (if any).
    if (!bridge_running.load() && bridge_thread.joinable()) {
        bridge_thread.join();
        if (bridge_instance) {
            auto err = bridge_instance->last_error();
            if (!err.empty()) state.error_message = std::string("ELM327 bridge stopped: ") + err;
        }
        bridge_instance.reset();
        bridge_exit_code.store(0);
    }

        // ----- Appearance: combined font size + color previews -----
        ImGui::SeparatorText("Fonts & Colors");

        // Bus Messages: font scale control and color swatches with preview
        ImGui::Text("Bus Messages");
        // Show a scaled sample above the slider
        ImGui::SetWindowFontScale(settings.font_scale_can);
        ImGui::TextColored(ImVec4(settings.color_can_default[0], settings.color_can_default[1],
                      settings.color_can_default[2], settings.color_can_default[3]),
                  "0x1A3 8 01 FF 3C 12 34 56 78");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderFloat("Font Scale##can_font", &settings.font_scale_can, 0.5f, 3.0f, "%.1fx"))
            settings.font_scale_can = std::clamp(settings.font_scale_can, 0.5f, 3.0f);
        ImGui::ColorEdit4("New ID##can_new", settings.color_can_new,
                 ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(settings.color_can_new[0], settings.color_can_new[1],
                      settings.color_can_new[2], settings.color_can_new[3]),
                  "0x1A3 New message");

        ImGui::ColorEdit4("Changed##can_chg", settings.color_can_changed,
                 ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(settings.color_can_changed[0], settings.color_can_changed[1],
                      settings.color_can_changed[2], settings.color_can_changed[3]),
                  "A0 FF 3C Changed bytes");

        ImGui::ColorEdit4("DLC Changed##can_dlc", settings.color_can_dlc,
                 ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(settings.color_can_dlc[0], settings.color_can_dlc[1],
                      settings.color_can_dlc[2], settings.color_can_dlc[3]),
                  "8 DLC changed");

        ImGui::ColorEdit4("Normal##can_def", settings.color_can_default,
                 ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(settings.color_can_default[0], settings.color_can_default[1],
                      settings.color_can_default[2], settings.color_can_default[3]),
                  "0x212 Normal text");

        ImGui::ColorEdit4("Watched##can_watch", settings.color_can_watched,
                 ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(settings.color_can_watched[0], settings.color_can_watched[1],
                      settings.color_can_watched[2], settings.color_can_watched[3]),
                  "0x7E8 Watched ID");

            // Preview combined scaled text for Bus Messages and OBD Data
            ImGui::Spacing();
            ImGui::TextDisabled("Preview:");
            // OBD font control lives in Settings (font sliders)
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("OBD Data##fontscale_obd", &settings.font_scale_obd, 0.5f, 3.0f, "%.1fx"))
                settings.font_scale_obd = std::clamp(settings.font_scale_obd, 0.5f, 3.0f);

            // OBD preview uses current OBD font scale
            ImGui::SetWindowFontScale(settings.font_scale_obd);
            ImGui::TextColored(ImVec4(settings.color_obd_normal[0], settings.color_obd_normal[1],
                                      settings.color_obd_normal[2], settings.color_obd_normal[3]),
                               "OBD data: Engine RPM 3200.5");
            ImGui::SetWindowFontScale(1.0f);

    // Reset to defaults button
    if (ImGui::Button("Reset Colors to Defaults")) {
        float d_new[4]     = {0.3f, 1.0f, 0.3f, 1.0f};
        float d_changed[4] = {1.0f, 0.3f, 0.3f, 1.0f};
        float d_dlc[4]     = {1.0f, 0.9f, 0.2f, 1.0f};
        float d_default[4] = {0.9f, 0.9f, 0.9f, 1.0f};
        float d_watched[4] = {0.4f, 0.8f, 1.0f, 1.0f};
        float d_obd_c[4]   = {0.3f, 1.0f, 0.3f, 1.0f};
        float d_obd_n[4]   = {1.0f, 1.0f, 1.0f, 1.0f};
        float d_obd_d[4]   = {0.6f, 0.6f, 0.7f, 1.0f};
        std::copy(d_new,     d_new + 4,     settings.color_can_new);
        std::copy(d_changed, d_changed + 4, settings.color_can_changed);
        std::copy(d_dlc,     d_dlc + 4,     settings.color_can_dlc);
        std::copy(d_default, d_default + 4, settings.color_can_default);
        std::copy(d_watched, d_watched + 4, settings.color_can_watched);
        std::copy(d_obd_c,   d_obd_c + 4,  settings.color_obd_changed);
        std::copy(d_obd_n,   d_obd_n + 4,  settings.color_obd_normal);
        std::copy(d_obd_d,   d_obd_d + 4,  settings.color_obd_dim);
    }

    // ----- Connection section -----
    ImGui::SeparatorText("Connection");

    // J2534 interface scan + dropdown
    static std::vector<std::string> provider_names;
    static int provider_idx = -1;
    static bool scanned_once = false;

    if (!scanned_once) {
        provider_names = capture.scan_providers(false);
        for (int i = 0; i < static_cast<int>(provider_names.size()); ++i)
            if (provider_names[i] == settings.provider) provider_idx = i;
        scanned_once = true;
    }

    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("J2534 Interface", (provider_idx >= 0 && provider_idx < static_cast<int>(provider_names.size()))
                                        ? provider_names[provider_idx].c_str() : "<none>")) {
        for (int i = 0; i < static_cast<int>(provider_names.size()); ++i) {
            bool selected = (i == provider_idx);
            ImGui::PushID(i);
            if (ImGui::Selectable(provider_names[i].c_str(), selected)) {
                provider_idx = i;
                settings.provider = provider_names[i];
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan")) {
        provider_names = capture.scan_providers(false);
        provider_idx = -1;
        for (int i = 0; i < static_cast<int>(provider_names.size()); ++i)
            if (provider_names[i] == settings.provider) provider_idx = i;
    }

    // Bus protocol selector
    static const char* proto_labels[] = {"CAN", "J1850 VPW (10.4 kbps)", "J1850 PWM (41.6 kbps)"};
    int proto_idx = static_cast<int>(settings.bus_protocol);
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Bus Protocol", &proto_idx, proto_labels, 3)) {
        settings.bus_protocol = static_cast<BusProtocol>(proto_idx);
        // Auto-set default bitrate when switching protocol
        if (settings.bus_protocol == BusProtocol::J1850_VPW) settings.bitrate = 10400;
        else if (settings.bus_protocol == BusProtocol::J1850_PWM) settings.bitrate = 41600;
        else settings.bitrate = 500000;
    }

    // Bitrate combo (options depend on protocol)
    if (settings.bus_protocol == BusProtocol::CAN) {
        static const uint32_t bitrates[] = {125000, 250000, 500000, 1000000};
        static const char* bitrate_labels[] = {"125 kbps", "250 kbps", "500 kbps", "1 Mbps"};
        int br_idx = 2; // default 500k
        for (int i = 0; i < 4; ++i)
            if (bitrates[i] == settings.bitrate) br_idx = i;
        ImGui::SetNextItemWidth(120);
        if (ImGui::Combo("Bitrate", &br_idx, bitrate_labels, 4))
            settings.bitrate = bitrates[br_idx];
    } else {
        // J1850: fixed bitrate, display as read-only
        ImGui::TextDisabled("Bitrate: %u bps (fixed)", settings.bitrate);
    }

    // Mock checkbox
    // Mock mode removed from GUI — use CLI for mock/demo mode

    // Connect / Disconnect button (main one is on CAN tab)
    ImGui::TextDisabled(capture.is_connected() ? "Connected" : "Disconnected");

    // ----- Proxy Mode section -----
    ImGui::SeparatorText("Proxy Mode");
    ImGui::TextDisabled("Expose a fake J2534 DLL for external tools.\n"
                        "Traffic is shown in Bus Messages. API calls appear in Logs.");

    // Mode radio buttons
    ImGui::Checkbox("Terminated (no real adapter)", &settings.proxy_terminated);
    if (settings.proxy_terminated) {
        ImGui::TextDisabled("Simulates a J2534 interface with no hardware.\n"
                            "Accepts all calls, displays TX data, returns empty reads.");
    }

    // Target provider selector (only in normal proxy mode)
    static int proxy_target_idx = -1;
    static bool proxy_scanned = false;

    if (!settings.proxy_terminated) {
        if (!proxy_scanned && !provider_names.empty()) {
            for (int i = 0; i < static_cast<int>(provider_names.size()); ++i)
                if (provider_names[i] == settings.proxy_target) proxy_target_idx = i;
            proxy_scanned = true;
        }

        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("Target adapter",
                (proxy_target_idx >= 0 && proxy_target_idx < static_cast<int>(provider_names.size()))
                    ? provider_names[proxy_target_idx].c_str() : "<select adapter>")) {
            for (int i = 0; i < static_cast<int>(provider_names.size()); ++i) {
                bool selected = (i == proxy_target_idx);
                if (ImGui::Selectable(provider_names[i].c_str(), selected)) {
                    proxy_target_idx = i;
                    settings.proxy_target = provider_names[i];
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Checkbox("Enable proxy", &settings.proxy_enabled);
    if (settings.proxy_enabled && !settings.proxy_terminated && settings.proxy_target.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Select a target adapter above to start proxy.");

    ImGui::TextDisabled("Pipe: \\\\.\\pipe\\canmatik_proxy");

    // --- Proxy DLL Installation ---
    ImGui::SeparatorText("Proxy DLL Registration");
    ImGui::TextDisabled("Register fake_j2534.dll in the Windows registry so\n"
                        "external scan tools discover it as a J2534 interface.\n"
                        "Registers under both PassThruSupport.04.04 and DeviceClasses.");

    // Preset or custom name
    static int preset_idx = 0;
    static char custom_name[128] = "CANmatik Proxy";
    static char custom_vendor[64] = "CANmatik";
    static bool use_custom = false;
    static std::string install_status;

    ImGui::Checkbox("Custom name", &use_custom);

    if (use_custom) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("Interface name", custom_name, sizeof(custom_name));
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("Vendor", custom_vendor, sizeof(custom_vendor));
    } else {
        // Preset combo
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("Interface preset",
                (preset_idx >= 0 && preset_idx < kPresetCount)
                    ? kPresets[preset_idx].name : "<select>")) {
            for (int i = 0; i < kPresetCount; ++i) {
                bool sel = (i == preset_idx);
                char label[128];
                snprintf(label, sizeof(label), "%s (%s)",
                         kPresets[i].name, kPresets[i].vendor);
                if (ImGui::Selectable(label, sel))
                    preset_idx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (preset_idx >= 0 && preset_idx < kPresetCount && kPresets[preset_idx].dll_name)
            ImGui::TextDisabled("Will deploy as %s in SysWOW64", kPresets[preset_idx].dll_name);
    }

    // DLL path
    static std::string dll_path_cache = find_proxy_dll_path();
    if (dll_path_cache.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "fake_j2534.dll not found next to canmatik_gui.exe!");
    } else {
        ImGui::TextDisabled("DLL: %s", dll_path_cache.c_str());
    }

    // Install button
    if (ImGui::Button("Install Proxy Interface") && !dll_path_cache.empty()) {
        const J2534Preset* p = &kPresets[0];
        std::string name, vendor;
        if (use_custom) {
            name = custom_name;
            vendor = custom_vendor;
            // Build a custom preset with all protocols enabled
            static J2534Preset custom_preset{"?", "?", nullptr, true, true, true, true, true, true, false, false};
            p = &custom_preset;
        } else {
            p = &kPresets[preset_idx];
            name = p->name;
            vendor = p->vendor;
        }
        install_status = install_proxy_j2534(name, vendor, dll_path_cache, *p);
        if (install_status.empty())
            install_status = "Installed OK: " + name;
    }

    if (!install_status.empty()) {
        bool ok = (install_status.rfind("Installed", 0) == 0);
        ImGui::TextColored(ok ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                              : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "%s", install_status.c_str());
    }

    // --- Installed J2534 Providers ---
    ImGui::SeparatorText("Installed J2534 Interfaces");
    ImGui::TextDisabled("View and remove J2534 providers from the registry.\n"
                        "Removing a provider hides it from external scan tools.");

    static std::vector<J2534RegEntry> reg_entries;
    static bool reg_loaded = false;
    static std::string uninstall_status;

    if (!reg_loaded) {
        reg_entries = enumerate_j2534_providers();
        reg_loaded = true;
    }
    if (ImGui::Button("Refresh")) {
        reg_entries = enumerate_j2534_providers();
        uninstall_status.clear();
    }

    if (!uninstall_status.empty()) {
        bool ok = (uninstall_status.rfind("Removed", 0) == 0);
        ImGui::TextColored(ok ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                              : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           "%s", uninstall_status.c_str());
    }

    if (reg_entries.empty()) {
        ImGui::TextDisabled("No J2534 providers found in registry.");
    } else {
        if (ImGui::BeginTable("##J2534RegTable", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Vendor", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("DLL",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Act",  ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            int remove_idx = -1;
            for (int i = 0; i < static_cast<int>(reg_entries.size()); ++i) {
                auto& e = reg_entries[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(e.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(e.vendor.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(e.dll_path.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(i);
                if (ImGui::SmallButton("Remove"))
                    remove_idx = i;
                ImGui::PopID();
            }

            ImGui::EndTable();

            if (remove_idx >= 0) {
                auto err = uninstall_j2534_provider(reg_entries[remove_idx].subkey);
                if (err.empty()) {
                    uninstall_status = "Removed: " + reg_entries[remove_idx].name;
                    reg_entries.erase(reg_entries.begin() + remove_idx);
                } else {
                    uninstall_status = err;
                }
            }
        }
    }

    // ----- Capture Buffer section -----
    ImGui::SeparatorText("Capture Buffer");
    int cap = static_cast<int>(settings.buffer_capacity);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Buffer capacity", &cap)) {
        settings.buffer_capacity = static_cast<uint32_t>(std::clamp(cap, 1000, 10000000));
        collector.resize_buffer(settings.buffer_capacity);
    }
    ImGui::TextDisabled("Range: 1,000 — 10,000,000 frames");
    ImGui::Checkbox("Overwrite when full", &settings.buffer_overwrite);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When enabled, the buffer wraps around and overwrites\n"
                          "oldest frames. When disabled, recording stops at the limit.");

    // ----- Watchdog section -----
    ImGui::SeparatorText("Watchdog");
    int whs = static_cast<int>(settings.watchdog_history_size);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("History size (samples)", &whs))
        settings.watchdog_history_size = static_cast<uint32_t>(std::clamp(whs, 10, 10000));
    ImGui::TextDisabled("Max decoded value samples stored per watched ID.");    
}

} // namespace canmatik
