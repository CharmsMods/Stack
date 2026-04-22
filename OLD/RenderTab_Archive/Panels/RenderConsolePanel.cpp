#include "RenderConsolePanel.h"

#include <imgui.h>

namespace RenderConsolePanel {

void Render(const RenderConsolePanelModel& model) {
    ImGui::TextUnformatted("Render Runtime Log");
    ImGui::Separator();

    if (model.lines.empty()) {
        ImGui::TextDisabled("No runtime events recorded yet.");
        return;
    }

    ImGui::BeginChild("RenderConsoleScrollRegion", ImVec2(0.0f, 0.0f), true);
    for (const std::string& line : model.lines) {
        ImGui::TextWrapped("%s", line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

} // namespace RenderConsolePanel
