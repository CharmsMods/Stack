#include "RenderDockLayout.h"

#include "../PanelRegistry.h"
#include <imgui_internal.h>

namespace RenderDockLayout {

ImGuiID GetDockSpaceId() {
    return ImGui::GetID("RenderDockSpace");
}

void ApplyDefaultLayout(ImGuiID dockSpaceId, const ImVec2& dockSpaceSize) {
    ImGui::DockBuilderRemoveNode(dockSpaceId);
    ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockSpaceId, dockSpaceSize);

    ImGuiID dockMain = dockSpaceId;
    ImGuiID dockLeft = 0;
    ImGuiID dockRight = 0;
    ImGuiID dockBottom = 0;
    ImGuiID dockRightBottom = 0;

    dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
    dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
    dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);
    dockRightBottom = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.48f, nullptr, &dockRight);

    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::Viewport).windowTitle, dockMain);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::Outliner).windowTitle, dockLeft);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::AssetBrowser).windowTitle, dockLeft);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::Inspector).windowTitle, dockRight);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::Settings).windowTitle, dockRightBottom);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::AovDebug).windowTitle, dockRightBottom);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::RenderManager).windowTitle, dockBottom);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::Statistics).windowTitle, dockBottom);
    ImGui::DockBuilderDockWindow(RenderPanelRegistry::GetDefinition(RenderPanelId::Console).windowTitle, dockBottom);

    ImGui::DockBuilderFinish(dockSpaceId);
}

} // namespace RenderDockLayout
