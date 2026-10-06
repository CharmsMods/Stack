#include "RawLabModeSwitcher.h"
#include "imgui_internal.h"
#include <algorithm>
#include <string>

namespace Stack::Editor::RawLabInternal {
int RenderRawLabModeSwitcher(int selectedMode, bool enabled,
    bool showBracket, bool showEdit, int editCount,
    const char* bracketLabel, bool localLayout) {
    const auto* viewport = ImGui::GetMainViewport();
    const float width = localLayout ? std::min(224.0f, ImGui::GetContentRegionAvail().x) : 224.0f;
    const float height = localLayout ? 30.0f : 36.0f;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin = localLayout
        ? ImVec2(cursor.x + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * .5f), cursor.y)
        : ImVec2(viewport->Pos.x + (viewport->Size.x - width) * .5f,
            std::max(viewport->Pos.y + 56.0f, ImGui::GetWindowPos().y + 3.0f));
    ImGui::SetCursorScreenPos(origin);
    auto* draw = ImGui::GetWindowDrawList();
    if (showBracket || showEdit)
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
            ImGui::GetColorU32(ImGuiCol_Text, .025f), height * .5f);
    ImGui::BeginDisabled(!enabled);
    int requested = -1;
    if (!showBracket && !showEdit) {
        ImGui::Dummy(ImVec2(width, height));
        ImGui::EndDisabled();
        return requested;
    }
    const float buttonWidth = showBracket && showEdit ? width * .5f : width;
    bool rendered = false;
    for (int mode = 0; mode < 2; ++mode) {
        const bool visible = mode == 0 ? showBracket : showEdit;
        if (!visible) continue;
        const std::string label = mode == 0 ? bracketLabel :
            editCount > 0 ? "Edit " + std::to_string(editCount) : "Edit";
        if (rendered) ImGui::SameLine(0, 0);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + buttonWidth, min.y + height);
        ImGui::PushID(mode);
        if (ImGui::InvisibleButton("##mode", ImVec2(buttonWidth, height)))
            requested = mode;
        const bool active = selectedMode == mode;
        if (active || ImGui::IsItemHovered())
            draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_Text, active ? .045f : .025f), height * .5f);
        if (active) draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Text, .08f), height * .5f);
        const auto textSize = ImGui::CalcTextSize(label.c_str());
        draw->AddText(ImVec2(min.x + (buttonWidth - textSize.x) * .5f, min.y + (height - textSize.y) * .5f),
            ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), label.c_str());
        ImGui::PopID();
        rendered = true;
    }
    ImGui::EndDisabled();
    return requested;
}
}
