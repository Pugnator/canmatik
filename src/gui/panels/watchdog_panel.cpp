/// @file watchdog_panel.cpp
/// Enhanced watchdog panel: per-ID table, clickable detail view with decode config,
/// scrolled value history, and graph placeholder.

#include "gui/panels/watchdog_panel.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace canmatik {

static ImVec4 to_imvec4(const float c[4]) { return {c[0], c[1], c[2], c[3]}; }

// Persistent state across frames
static uint32_t selected_watchdog_id = 0xFFFFFFFF;

void render_watchdog_panel(const std::vector<WatchdogSnapshot>& snapshots,
                           FrameCollector& collector,
                           const GuiSettings& settings) {
    const ImVec4 kColorWatched = to_imvec4(settings.color_can_watched);
    const ImVec4 kColorDefault = to_imvec4(settings.color_can_default);
    const ImVec4 kColorChanged = to_imvec4(settings.color_can_changed);
    const ImVec4 kColorNew     = to_imvec4(settings.color_can_new);
    const ImVec4 kColorDlc     = to_imvec4(settings.color_can_dlc);

    ImGui::TextColored(kColorWatched, "Watchdog (%zu IDs)", snapshots.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear All"))
        collector.clear_watchdogs();

    if (snapshots.empty()) return;

    // Determine layout: if something is selected, split left/right
    bool has_selection = false;
    const WatchdogSnapshot* sel_snap = nullptr;
    for (auto& ws : snapshots) {
        if (ws.arb_id == selected_watchdog_id) {
            has_selection = true;
            sel_snap = &ws;
            break;
        }
    }

    float total_w = ImGui::GetContentRegionAvail().x;
    float list_w  = has_selection ? total_w * 0.4f : total_w;
    float detail_w = total_w - list_w - 8;

    // === Left: Watched IDs table ===
    ImGui::BeginChild("##WatchList", ImVec2(list_w, 0), true);
    ImGui::SetWindowFontScale(settings.font_scale_can);
    {
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        float avail = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginTable("##WDTable", 6, flags, ImVec2(0, avail))) {
            ImGui::SetWindowFontScale(settings.font_scale_can);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID",    ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("DLC",   ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableSetupColumn("Data",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Time",  ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Cnt",   ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < snapshots.size(); ++i) {
                const auto& ws = snapshots[i];
                const auto& row = ws.row;
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                bool is_sel = (ws.arb_id == selected_watchdog_id);

                // ID (clickable)
                ImGui::TableNextColumn();
                char id_label[16];
                snprintf(id_label, sizeof(id_label), "0x%03X", ws.arb_id);
                if (ImGui::Selectable(id_label, is_sel,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_watchdog_id = is_sel ? 0xFFFFFFFF : ws.arb_id;
                }

                // Right-click to remove
                if (ImGui::BeginPopupContextItem("##wctx")) {
                    if (ImGui::MenuItem("Remove Watchdog")) {
                        collector.remove_watchdog(ws.arb_id);
                        if (is_sel) selected_watchdog_id = 0xFFFFFFFF;
                    }
                    ImGui::EndPopup();
                }

                // DLC
                ImGui::TableNextColumn();
                ImVec4 dlc_color = row.dlc_changed ? kColorDlc : kColorDefault;
                ImGui::TextColored(dlc_color, "%u", row.dlc);

                // Data bytes
                ImGui::TableNextColumn();
                for (uint8_t b = 0; b < row.dlc && b < 8; ++b) {
                    if (b > 0) ImGui::SameLine(0, 2);
                    ImVec4 color = row.is_new     ? kColorNew
                                 : row.changed[b] ? kColorChanged
                                 : kColorDefault;
                    ImGui::TextColored(color, "%02X", row.data[b]);
                }

                // Decoded value
                ImGui::TableNextColumn();
                ImGui::TextColored(kColorWatched, "%.2f", ws.current_value);

                // Time
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", row.last_seen);

                // Count
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.update_count));

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // === Right: Detail view for selected watchdog ===
    if (has_selection && sel_snap) {
        ImGui::SameLine();
        ImGui::BeginChild("##WatchDetail", ImVec2(detail_w, 0), true);
        ImGui::SetWindowFontScale(settings.font_scale_can);
        {
            ImGui::TextColored(kColorWatched, "0x%03X Detail", sel_snap->arb_id);
            ImGui::Separator();

            // --- Decode Configuration ---
            ImGui::Text("Decode:");
            WatchdogConfig cfg = sel_snap->config;
            bool cfg_changed = false;

            static const char* mode_labels[] = {"Single Byte", "Byte Range", "Formula"};
            int mode_idx = static_cast<int>(cfg.mode);
            ImGui::SetNextItemWidth(120);
            if (ImGui::Combo("Mode##wd", &mode_idx, mode_labels, 3)) {
                cfg.mode = static_cast<WatchdogDecodeMode>(mode_idx);
                cfg_changed = true;
            }

            // Byte start
            int bs = cfg.byte_start;
            ImGui::SetNextItemWidth(60);
            if (ImGui::InputInt("Start byte##wd", &bs)) {
                cfg.byte_start = static_cast<uint8_t>(std::clamp(bs, 0, 7));
                cfg_changed = true;
            }

            if (cfg.mode == WatchdogDecodeMode::RAW_BYTES ||
                cfg.mode == WatchdogDecodeMode::FORMULA) {
                int bc = cfg.byte_count;
                ImGui::SetNextItemWidth(60);
                if (ImGui::InputInt("Byte count##wd", &bc)) {
                    cfg.byte_count = static_cast<uint8_t>(std::clamp(bc, 1, 8 - cfg.byte_start));
                    cfg_changed = true;
                }
            }

            if (cfg.mode == WatchdogDecodeMode::FORMULA) {
                ImGui::SetNextItemWidth(80);
                if (ImGui::InputDouble("Scale##wd", &cfg.scale, 0, 0, "%.4f"))
                    cfg_changed = true;
                ImGui::SetNextItemWidth(80);
                if (ImGui::InputDouble("Offset##wd", &cfg.offset, 0, 0, "%.4f"))
                    cfg_changed = true;
                ImGui::SetNextItemWidth(80);
                if (ImGui::InputDouble("Divisor##wd", &cfg.divisor, 0, 0, "%.4f"))
                    cfg_changed = true;
                ImGui::TextDisabled("Result = (raw * scale + offset) / divisor");
            }

            if (cfg_changed)
                collector.set_watchdog_config(sel_snap->arb_id, cfg);

            ImGui::Separator();

            // --- Current value ---
            ImGui::Text("Current: ");
            ImGui::SameLine();
            ImGui::TextColored(kColorWatched, "%.4f", sel_snap->current_value);
            ImGui::Text("Samples: %zu", sel_snap->history.size());

            ImGui::Separator();

            // --- Graph (placeholder using PlotLines) ---
            ImGui::Text("Graph:");
            if (!sel_snap->history.empty()) {
                std::vector<float> vals;
                vals.reserve(sel_snap->history.size());
                for (auto& h : sel_snap->history)
                    vals.push_back(static_cast<float>(h.value));

                float vmin = *std::min_element(vals.begin(), vals.end());
                float vmax = *std::max_element(vals.begin(), vals.end());
                if (vmin == vmax) { vmin -= 1.0f; vmax += 1.0f; }

                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%.2f",
                         sel_snap->current_value);
                ImGui::PlotLines("##WDGraph", vals.data(),
                                 static_cast<int>(vals.size()),
                                 0, overlay, vmin, vmax,
                                 ImVec2(ImGui::GetContentRegionAvail().x, 120));
            } else {
                ImGui::TextDisabled("No history yet.");
            }

            ImGui::Separator();

            // --- Scrolled value history ---
            ImGui::Text("History (newest first):");
            float hist_h = ImGui::GetContentRegionAvail().y;
            if (ImGui::BeginChild("##WDHist", ImVec2(0, hist_h), false)) {
                ImGui::SetWindowFontScale(settings.font_scale_can);
                // Show newest first
                for (int j = static_cast<int>(sel_snap->history.size()) - 1; j >= 0; --j) {
                    auto& h = sel_snap->history[j];
                    ImGui::Text("[%.3fs] %.4f", h.timestamp, h.value);
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }
}

} // namespace canmatik
