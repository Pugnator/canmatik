/// @file dtc_panel.cpp
/// DTC tab: read stored/pending DTCs, clear codes.

#include "gui/panels/dtc_panel.h"
#include "obd/obd_session.h"
#include "imgui.h"

#include <cctype>
#include <cstring>
#include <format>

namespace canmatik {

static void render_dtc_table(const char* label, const std::vector<DtcCode>& dtcs) {
    if (dtcs.empty()) {
        ImGui::TextDisabled("No %s.", label);
        return;
    }

    ImGui::Text("%s (%d):", label, static_cast<int>(dtcs.size()));

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

    char table_id[32];
    snprintf(table_id, sizeof(table_id), "##%s", label);

    float h = std::min(200.0f, ImGui::GetContentRegionAvail().y * 0.4f);
    if (ImGui::BeginTable(table_id, 4, flags, ImVec2(0, h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Code",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("ECU",      ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Raw",      ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < dtcs.size(); ++i) {
            const auto& dtc = dtcs[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableNextColumn();
            ImVec4 color = dtc.pending ? ImVec4{1.0f, 0.9f, 0.2f, 1.0f}
                                       : ImVec4{1.0f, 0.3f, 0.3f, 1.0f};
            ImGui::TextColored(color, "%s", dtc.code.c_str());

            ImGui::TableNextColumn();
            const char* cat_names[] = {"Powertrain", "Chassis", "Body", "Network"};
            int cat_idx = static_cast<int>(dtc.category);
            ImGui::Text("%s", (cat_idx >= 0 && cat_idx < 4) ? cat_names[cat_idx] : "?");

            ImGui::TableNextColumn();
            ImGui::Text("0x%03X", dtc.ecu_id);

            ImGui::TableNextColumn();
            ImGui::TextDisabled("0x%04X", dtc.raw);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

/// Parse a DTC string like "P0300" into a DtcCode.
static bool parse_dtc_string(const char* str, DtcCode& out) {
    if (!str || strlen(str) != 5) return false;
    char prefix = static_cast<char>(toupper(str[0]));
    DtcCategory cat;
    switch (prefix) {
    case 'P': cat = DtcCategory::Powertrain; break;
    case 'C': cat = DtcCategory::Chassis;    break;
    case 'B': cat = DtcCategory::Body;       break;
    case 'U': cat = DtcCategory::Network;    break;
    default: return false;
    }
    for (int i = 1; i < 5; ++i)
        if (!isdigit(static_cast<unsigned char>(str[i]))) return false;

    uint8_t d2 = str[1] - '0';
    uint8_t d3 = str[2] - '0';
    uint8_t d4 = str[3] - '0';
    uint8_t d5 = str[4] - '0';
    uint8_t byte0 = static_cast<uint8_t>((static_cast<uint8_t>(cat) << 6) | (d2 << 4) | d3);
    uint8_t byte1 = static_cast<uint8_t>((d4 << 4) | d5);

    out.category = cat;
    out.code     = std::string(1, prefix) + std::string(str + 1, 4);
    out.raw      = static_cast<uint16_t>((byte0 << 8) | byte1);
    out.ecu_id   = 0;
    out.pending  = false;
    return true;
}

void render_dtc_panel(DtcPanelState& dtc_state, CaptureController& capture,
                      GuiState& gui_state, bool is_file_mode) {

    // Live action buttons (only when connected and not in file mode)
    if (!is_file_mode) {
        if (!capture.is_connected()) {
            ImGui::TextDisabled("Not connected. Connect from the CAN Messages tab first.");
        } else {
            if (ImGui::Button("Read Stored DTCs")) {
                auto* ch = capture.channel();
                if (ch) {
                    ObdSession session(*ch);
                    auto result = session.read_dtcs();
                    if (result)
                        dtc_state.stored_dtcs = std::move(*result);
                    else
                        dtc_state.status_message = result.error();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Read Pending DTCs")) {
                auto* ch = capture.channel();
                if (ch) {
                    ObdSession session(*ch);
                    auto result = session.read_pending_dtcs();
                    if (result)
                        dtc_state.pending_dtcs = std::move(*result);
                    else
                        dtc_state.status_message = result.error();
                }
            }
            ImGui::SameLine();

            // Clear DTCs with confirmation
            static bool confirm_clear = false;
            if (!confirm_clear) {
                if (ImGui::Button("Clear DTCs"))
                    confirm_clear = true;
            } else {
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Are you sure?");
                ImGui::SameLine();
                if (ImGui::Button("Yes, Clear")) {
                    auto* ch = capture.channel();
                    if (ch) {
                        ObdSession session(*ch);
                        auto result = session.clear_dtcs(true);
                        if (result) {
                            dtc_state.stored_dtcs.clear();
                            dtc_state.pending_dtcs.clear();
                            dtc_state.status_message = "DTCs cleared successfully.";
                        } else {
                            dtc_state.status_message = result.error();
                        }
                    }
                    confirm_clear = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    confirm_clear = false;
            }
        }
    } else {
        ImGui::TextDisabled("File mode ÔÇö DTCs decoded from recording (if any).");
    }

    ImGui::Separator();

    // Manual DTC entry (available in all modes)
    ImGui::Text("Add DTC manually:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    // Category dropdown + 4-digit input
    static int manual_cat = 0;
    const char* cat_prefixes[] = {"P", "C", "B", "U"};
    ImGui::Combo("##DtcCat", &manual_cat, cat_prefixes, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    static char manual_digits[5] = {};
    ImGui::InputText("##DtcDigits", manual_digits, sizeof(manual_digits),
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        char full_code[6] = {};
        snprintf(full_code, sizeof(full_code), "%s%s",
                 cat_prefixes[manual_cat], manual_digits);
        DtcCode dtc;
        if (parse_dtc_string(full_code, dtc)) {
            dtc_state.manual_dtcs.push_back(dtc);
            dtc_state.status_message.clear();
        } else {
            dtc_state.status_message = "Invalid DTC code. Format: P0300 (1 letter + 4 digits).";
        }
    }
    if (!dtc_state.manual_dtcs.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear Manual"))
            dtc_state.manual_dtcs.clear();
    }

    // Status message
    if (!dtc_state.status_message.empty()) {
        ImGui::TextWrapped("%s", dtc_state.status_message.c_str());
    }

    ImGui::Separator();

    // DTC tables
    render_dtc_table("Stored DTCs", dtc_state.stored_dtcs);
    ImGui::Separator();
    render_dtc_table("Pending DTCs", dtc_state.pending_dtcs);
    if (!dtc_state.manual_dtcs.empty()) {
        ImGui::Separator();
        render_dtc_table("Manual DTCs", dtc_state.manual_dtcs);
    }
}

} // namespace canmatik
