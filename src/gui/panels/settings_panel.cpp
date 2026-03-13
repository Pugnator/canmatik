/// @file settings_panel.cpp
/// Settings tab implementation.

#include "gui/panels/settings_panel.h"
#include "proxy/proxy_registry.h"
#include "obd/obd_session.h"
#include "obd/pid_table.h"
#include "imgui.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace canmatik {

void render_settings_panel(GuiSettings& settings, GuiState& state,
                           CaptureController& capture,
                           FrameCollector& collector,
                           ObdController& obd,
                           bool& scheme_changed) {
    scheme_changed = false;

    // ----- Appearance section -----
    ImGui::SeparatorText("Appearance");
    static const char* scheme_labels[] = {"Dark", "Light", "Retro (Classic)"};
    int cs_idx = static_cast<int>(settings.color_scheme);
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Color scheme", &cs_idx, scheme_labels, 3)) {
        settings.color_scheme = static_cast<ColorScheme>(cs_idx);
        scheme_changed = true;
    }

    // ----- Font Sizes section -----
    ImGui::SeparatorText("Font Sizes");
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderFloat("Bus Messages##fontscale_can", &settings.font_scale_can, 0.5f, 3.0f, "%.1fx"))
        settings.font_scale_can = std::clamp(settings.font_scale_can, 0.5f, 3.0f);
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderFloat("OBD Data##fontscale_obd", &settings.font_scale_obd, 0.5f, 3.0f, "%.1fx"))
        settings.font_scale_obd = std::clamp(settings.font_scale_obd, 0.5f, 3.0f);
    if (ImGui::Button("Reset Font Sizes")) {
        settings.font_scale_can = 1.0f;
        settings.font_scale_obd = 1.0f;
    }

    // ----- Font Colors section -----
    ImGui::SeparatorText("Font Colors");
    ImGui::TextDisabled("Click a color swatch to change it. Preview text is shown next to each.");

    // CAN Messages panel colors
    ImGui::Text("CAN Messages:");
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

    // OBD Data panel colors
    ImGui::Spacing();
    ImGui::Text("OBD Data:");
    ImGui::ColorEdit4("Value Changed##obd_chg", settings.color_obd_changed,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(settings.color_obd_changed[0], settings.color_obd_changed[1],
                              settings.color_obd_changed[2], settings.color_obd_changed[3]),
                       "3200.5 RPM (changed)");

    ImGui::ColorEdit4("Normal##obd_norm", settings.color_obd_normal,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(settings.color_obd_normal[0], settings.color_obd_normal[1],
                              settings.color_obd_normal[2], settings.color_obd_normal[3]),
                       "Engine RPM");

    ImGui::ColorEdit4("Dim / Raw##obd_dim", settings.color_obd_dim,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(settings.color_obd_dim[0], settings.color_obd_dim[1],
                              settings.color_obd_dim[2], settings.color_obd_dim[3]),
                       "0C 80 raw hex");

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
        provider_names = capture.scan_providers(settings.mock_enabled);
        for (int i = 0; i < static_cast<int>(provider_names.size()); ++i)
            if (provider_names[i] == settings.provider) provider_idx = i;
        scanned_once = true;
    }

    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("J2534 Interface", (provider_idx >= 0 && provider_idx < static_cast<int>(provider_names.size()))
                                        ? provider_names[provider_idx].c_str() : "<none>")) {
        for (int i = 0; i < static_cast<int>(provider_names.size()); ++i) {
            bool selected = (i == provider_idx);
            if (ImGui::Selectable(provider_names[i].c_str(), selected)) {
                provider_idx = i;
                settings.provider = provider_names[i];
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan")) {
        provider_names = capture.scan_providers(settings.mock_enabled);
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
    ImGui::Checkbox("Mock mode", &settings.mock_enabled);

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
        bool ok = install_status.starts_with("Installed");
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
        bool ok = uninstall_status.starts_with("Removed");
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

    // ----- OBD Settings section -----
    ImGui::SeparatorText("OBD Settings");
    int interval = static_cast<int>(settings.obd_interval_ms);
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Query interval (ms)", &interval))
        settings.obd_interval_ms = static_cast<uint32_t>(std::clamp(interval, 50, 5000));

    // PID list (hex + human-readable name)
    ImGui::Text("Selected PIDs:");
    int remove_idx = -1;
    for (size_t i = 0; i < settings.obd_pids.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        auto* def = pid_lookup(0x01, settings.obd_pids[i]);
        if (def)
            ImGui::BulletText("0x%02X - %s", settings.obd_pids[i], def->name);
        else
            ImGui::BulletText("0x%02X", settings.obd_pids[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            remove_idx = static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove_idx >= 0)
        settings.obd_pids.erase(settings.obd_pids.begin() + remove_idx);

    // Known PID dropdown
    struct KnownPid { uint8_t id; const char* label; };
    static const KnownPid known_pids[] = {
        {0x00, "00 - PIDs supported [01-20]"},
        {0x04, "04 - Engine Load"},
        {0x05, "05 - Coolant Temp"},
        {0x06, "06 - Short Fuel Trim Bank 1"},
        {0x07, "07 - Long Fuel Trim Bank 1"},
        {0x0B, "0B - Intake MAP"},
        {0x0C, "0C - Engine RPM"},
        {0x0D, "0D - Vehicle Speed"},
        {0x0E, "0E - Timing Advance"},
        {0x0F, "0F - Intake Air Temp"},
        {0x10, "10 - MAF Rate"},
        {0x11, "11 - Throttle Position"},
        {0x1C, "1C - OBD Standard"},
        {0x1F, "1F - Run Time"},
        {0x21, "21 - Distance with MIL"},
        {0x2F, "2F - Fuel Level"},
        {0x31, "31 - Distance since codes cleared"},
        {0x33, "33 - Barometric Pressure"},
        {0x42, "42 - Control Module Voltage"},
        {0x46, "46 - Ambient Air Temp"},
        {0x49, "49 - Accelerator Pedal D"},
        {0x51, "51 - Fuel Type"},
        {0x5C, "5C - Engine Oil Temp"},
        {0x5E, "5E - Fuel Rate"},
    };
    static int known_sel = -1;
    ImGui::SetNextItemWidth(250);
    if (ImGui::BeginCombo("Known PIDs", known_sel >= 0
            ? known_pids[known_sel].label : "Select a PID...")) {
        for (int i = 0; i < static_cast<int>(std::size(known_pids)); ++i) {
            if (ImGui::Selectable(known_pids[i].label, i == known_sel)) {
                known_sel = i;
                uint8_t pid = known_pids[i].id;
                if (std::find(settings.obd_pids.begin(), settings.obd_pids.end(), pid)
                        == settings.obd_pids.end())
                    settings.obd_pids.push_back(pid);
            }
        }
        ImGui::EndCombo();
    }

    // Manual hex PID add
    static char pid_buf[8] = {};
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##AddPID", pid_buf, sizeof(pid_buf));
    ImGui::SameLine();
    if (ImGui::Button("Add PID")) {
        unsigned val = 0;
        if (sscanf(pid_buf, "%x", &val) == 1 && val <= 0xFF) {
            uint8_t pid = static_cast<uint8_t>(val);
            if (std::find(settings.obd_pids.begin(), settings.obd_pids.end(), pid)
                    == settings.obd_pids.end()) {
                settings.obd_pids.push_back(pid);
            }
            pid_buf[0] = '\0';
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Hex (00-FF)");

    // Query ECU for supported PIDs
    if (capture.is_connected()) {
        if (ImGui::Button("Query ECU Supported PIDs")) {
            auto* ch = capture.channel();
            if (ch) {
                ObdSession session(*ch);
                auto result = session.query_supported_pids();
                if (result) {
                    settings.obd_pids.clear();
                    for (auto& sp : *result) {
                        for (auto pid : sp.pids) {
                            // Skip supported-PIDs-range PIDs (0x00, 0x20, 0x40)
                            if (pid == 0x00 || pid == 0x20 || pid == 0x40 || pid == 0x60)
                                continue;
                            if (std::find(settings.obd_pids.begin(), settings.obd_pids.end(), pid)
                                    == settings.obd_pids.end())
                                settings.obd_pids.push_back(pid);
                        }
                    }
                } else {
                    state.error_message = result.error();
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Auto-detect from ECU");
    }
}

} // namespace canmatik
