/// @file sniff_panel.cpp
/// ImGui sniff panel implementation: per-ID table with byte-level diff coloring.

#include "gui/panels/sniff_panel.h"
#include "core/timestamp.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace canmatik {

// ---------------------------------------------------------------------------
// SniffCollector
// ---------------------------------------------------------------------------

SniffCollector::SniffCollector(uint64_t session_start_us)
    : session_start_us_(session_start_us) {}

void SniffCollector::onFrame(const CanFrame& frame) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us_);

    std::lock_guard lock(mu_);

    auto [it, inserted] = rows_.try_emplace(frame.arbitration_id);
    SniffRow& row = it->second;

    if (inserted) {
        // First time seeing this ID
        row.arb_id   = frame.arbitration_id;
        row.dlc      = frame.dlc;
        row.is_new   = true;
        row.dlc_changed = false;
        uint8_t n = (frame.dlc <= 8) ? frame.dlc : uint8_t{8};
        for (uint8_t i = 0; i < n; ++i) {
            row.data[i]    = frame.data[i];
            row.changed[i] = false;
        }
        row.last_seen    = rel;
        row.update_count = 1;
        return;
    }

    // Check for actual changes
    bool any_change = false;
    row.dlc_changed  = (frame.dlc != row.dlc);
    if (row.dlc_changed) any_change = true;

    uint8_t n = (frame.dlc <= 8) ? frame.dlc : uint8_t{8};
    for (uint8_t i = 0; i < 8; ++i) {
        if (i < n) {
            bool diff = (i >= row.dlc) || (frame.data[i] != row.data[i]);
            row.changed[i] = diff;
            if (diff) any_change = true;
        } else {
            row.changed[i] = false;
        }
    }

    if (!any_change) return;  // skip unchanged

    // Apply update
    row.is_new = false;
    row.dlc    = frame.dlc;
    for (uint8_t i = 0; i < n; ++i) row.data[i] = frame.data[i];
    row.last_seen = rel;
    row.update_count++;
}

void SniffCollector::onError(const TransportError& /*error*/) {
    // Errors not shown in sniff panel for now
}

std::vector<SniffRow> SniffCollector::snapshot() {
    std::lock_guard lock(mu_);
    std::vector<SniffRow> out;
    out.reserve(rows_.size());
    for (auto& [id, row] : rows_) {
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(), [](const SniffRow& a, const SniffRow& b) {
        return a.arb_id < b.arb_id;
    });
    return out;
}

uint64_t SniffCollector::unique_ids() const {
    std::lock_guard lock(mu_);
    return rows_.size();
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

static constexpr ImVec4 kColorNormal  = {0.86f, 0.86f, 0.86f, 1.0f};  // light gray
static constexpr ImVec4 kColorChanged = {1.0f,  0.3f,  0.3f,  1.0f};  // red
static constexpr ImVec4 kColorNew     = {0.3f,  1.0f,  0.3f,  1.0f};  // green
static constexpr ImVec4 kColorDlc     = {1.0f,  0.9f,  0.2f,  1.0f};  // yellow
static constexpr ImVec4 kColorId      = {0.4f,  0.8f,  1.0f,  1.0f};  // cyan

void render_sniff_panel(SniffCollector& collector) {
    auto rows = collector.snapshot();

    ImGui::Begin("CAN Sniff");

    ImGui::Text("Unique IDs: %zu", rows.size());
    ImGui::Separator();

    if (ImGui::BeginTable("sniff_table", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",      ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("DLC",     ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Data",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time",    ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Count",   ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const auto& row : rows) {
            ImGui::TableNextRow();

            // Column 0: Arb ID (cyan)
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kColorId, "%03X", row.arb_id);

            // Column 1: DLC (yellow if changed)
            ImGui::TableSetColumnIndex(1);
            if (row.dlc_changed) {
                ImGui::TextColored(kColorDlc, "%d", row.dlc);
            } else {
                ImGui::Text("%d", row.dlc);
            }

            // Column 2: Payload bytes (red for changed, green for new)
            ImGui::TableSetColumnIndex(2);
            for (uint8_t i = 0; i < row.dlc && i < 8; ++i) {
                if (i > 0) ImGui::SameLine(0.0f, 4.0f);
                char byte_str[4];
                snprintf(byte_str, sizeof(byte_str), "%02X", row.data[i]);

                if (row.is_new) {
                    ImGui::TextColored(kColorNew, "%s", byte_str);
                } else if (row.changed[i]) {
                    ImGui::TextColored(kColorChanged, "%s", byte_str);
                } else {
                    ImGui::TextColored(kColorNormal, "%s", byte_str);
                }
            }

            // Column 3: Last-seen relative timestamp
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("+%.3f", row.last_seen);

            // Column 4: Update count
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%llu", static_cast<unsigned long long>(row.update_count));
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace canmatik
