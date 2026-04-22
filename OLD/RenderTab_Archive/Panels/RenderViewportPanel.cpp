#include "RenderViewportPanel.h"

#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace RenderViewportPanel {

RenderViewportPanelResult Render(const RenderViewportPanelModel& model) {
    RenderViewportPanelResult result {};

    ImGui::Text("Viewport: %s", model.jobStateLabel);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", model.jobStatusText.c_str());
    ImGui::TextDisabled("%dx%d | %u samples | goal: %d | resets: %u",
        model.textureWidth,
        model.textureHeight,
        model.sampleCount,
        model.sampleTarget,
        model.resetCount);

    if (model.textureId == 0 || model.textureWidth <= 0 || model.textureHeight <= 0) {
        ImGui::Spacing();
        ImGui::TextWrapped("No viewport texture is available yet. Resume the live viewport from Render Manager.");
        result.windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        return result;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 220.0f);
    available.y = std::max(available.y, 180.0f);

    const float scaleX = available.x / static_cast<float>(model.textureWidth);
    const float scaleY = available.y / static_cast<float>(model.textureHeight);
    const float scale = std::max(0.1f, std::min(scaleX, scaleY));
    const ImVec2 imageSize(
        static_cast<float>(model.textureWidth) * scale,
        static_cast<float>(model.textureHeight) * scale);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - imageSize.x) * 0.5f));
    ImGui::Image((ImTextureID)(intptr_t)model.textureId, imageSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    result.hasRenderableImage = true;
    result.imageHovered = ImGui::IsItemHovered();
    result.windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    result.leftClicked = result.imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    result.rightClicked = result.imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    result.leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    result.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    result.mouseDownLeft = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    result.mouseDownRight = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    result.mousePosition = ImGui::GetMousePos();

    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    result.imageMin = imageMin;
    result.imageMax = imageMax;
    result.imageSize = imageSize;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(imageMin, imageMax, IM_COL32(120, 136, 168, 220), 0.0f, 0, 1.5f);

    char overlayLine[256];
    std::snprintf(
        overlayLine,
        sizeof(overlayLine),
        "Samples %u | goal %d | %s",
        model.sampleCount,
        model.sampleTarget,
        model.previewRequested ? "active" : "paused");
    char controlLine[256];
    std::snprintf(
        controlLine,
        sizeof(controlLine),
        "%s | %.2f m/s | RMB look | WASD move | Q/E vertical | Shift fast | Ctrl slow",
        model.navigationActive ? "Fly active" : "Hold RMB over the viewport to fly",
        model.navigationMoveSpeed);

    const ImVec2 overlayMin(imageMin.x + 10.0f, imageMin.y + 10.0f);
    const ImVec2 overlayMax(imageMin.x + std::min(imageSize.x - 10.0f, 520.0f), imageMin.y + 84.0f);
    drawList->AddRectFilled(overlayMin, overlayMax, IM_COL32(8, 12, 18, 210), 6.0f);
    drawList->AddText(ImVec2(overlayMin.x + 10.0f, overlayMin.y + 8.0f), IM_COL32(232, 239, 252, 255), overlayLine);
    drawList->AddText(ImVec2(overlayMin.x + 10.0f, overlayMin.y + 28.0f), IM_COL32(185, 197, 219, 255), model.lastResetReason.c_str());
    drawList->AddText(
        ImVec2(overlayMin.x + 10.0f, overlayMin.y + 48.0f),
        model.navigationActive ? IM_COL32(112, 225, 176, 255) : IM_COL32(185, 197, 219, 255),
        controlLine);

    const ImVec2 badgeMin(imageMax.x - 250.0f, imageMin.y + 10.0f);
    const ImVec2 badgeMax(imageMax.x - 10.0f, imageMin.y + 62.0f);
    drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(8, 12, 18, 210), 6.0f);
    drawList->AddText(ImVec2(badgeMin.x + 10.0f, badgeMin.y + 8.0f), IM_COL32(232, 239, 252, 255), model.integratorLabel);
    drawList->AddText(
        ImVec2(badgeMin.x + 10.0f, badgeMin.y + 28.0f),
        IM_COL32(185, 197, 219, 255),
        model.gizmoLabel);
    drawList->AddText(
        ImVec2(badgeMin.x + 10.0f, badgeMin.y + 44.0f),
        model.hasSelection ? IM_COL32(112, 225, 176, 255) : IM_COL32(185, 197, 219, 255),
        model.transformSpaceLabel);

    if (!model.contextVersion.empty()) {
        drawList->AddText(
            ImVec2(imageMin.x + 10.0f, imageMax.y - 24.0f),
            IM_COL32(180, 190, 208, 230),
            model.contextVersion.c_str());
    }

    return result;
}

} // namespace RenderViewportPanel
