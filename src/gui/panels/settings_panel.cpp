/// @file settings_panel.cpp
/// Settings tab implementation.

#include "gui/panels/settings_panel.h"
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

    // ----- Connection section -----
    ImGui::SeparatorText("Connection");

    // Provider scan + dropdown
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
    if (ImGui::BeginCombo("Provider", provider_idx >= 0 && provider_idx < static_cast<int>(provider_names.size())
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

    // Bitrate combo
    static const uint32_t bitrates[] = {125000, 250000, 500000, 1000000};
    static const char* bitrate_labels[] = {"125 kbps", "250 kbps", "500 kbps", "1 Mbps"};
    int br_idx = 2; // default 500k
    for (int i = 0; i < 4; ++i)
        if (bitrates[i] == settings.bitrate) br_idx = i;
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Bitrate", &br_idx, bitrate_labels, 4))
        settings.bitrate = bitrates[br_idx];

    // Mock checkbox
    ImGui::Checkbox("Mock mode", &settings.mock_enabled);

    // Connect / Disconnect button (main one is on CAN tab)
    ImGui::TextDisabled(capture.is_connected() ? "Connected" : "Disconnected");

    // ----- Capture Buffer section -----
    ImGui::SeparatorText("Capture Buffer");
    int cap = static_cast<int>(settings.buffer_capacity);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Buffer capacity", &cap)) {
        settings.buffer_capacity = static_cast<uint32_t>(std::clamp(cap, 1000, 10000000));
        collector.resize_buffer(settings.buffer_capacity);
    }
    ImGui::TextDisabled("Range: 1,000 — 10,000,000 frames");

    // ----- ID Filter section -----
    ImGui::SeparatorText("ID Filter");
    {
        int fm = static_cast<int>(settings.id_filter_mode);
        ImGui::RadioButton("Exclude listed IDs", &fm, 0); ImGui::SameLine();
        ImGui::RadioButton("Include only listed IDs", &fm, 1);
        settings.id_filter_mode = static_cast<IdFilterMode>(fm);
        ImGui::TextDisabled(settings.id_filter_mode == IdFilterMode::EXCLUDE
            ? "Listed IDs are hidden. Empty list = show all."
            : "Only listed IDs are shown. Empty list = show all.");

        int remove_filter_idx = -1;
        for (size_t i = 0; i < settings.id_filter_list.size(); ++i) {
            ImGui::PushID(static_cast<int>(i) + 10000);
            ImGui::BulletText("0x%03X", settings.id_filter_list[i]);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                remove_filter_idx = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove_filter_idx >= 0)
            settings.id_filter_list.erase(settings.id_filter_list.begin() + remove_filter_idx);

        static char id_buf[12] = {};
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("##AddFilterID", id_buf, sizeof(id_buf));
        ImGui::SameLine();
        if (ImGui::Button("Add ID")) {
            unsigned val = 0;
            if (sscanf(id_buf, "%x", &val) == 1 && val <= 0x1FFFFFFF) {
                auto id_val = static_cast<uint32_t>(val);
                if (std::find(settings.id_filter_list.begin(), settings.id_filter_list.end(), id_val)
                        == settings.id_filter_list.end())
                    settings.id_filter_list.push_back(id_val);
                id_buf[0] = '\0';
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Hex CAN ID");
        if (!settings.id_filter_list.empty()) {
            if (ImGui::Button("Clear All IDs"))
                settings.id_filter_list.clear();
        }
    }

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
