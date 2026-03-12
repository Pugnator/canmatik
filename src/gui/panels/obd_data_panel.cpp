/// @file obd_data_panel.cpp
/// OBD Data tab: decoded PID table with value-change flash and history graph.

#include "gui/panels/obd_data_panel.h"
#include "imgui.h"

#include <vector>
#include <limits>
#include <algorithm>

namespace canmatik {

static ImVec4 to_imvec4(const float c[4]) { return {c[0], c[1], c[2], c[3]}; }

void render_obd_data_panel(const std::vector<ObdPidRow>& rows, const GuiSettings& settings,
                          bool show_graph) {
    const ImVec4 kColorChanged = to_imvec4(settings.color_obd_changed);
    const ImVec4 kColorNormal  = to_imvec4(settings.color_obd_normal);
    const ImVec4 kColorDimText = to_imvec4(settings.color_obd_dim);
    static int selected_pid_idx = -1;

    if (rows.empty()) {
        ImGui::TextDisabled("No OBD data. Press Start OBD above to begin streaming.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

    float table_h = (show_graph && selected_pid_idx >= 0)
                        ? ImGui::GetContentRegionAvail().y * 0.5f
                        : ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginTable("##OBDPIDs", 5, flags, ImVec2(0, table_h))) {
        ImGui::SetWindowFontScale(settings.font_scale_obd);
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
            ImGui::TextColored(kColorNormal, "%s", row.name.c_str());

            // Value (flash on change)
            ImGui::TableNextColumn();
            ImVec4 color = row.value_changed ? kColorChanged : kColorNormal;
            ImGui::TextColored(color, "%.1f", row.value);

            // Unit
            ImGui::TableNextColumn();
            ImGui::TextColored(kColorNormal, "%s", row.unit.c_str());

            // Raw hex
            ImGui::TableNextColumn();
            ImGui::TextColored(kColorDimText, "%s", row.raw_hex.c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // History graph for selected PID
    if (show_graph && selected_pid_idx >= 0 && selected_pid_idx < static_cast<int>(rows.size())) {
        const auto& sel = rows[selected_pid_idx];
        ImGui::Separator();
        ImGui::Text("History: %s (0x%02X)", sel.name.c_str(), sel.pid);

        if (!sel.history.empty()) {
            // Convert deque to float array for PlotLines
            std::vector<float> values;
            values.reserve(sel.history.size());
            for (auto& [t, v] : sel.history)
                values.push_back(static_cast<float>(v));

            // Find data min/max
            float data_min = *std::min_element(values.begin(), values.end());
            float data_max = *std::max_element(values.begin(), values.end());
            if (data_min == data_max) { data_min -= 1.0f; data_max += 1.0f; }

            // Y-axis scale controls
            static bool obd_auto_scale = true;
            static float obd_y_min = 0.0f;
            static float obd_y_max = 100.0f;
            ImGui::SameLine();
            ImGui::Checkbox("Auto Y", &obd_auto_scale);
            if (!obd_auto_scale) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                ImGui::DragFloat("Y min##obd", &obd_y_min, 0.1f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                ImGui::DragFloat("Y max##obd", &obd_y_max, 0.1f);
                if (obd_y_min >= obd_y_max) obd_y_max = obd_y_min + 1.0f;
            } else {
                obd_y_min = data_min;
                obd_y_max = data_max;
            }

            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.1f %s (last 60s)",
                     sel.value, sel.unit.c_str());

            ImGui::PlotLines("##History", values.data(),
                             static_cast<int>(values.size()),
                             0, overlay, obd_y_min, obd_y_max,
                             ImVec2(ImGui::GetContentRegionAvail().x,
                                    ImGui::GetContentRegionAvail().y));
        } else {
            ImGui::TextDisabled("No history data yet.");
        }
    }
}

} // namespace canmatik
