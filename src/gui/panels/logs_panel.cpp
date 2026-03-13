/// @file logs_panel.cpp
/// Session logs panel implementation.

#include "gui/panels/logs_panel.h"
#include "imgui.h"

#include <format>

namespace canmatik {

static const char* severity_label(TraceSeverity s) {
    switch (s) {
    case TraceSeverity::info:     return "INFO";
    case TraceSeverity::warning:  return "WARN";
    case TraceSeverity::error:    return "ERROR";
    case TraceSeverity::debug:    return "DEBUG";
    case TraceSeverity::verbose:  return "VERB";
    case TraceSeverity::critical: return "CRIT";
    default:                      return "???";
    }
}

static ImVec4 severity_color(TraceSeverity s) {
    switch (s) {
    case TraceSeverity::warning:  return {1.0f, 0.9f, 0.2f, 1.0f};
    case TraceSeverity::error:    return {1.0f, 0.35f, 0.35f, 1.0f};
    case TraceSeverity::critical: return {1.0f, 0.1f, 0.1f, 1.0f};
    case TraceSeverity::debug:    return {0.6f, 0.6f, 0.7f, 1.0f};
    case TraceSeverity::verbose:  return {0.5f, 0.5f, 0.5f, 1.0f};
    default:                      return {0.9f, 0.9f, 0.9f, 1.0f};
    }
}

void render_logs_panel(GuiLogSink& sink) {
    // Toolbar
    static bool copy_requested = false;

    ImGui::Text("Entries: %u", static_cast<unsigned>(sink.size()));
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        sink.clear();
    ImGui::SameLine();
    if (ImGui::Button("Copy All"))
        copy_requested = true;
    ImGui::SameLine();

    // Severity filter checkboxes
    static bool show_info = true, show_warn = true, show_err = true,
                show_debug = true, show_verb = false;
    ImGui::Checkbox("Info", &show_info);   ImGui::SameLine();
    ImGui::Checkbox("Warn", &show_warn);   ImGui::SameLine();
    ImGui::Checkbox("Error", &show_err);   ImGui::SameLine();
    ImGui::Checkbox("Debug", &show_debug); ImGui::SameLine();
    ImGui::Checkbox("Verbose", &show_verb);

    ImGui::Separator();

    auto entries = sink.snapshot();

    // Build plain-text log
    static std::string log_text;
    log_text.clear();
    log_text.reserve(entries.size() * 80);

    for (auto& e : entries) {
        bool show = false;
        switch (e.severity) {
        case TraceSeverity::info:
        case TraceSeverity::verbose:
            show = (e.severity == TraceSeverity::info) ? show_info : show_verb; break;
        case TraceSeverity::warning:  show = show_warn;  break;
        case TraceSeverity::error:    show = show_err;   break;
        case TraceSeverity::critical: show = show_err;   break;
        case TraceSeverity::debug:    show = show_debug; break;
        default:                     show = true;        break;
        }
        if (!show) continue;

        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%u.%03u",
                 static_cast<unsigned>(e.timestamp_ms / 1000),
                 static_cast<unsigned>(e.timestamp_ms % 1000));
        log_text += time_buf;
        log_text += "  ";
        log_text += severity_label(e.severity);
        log_text += "  ";
        log_text += e.message;
        log_text += '\n';
    }

    if (copy_requested) {
        ImGui::SetClipboardText(log_text.c_str());
        copy_requested = false;
    }

    // Read-only multiline text — supports Ctrl+A / Ctrl+C natively
    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - footer);
    ImGui::InputTextMultiline("##LogText", log_text.data(), log_text.size() + 1,
                              size, ImGuiInputTextFlags_ReadOnly);

    // Status line
    ImGui::Text("Max: 10000 entries (oldest are discarded)");
}

} // namespace canmatik
