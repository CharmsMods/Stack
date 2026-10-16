#include "Editor/EditorModule.h"
#include "App/AppHeaderStyle.h"
#include "RawLabUiSupport.h"

#include <imgui_internal.h>

bool EditorModule::RenderRawWorkspaceLabGalleryNavigation(const ImVec2& size) {
    const float unit = ImGui::GetFontSize() / 16.f;
    const ImVec4 panel = Stack::Header::ResolvePanelColor(GetWorkspaceBaseColor());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f * unit, 16.f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    if (ImGui::BeginChild("RawGalleryNavigation", size, ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) RenderRawGalleryFolderPanel();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return false;
}
