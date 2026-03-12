/// @file menu_bar.cpp
/// Main menu bar implementation.

#include "gui/panels/menu_bar.h"
#include "imgui.h"

namespace canmatik {

void render_menu_bar(GuiState& state, bool& show_watchdog, bool& show_graph,
                     std::function<void()> on_open_file,
                     std::function<void()> on_save_buffer) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O"))
                on_open_file();
            if (ImGui::MenuItem("Save Buffer...", "Ctrl+S"))
                on_save_buffer();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                state.playback = PlaybackState::STOPPED; // signal quit handled by main loop
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Watchdog Panel", nullptr, &show_watchdog);
            ImGui::MenuItem("Graph Panel", nullptr, &show_graph);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                ImGui::OpenPopup("About CANmatik");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // About popup
    if (ImGui::BeginPopupModal("About CANmatik", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("CANmatik GUI Analyzer");
        ImGui::Text("CAN Bus Scanner & OBD-II Tool");
        ImGui::Separator();
        ImGui::Text("Built with ImGui + Win32/OpenGL");
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace canmatik
