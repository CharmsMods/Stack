#include "Editor/EditorModule.h"
#include "App/AppHeaderStyle.h"
#include "RawLabUiSupport.h"

#include <imgui_internal.h>

bool EditorModule::RenderRawWorkspaceLabGalleryNavigation(const ImVec2& size) {
    const float unit = ImGui::GetFontSize() / 13.0f;
    const ImVec4 panel = Stack::Header::ResolvePanelColor(GetWorkspaceBaseColor());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * unit, 10.0f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * unit, 4.0f * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("RawGalleryNavigation", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    RenderRawGalleryFolderPanel();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    return false;
}
