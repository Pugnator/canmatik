/// @file status_bar.cpp
/// Bottom status bar implementation.

#include "gui/panels/status_bar.h"
#include "imgui.h"

#include <format>

namespace canmatik {

void render_status_bar(const GuiState& state,
                       const CaptureController& capture,
                       const FrameCollector& collector) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float bar_h = 24.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - bar_h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, bar_h));

    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Connection state
    if (capture.is_connected()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Connected");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Disconnected");
    }

    ImGui::SameLine(0, 20);
    ImGui::Text("Frames: %llu", static_cast<unsigned long long>(collector.buffer_count()));

    ImGui::SameLine(0, 20);
    ImGui::Text("IDs: %llu", static_cast<unsigned long long>(collector.unique_ids()));

    ImGui::SameLine(0, 20);
    auto cap  = collector.buffer_capacity();
    auto used = collector.buffer_count();
    float pct = cap > 0 ? (static_cast<float>(used) / static_cast<float>(cap)) * 100.0f : 0.0f;
    ImGui::Text("Buffer: %llu / %u (%.0f%%)",
                static_cast<unsigned long long>(used), cap, pct);

    ImGui::SameLine(0, 20);
    if (state.total_errors > 0)
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Errors: %llu",
                           static_cast<unsigned long long>(state.total_errors));

    ImGui::End();
}

} // namespace canmatik
