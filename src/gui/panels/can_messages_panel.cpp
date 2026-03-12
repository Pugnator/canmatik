/// @file can_messages_panel.cpp
/// CAN Messages table with diff highlighting, row selection, watchdog context menu.

#include "gui/panels/can_messages_panel.h"
#include "imgui.h"

#include <algorithm>
#include <format>

namespace canmatik {

static bool id_in_filter(const std::vector<uint32_t>& list, uint32_t id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

static ImVec4 to_imvec4(const float c[4]) { return {c[0], c[1], c[2], c[3]}; }

void render_can_messages_panel(const std::vector<MessageRow>& rows,
                               GuiState& state,
                               FrameCollector& collector,
                               GuiSettings& settings,
                               bool show_graph) {
    const ImVec4 kColorNew     = to_imvec4(settings.color_can_new);
    const ImVec4 kColorChanged = to_imvec4(settings.color_can_changed);
    const ImVec4 kColorDlc     = to_imvec4(settings.color_can_dlc);
    const ImVec4 kColorDefault = to_imvec4(settings.color_can_default);
    const ImVec4 kColorWatched = to_imvec4(settings.color_can_watched);

    static bool auto_scroll = true;
    ImGui::Checkbox("Auto-scroll", &auto_scroll);
    ImGui::SameLine();
    ImGui::TextColored(kColorNew, "New");
    ImGui::SameLine();
    ImGui::TextColored(kColorChanged, "Changed");
    ImGui::SameLine();
    ImGui::TextColored(kColorDlc, "DLC changed");
    ImGui::SameLine();
    ImGui::TextColored(kColorWatched, "Watched");

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;

    float avail = (show_graph && state.selected_row != 0)
                     ? ImGui::GetContentRegionAvail().y * 0.65f
                     : ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("##CANMessages", 6, flags, ImVec2(0, avail))) {
        ImGui::SetWindowFontScale(settings.font_scale_can);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("DLC",    ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Data",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time",   ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Count",  ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Watch",  ImGuiTableColumnFlags_WidthFixed, 20);
        ImGui::TableHeadersRow();

        // Tooltip for Count column header
        if (ImGui::TableGetColumnFlags(4) & ImGuiTableColumnFlags_IsHovered) {
            ImGui::SetTooltip("Number of times this CAN ID was received.\n"
                              "Resets when the buffer is cleared or a new file is opened.");
        }

        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            bool is_selected = (state.selected_row == row.arb_id);

            // ID column
            ImGui::TableNextColumn();
            ImVec4 id_color = row.is_new ? kColorNew
                            : row.is_watched ? kColorWatched
                            : kColorDefault;
            ImGui::TextColored(id_color, "0x%03X", row.arb_id);

            // Make entire row selectable
            ImGui::SameLine(0, 0);
            char sel_id[32];
            snprintf(sel_id, sizeof(sel_id), "##sel%d", static_cast<int>(i));
            if (ImGui::Selectable(sel_id, is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowOverlap)) {
                state.selected_row = row.arb_id;
            }

            // Right-click context menu
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (collector.is_watched(row.arb_id)) {
                    if (ImGui::MenuItem("Remove Watchdog"))
                        collector.remove_watchdog(row.arb_id);
                } else {
                    if (ImGui::MenuItem("Add Watchdog"))
                        collector.add_watchdog(row.arb_id);
                }
                ImGui::Separator();
                bool in_filter = id_in_filter(settings.id_filter_list, row.arb_id);
                if (in_filter) {
                    if (ImGui::MenuItem("Remove from ID Filter"))
                        settings.id_filter_list.erase(
                            std::remove(settings.id_filter_list.begin(),
                                        settings.id_filter_list.end(), row.arb_id),
                            settings.id_filter_list.end());
                } else {
                    if (ImGui::MenuItem("Exclude ID")) {
                        settings.id_filter_mode = IdFilterMode::EXCLUDE;
                        settings.id_filter_list.push_back(row.arb_id);
                    }
                    if (ImGui::MenuItem("Include Only ID")) {
                        settings.id_filter_mode = IdFilterMode::INCLUDE;
                        settings.id_filter_list.clear();
                        settings.id_filter_list.push_back(row.arb_id);
                    }
                }
                ImGui::EndPopup();
            }

            // DLC column
            ImGui::TableNextColumn();
            ImVec4 dlc_color = row.dlc_changed ? kColorDlc : kColorDefault;
            ImGui::TextColored(dlc_color, "%u", row.dlc);

            // Data column — per-byte coloring
            ImGui::TableNextColumn();
            for (uint8_t b = 0; b < row.dlc && b < 8; ++b) {
                if (b > 0) ImGui::SameLine(0, 4);
                ImVec4 color = row.is_new    ? kColorNew
                             : row.changed[b] ? kColorChanged
                             : kColorDefault;
                ImGui::TextColored(color, "%02X", row.data[b]);
            }

            // Time column
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", row.last_seen);

            // Count column
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(row.update_count));

            // Watch indicator
            ImGui::TableNextColumn();
            if (row.is_watched)
                ImGui::TextColored(kColorWatched, "*");

            ImGui::PopID();
        }

        // Auto-scroll
        if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndTable();
    }

    // Graph for selected CAN ID byte value
    if (show_graph && state.selected_row != 0) {
        static int graph_byte = 0;
        static bool can_auto_scale = true;
        static float can_y_min = 0.0f;
        static float can_y_max = 255.0f;

        ImGui::Separator();
        ImGui::Text("Byte graph: 0x%03X", state.selected_row);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("Byte##can_byte", &graph_byte);
        graph_byte = std::clamp(graph_byte, 0, 7);
        ImGui::SameLine();
        ImGui::Checkbox("Auto Y##can", &can_auto_scale);
        if (!can_auto_scale) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::DragFloat("Y min##can", &can_y_min, 0.5f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::DragFloat("Y max##can", &can_y_max, 0.5f);
            if (can_y_min >= can_y_max) can_y_max = can_y_min + 1.0f;
        }

        auto hist = collector.byte_history(state.selected_row, static_cast<uint8_t>(graph_byte));
        if (!hist.empty()) {
            std::vector<float> vals;
            vals.reserve(hist.size());
            for (auto& [t, v] : hist)
                vals.push_back(static_cast<float>(v));

            float data_min = *std::min_element(vals.begin(), vals.end());
            float data_max = *std::max_element(vals.begin(), vals.end());
            if (data_min == data_max) { data_min -= 1.0f; data_max += 1.0f; }
            if (can_auto_scale) { can_y_min = data_min; can_y_max = data_max; }

            char overlay[64];
            snprintf(overlay, sizeof(overlay), "Byte[%d] = %d (%zu samples)",
                     graph_byte, static_cast<int>(vals.back()), vals.size());

            ImGui::PlotLines("##CANGraph", vals.data(),
                             static_cast<int>(vals.size()),
                             0, overlay, can_y_min, can_y_max,
                             ImVec2(ImGui::GetContentRegionAvail().x,
                                    ImGui::GetContentRegionAvail().y));
        } else {
            ImGui::TextDisabled("No history for this ID yet.");
        }
    }
}

} // namespace canmatik
