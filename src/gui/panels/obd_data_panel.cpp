/// @file obd_data_panel.cpp
/// OBD Data tab: decoded PID table with value-change flash and history graph.

#include "gui/panels/obd_data_panel.h"
#include "imgui.h"

#include <vector>
#include <limits>
#include <algorithm>

namespace canmatik {

static const ImVec4 kColorChanged  = {1.0f, 1.0f, 0.3f, 1.0f}; // yellow flash
static const ImVec4 kColorNormal   = {0.9f, 0.9f, 0.9f, 1.0f};

void render_obd_data_panel(const std::vector<ObdPidRow>& rows) {
    static int selected_pid_idx = -1;

    if (rows.empty()) {
        ImGui::TextDisabled("No OBD data. Press Start OBD above to begin streaming.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

    float table_h = (selected_pid_idx >= 0) ? ImGui::GetContentRegionAvail().y * 0.5f
                                             : ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginTable("##OBDPIDs", 5, flags, ImVec2(0, table_h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("PID",   ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Unit",  ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Raw",   ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // PID
            ImGui::TableNextColumn();
            bool is_selected = (selected_pid_idx == static_cast<int>(i));
            char label[16];
            snprintf(label, sizeof(label), "0x%02X", row.pid);
            if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns))
                selected_pid_idx = static_cast<int>(i);

            // Name
            ImGui::TableNextColumn();
            ImGui::Text("%s", row.name.c_str());

            // Value (flash on change)
            ImGui::TableNextColumn();
            ImVec4 color = row.value_changed ? kColorChanged : kColorNormal;
            ImGui::TextColored(color, "%.1f", row.value);

            // Unit
            ImGui::TableNextColumn();
            ImGui::Text("%s", row.unit.c_str());

            // Raw hex
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", row.raw_hex.c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // History graph for selected PID
    if (selected_pid_idx >= 0 && selected_pid_idx < static_cast<int>(rows.size())) {
        const auto& sel = rows[selected_pid_idx];
        ImGui::Separator();
        ImGui::Text("History: %s (0x%02X)", sel.name.c_str(), sel.pid);

        if (!sel.history.empty()) {
            // Convert deque to float array for PlotLines
            std::vector<float> values;
            values.reserve(sel.history.size());
            for (auto& [t, v] : sel.history)
                values.push_back(static_cast<float>(v));

            // Find min/max for axis
            float vmin = *std::min_element(values.begin(), values.end());
            float vmax = *std::max_element(values.begin(), values.end());
            if (vmin == vmax) { vmin -= 1.0f; vmax += 1.0f; }

            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.1f %s (last 60s)",
                     sel.value, sel.unit.c_str());

            ImGui::PlotLines("##History", values.data(),
                             static_cast<int>(values.size()),
                             0, overlay, vmin, vmax,
                             ImVec2(ImGui::GetContentRegionAvail().x,
                                    ImGui::GetContentRegionAvail().y));
        } else {
            ImGui::TextDisabled("No history data yet.");
        }
    }
}

} // namespace canmatik
