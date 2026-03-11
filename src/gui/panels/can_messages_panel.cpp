/// @file can_messages_panel.cpp
/// CAN Messages table with diff highlighting, row selection, watchdog context menu.

#include "gui/panels/can_messages_panel.h"
#include "imgui.h"

#include <algorithm>
#include <format>

namespace canmatik {

static const ImVec4 kColorChanged = {1.0f, 0.3f, 0.3f, 1.0f};  // red
static const ImVec4 kColorNew     = {0.3f, 1.0f, 0.3f, 1.0f};  // green
static const ImVec4 kColorDlc     = {1.0f, 0.9f, 0.2f, 1.0f};  // yellow
static const ImVec4 kColorDefault = {0.9f, 0.9f, 0.9f, 1.0f};  // white-ish
static const ImVec4 kColorWatched = {0.4f, 0.8f, 1.0f, 1.0f};  // cyan

static bool id_in_filter(const std::vector<uint32_t>& list, uint32_t id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

void render_can_messages_panel(const std::vector<MessageRow>& rows,
                               GuiState& state,
                               FrameCollector& collector,
                               GuiSettings& settings) {
    static bool auto_scroll = true;
    ImGui::Checkbox("Auto-scroll", &auto_scroll);

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;

    float avail = ImGui::GetContentRegionAvail().y * 0.65f;
    if (ImGui::BeginTable("##CANMessages", 6, flags, ImVec2(0, avail))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",     ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("DLC",    ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Data",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time",   ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Count",  ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Watch",  ImGuiTableColumnFlags_WidthFixed, 20);
        ImGui::TableHeadersRow();

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
                    if (settings.id_filter_mode == IdFilterMode::EXCLUDE) {
                        if (ImGui::MenuItem("Exclude ID"))
                            settings.id_filter_list.push_back(row.arb_id);
                    } else {
                        if (ImGui::MenuItem("Include ID"))
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
}

} // namespace canmatik
