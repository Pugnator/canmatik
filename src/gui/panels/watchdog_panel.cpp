/// @file watchdog_panel.cpp
/// Watchdog sub-panel implementation.

#include "gui/panels/watchdog_panel.h"
#include "imgui.h"

namespace canmatik {

static const ImVec4 kColorChanged = {1.0f, 0.3f, 0.3f, 1.0f};
static const ImVec4 kColorNew     = {0.3f, 1.0f, 0.3f, 1.0f};
static const ImVec4 kColorDlc     = {1.0f, 0.9f, 0.2f, 1.0f};
static const ImVec4 kColorDefault = {0.9f, 0.9f, 0.9f, 1.0f};
static const ImVec4 kColorWatched = {0.4f, 0.8f, 1.0f, 1.0f};

void render_watchdog_panel(const std::vector<MessageRow>& watched_rows,
                           FrameCollector& collector) {
    ImGui::TextColored(kColorWatched, "Watchdog Panel (%zu IDs)", watched_rows.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear All"))
        collector.clear_watchdogs();

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    float avail = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("##Watchdog", 5, flags, ImVec2(0, avail))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",    ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("DLC",   ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Data",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < watched_rows.size(); ++i) {
            const auto& row = watched_rows[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // ID
            ImGui::TableNextColumn();
            ImGui::TextColored(kColorWatched, "0x%03X", row.arb_id);

            // Right-click to remove
            if (ImGui::BeginPopupContextItem("##wctx")) {
                if (ImGui::MenuItem("Remove Watchdog"))
                    collector.remove_watchdog(row.arb_id);
                ImGui::EndPopup();
            }

            // DLC
            ImGui::TableNextColumn();
            ImVec4 dlc_color = row.dlc_changed ? kColorDlc : kColorDefault;
            ImGui::TextColored(dlc_color, "%u", row.dlc);

            // Data bytes
            ImGui::TableNextColumn();
            for (uint8_t b = 0; b < row.dlc && b < 8; ++b) {
                if (b > 0) ImGui::SameLine(0, 4);
                ImVec4 color = row.is_new     ? kColorNew
                             : row.changed[b] ? kColorChanged
                             : kColorDefault;
                ImGui::TextColored(color, "%02X", row.data[b]);
            }

            // Time
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", row.last_seen);

            // Count
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(row.update_count));

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace canmatik
