#include "EditorViewport.h"
#include "App/Resources/EmbeddedTabIcons.h"
#include "Editor/EditorModule.h"
#include "App/settings/AppearanceTheme.h"
#include "EditorViewportHelpers.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <string>

using namespace EditorViewportHelpers;

namespace {

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

ImVec2 LerpImVec2(const ImVec2& a, const ImVec2& b, float t) {
    return ImVec2(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t);
}

ImU32 ApplyAlpha(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

float AnimateUiValue(float current, float target, float deltaTime, float onSpeed = 16.0f, float offSpeed = 10.0f) {
    return ImGuiExtras::AnimateTowards(current, target, deltaTime, target > current ? onSpeed : offSpeed);
}

bool SeamlessSurfaceStylingEnabled(EditorModule* editor) {
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    return appearance && appearance->GetSeamlessSurfaceStylingEnabled();
}

StackAppearance::RuntimeSurfacePalette GetWallpaperSurfacePalette(EditorModule* editor) {
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    return appearance ? appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
}

unsigned int LoadViewportResourceTexture(const unsigned char* data, unsigned int size, const char* debugName) {
    if (!data || size == 0) {
        return 0;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[EditorViewport] Failed to decode embedded %s icon.\n", debugName);
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

bool DrawDetachedPreviewToggle(
    EditorModule* editor,
    ImDrawList* drawList,
    const ImVec2& contentScreen,
    const ImVec2& avail,
    float alpha,
    EditorViewport::HostMode hostMode,
    unsigned int iconTexture,
    float& hoverAnim,
    float& pressAnim,
    float deltaTime) {
    if (!editor || !drawList || alpha <= 0.01f || avail.x <= 48.0f || avail.y <= 32.0f) {
        hoverAnim = AnimateUiValue(hoverAnim, 0.0f, deltaTime);
        pressAnim = AnimateUiValue(pressAnim, 0.0f, deltaTime, 22.0f, 14.0f);
        return false;
    }

    if (iconTexture == 0) {
        hoverAnim = AnimateUiValue(hoverAnim, 0.0f, deltaTime);
        pressAnim = AnimateUiValue(pressAnim, 0.0f, deltaTime, 22.0f, 14.0f);
        return false;
    }

    const char* label = hostMode == EditorViewport::HostMode::DetachedFullscreen ? "Dock Back" : "Pop Out";
    constexpr float hitSize = 34.0f;
    constexpr float iconSize = 22.0f;
    constexpr float edgeMargin = 20.0f;
    const ImVec2 buttonSize(hitSize, hitSize);
    const ImVec2 buttonMin(
        contentScreen.x + std::max(0.0f, avail.x - buttonSize.x - edgeMargin),
        contentScreen.y + std::max(0.0f, avail.y - buttonSize.y - edgeMargin));
    const ImVec2 buttonMax(buttonMin.x + buttonSize.x, buttonMin.y + buttonSize.y);

    const bool hovered = ImGui::IsMouseHoveringRect(buttonMin, buttonMax, false);
    const bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    hoverAnim = AnimateUiValue(hoverAnim, hovered ? 1.0f : 0.0f, deltaTime, 17.0f, 11.0f);
    pressAnim = AnimateUiValue(pressAnim, active ? 1.0f : 0.0f, deltaTime, 24.0f, 15.0f);

    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", label);
    }

    const float scale = 1.0f + hoverAnim * 0.020f - pressAnim * 0.024f;
    const float verticalLift = hoverAnim * 1.25f - pressAnim * 0.45f;
    const ImVec2 center((buttonMin.x + buttonMax.x) * 0.5f, (buttonMin.y + buttonMax.y) * 0.5f - verticalLift);
    const ImVec2 halfSize(iconSize * 0.5f * scale, iconSize * 0.5f * scale);
    const ImVec2 animatedMin(center.x - halfSize.x, center.y - halfSize.y);
    const ImVec2 animatedMax(center.x + halfSize.x, center.y + halfSize.y);
    ImU32 iconTint = StackAppearance::ResolveThemedMonochromeIconTint(
        editor->GetAppearance(),
        active || hostMode == EditorViewport::HostMode::DetachedFullscreen,
        hovered);
    iconTint = ApplyAlpha(iconTint, alpha);
    drawList->AddImage(
        (ImTextureID)(intptr_t)iconTexture,
        animatedMin,
        animatedMax,
        ImVec2(0, 0),
        ImVec2(1, 1),
        iconTint);

    if (clicked) {
        editor->RequestToggleDetachedPreviewFullscreen();
    }

    return hovered;
}

ImU32 DevelopSubjectModeColor(EditorNodeGraph::DevelopSubjectImportanceMode mode, float alpha) {
    const int a = static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
    switch (mode) {
        case EditorNodeGraph::DevelopSubjectImportanceMode::Reveal:
            return IM_COL32(62, 186, 232, a);
        case EditorNodeGraph::DevelopSubjectImportanceMode::Protect:
            return IM_COL32(87, 210, 128, a);
        case EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood:
            return IM_COL32(178, 126, 232, a);
        case EditorNodeGraph::DevelopSubjectImportanceMode::Ignore:
            return IM_COL32(158, 164, 172, a);
        case EditorNodeGraph::DevelopSubjectImportanceMode::Important:
        default:
            return IM_COL32(245, 204, 75, a);
    }
}

ImU32 DevelopSubjectStrokeColor(
    EditorNodeGraph::DevelopSubjectImportanceMode mode,
    bool subtract,
    float alpha) {
    if (subtract) {
        const int a = static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
        return IM_COL32(48, 54, 64, a);
    }
    return DevelopSubjectModeColor(mode, alpha);
}

void BuildEllipsePoints(ImVec2 center, float radiusX, float radiusY, ImVec2* points, int pointCount) {
    constexpr float kTwoPi = 6.28318530718f;
    for (int i = 0; i < pointCount; ++i) {
        const float angle = (static_cast<float>(i) / static_cast<float>(pointCount)) * kTwoPi;
        points[i] = ImVec2(
            center.x + std::cos(angle) * radiusX,
            center.y + std::sin(angle) * radiusY);
    }
}

void DrawDevelopSubjectRegionOverlay(
    ImDrawList* drawList,
    const EditorModule::DevelopSubjectViewportRegion& region,
    int activeRegionId,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    float overlayOpacity) {
    const float imageW = std::max(1.0f, imageMax.x - imageMin.x);
    const float imageH = std::max(1.0f, imageMax.y - imageMin.y);
    const ImVec2 center(
        imageMin.x + region.centerX * imageW,
        imageMin.y + region.centerY * imageH);
    const float radiusX = std::max(2.0f, region.radiusX * imageW);
    const float radiusY = std::max(2.0f, region.radiusY * imageH);
    const bool active = region.id == activeRegionId;
    const float enabledAlpha = region.enabled ? 1.0f : 0.38f;
    const float strengthAlpha = std::clamp(0.35f + region.strength * 0.65f, 0.2f, 1.0f);
    const float fillAlpha = overlayOpacity * enabledAlpha * strengthAlpha * (active ? 0.34f : 0.22f);
    const float outlineAlpha = overlayOpacity * enabledAlpha * (active ? 1.0f : 0.72f);

    constexpr int kPointCount = 72;
    ImVec2 points[kPointCount];
    BuildEllipsePoints(center, radiusX, radiusY, points, kPointCount);
    drawList->AddConvexPolyFilled(points, kPointCount, DevelopSubjectModeColor(region.mode, fillAlpha));
    drawList->AddPolyline(
        points,
        kPointCount,
        DevelopSubjectModeColor(region.mode, outlineAlpha),
        ImDrawFlags_Closed,
        active ? 2.4f : 1.5f);

    const float featherAlpha = overlayOpacity * enabledAlpha * 0.34f;
    if (region.feather > 0.02f) {
        const float featherScale = 1.0f + region.feather * 0.35f;
        ImVec2 featherPoints[kPointCount];
        BuildEllipsePoints(center, radiusX * featherScale, radiusY * featherScale, featherPoints, kPointCount);
        drawList->AddPolyline(
            featherPoints,
            kPointCount,
            DevelopSubjectModeColor(region.mode, featherAlpha),
            ImDrawFlags_Closed,
            1.0f);
    }

    if (active) {
        drawList->AddCircleFilled(center, 4.0f, IM_COL32(8, 12, 16, 210), 16);
        drawList->AddCircleFilled(center, 2.5f, DevelopSubjectModeColor(region.mode, overlayOpacity), 16);
        drawList->AddCircleFilled(
            ImVec2(center.x + radiusX, center.y),
            3.5f,
            IM_COL32(250, 252, 255, static_cast<int>(210.0f * overlayOpacity)),
            12);
    }
}

ImVec2 DevelopSubjectPointToScreen(
    const EditorModule::DevelopSubjectViewportStrokePoint& point,
    const ImVec2& imageMin,
    float imageW,
    float imageH) {
    return ImVec2(
        imageMin.x + point.x * imageW,
        imageMin.y + point.y * imageH);
}

void DrawDevelopSubjectStrokeOverlay(
    ImDrawList* drawList,
    const EditorModule::DevelopSubjectViewportStroke& stroke,
    int activeStrokeId,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    float overlayOpacity) {
    if (stroke.points.empty()) {
        return;
    }

    const float imageW = std::max(1.0f, imageMax.x - imageMin.x);
    const float imageH = std::max(1.0f, imageMax.y - imageMin.y);
    const float radiusPx = std::max(2.5f, stroke.radius * std::min(imageW, imageH));
    const float enabledAlpha = stroke.enabled ? 1.0f : 0.38f;
    const float strengthAlpha = std::clamp(0.28f + stroke.strength * 0.72f, 0.15f, 1.0f);
    const float fillAlpha = overlayOpacity * enabledAlpha * strengthAlpha * (stroke.subtract ? 0.34f : 0.30f);
    const float outlineAlpha = overlayOpacity * enabledAlpha * (stroke.id == activeStrokeId ? 0.90f : 0.50f);
    const ImU32 fillColor = DevelopSubjectStrokeColor(stroke.mode, stroke.subtract, fillAlpha);
    const ImU32 outlineColor =
        stroke.subtract
            ? IM_COL32(236, 242, 248, static_cast<int>(160.0f * outlineAlpha))
            : DevelopSubjectModeColor(stroke.mode, outlineAlpha);
    const ImU32 shadowColor = IM_COL32(5, 8, 12, static_cast<int>(120.0f * overlayOpacity * enabledAlpha));

    if (stroke.points.size() == 1) {
        const ImVec2 p = DevelopSubjectPointToScreen(stroke.points.front(), imageMin, imageW, imageH);
        drawList->AddCircleFilled(p, radiusPx + 1.5f, shadowColor, 32);
        drawList->AddCircleFilled(p, radiusPx, fillColor, 32);
        drawList->AddCircle(p, radiusPx, outlineColor, 32, stroke.id == activeStrokeId ? 2.0f : 1.0f);
        return;
    }

    const float strokeThickness = std::max(2.0f, radiusPx * 2.0f);
    for (std::size_t i = 1; i < stroke.points.size(); ++i) {
        const ImVec2 a = DevelopSubjectPointToScreen(stroke.points[i - 1], imageMin, imageW, imageH);
        const ImVec2 b = DevelopSubjectPointToScreen(stroke.points[i], imageMin, imageW, imageH);
        drawList->AddLine(a, b, shadowColor, strokeThickness + 3.0f);
        drawList->AddLine(a, b, fillColor, strokeThickness);
    }

    const ImVec2 first = DevelopSubjectPointToScreen(stroke.points.front(), imageMin, imageW, imageH);
    const ImVec2 last = DevelopSubjectPointToScreen(stroke.points.back(), imageMin, imageW, imageH);
    drawList->AddCircleFilled(first, radiusPx, fillColor, 32);
    drawList->AddCircleFilled(last, radiusPx, fillColor, 32);

    if (stroke.feather > 0.02f || stroke.id == activeStrokeId) {
        const float featherRadius = radiusPx * (1.0f + stroke.feather * 0.55f);
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            const ImVec2 a = DevelopSubjectPointToScreen(stroke.points[i - 1], imageMin, imageW, imageH);
            const ImVec2 b = DevelopSubjectPointToScreen(stroke.points[i], imageMin, imageW, imageH);
            drawList->AddLine(a, b, outlineColor, std::max(1.0f, featherRadius * 2.0f));
        }
    }
}

ImU32 DevelopSubjectMapCellColor(
    const EditorModule::DevelopSubjectViewportMapCell& cell,
    float opacity) {
    const float positive = std::max({
        cell.importance,
        cell.reveal,
        cell.protect,
        cell.preserveMood });
    const float lowPriority = std::clamp(cell.lowPriority, 0.0f, 1.0f);
    const float strength = std::clamp(std::max(positive, lowPriority), 0.0f, 1.0f);
    if (strength <= 0.001f) {
        return IM_COL32(0, 0, 0, 0);
    }

    EditorNodeGraph::DevelopSubjectImportanceMode mode =
        EditorNodeGraph::DevelopSubjectImportanceMode::Important;
    if (lowPriority > positive * 0.92f) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::Ignore;
    } else if (cell.protect >= cell.reveal &&
               cell.protect >= cell.preserveMood &&
               cell.protect >= cell.importance) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::Protect;
    } else if (cell.reveal >= cell.preserveMood && cell.reveal >= cell.importance) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    } else if (cell.preserveMood >= cell.importance) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood;
    }
    return DevelopSubjectModeColor(mode, opacity * (0.08f + strength * 0.52f));
}

ImU32 DevelopSubjectRefinedMapCellColor(
    const EditorModule::DevelopSubjectViewportMapCell& cell,
    float opacity) {
    const float priority = std::clamp(cell.importance, 0.0f, 1.0f);
    const float confidence = std::clamp(cell.confidence, 0.0f, 1.0f);
    const float lowPriority = std::clamp(cell.lowPriority, 0.0f, 1.0f);
    const float strength = std::max({ priority, confidence, lowPriority });
    if (strength <= 0.001f) {
        return IM_COL32(0, 0, 0, 0);
    }

    EditorNodeGraph::DevelopSubjectImportanceMode mode =
        EditorNodeGraph::DevelopSubjectImportanceMode::Important;
    if (lowPriority > priority * 0.88f && lowPriority > confidence * 0.55f) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::Ignore;
    } else if (cell.protect >= cell.reveal &&
               cell.protect >= cell.preserveMood &&
               cell.protect >= priority) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::Protect;
    } else if (cell.reveal >= cell.preserveMood && cell.reveal >= priority) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::Reveal;
    } else if (cell.preserveMood >= priority) {
        mode = EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood;
    }
    return DevelopSubjectModeColor(mode, opacity * (0.10f + strength * 0.58f));
}

void DrawDevelopSubjectInterpretedMapOverlay(
    ImDrawList* drawList,
    const EditorModule::DevelopSubjectViewportState& state,
    const ImVec2& imageMin,
    const ImVec2& imageMax) {
    if (!state.interpretedMapActive ||
        state.interpretedMapGridWidth <= 0 ||
        state.interpretedMapGridHeight <= 0 ||
        state.interpretedMapCells.empty()) {
        return;
    }

    const float imageW = std::max(1.0f, imageMax.x - imageMin.x);
    const float imageH = std::max(1.0f, imageMax.y - imageMin.y);
    const float cellW = imageW / static_cast<float>(state.interpretedMapGridWidth);
    const float cellH = imageH / static_cast<float>(state.interpretedMapGridHeight);
    const float opacity = std::clamp(state.interpretedMapOpacity, 0.0f, 1.0f);
    const ImU32 gridColor = IM_COL32(236, 244, 250, static_cast<int>(42.0f * opacity));

    for (int y = 0; y < state.interpretedMapGridHeight; ++y) {
        for (int x = 0; x < state.interpretedMapGridWidth; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y * state.interpretedMapGridWidth + x);
            if (index >= state.interpretedMapCells.size()) {
                continue;
            }

            const EditorModule::DevelopSubjectViewportMapCell& cell =
                state.interpretedMapCells[index];
            const ImVec2 cellMin(
                imageMin.x + static_cast<float>(x) * cellW + 1.0f,
                imageMin.y + static_cast<float>(y) * cellH + 1.0f);
            const ImVec2 cellMax(
                imageMin.x + static_cast<float>(x + 1) * cellW - 1.0f,
                imageMin.y + static_cast<float>(y + 1) * cellH - 1.0f);
            const ImU32 fill = DevelopSubjectMapCellColor(cell, opacity);
            if ((fill & IM_COL32_A_MASK) != 0) {
                drawList->AddRectFilled(cellMin, cellMax, fill, 3.0f);
                drawList->AddRect(cellMin, cellMax, gridColor, 3.0f, 0, 1.0f);
            }
        }
    }
}

void DrawDevelopSubjectRefinedMapOverlay(
    ImDrawList* drawList,
    const EditorModule::DevelopSubjectViewportState& state,
    const ImVec2& imageMin,
    const ImVec2& imageMax) {
    if (!state.refinedMapActive ||
        state.refinedMapGridWidth <= 0 ||
        state.refinedMapGridHeight <= 0 ||
        state.refinedMapCells.empty()) {
        return;
    }

    const float imageW = std::max(1.0f, imageMax.x - imageMin.x);
    const float imageH = std::max(1.0f, imageMax.y - imageMin.y);
    const float cellW = imageW / static_cast<float>(state.refinedMapGridWidth);
    const float cellH = imageH / static_cast<float>(state.refinedMapGridHeight);
    const float opacity = std::clamp(state.refinedMapOpacity, 0.0f, 1.0f);
    const ImU32 gridColor = IM_COL32(252, 246, 230, static_cast<int>(36.0f * opacity));

    for (int y = 0; y < state.refinedMapGridHeight; ++y) {
        for (int x = 0; x < state.refinedMapGridWidth; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y * state.refinedMapGridWidth + x);
            if (index >= state.refinedMapCells.size()) {
                continue;
            }

            const EditorModule::DevelopSubjectViewportMapCell& cell =
                state.refinedMapCells[index];
            const ImVec2 cellMin(
                imageMin.x + static_cast<float>(x) * cellW + 1.5f,
                imageMin.y + static_cast<float>(y) * cellH + 1.5f);
            const ImVec2 cellMax(
                imageMin.x + static_cast<float>(x + 1) * cellW - 1.5f,
                imageMin.y + static_cast<float>(y + 1) * cellH - 1.5f);
            const ImU32 fill = DevelopSubjectRefinedMapCellColor(cell, opacity);
            if ((fill & IM_COL32_A_MASK) == 0) {
                continue;
            }
            drawList->AddRectFilled(cellMin, cellMax, fill, 2.0f);
            const float boundaryAlpha =
                std::clamp(cell.boundaryHint, 0.0f, 1.0f) * 92.0f * opacity;
            if (boundaryAlpha > 2.0f) {
                drawList->AddRect(
                    cellMin,
                    cellMax,
                    IM_COL32(255, 248, 220, static_cast<int>(boundaryAlpha)),
                    2.0f,
                    0,
                    1.4f);
            } else {
                drawList->AddRect(cellMin, cellMax, gridColor, 2.0f, 0, 0.8f);
            }
        }
    }
}

void DrawDevelopSubjectBrushCursor(
    ImDrawList* drawList,
    const EditorModule::DevelopSubjectViewportState& state,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    float u,
    float v) {
    const float imageW = std::max(1.0f, imageMax.x - imageMin.x);
    const float imageH = std::max(1.0f, imageMax.y - imageMin.y);
    const ImVec2 center(
        imageMin.x + std::clamp(u, 0.0f, 1.0f) * imageW,
        imageMin.y + std::clamp(v, 0.0f, 1.0f) * imageH);
    const float radiusPx = std::max(2.5f, state.brushRadius * std::min(imageW, imageH));
    const ImU32 color = DevelopSubjectStrokeColor(
        state.brushMode,
        state.brushSubtract,
        std::clamp(state.overlayOpacity * 0.92f, 0.10f, 1.0f));
    drawList->AddCircle(center, radiusPx, IM_COL32(8, 12, 16, 210), 48, 2.7f);
    drawList->AddCircle(center, radiusPx, color, 48, 1.7f);
    if (state.brushFeather > 0.02f) {
        drawList->AddCircle(
            center,
            radiusPx * (1.0f + state.brushFeather * 0.55f),
            DevelopSubjectStrokeColor(state.brushMode, state.brushSubtract, state.overlayOpacity * 0.38f),
            48,
            1.0f);
    }
}

} // namespace

EditorViewport::EditorViewport() 
    : m_ZoomLevel(1.0f), m_PanX(0.0f), m_PanY(0.0f), m_IsLocked(false) 
{}

EditorViewport::~EditorViewport() {
    if (m_CheckerTex) glDeleteTextures(1, &m_CheckerTex);
    if (m_DetachedToggleTexture) glDeleteTextures(1, &m_DetachedToggleTexture);
}

void EditorViewport::ResetSinglePreviewState() {
    m_ZoomLevel = 1.0f;
    m_PanX = 0.0f;
    m_PanY = 0.0f;
    m_IsLocked = false;
    m_ShowStaticSingleCompare = false;
    m_StaticSingleCompareBlend = 0.0f;
    m_StaticCompareRectsInitialized = false;
    m_DetachedToggleHoverAnim = 0.0f;
    m_DetachedTogglePressAnim = 0.0f;
    m_ViewportHudAnim = 0.0f;
    m_CompositeSelectionOutlineAnim = 0.0f;
    m_CompositeSelectionHandleAnim = 0.0f;
    m_CompositeHoverOutlineAnim = 0.0f;
    m_ActiveDevelopSubjectHandle = DevelopSubjectRegionHandle::None;
    m_ActiveDevelopSubjectNodeId = -1;
    m_ActiveDevelopSubjectRegionId = -1;
    m_ActiveDevelopSubjectStrokeId = -1;
    m_DevelopSubjectBrushStrokeActive = false;
}

void EditorViewport::Initialize() {
    // Create a 2x2 checkerboard texture for transparency display
    unsigned char checker[2 * 2 * 4] = {
        52, 60, 66, 255,   68, 78, 84, 255,
        68, 78, 84, 255,   52, 60, 66, 255
    };
    m_CheckerTex = GLHelpers::CreateTextureFromPixels(checker, 2, 2, 4);
    glBindTexture(GL_TEXTURE_2D, m_CheckerTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (m_DetachedToggleTexture == 0) {
        m_DetachedToggleTexture = LoadViewportResourceTexture(
            EmbeddedTabIcons::PopoutCanvasWindow_png_data,
            EmbeddedTabIcons::PopoutCanvasWindow_png_size,
            "PopoutCanvasWindow");
    }
}

void EditorViewport::Render(EditorModule* editor, float revealAlpha, HostMode hostMode) {
    const float viewportRevealAlpha = std::clamp(revealAlpha, 0.0f, 1.0f);
    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    const bool wallpaperSurfaces = SeamlessSurfaceStylingEnabled(editor);
    auto& pipeline = editor->GetPipeline();
    const bool compositeMode = editor->IsCompositeViewportMode();
    const ImVec2 hostAvail = ImGui::GetContentRegionAvail();
    const ImVec2 hostScreen = ImGui::GetCursorScreenPos();
    ImDrawList* hostDrawList = ImGui::GetWindowDrawList();
    const bool inputBlocked = DrawDetachedPreviewToggle(
        editor,
        hostDrawList,
        hostScreen,
        hostAvail,
        viewportRevealAlpha,
        hostMode,
        m_DetachedToggleTexture,
        m_DetachedToggleHoverAnim,
        m_DetachedTogglePressAnim,
        deltaTime);

    if (!compositeMode && m_PendingCompositeAddImageDialog) {
        m_PendingCompositeAddImageDialog = false;
    }

    if (compositeMode) {
        if (m_PendingCompositeAddImageDialog) {
            m_PendingCompositeAddImageDialog = false;
            const std::string path = FileDialogs::OpenImageFileDialog("Add image to composite");
            if (!path.empty()) {
                editor->AddCompositeImageChainFromFile(path);
            }
        }
        editor->ClearToneCurveViewportProbe();
        const ImVec2 avail = hostAvail;
        const ImVec2 screenPos = hostScreen;
        ImDrawList* drawList = hostDrawList;
        constexpr float kCanvasRounding = 18.0f;
        editor->EnsureCompositeSceneState(avail);
        editor->SetLastCompositeCanvasSize(avail);
        auto& exportSettings = editor->GetMutableCompositeExportSettings();

        const float margin = 12.0f;
        const ImVec2 canvasMin(screenPos.x + margin, screenPos.y + margin);
        const ImVec2 canvasMax(screenPos.x + avail.x - margin, screenPos.y + avail.y - margin);
        const ImU32 workspaceFill = ImGui::ColorConvertFloat4ToU32(editor->GetWorkspaceBaseColor());
        if (!wallpaperSurfaces) {
            drawList->AddRectFilled(canvasMin, canvasMax, ApplyAlpha(workspaceFill, viewportRevealAlpha), kCanvasRounding);
        }

        const float checkerScale = 24.0f;
        const float canvasW = std::max(1.0f, avail.x - margin * 2.0f);
        const float canvasH = std::max(1.0f, avail.y - margin * 2.0f);
        const float tilesX = std::max(1.0f, canvasW / checkerScale);
        const float tilesY = std::max(1.0f, canvasH / checkerScale);
        drawList->AddImageRounded(
            (ImTextureID)(intptr_t)m_CheckerTex,
            canvasMin,
            canvasMax,
            ImVec2(0.0f, 0.0f),
            ImVec2(tilesX, tilesY),
            IM_COL32(255, 255, 255, static_cast<int>((wallpaperSurfaces ? 84.0f : 105.0f) * viewportRevealAlpha)),
            kCanvasRounding);

        ImGui::InvisibleButton("CompositeCanvasSurface", avail);
        const bool hovered = !inputBlocked && ImGui::IsItemHovered();
        const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (hovered && editor->CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            editor->ToggleCompositeExportBoundsEditMode();
        }
        if ((hovered || canvasFocused) && editor->CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
            const auto& settings = editor->GetCompositeSnapSettings();
            if (settings.enabled) {
                editor->ApplyCompositeSnapModePreset(EditorModule::CompositeSnapModePreset::Off);
            } else {
                editor->ApplyCompositeSnapModePreset(EditorModule::CompositeSnapModePreset::Full);
            }
            editor->MarkDirty();
        }
        const ImVec2 canvasCenter((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f);
        const ImVec2 mousePos = ImGui::GetMousePos();
        const ImVec2 mouseWorld = ScreenToWorld(
            canvasCenter,
            editor->GetCompositeViewZoom(),
            editor->GetCompositeViewPanX(),
            editor->GetCompositeViewPanY(),
            mousePos);
        const bool isExportSettingsActive = editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::ExportSettings;
        const bool exportBoundsEditMode = editor->IsCompositeExportBoundsEditMode() || isExportSettingsActive;
        EditorModule::CompositeFloatRect exportBounds {};
        bool hasExportBounds = false;
        if (exportBoundsEditMode) {
            if (exportSettings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom) {
                exportBounds = {
                    exportSettings.customX,
                    exportSettings.customY,
                    exportSettings.customWidth,
                    exportSettings.customHeight
                };
                hasExportBounds = exportBounds.width > 0.0f && exportBounds.height > 0.0f;
            } else {
                hasExportBounds = editor->TryGetCompositeAutoExportBounds(exportBounds);
            }
        }

        m_CompositeSnapGuides.clear();

        const EditorModule::CompositeSceneItem* hoveredItem = nullptr;
        if (!isExportSettingsActive) {
            for (int outputNodeId : editor->GetCompositeZOrder()) {
                const EditorModule::CompositeSceneItem* candidate = editor->FindCompositeSceneItem(outputNodeId);
                if (!candidate || !candidate->visible || candidate->texture == 0) {
                    continue;
                }
                if (PointInQuad(ComputeSceneQuadWorld(*candidate), mouseWorld)) {
                    hoveredItem = candidate;
                    break;
                }
            }
        }
        const EditorModule::CompositeSceneItem* selectedItem = editor->FindCompositeSceneItem(editor->GetCompositeSelectedOutputNodeId());
        const bool isEditingComplexNode = editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::ComplexNode;
        const bool showSelectedOutline = selectedItem && !isExportSettingsActive;
        const bool showSelectedHandles = showSelectedOutline && !editor->IsPickingColor() && !isEditingComplexNode;
        const bool showHoveredOutline =
            !editor->IsPickingColor() &&
            !isEditingComplexNode &&
            !isExportSettingsActive &&
            hoveredItem &&
            (!selectedItem || hoveredItem->outputNodeId != selectedItem->outputNodeId);
        m_CompositeSelectionOutlineAnim = AnimateUiValue(
            m_CompositeSelectionOutlineAnim,
            showSelectedOutline ? 1.0f : 0.0f,
            deltaTime,
            15.0f,
            10.0f);
        m_CompositeSelectionHandleAnim = AnimateUiValue(
            m_CompositeSelectionHandleAnim,
            showSelectedHandles ? 1.0f : 0.0f,
            deltaTime,
            17.0f,
            11.0f);
        m_CompositeHoverOutlineAnim = AnimateUiValue(
            m_CompositeHoverOutlineAnim,
            showHoveredOutline ? 1.0f : 0.0f,
            deltaTime,
            16.0f,
            11.0f);
        if (selectedItem && (canvasFocused || hovered) && editor->CanConsumeEditorCommandKeys()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                editor->SetCompositeResizeMode(EditorModule::CompositeResizeMode::Stretch);
            }
            if (!ImGuiExtras::IsSliderWheelModifierActive() && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                if (editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Scale) {
                    editor->ToggleCompositeScaleOriginMode();
                } else {
                    editor->SetCompositeResizeMode(EditorModule::CompositeResizeMode::Scale);
                }
            }
            
            // Z-Order Keyboard Navigation (Up/Down Arrow keys)
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                const std::vector<int>& zOrder = editor->GetCompositeZOrder();
                int selectedId = selectedItem->outputNodeId;
                int selectedIdx = -1;
                for (int idx = 0; idx < static_cast<int>(zOrder.size()); ++idx) {
                    if (zOrder[idx] == selectedId) {
                        selectedIdx = idx;
                        break;
                    }
                }
                if (selectedIdx > 0) {
                    editor->MoveCompositeOutputToIndex(selectedId, selectedIdx - 1);
                    editor->MarkDirty();
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                const std::vector<int>& zOrder = editor->GetCompositeZOrder();
                int selectedId = selectedItem->outputNodeId;
                int selectedIdx = -1;
                for (int idx = 0; idx < static_cast<int>(zOrder.size()); ++idx) {
                    if (zOrder[idx] == selectedId) {
                        selectedIdx = idx;
                        break;
                    }
                }
                if (selectedIdx != -1 && selectedIdx < static_cast<int>(zOrder.size()) - 1) {
                    editor->MoveCompositeOutputToIndex(selectedId, selectedIdx + 1);
                    editor->MarkDirty();
                }
            }
        }

        auto addSnapGuideWorld = [&](const ImVec2& a, const ImVec2& b, const ImU32 color) {
            m_CompositeSnapGuides.push_back({
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), a),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), b),
                color
            });
        };

        auto beginSceneHandleInteraction = [&](EditorModule::CompositeSceneItem& item, const SceneHandleType handleType) {
            editor->SetCompositeSelectedOutputNodeId(item.outputNodeId);
            m_ActiveSceneHandle = handleType;
            m_ActiveSceneOutputNodeId = item.outputNodeId;
            m_SceneDragStartMouseWorldX = mouseWorld.x;
            m_SceneDragStartMouseWorldY = mouseWorld.y;
            m_SceneStartX = item.position.x;
            m_SceneStartY = item.position.y;
            m_SceneStartScaleX = item.scale.x;
            m_SceneStartScaleY = item.scale.y;
            m_SceneStartRotation = item.rotation;
            m_SceneStartWidth = std::max(1.0f, (item.isScalable ? 256.0f : static_cast<float>(item.textureWidth)) * std::max(0.0001f, item.scale.x));
            m_SceneStartHeight = std::max(1.0f, (item.isScalable ? 256.0f : static_cast<float>(item.textureHeight)) * std::max(0.0001f, item.scale.y));

            const std::array<ImVec2, 4> worldQuad = ComputeSceneQuadWorld(item);
            const ImVec2 worldTopCenter = Midpoint(worldQuad[0], worldQuad[1]);
            const ImVec2 worldBottomCenter = Midpoint(worldQuad[2], worldQuad[3]);
            const ImVec2 worldLeftCenter = Midpoint(worldQuad[0], worldQuad[3]);
            const ImVec2 worldRightCenter = Midpoint(worldQuad[1], worldQuad[2]);
            switch (handleType) {
                case SceneHandleType::ResizeTopLeft:
                    m_SceneResizeAnchorX = worldQuad[2].x;
                    m_SceneResizeAnchorY = worldQuad[2].y;
                    break;
                case SceneHandleType::ResizeTopRight:
                    m_SceneResizeAnchorX = worldQuad[3].x;
                    m_SceneResizeAnchorY = worldQuad[3].y;
                    break;
                case SceneHandleType::ResizeBottomRight:
                    m_SceneResizeAnchorX = worldQuad[0].x;
                    m_SceneResizeAnchorY = worldQuad[0].y;
                    break;
                case SceneHandleType::ResizeBottomLeft:
                    m_SceneResizeAnchorX = worldQuad[1].x;
                    m_SceneResizeAnchorY = worldQuad[1].y;
                    break;
                case SceneHandleType::ResizeLeft:
                    m_SceneResizeAnchorX = worldRightCenter.x;
                    m_SceneResizeAnchorY = worldRightCenter.y;
                    break;
                case SceneHandleType::ResizeRight:
                    m_SceneResizeAnchorX = worldLeftCenter.x;
                    m_SceneResizeAnchorY = worldLeftCenter.y;
                    break;
                case SceneHandleType::ResizeTop:
                    m_SceneResizeAnchorX = worldBottomCenter.x;
                    m_SceneResizeAnchorY = worldBottomCenter.y;
                    break;
                case SceneHandleType::ResizeBottom:
                    m_SceneResizeAnchorX = worldTopCenter.x;
                    m_SceneResizeAnchorY = worldTopCenter.y;
                    break;
                case SceneHandleType::Rotate: {
                    const ImVec2 center = Midpoint(worldQuad[0], worldQuad[2]);
                    m_SceneStartMouseAngle = std::atan2(mouseWorld.y - center.y, mouseWorld.x - center.x) - item.rotation;
                    break;
                }
                case SceneHandleType::Move:
                case SceneHandleType::None:
                default:
                    break;
            }
        };

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            editor->BeginCompositePan(mousePos);
        }
        if (editor->IsCompositePanActive()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                editor->UpdateCompositePan(mousePos);
            } else {
                editor->EndCompositePan();
            }
        }

        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            const float nextZoom = std::clamp(
                editor->GetCompositeViewZoom() * (1.0f + ImGui::GetIO().MouseWheel * 0.1f),
                0.05f,
                32.0f);
            editor->SetCompositeViewZoom(nextZoom);
        }
        if (!editor->IsCompositePanActive() && m_ActiveSceneHandle == SceneHandleType::None) {
            editor->ClampCompositeViewPanToContent(avail);
        }

        if (!editor->IsPickingColor() && !isEditingComplexNode && !isExportSettingsActive && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("CompositeCanvasContext");
        }

        const bool editableCustomBounds =
            exportBoundsEditMode &&
            exportSettings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom &&
            hasExportBounds;
        ImVec2 exportTopLeft {};
        ImVec2 exportTopRight {};
        ImVec2 exportBottomRight {};
        ImVec2 exportBottomLeft {};
        ImVec2 exportTopCenter {};
        ImVec2 exportBottomCenter {};
        ImVec2 exportLeftCenter {};
        ImVec2 exportRightCenter {};
        if (hasExportBounds) {
            exportTopLeft = WorldToScreen(
                canvasCenter,
                editor->GetCompositeViewZoom(),
                editor->GetCompositeViewPanX(),
                editor->GetCompositeViewPanY(),
                ImVec2(exportBounds.x, exportBounds.y));
            exportBottomRight = WorldToScreen(
                canvasCenter,
                editor->GetCompositeViewZoom(),
                editor->GetCompositeViewPanX(),
                editor->GetCompositeViewPanY(),
                ImVec2(exportBounds.x + exportBounds.width, exportBounds.y + exportBounds.height));
            exportTopRight = ImVec2(exportBottomRight.x, exportTopLeft.y);
            exportBottomLeft = ImVec2(exportTopLeft.x, exportBottomRight.y);
            exportTopCenter = ImVec2((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportTopLeft.y);
            exportBottomCenter = ImVec2((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportBottomRight.y);
            exportLeftCenter = ImVec2(exportTopLeft.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
            exportRightCenter = ImVec2(exportBottomRight.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
        }

        // ── Force Selection & Sample Color in Picking State ──────────────────────
        int activeNodeId = editor->GetActiveComplexNodeId();
        int targetOutputNodeId = -1;
        if (activeNodeId > 0) {
            for (const auto& chain : editor->GetNodeGraph().GetCompletedChains()) {
                for (int nid : chain.nodeIds) {
                    if (nid == activeNodeId) {
                        targetOutputNodeId = chain.outputNodeId;
                        break;
                    }
                }
                if (targetOutputNodeId > 0) break;
            }
        }

        if (editor->IsPickingColor() || isEditingComplexNode) {
            if (targetOutputNodeId > 0 && editor->GetCompositeSelectedOutputNodeId() != targetOutputNodeId) {
                editor->SetCompositeSelectedOutputNodeId(targetOutputNodeId);
            }
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && targetOutputNodeId > 0) {
                    const auto* item = editor->FindCompositeSceneItem(targetOutputNodeId);
                    if (item && !item->rgbaPixels.empty()) {
                        AffineTransform2D inverse = EditorViewportHelpers::Inverse(EditorViewportHelpers::BuildLocalTransform(*item));
                        ImVec2 localPoint = EditorViewportHelpers::TransformPoint(inverse, mouseWorld);
                        
                        const float baseWidth = item->isScalable ? 256.0f : static_cast<float>(item->textureWidth);
                        const float baseHeight = item->isScalable ? 256.0f : static_cast<float>(item->textureHeight);
                        
                        if (localPoint.x >= 0.0f && localPoint.x <= baseWidth && localPoint.y >= 0.0f && localPoint.y <= baseHeight) {
                            int px = std::clamp(static_cast<int>((localPoint.x / baseWidth) * item->textureWidth), 0, item->textureWidth - 1);
                            int py = std::clamp(static_cast<int>((localPoint.y / baseHeight) * item->textureHeight), 0, item->textureHeight - 1);
                            
                            int ch = 4;
                            int idx = (py * item->textureWidth + px) * ch;
                            if (idx >= 0 && static_cast<size_t>(idx + 2) < item->rgbaPixels.size()) {
                                float r = item->rgbaPixels[idx] / 255.0f;
                                float g = item->rgbaPixels[idx + 1] / 255.0f;
                                float b = item->rgbaPixels[idx + 2] / 255.0f;
                                editor->OnColorPicked(r, g, b);
                            }
                        }
                    }
                }
            }
        }

        if (!editor->IsPickingColor() && !isEditingComplexNode && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_ActiveExportHandle = ExportHandleType::None;
            m_ActiveSceneHandle = SceneHandleType::None;
            m_ActiveSceneOutputNodeId = -1;
            if (editableCustomBounds) {
                const float threshold = 12.0f;
                const float minX = std::min(exportTopLeft.x, exportBottomRight.x);
                const float maxX = std::max(exportTopLeft.x, exportBottomRight.x);
                const float minY = std::min(exportTopLeft.y, exportBottomRight.y);
                const float maxY = std::max(exportTopLeft.y, exportBottomRight.y);
                const bool insideExportRect =
                    mousePos.x >= minX &&
                    mousePos.x <= maxX &&
                    mousePos.y >= minY &&
                    mousePos.y <= maxY;

                if (DistanceToPoint(mousePos, exportTopLeft) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::TopLeft;
                } else if (DistanceToPoint(mousePos, exportTopRight) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::TopRight;
                } else if (DistanceToPoint(mousePos, exportBottomRight) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::BottomRight;
                } else if (DistanceToPoint(mousePos, exportBottomLeft) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::BottomLeft;
                } else if (DistanceToPoint(mousePos, exportTopCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Top;
                } else if (DistanceToPoint(mousePos, exportRightCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Right;
                } else if (DistanceToPoint(mousePos, exportBottomCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Bottom;
                } else if (DistanceToPoint(mousePos, exportLeftCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Left;
                } else if (insideExportRect) {
                    m_ActiveExportHandle = ExportHandleType::Move;
                }

                if (m_ActiveExportHandle != ExportHandleType::None) {
                    m_ExportDragStartX = exportSettings.customX;
                    m_ExportDragStartY = exportSettings.customY;
                    m_ExportDragStartWidth = exportSettings.customWidth;
                    m_ExportDragStartHeight = exportSettings.customHeight;
                    m_ExportDragStartMouseWorldX = mouseWorld.x;
                    m_ExportDragStartMouseWorldY = mouseWorld.y;
                }
            }

            if (isExportSettingsActive || m_ActiveExportHandle != ExportHandleType::None) {
                editor->ClearCompositeSelection();
            } else {
                const EditorModule::CompositeSceneItem* activeSelectedItem = editor->FindCompositeSceneItem(editor->GetCompositeSelectedOutputNodeId());
                bool startedHandle = false;
                if (activeSelectedItem && !activeSelectedItem->locked && activeSelectedItem->textureWidth > 0 && activeSelectedItem->textureHeight > 0) {
                    const std::array<ImVec2, 4> selectedWorldQuad = ComputeSceneQuadWorld(*activeSelectedItem);
                    const std::array<ImVec2, 4> screenQuad = {
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[0]),
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[1]),
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[2]),
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[3]),
                    };
                    const ImVec2 topCenter = Midpoint(screenQuad[0], screenQuad[1]);
                    const ImVec2 rightCenter = Midpoint(screenQuad[1], screenQuad[2]);
                    const ImVec2 bottomCenter = Midpoint(screenQuad[2], screenQuad[3]);
                    const ImVec2 leftCenter = Midpoint(screenQuad[3], screenQuad[0]);
                    const ImVec2 center = Midpoint(screenQuad[0], screenQuad[2]);
                    const ImVec2 outward = NormalizeOrZero(ImVec2(topCenter.x - center.x, topCenter.y - center.y));
                    const ImVec2 rotateHandle = ImVec2(topCenter.x + outward.x * 30.0f, topCenter.y + outward.y * 30.0f);
                    constexpr float kHandleRadius = 7.0f;
                    constexpr float kRotateRadius = 9.0f;
                    SceneHandleType hitHandle = SceneHandleType::None;
                    if (DistanceToPoint(mousePos, rotateHandle) <= kRotateRadius) {
                        hitHandle = SceneHandleType::Rotate;
                    } else if (DistanceToPoint(mousePos, screenQuad[0]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeTopLeft;
                    } else if (DistanceToPoint(mousePos, screenQuad[1]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeTopRight;
                    } else if (DistanceToPoint(mousePos, screenQuad[2]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeBottomRight;
                    } else if (DistanceToPoint(mousePos, screenQuad[3]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeBottomLeft;
                    } else if (DistanceToPoint(mousePos, topCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeTop;
                    } else if (DistanceToPoint(mousePos, rightCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeRight;
                    } else if (DistanceToPoint(mousePos, bottomCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeBottom;
                    } else if (DistanceToPoint(mousePos, leftCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeLeft;
                    }
                    if (hitHandle != SceneHandleType::None) {
                        if (EditorModule::CompositeSceneItem* selectedMutable = editor->FindCompositeSceneItem(activeSelectedItem->outputNodeId)) {
                            beginSceneHandleInteraction(*selectedMutable, hitHandle);
                            startedHandle = true;
                        }
                    }
                }

                if (!startedHandle && hoveredItem) {
                    if (EditorModule::CompositeSceneItem* hoveredMutable = editor->FindCompositeSceneItem(hoveredItem->outputNodeId)) {
                        beginSceneHandleInteraction(*hoveredMutable, SceneHandleType::Move);
                    }
                } else if (!startedHandle) {
                    editor->ClearCompositeSelection();
                }
            }
        }
        if (m_ActiveExportHandle != ExportHandleType::None) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (m_ActiveExportHandle != ExportHandleType::Move &&
                    exportSettings.aspectPreset != EditorModule::CompositeExportAspectPreset::Custom) {
                    exportSettings.aspectPreset = EditorModule::CompositeExportAspectPreset::Custom;
                }
                const float deltaX = mouseWorld.x - m_ExportDragStartMouseWorldX;
                const float deltaY = mouseWorld.y - m_ExportDragStartMouseWorldY;
                const bool lockedAspect = exportSettings.aspectPreset != EditorModule::CompositeExportAspectPreset::Custom;
                const float ratio = editor->GetCurrentCompositeExportAspectRatio();
                EditorModule::CompositeFloatRect newBounds {
                    m_ExportDragStartX,
                    m_ExportDragStartY,
                    m_ExportDragStartWidth,
                    m_ExportDragStartHeight
                };

                auto applyCornerWithAspect = [&](const float anchorX, const float anchorY, const bool dragLeft, const bool dragTop) {
                    const float widthByX = std::max(1.0f, std::abs(mouseWorld.x - anchorX));
                    const float heightByX = std::max(1.0f, widthByX / std::max(0.0001f, ratio));
                    const float heightByY = std::max(1.0f, std::abs(mouseWorld.y - anchorY));
                    const float widthByY = std::max(1.0f, heightByY * std::max(0.0001f, ratio));
                    const float xCornerByX = dragLeft ? (anchorX - widthByX) : (anchorX + widthByX);
                    const float yCornerByX = dragTop ? (anchorY - heightByX) : (anchorY + heightByX);
                    const float xCornerByY = dragLeft ? (anchorX - widthByY) : (anchorX + widthByY);
                    const float yCornerByY = dragTop ? (anchorY - heightByY) : (anchorY + heightByY);
                    const float errorByX = std::pow(mouseWorld.x - xCornerByX, 2.0f) + std::pow(mouseWorld.y - yCornerByX, 2.0f);
                    const float errorByY = std::pow(mouseWorld.x - xCornerByY, 2.0f) + std::pow(mouseWorld.y - yCornerByY, 2.0f);
                    const float chosenWidth = (errorByX <= errorByY) ? widthByX : widthByY;
                    const float chosenHeight = std::max(1.0f, chosenWidth / std::max(0.0001f, ratio));
                    const float left = dragLeft ? (anchorX - chosenWidth) : anchorX;
                    const float top = dragTop ? (anchorY - chosenHeight) : anchorY;
                    newBounds = { left, top, chosenWidth, chosenHeight };
                };

                if (m_ActiveExportHandle == ExportHandleType::Move) {
                    newBounds.x = m_ExportDragStartX + deltaX;
                    newBounds.y = m_ExportDragStartY + deltaY;
                } else if (!lockedAspect) {
                    float x1 = m_ExportDragStartX;
                    float y1 = m_ExportDragStartY;
                    float x2 = m_ExportDragStartX + m_ExportDragStartWidth;
                    float y2 = m_ExportDragStartY + m_ExportDragStartHeight;
                    if (m_ActiveExportHandle == ExportHandleType::Left ||
                        m_ActiveExportHandle == ExportHandleType::TopLeft ||
                        m_ActiveExportHandle == ExportHandleType::BottomLeft) {
                        x1 += deltaX;
                    }
                    if (m_ActiveExportHandle == ExportHandleType::Right ||
                        m_ActiveExportHandle == ExportHandleType::TopRight ||
                        m_ActiveExportHandle == ExportHandleType::BottomRight) {
                        x2 += deltaX;
                    }
                    if (m_ActiveExportHandle == ExportHandleType::Top ||
                        m_ActiveExportHandle == ExportHandleType::TopLeft ||
                        m_ActiveExportHandle == ExportHandleType::TopRight) {
                        y1 += deltaY;
                    }
                    if (m_ActiveExportHandle == ExportHandleType::Bottom ||
                        m_ActiveExportHandle == ExportHandleType::BottomLeft ||
                        m_ActiveExportHandle == ExportHandleType::BottomRight) {
                        y2 += deltaY;
                    }
                    newBounds = MakeNormalizedRect(x1, y1, x2, y2);
                } else {
                    const float startRight = m_ExportDragStartX + m_ExportDragStartWidth;
                    const float startBottom = m_ExportDragStartY + m_ExportDragStartHeight;
                    switch (m_ActiveExportHandle) {
                        case ExportHandleType::TopLeft:
                            applyCornerWithAspect(startRight, startBottom, true, true);
                            break;
                        case ExportHandleType::TopRight:
                            applyCornerWithAspect(m_ExportDragStartX, startBottom, false, true);
                            break;
                        case ExportHandleType::BottomRight:
                            applyCornerWithAspect(m_ExportDragStartX, m_ExportDragStartY, false, false);
                            break;
                        case ExportHandleType::BottomLeft:
                            applyCornerWithAspect(startRight, m_ExportDragStartY, true, false);
                            break;
                        default:
                            break;
                    }
                }

                exportSettings.customX = newBounds.x;
                exportSettings.customY = newBounds.y;
                exportSettings.customWidth = std::max(1.0f, newBounds.width);
                exportSettings.customHeight = std::max(1.0f, newBounds.height);
                if (exportSettings.aspectPreset == EditorModule::CompositeExportAspectPreset::Custom) {
                    editor->UpdateCompositeCustomExportAspectFromBounds();
                }
                editor->SyncCompositeExportResolutionFromWidth();
            } else {
                m_ActiveExportHandle = ExportHandleType::None;
            }
        }
        if (m_ActiveSceneHandle != SceneHandleType::None) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_ActiveSceneHandle = SceneHandleType::None;
                m_ActiveSceneOutputNodeId = -1;
            } else if (EditorModule::CompositeSceneItem* activeItem = editor->FindCompositeSceneItem(m_ActiveSceneOutputNodeId)) {
                const auto& snapSettings = editor->GetCompositeSnapSettings();
                const bool preserveAspect = editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Scale;
                const bool scaleFromCenter =
                    preserveAspect &&
                    editor->GetCompositeScaleOriginMode() == EditorModule::CompositeScaleOriginMode::Center;
                const float threshold = 10.0f / std::max(0.001f, editor->GetCompositeViewZoom());
                auto buildBoundsAt = [&](const float topLeftX, const float topLeftY, const float scaleX, const float scaleY, const float rotation) {
                    EditorModule::CompositeSceneItem temp = *activeItem;
                    temp.position = ImVec2(topLeftX, topLeftY);
                    temp.scale = ImVec2(scaleX, scaleY);
                    temp.rotation = rotation;
                    return QuadBounds(ComputeSceneQuadWorld(temp));
                };

                auto applyMoveSnapping = [&](float& targetX, float& targetY) {
                    if (!snapSettings.enabled) {
                        return;
                    }
                    EditorModule::CompositeFloatRect activeBounds =
                        buildBoundsAt(targetX, targetY, m_SceneStartScaleX, m_SceneStartScaleY, m_SceneStartRotation);
                    const float activeLeft = activeBounds.x;
                    const float activeCenterX = activeBounds.x + activeBounds.width * 0.5f;
                    const float activeRight = activeBounds.x + activeBounds.width;
                    const float activeTop = activeBounds.y;
                    const float activeCenterY = activeBounds.y + activeBounds.height * 0.5f;
                    const float activeBottom = activeBounds.y + activeBounds.height;

                    float bestDeltaX = std::numeric_limits<float>::max();
                    float bestDeltaY = std::numeric_limits<float>::max();
                    bool snappedX = false;
                    bool snappedY = false;
                    ImVec2 bestXGuideA {};
                    ImVec2 bestXGuideB {};
                    ImVec2 bestYGuideA {};
                    ImVec2 bestYGuideB {};

                    auto considerX = [&](const float delta, const float guideX, const float y1, const float y2) {
                        if (std::abs(delta) <= threshold && (!snappedX || std::abs(delta) < std::abs(bestDeltaX))) {
                            snappedX = true;
                            bestDeltaX = delta;
                            bestXGuideA = ImVec2(guideX, y1);
                            bestXGuideB = ImVec2(guideX, y2);
                        }
                    };
                    auto considerY = [&](const float delta, const float guideY, const float x1, const float x2) {
                        if (std::abs(delta) <= threshold && (!snappedY || std::abs(delta) < std::abs(bestDeltaY))) {
                            snappedY = true;
                            bestDeltaY = delta;
                            bestYGuideA = ImVec2(x1, guideY);
                            bestYGuideB = ImVec2(x2, guideY);
                        }
                    };

                    for (const int otherId : editor->GetCompositeZOrder()) {
                        if (otherId == activeItem->outputNodeId) {
                            continue;
                        }
                        const EditorModule::CompositeSceneItem* otherItem = editor->FindCompositeSceneItem(otherId);
                        if (!otherItem || !otherItem->visible || otherItem->textureWidth <= 0 || otherItem->textureHeight <= 0) {
                            continue;
                        }
                        const EditorModule::CompositeFloatRect otherBounds = QuadBounds(ComputeSceneQuadWorld(*otherItem));
                        const float otherLeft = otherBounds.x;
                        const float otherCenterX = otherBounds.x + otherBounds.width * 0.5f;
                        const float otherRight = otherBounds.x + otherBounds.width;
                        const float otherTop = otherBounds.y;
                        const float otherCenterY = otherBounds.y + otherBounds.height * 0.5f;
                        const float otherBottom = otherBounds.y + otherBounds.height;

                        if (snapSettings.snapToObjects) {
                            considerX(otherLeft - activeLeft, otherLeft, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                            considerX(otherRight - activeRight, otherRight, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                            considerY(otherTop - activeTop, otherTop, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                            considerY(otherBottom - activeBottom, otherBottom, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                        }
                        if (snapSettings.snapToCenters) {
                            considerX(otherCenterX - activeCenterX, otherCenterX, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                            considerY(otherCenterY - activeCenterY, otherCenterY, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                        }
                    }

                    if (snapSettings.snapToCanvasCenter) {
                        considerX(-activeCenterX, 0.0f, activeBounds.y, activeBottom);
                        considerY(-activeCenterY, 0.0f, activeBounds.x, activeRight);
                    }

                    if (snapSettings.snapToExportBounds && hasExportBounds) {
                        const float exportLeftW = exportBounds.x;
                        const float exportCenterXW = exportBounds.x + exportBounds.width * 0.5f;
                        const float exportRightW = exportBounds.x + exportBounds.width;
                        const float exportTopW = exportBounds.y;
                        const float exportCenterYW = exportBounds.y + exportBounds.height * 0.5f;
                        const float exportBottomW = exportBounds.y + exportBounds.height;
                        considerX(exportLeftW - activeLeft, exportLeftW, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottomW));
                        considerX(exportRightW - activeRight, exportRightW, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottomW));
                        considerY(exportTopW - activeTop, exportTopW, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRightW));
                        considerY(exportBottomW - activeBottom, exportBottomW, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRightW));
                        considerX(exportCenterXW - activeCenterX, exportCenterXW, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottomW));
                        considerY(exportCenterYW - activeCenterY, exportCenterYW, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRightW));
                    }

                    if (snappedX) {
                        targetX += bestDeltaX;
                        addSnapGuideWorld(bestXGuideA, bestXGuideB, IM_COL32(80, 200, 255, 255));
                    }
                    if (snappedY) {
                        targetY += bestDeltaY;
                        addSnapGuideWorld(bestYGuideA, bestYGuideB, IM_COL32(80, 200, 255, 255));
                    }
                };

                if (m_ActiveSceneHandle == SceneHandleType::Move) {
                    float targetX = m_SceneStartX + (mouseWorld.x - m_SceneDragStartMouseWorldX);
                    float targetY = m_SceneStartY + (mouseWorld.y - m_SceneDragStartMouseWorldY);
                    applyMoveSnapping(targetX, targetY);
                    activeItem->position = ImVec2(targetX, targetY);
                } else if (m_ActiveSceneHandle == SceneHandleType::Rotate) {
                    const std::array<ImVec2, 4> activeQuad = ComputeSceneQuadWorld(*activeItem);
                    const ImVec2 center = Midpoint(activeQuad[0], activeQuad[2]);
                    float targetRotation = std::atan2(mouseWorld.y - center.y, mouseWorld.x - center.x) - m_SceneStartMouseAngle;
                    if (snapSettings.enabled && snapSettings.rotateSnapStep > 0.0f) {
                        float degrees = targetRotation * (180.0f / 3.14159265358979323846f);
                        degrees = std::round(degrees / snapSettings.rotateSnapStep) * snapSettings.rotateSnapStep;
                        targetRotation = degrees * (3.14159265358979323846f / 180.0f);
                    }
                    activeItem->rotation = targetRotation;
                } else {
                    const ImVec2 axisX(std::cos(m_SceneStartRotation), std::sin(m_SceneStartRotation));
                    const ImVec2 axisY(-std::sin(m_SceneStartRotation), std::cos(m_SceneStartRotation));
                    const std::array<ImVec2, 4> activeQuad = ComputeSceneQuadWorld(*activeItem);
                    const ImVec2 currentCenter = Midpoint(activeQuad[0], activeQuad[2]);
                    const ImVec2 anchor = scaleFromCenter ? currentCenter : ImVec2(m_SceneResizeAnchorX, m_SceneResizeAnchorY);
                    const ImVec2 delta(mouseWorld.x - anchor.x, mouseWorld.y - anchor.y);
                    const float alongX = delta.x * axisX.x + delta.y * axisX.y;
                    const float alongY = delta.x * axisY.x + delta.y * axisY.y;
                    const float aspectRatio = std::max(0.0001f, m_SceneStartWidth / std::max(1.0f, m_SceneStartHeight));
                    float newWidth = m_SceneStartWidth;
                    float newHeight = m_SceneStartHeight;
                    float signX = 0.0f;
                    float signY = 0.0f;

                    auto applyCornerWithAspect = [&](const bool dragLeft, const bool dragTop) {
                        signX = dragLeft ? -1.0f : 1.0f;
                        signY = dragTop ? -1.0f : 1.0f;
                        const float widthByX = std::max(1.0f, std::abs(alongX));
                        const float heightByX = std::max(1.0f, widthByX / aspectRatio);
                        const float heightByY = std::max(1.0f, std::abs(alongY));
                        const float widthByY = std::max(1.0f, heightByY * aspectRatio);
                        const float errorByX = std::abs(std::abs(alongY) - heightByX);
                        const float errorByY = std::abs(std::abs(alongX) - widthByY);
                        if (errorByX <= errorByY) {
                            newWidth = widthByX;
                            newHeight = heightByX;
                        } else {
                            newWidth = widthByY;
                            newHeight = heightByY;
                        }
                    };

                    switch (m_ActiveSceneHandle) {
                        case SceneHandleType::ResizeTopLeft:
                            if (preserveAspect) {
                                applyCornerWithAspect(true, true);
                            } else {
                                signX = -1.0f;
                                signY = -1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeTopRight:
                            if (preserveAspect) {
                                applyCornerWithAspect(false, true);
                            } else {
                                signX = 1.0f;
                                signY = -1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeBottomRight:
                            if (preserveAspect) {
                                applyCornerWithAspect(false, false);
                            } else {
                                signX = 1.0f;
                                signY = 1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeBottomLeft:
                            if (preserveAspect) {
                                applyCornerWithAspect(true, false);
                            } else {
                                signX = -1.0f;
                                signY = 1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeLeft:
                            signX = -1.0f;
                            newWidth = std::max(1.0f, std::abs(alongX));
                            newHeight = preserveAspect ? std::max(1.0f, newWidth / aspectRatio) : m_SceneStartHeight;
                            break;
                        case SceneHandleType::ResizeRight:
                            signX = 1.0f;
                            newWidth = std::max(1.0f, std::abs(alongX));
                            newHeight = preserveAspect ? std::max(1.0f, newWidth / aspectRatio) : m_SceneStartHeight;
                            break;
                        case SceneHandleType::ResizeTop:
                            signY = -1.0f;
                            newHeight = std::max(1.0f, std::abs(alongY));
                            newWidth = preserveAspect ? std::max(1.0f, newHeight * aspectRatio) : m_SceneStartWidth;
                            break;
                        case SceneHandleType::ResizeBottom:
                            signY = 1.0f;
                            newHeight = std::max(1.0f, std::abs(alongY));
                            newWidth = preserveAspect ? std::max(1.0f, newHeight * aspectRatio) : m_SceneStartWidth;
                            break;
                        default:
                            break;
                    }

                    if (snapSettings.enabled && snapSettings.scaleSnapStep > 0.0f) {
                        const float baseWidth = activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureWidth));
                        const float baseHeight = activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureHeight));
                        newWidth = std::max(1.0f, std::round((newWidth / baseWidth) / snapSettings.scaleSnapStep) * snapSettings.scaleSnapStep * baseWidth);
                        newHeight = std::max(1.0f, std::round((newHeight / baseHeight) / snapSettings.scaleSnapStep) * snapSettings.scaleSnapStep * baseHeight);
                    }

                    const ImVec2 newCenter = scaleFromCenter
                        ? currentCenter
                        : ImVec2(
                            anchor.x + axisX.x * signX * newWidth * 0.5f + axisY.x * signY * newHeight * 0.5f,
                            anchor.y + axisX.y * signX * newWidth * 0.5f + axisY.y * signY * newHeight * 0.5f);
                    activeItem->scale.x = std::max(0.01f, newWidth / (activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureWidth))));
                    activeItem->scale.y = std::max(0.01f, newHeight / (activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureHeight))));
                    activeItem->position = ImVec2(newCenter.x - newWidth * 0.5f, newCenter.y - newHeight * 0.5f);
                }
            } else {
                m_ActiveSceneHandle = SceneHandleType::None;
                m_ActiveSceneOutputNodeId = -1;
            }
        }

        if (ImGui::BeginPopup("CompositeCanvasContext")) {
            if (ImGui::MenuItem("Add Image")) {
                m_PendingCompositeAddImageDialog = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Add Library Asset")) {
                m_ShowCompositeAssetPicker = true;
                ImGui::OpenPopup("CompositeCanvasAssetPicker");
            }
            if (ImGui::MenuItem("Add Square")) {
                editor->AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind::Square);
            }
            if (ImGui::MenuItem("Add Circle")) {
                editor->AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind::Circle);
            }
            if (ImGui::MenuItem("Add Text")) {
                editor->AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind::Text);
            }
            ImGui::EndPopup();
        }

        if (m_ShowCompositeAssetPicker) {
            ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("CompositeCanvasAssetPicker", nullptr, ImGuiWindowFlags_NoCollapse)) {
                const auto& assets = LibraryManager::Get().GetAssets();
                if (assets.empty()) {
                    ImGui::TextDisabled("No library image assets are available.");
                } else {
                    ImGui::TextDisabled("Select a library asset to create a new image chain.");
                    ImGui::Separator();
                    ImGui::BeginChild("CompositeCanvasAssetList", ImVec2(0.0f, -42.0f), false);
                    for (const auto& asset : assets) {
                        if (!asset) {
                            continue;
                        }
                        const std::string label = asset->displayName.empty()
                            ? asset->fileName
                            : (asset->displayName + "##" + asset->fileName);
                        if (ImGui::Selectable(label.c_str(), false)) {
                            if (editor->AddCompositeLibraryAssetChain(asset->fileName)) {
                                m_ShowCompositeAssetPicker = false;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        if (asset->width > 0 && asset->height > 0) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("%d x %d", asset->width, asset->height);
                        }
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
                    m_ShowCompositeAssetPicker = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            } else {
                m_ShowCompositeAssetPicker = false;
            }
        }

        drawList->PushClipRect(canvasMin, canvasMax, true);
        for (auto it = editor->GetCompositeZOrder().rbegin(); it != editor->GetCompositeZOrder().rend(); ++it) {
            const int outputNodeId = *it;
            const EditorModule::CompositeSceneItem* item = editor->FindCompositeSceneItem(outputNodeId);
            if (!item || !item->visible || item->texture == 0 || item->textureWidth <= 0 || item->textureHeight <= 0) {
                continue;
            }

            const std::array<ImVec2, 4> worldQuad = ComputeSceneQuadWorld(*item);
            const std::array<ImVec2, 4> screenQuad = {
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[0]),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[1]),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[2]),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[3]),
            };

            drawList->AddImageQuad(
                (ImTextureID)(intptr_t)item->texture,
                screenQuad[0],
                screenQuad[1],
                screenQuad[2],
                screenQuad[3]);

            if (editor->GetCompositeSelectedOutputNodeId() == item->outputNodeId) {
                if (isExportSettingsActive) {
                    // Locked: render no selection handles or borders
                } else if (editor->IsPickingColor() || isEditingComplexNode) {
                    drawList->AddQuad(
                        screenQuad[0],
                        screenQuad[1],
                        screenQuad[2],
                        screenQuad[3],
                        ApplyAlpha(IM_COL32(95, 165, 255, 200), viewportRevealAlpha * (0.55f + m_CompositeSelectionOutlineAnim * 0.45f)),
                        1.7f + m_CompositeSelectionOutlineAnim * 0.7f);
                } else {
                    if (m_CompositeSelectionOutlineAnim > 0.001f) {
                        drawList->AddQuad(
                            screenQuad[0],
                            screenQuad[1],
                            screenQuad[2],
                            screenQuad[3],
                            ApplyAlpha(IM_COL32(120, 196, 255, 155), viewportRevealAlpha * 0.55f * m_CompositeSelectionOutlineAnim),
                            3.0f + m_CompositeSelectionOutlineAnim * 1.5f);
                    }
                    drawList->AddQuad(
                        screenQuad[0],
                        screenQuad[1],
                        screenQuad[2],
                        screenQuad[3],
                        ApplyAlpha(IM_COL32(225, 240, 247, 235), viewportRevealAlpha * (0.45f + m_CompositeSelectionOutlineAnim * 0.55f)),
                        1.6f + m_CompositeSelectionOutlineAnim * 0.9f);
                    const ImVec2 topCenter = Midpoint(screenQuad[0], screenQuad[1]);
                    const ImVec2 rightCenter = Midpoint(screenQuad[1], screenQuad[2]);
                    const ImVec2 bottomCenter = Midpoint(screenQuad[2], screenQuad[3]);
                    const ImVec2 leftCenter = Midpoint(screenQuad[3], screenQuad[0]);
                    const ImVec2 center = Midpoint(screenQuad[0], screenQuad[2]);
                    const ImVec2 outward = NormalizeOrZero(ImVec2(topCenter.x - center.x, topCenter.y - center.y));
                    const ImVec2 rotateHandle = ImVec2(topCenter.x + outward.x * 30.0f, topCenter.y + outward.y * 30.0f);
                    const float handleAnim = m_CompositeSelectionHandleAnim;
                    drawList->AddLine(
                        topCenter,
                        rotateHandle,
                        ApplyAlpha(IM_COL32(225, 240, 247, 200), viewportRevealAlpha * (0.35f + handleAnim * 0.65f)),
                        1.2f + handleAnim * 0.7f);
                    auto drawHandle = [&](const ImVec2& point, const ImU32 fillColor, const float radius) {
                        const float animatedRadius = radius + handleAnim * 0.75f;
                        if (handleAnim > 0.001f) {
                            drawList->AddCircle(
                                point,
                                animatedRadius + 1.5f,
                                ApplyAlpha(IM_COL32(120, 196, 255, 150), viewportRevealAlpha * 0.50f * handleAnim),
                                20,
                                1.0f + handleAnim * 0.6f);
                        }
                        drawList->AddCircleFilled(point, animatedRadius, ApplyAlpha(fillColor, viewportRevealAlpha), 20);
                        drawList->AddCircle(point, animatedRadius, ApplyAlpha(IM_COL32(10, 14, 18, 255), viewportRevealAlpha), 20, 1.2f + handleAnim * 0.4f);
                    };
                    const ImU32 resizeFill = IM_COL32(230, 238, 244, 245);
                    drawHandle(screenQuad[0], resizeFill, 5.0f);
                    drawHandle(screenQuad[1], resizeFill, 5.0f);
                    drawHandle(screenQuad[2], resizeFill, 5.0f);
                    drawHandle(screenQuad[3], resizeFill, 5.0f);
                    drawHandle(topCenter, resizeFill, 4.5f);
                    drawHandle(rightCenter, resizeFill, 4.5f);
                    drawHandle(bottomCenter, resizeFill, 4.5f);
                    drawHandle(leftCenter, resizeFill, 4.5f);
                    drawHandle(rotateHandle, IM_COL32(120, 196, 255, 245), 6.0f);
                }
            } else if (!editor->IsPickingColor() && !isEditingComplexNode && !isExportSettingsActive && hoveredItem && hoveredItem->outputNodeId == item->outputNodeId) {
                drawList->AddQuad(
                    screenQuad[0],
                    screenQuad[1],
                    screenQuad[2],
                    screenQuad[3],
                    ApplyAlpha(IM_COL32(154, 199, 224, 150), viewportRevealAlpha * (0.20f + m_CompositeHoverOutlineAnim * 0.80f)),
                    0.9f + m_CompositeHoverOutlineAnim * 0.7f);
            }
        }
        for (const SnapGuideLine& guide : m_CompositeSnapGuides) {
            drawList->AddLine(guide.a, guide.b, guide.color, 1.4f);
        }

        // Draw an ultra-premium, gap-free, non-overlapping concentric rounded vignette.
        // This completely eliminates overlapping artifacts, double-blending at corners,
        // and hard edges, creating a mathematically perfect and smooth blend for the canvas.
        if (!wallpaperSurfaces) {
            const float seamFade = 48.0f;
            const float edgeFade = 24.0f;
            const ImVec4 workspaceBg = editor->GetWorkspaceBaseColor();

            constexpr int N = 32;
            for (int i = 0; i < N; ++i) {
                float t = static_cast<float>(i) / N;

                // Inset from canvas edges moving inward
                float leftInset = t * seamFade;
                float rightInset = t * edgeFade;
                float topInset = t * edgeFade;
                float bottomInset = t * edgeFade;

                ImVec2 rectMin(canvasMin.x + leftInset, canvasMin.y + topInset);
                ImVec2 rectMax(canvasMax.x - rightInset, canvasMax.y - bottomInset);

                // Premium cubic falloff for a cinematic, natural-looking smooth transition
                float smoothAlpha = std::pow(1.0f - t, 2.5f);
                ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(workspaceBg.x, workspaceBg.y, workspaceBg.z, smoothAlpha));

                // Adjust corner rounding radius to match the inset rectangle perfectly
                float rounding = std::max(0.0f, kCanvasRounding - (t * edgeFade));

                // Draw concentric rounded rectangle outlines with a thickness of 2.0f.
                // This ensures overlaps are continuous and completely gap-free.
                drawList->AddRect(rectMin, rectMax, color, rounding, ImDrawFlags_None, 2.0f);
            }
        }

        drawList->PopClipRect();

        if (hasExportBounds) {
            const ImU32 boundsColor = exportBoundsEditMode
                ? IM_COL32(96, 192, 255, 255)
                : IM_COL32(96, 192, 255, 110);
            drawList->AddRect(exportTopLeft, exportBottomRight, boundsColor, 0.0f, 0, 2.0f);
            if (editableCustomBounds) {
                const bool freeAspect = true;
                std::array<ImVec2, 8> handlePoints = {
                    exportTopLeft,
                    exportTopCenter,
                    exportTopRight,
                    exportRightCenter,
                    exportBottomRight,
                    exportBottomCenter,
                    exportBottomLeft,
                    exportLeftCenter
                };
                const int handleCount = 8;
                const int handleIndices[] = { 0, 2, 4, 6, 1, 3, 5, 7 };
                for (int handleIndex = 0; handleIndex < handleCount; ++handleIndex) {
                    const ImVec2& point = handlePoints[handleIndices[handleIndex]];
                    drawList->AddRectFilled(
                        ImVec2(point.x - 5.0f, point.y - 5.0f),
                        ImVec2(point.x + 5.0f, point.y + 5.0f),
                        boundsColor,
                        2.0f);
                    drawList->AddRect(
                        ImVec2(point.x - 5.0f, point.y - 5.0f),
                        ImVec2(point.x + 5.0f, point.y + 5.0f),
                        IM_COL32(10, 14, 18, 255),
                        2.0f);
                }
            }
        }

        if (editor->GetCompositeSceneItems().empty()) {
            const char* emptyMessage = "Add separate completed chains to start compositing.";
            const ImVec2 textSize = ImGui::CalcTextSize(emptyMessage);
            drawList->AddText(
                ImVec2(canvasMin.x + std::max(20.0f, (avail.x - textSize.x) * 0.5f),
                       canvasMin.y + std::max(28.0f, (avail.y - textSize.y) * 0.18f)),
                IM_COL32(190, 205, 212, 220),
                emptyMessage);
        }
        return;
    }

    const bool outputConnected = editor->GetNodeGraph().IsOutputConnected();
    const bool hasViewportTiles = editor->HasViewportOutputTiles();
    const bool hasOutputTexture = pipeline.GetOutputTexture() != 0 || hasViewportTiles;
    if (!outputConnected || !hasOutputTexture) {
        editor->ClearToneCurveViewportProbe();
        const char* emptyMessage = nullptr;
        if (editor->IsEditorRenderBusy()) {
            emptyMessage = "Rendering editor output...";
        } else if (!outputConnected) {
            emptyMessage = editor->GetNodeGraph().GetActiveImageNodeId() > 0
                ? "Connect the graph to the output to preview it."
                : "Drop an image into the graph or press Tab to start.";
        } else {
            emptyMessage = "Graph render produced no output.";
        }
        const ImVec2 avail = hostAvail;
        const ImVec2 textSize = ImGui::CalcTextSize(emptyMessage);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (avail.x - textSize.x) * 0.5f),
            std::max(24.0f, (avail.y - textSize.y) * 0.22f)));
        ImGui::TextDisabled("%s", emptyMessage);
        return;
    }

    // ── Inputs & Zoom Logic ──────────────────────────────────────────────────
    
    // Toggle Lock with 'L' key
    if (!inputBlocked && editor->CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        m_IsLocked = !m_IsLocked;
    }

    bool isHovered = !inputBlocked && ImGui::IsWindowHovered();
    ImVec2 avail = hostAvail;
    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 contentScreen = hostScreen;
    ImVec2 relativeMouse = ImVec2(mousePos.x - contentScreen.x, mousePos.y - contentScreen.y);

    if (isHovered && !m_IsLocked) {
        // Zoom with Scroll
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            m_ZoomLevel += wheel * m_ZoomLevel * 0.1f;
            m_ZoomLevel = std::max(1.0f, std::min(m_ZoomLevel, 100.0f)); // Min 1.0 (Fit), Max 100x
        }
    }

    // ── Rendering Logic ──────────────────────────────────────────────────────
    
    unsigned int outputTex = pipeline.GetOutputTexture();
    unsigned int sourceTex = pipeline.GetCompareSourceTexture();
    const EditorRenderWorker::SharedTextureTileSet& viewportTiles = editor->GetViewportOutputTiles();
    int imgW = hasViewportTiles ? viewportTiles.fullWidth : pipeline.GetCanvasWidth();
    int imgH = hasViewportTiles ? viewportTiles.fullHeight : pipeline.GetCanvasHeight();
    const bool singleOutputMode = editor->GetViewportMode() == EditorModule::ViewportMode::SingleOutputPreview;
    EditorModule::DevelopSubjectViewportState developSubjectState;
    const bool hasDevelopSubjectOverlay =
        singleOutputMode && editor->GetDevelopSubjectImportanceViewportState(developSubjectState);
    const bool canStaticCompare = singleOutputMode && sourceTex != 0 && (hasViewportTiles || sourceTex != outputTex);
    if (!canStaticCompare || !singleOutputMode) {
        m_ShowStaticSingleCompare = false;
    }
    if (!canStaticCompare) {
        m_StaticSingleCompareBlend = 0.0f;
        m_StaticCompareRectsInitialized = false;
    }
    if (!inputBlocked &&
        canStaticCompare &&
        editor->CanConsumeEditorCommandKeys() &&
        ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        m_ShowStaticSingleCompare = !m_ShowStaticSingleCompare;
        editor->SetHoverFade(0.0f);
    }

    // Base scale to fit screen
    float scaleX = avail.x / (float)imgW;
    float scaleY = avail.y / (float)imgH;
    float baseScale = std::min(scaleX, scaleY) * 0.94f; // Use more of the pane so detail holds up better before zooming

    float finalScale = baseScale * m_ZoomLevel;
    float dispW = (float)imgW * finalScale;
    float dispH = (float)imgH * finalScale;

    // Panning (Follow mouse if zoomed in and not locked)
    if (!m_IsLocked && finalScale > baseScale) {
        // Map mouse position [0, avail] to pan offset
        // Normalizing relativeMouse to [0, 1] across the available region
        float mouseNormX = (relativeMouse.x / avail.x) - 0.5f;
        float mouseNormY = (relativeMouse.y / avail.y) - 0.5f;

        // The overflow is how much larger the image is than the viewport
        float overflowX = std::max(0.0f, dispW - avail.x);
        float overflowY = std::max(0.0f, dispH - avail.y);

        m_PanX = -mouseNormX * overflowX;
        m_PanY = -mouseNormY * overflowY;
    }

    const ImVec2 contentCursor = ImGui::GetCursorPos();
    ImDrawList* drawList = hostDrawList;
    float offsetX = (avail.x - dispW) * 0.5f + m_PanX;
    float offsetY = (avail.y - dispH) * 0.5f + m_PanY;
    const ImVec2 singleImageMin(contentScreen.x + offsetX, contentScreen.y + offsetY);
    const ImVec2 singleImageMax(singleImageMin.x + dispW, singleImageMin.y + dispH);

    if (canStaticCompare) {
        const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
        const float blendTarget = m_ShowStaticSingleCompare ? 1.0f : 0.0f;
        const float blendT = 1.0f - std::exp(-dt / 0.11f);
        m_StaticSingleCompareBlend += (blendTarget - m_StaticSingleCompareBlend) * blendT;
        if (!m_ShowStaticSingleCompare && m_StaticSingleCompareBlend < 0.001f) {
            m_StaticSingleCompareBlend = 0.0f;
            m_StaticCompareRectsInitialized = false;
        }
    }

    if (canStaticCompare && (m_ShowStaticSingleCompare || m_StaticSingleCompareBlend > 0.001f)) {
        editor->SetHoverFade(0.0f);
        ImGui::InvisibleButton("StaticSingleCompareSurface", avail);

        constexpr float kImageRounding = 18.0f;
        const float outerMargin = 8.0f;
        const float gap = 14.0f;
        const float drawAvailW = std::max(1.0f, avail.x - outerMargin * 2.0f);
        const float drawAvailH = std::max(1.0f, avail.y - outerMargin * 2.0f);
        auto fitArea = [&](float slotW, float slotH) {
            const float scale = std::min(slotW / static_cast<float>(imgW), slotH / static_cast<float>(imgH));
            const float w = static_cast<float>(imgW) * scale;
            const float h = static_cast<float>(imgH) * scale;
            return w * h;
        };
        const float stackedSlotH = std::max(1.0f, (drawAvailH - gap) * 0.5f);
        const float sideSlotW = std::max(1.0f, (drawAvailW - gap) * 0.5f);
        const float stackedArea = fitArea(drawAvailW, stackedSlotH);
        const float sideArea = fitArea(sideSlotW, drawAvailH);
        const float imageAspect = imgH > 0 ? (static_cast<float>(imgW) / static_cast<float>(imgH)) : 1.0f;
        const float viewportAspect = drawAvailH > 0.0f ? (drawAvailW / drawAvailH) : 1.0f;
        const float horizontalBias = std::clamp((imageAspect - 1.0f) / 0.85f, 0.0f, 1.0f);
        const float verticalBias = std::clamp((1.0f - imageAspect) / 0.55f, 0.0f, 1.0f);
        const float stackedScore = stackedArea * (1.0f + verticalBias * 0.18f - horizontalBias * 0.18f);
        const float sideScore = sideArea * (1.0f + horizontalBias * 0.28f);
        const bool forceStacked = viewportAspect < 0.92f;
        const bool forceSideBySide = imageAspect >= 1.22f && viewportAspect >= 0.98f;
        const bool stacked = forceStacked
            ? true
            : (forceSideBySide
                ? false
                : (std::abs(stackedScore - sideScore) < 1.0f ? imgW < imgH : stackedScore > sideScore));

        const ImU32 workspaceFill = ImGui::ColorConvertFloat4ToU32(editor->GetWorkspaceBaseColor());
        const float uInset = imgW > 1 ? (0.5f / static_cast<float>(imgW)) : 0.0f;
        const float vInset = imgH > 1 ? (0.5f / static_cast<float>(imgH)) : 0.0f;
        auto fitImageRect = [&](ImVec2 slotMin, ImVec2 slotMax, ImVec2& outMin, ImVec2& outMax) {
            const float slotW = std::max(1.0f, slotMax.x - slotMin.x);
            const float slotH = std::max(1.0f, slotMax.y - slotMin.y);
            const float scale = std::min(slotW / static_cast<float>(imgW), slotH / static_cast<float>(imgH)) * 0.98f;
            const float imageW = static_cast<float>(imgW) * scale;
            const float imageH = static_cast<float>(imgH) * scale;
            outMin = ImVec2(slotMin.x + (slotW - imageW) * 0.5f, slotMin.y + (slotH - imageH) * 0.5f);
            outMax = ImVec2(outMin.x + imageW, outMin.y + imageH);
        };
        auto drawViewportTileSet = [&](ImVec2 min, ImVec2 max, float alpha) {
            alpha *= viewportRevealAlpha;
            if (!hasViewportTiles || alpha <= 0.001f || viewportTiles.fullWidth <= 0 || viewportTiles.fullHeight <= 0) {
                return;
            }
            const float drawW = std::max(1.0f, max.x - min.x);
            const float drawH = std::max(1.0f, max.y - min.y);
            drawList->PushClipRect(min, max, true);
            for (const EditorRenderWorker::SharedTextureTile& tile : viewportTiles.tiles) {
                if (tile.texture == 0 || tile.width <= 0 || tile.height <= 0 || tile.haloWidth <= 0 || tile.haloHeight <= 0) {
                    continue;
                }
                const float tileMinX = min.x + (static_cast<float>(tile.x) / static_cast<float>(viewportTiles.fullWidth)) * drawW;
                const float tileMaxX = min.x + (static_cast<float>(tile.x + tile.width) / static_cast<float>(viewportTiles.fullWidth)) * drawW;
                const float tileMinY = max.y - (static_cast<float>(tile.y + tile.height) / static_cast<float>(viewportTiles.fullHeight)) * drawH;
                const float tileMaxY = max.y - (static_cast<float>(tile.y) / static_cast<float>(viewportTiles.fullHeight)) * drawH;
                const float localX = static_cast<float>(tile.x - tile.haloX);
                const float localY = static_cast<float>(tile.y - tile.haloY);
                const float u0 = (localX + 0.5f) / static_cast<float>(tile.haloWidth);
                const float u1 = (localX + static_cast<float>(tile.width) - 0.5f) / static_cast<float>(tile.haloWidth);
                const float bottomV = (localY + 0.5f) / static_cast<float>(tile.haloHeight);
                const float topV = (localY + static_cast<float>(tile.height) - 0.5f) / static_cast<float>(tile.haloHeight);
                drawList->AddImage(
                    (ImTextureID)(intptr_t)tile.texture,
                    ImVec2(tileMinX, tileMinY),
                    ImVec2(tileMaxX, tileMaxY),
                    ImVec2(u0, 1.0f - topV),
                    ImVec2(u1, 1.0f - bottomV),
                    IM_COL32(255, 255, 255, static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f))));
                if (viewportTiles.debugOverlay) {
                    drawList->AddRect(
                        ImVec2(tileMinX, tileMinY),
                        ImVec2(tileMaxX, tileMaxY),
                        IM_COL32(95, 190, 255, static_cast<int>(190.0f * std::clamp(alpha, 0.0f, 1.0f))),
                        0.0f,
                        0,
                        1.0f);
                }
            }
            drawList->PopClipRect();
        };
        auto drawAnimatedImage = [&](unsigned int texture, ImVec2 min, ImVec2 max, float alpha) {
            alpha *= viewportRevealAlpha;
            if (alpha <= 0.001f) {
                return;
            }
            const float imageW = std::max(1.0f, max.x - min.x);
            const float imageH = std::max(1.0f, max.y - min.y);
            const float tilesX = std::max(1.0f, imageW / 16.0f);
            const float tilesY = std::max(1.0f, imageH / 16.0f);
            if (!wallpaperSurfaces) {
                drawList->AddRectFilled(min, max, ApplyAlpha(workspaceFill, alpha), kImageRounding);
            }
            drawList->AddImageRounded(
                (ImTextureID)(intptr_t)m_CheckerTex,
                min,
                max,
                ImVec2(0.0f, 0.0f),
                ImVec2(tilesX, tilesY),
                IM_COL32(255, 255, 255, static_cast<int>(170.0f * std::clamp(alpha, 0.0f, 1.0f))),
                kImageRounding);
            if (hasViewportTiles && texture == outputTex) {
                drawViewportTileSet(min, max, alpha / std::max(0.001f, viewportRevealAlpha));
            } else {
                drawList->AddImageRounded(
                    (ImTextureID)(intptr_t)texture,
                    min,
                    max,
                    ImVec2(uInset, 1.0f - vInset),
                    ImVec2(1.0f - uInset, vInset),
                    IM_COL32(255, 255, 255, static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f))),
                    kImageRounding);
            }
        };

        const ImVec2 min(contentScreen.x + outerMargin, contentScreen.y + outerMargin);
        const ImVec2 max(contentScreen.x + avail.x - outerMargin, contentScreen.y + avail.y - outerMargin);
        ImVec2 outputTargetMin = singleImageMin;
        ImVec2 outputTargetMax = singleImageMax;
        ImVec2 sourceTargetMin = singleImageMin;
        ImVec2 sourceTargetMax = singleImageMax;

        if (stacked) {
            const float midY = min.y + (max.y - min.y - gap) * 0.5f;
            fitImageRect(min, ImVec2(max.x, midY), outputTargetMin, outputTargetMax);
            fitImageRect(ImVec2(min.x, midY + gap), max, sourceTargetMin, sourceTargetMax);
        } else {
            const float midX = min.x + (max.x - min.x - gap) * 0.5f;
            fitImageRect(min, ImVec2(midX, max.y), outputTargetMin, outputTargetMax);
            fitImageRect(ImVec2(midX + gap, min.y), max, sourceTargetMin, sourceTargetMax);
        }
        if (!m_ShowStaticSingleCompare) {
            outputTargetMin = singleImageMin;
            outputTargetMax = singleImageMax;
            sourceTargetMin = singleImageMin;
            sourceTargetMax = singleImageMax;
        }

        if (!m_StaticCompareRectsInitialized) {
            m_StaticCompareOutputMin = singleImageMin;
            m_StaticCompareOutputMax = singleImageMax;
            m_StaticCompareSourceMin = singleImageMin;
            m_StaticCompareSourceMax = singleImageMax;
            m_StaticCompareRectsInitialized = true;
        }

        const float rectT = 1.0f - std::exp(-std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) / 0.16f);
        m_StaticCompareOutputMin = LerpImVec2(m_StaticCompareOutputMin, outputTargetMin, rectT);
        m_StaticCompareOutputMax = LerpImVec2(m_StaticCompareOutputMax, outputTargetMax, rectT);
        m_StaticCompareSourceMin = LerpImVec2(m_StaticCompareSourceMin, sourceTargetMin, rectT);
        m_StaticCompareSourceMax = LerpImVec2(m_StaticCompareSourceMax, sourceTargetMax, rectT);

        const float sourceAlpha = SmoothStep01(m_StaticSingleCompareBlend);
        drawAnimatedImage(sourceTex, m_StaticCompareSourceMin, m_StaticCompareSourceMax, sourceAlpha);
        drawAnimatedImage(outputTex, m_StaticCompareOutputMin, m_StaticCompareOutputMax, 1.0f);
        return;
    }

    const bool showHud = m_IsLocked || editor->IsEditorRenderBusy() || editor->IsAutoGainMaskPreviewActive();
    m_ViewportHudAnim = AnimateUiValue(m_ViewportHudAnim, showHud ? 1.0f : 0.0f, deltaTime, 13.0f, 9.0f);
    if (m_ViewportHudAnim > 0.01f) {
        const float hudBaseY = contentScreen.y + 10.0f + (1.0f - m_ViewportHudAnim) * -6.0f;
        const ImVec2 hudPos = ImVec2(contentScreen.x + 10.0f, hudBaseY);
        const float hudAlpha = viewportRevealAlpha * m_ViewportHudAnim;
        if (m_IsLocked) {
            drawList->AddText(hudPos, ApplyAlpha(IM_COL32(255, 150, 40, 255), hudAlpha), "[ ZOOM LOCKED ] - Press 'L' to unlock");
        }
        if (editor->IsEditorRenderBusy()) {
            const float busyOffsetY = m_IsLocked ? 22.0f : 0.0f;
            drawList->AddText(ImVec2(contentScreen.x + 10.0f, hudBaseY + busyOffsetY),
                              ApplyAlpha(IM_COL32(190, 195, 205, 230), hudAlpha),
                              "Rendering...");
        }
        if (editor->IsAutoGainMaskPreviewActive()) {
            const float maskOffsetY = (m_IsLocked ? 22.0f : 0.0f) + (editor->IsEditorRenderBusy() ? 22.0f : 0.0f);
            drawList->AddText(ImVec2(contentScreen.x + 10.0f, hudBaseY + maskOffsetY),
                              ApplyAlpha(IM_COL32(180, 215, 255, 235), hudAlpha),
                              "Pre-Local Exposure preview - click image to return");
        }
    }

    ImVec2 startCursorPos = ImVec2(contentCursor.x + offsetX, contentCursor.y + offsetY);
    ImGui::SetCursorPos(startCursorPos);
    
    ImVec2 drawScreenPos = ImGui::GetCursorScreenPos();
    float imgMouseX = mousePos.x - drawScreenPos.x;
    float imgMouseY = mousePos.y - drawScreenPos.y;
    const bool overImage = (imgMouseX >= 0 && imgMouseX <= dispW && imgMouseY >= 0 && imgMouseY <= dispH);
    const float clampedImageU = dispW > 0.0f ? std::clamp(imgMouseX / dispW, 0.0f, 1.0f) : 0.0f;
    const float clampedImageV = dispH > 0.0f ? std::clamp(imgMouseY / dispH, 0.0f, 1.0f) : 0.0f;
    const float imageU = overImage ? clampedImageU : 0.0f;
    const float imageV = overImage ? clampedImageV : 0.0f;
    const bool toneCurveProbeActive = editor->HasFocusedToneCurveViewportInteraction() || editor->IsToneCurveTargeting();

    if (toneCurveProbeActive && overImage && isHovered && !m_IsLocked && !editor->IsPickingColor()) {
        editor->UpdateToneCurveViewportProbe(imageU, imageV);
    } else if (!editor->IsToneCurveTargeting()) {
        editor->ClearToneCurveViewportProbe();
    }

    if (editor->IsToneCurveTargeting()) {
        if (overImage && isHovered && !m_IsLocked) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                editor->BeginToneCurveViewportTargetDrag(imageU, imageV);
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const float deltaCurveY = dispH > 0.0f ? (-ImGui::GetIO().MouseDelta.y / dispH) : 0.0f;
                if (deltaCurveY != 0.0f) {
                    editor->UpdateToneCurveViewportTargetDrag(deltaCurveY);
                }
            } else {
                editor->EndToneCurveViewportTargetDrag();
            }
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            editor->EndToneCurveViewportTargetDrag();
        }
    }

    // ── Handle Color Picking ─────────────────────────────────────────────────
    if (editor->IsPickingColor() && isHovered && !m_IsLocked) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (overImage) {
            int px = std::clamp((int)(imageU * imgW), 0, imgW - 1);
            int py = std::clamp((int)(imageV * imgH), 0, imgH - 1);

            const auto& sourcePixels = pipeline.GetSourcePixelsRaw();
            int ch = pipeline.GetSourceChannels();

            // Click to pick
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int flippedY = imgH - 1 - py;
                int idx = (flippedY * imgW + px) * ch;
                if (!sourcePixels.empty() && idx >= 0 && (size_t)(idx + 2) < sourcePixels.size()) {
                    float r = sourcePixels[idx] / 255.0f;
                    float g = sourcePixels[idx + 1] / 255.0f;
                    float b = sourcePixels[idx + 2] / 255.0f;
                    editor->OnColorPicked(r, g, b);
                }
            }

            // Draw magnifier tooltip
            if (!sourcePixels.empty()) {
                const int magRadius = 5; // 11x11 grid
                const float cellSize = 12.0f;
                const int gridSize = magRadius * 2 + 1;
                const float magSize = gridSize * cellSize;

                ImVec2 magPos = ImVec2(mousePos.x + 20, mousePos.y + 20);

                // Keep magnifier on screen
                ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                if (magPos.x + magSize + 4 > displaySize.x) magPos.x = mousePos.x - magSize - 20;
                if (magPos.y + magSize + 4 > displaySize.y) magPos.y = mousePos.y - magSize - 20;

                ImDrawList* fg = ImGui::GetForegroundDrawList();
                fg->AddRectFilled(ImVec2(magPos.x - 2, magPos.y - 2),
                                  ImVec2(magPos.x + magSize + 2, magPos.y + magSize + 2),
                                  IM_COL32(30, 30, 30, 230), 4.0f);

                for (int gy = -magRadius; gy <= magRadius; gy++) {
                    for (int gx = -magRadius; gx <= magRadius; gx++) {
                        int sx = std::clamp(px + gx, 0, imgW - 1);
                        int sy = std::clamp(py + gy, 0, imgH - 1);
                        int flippedSY = imgH - 1 - sy;
                        int sIdx = (flippedSY * imgW + sx) * ch;

                        ImU32 col = IM_COL32(0, 0, 0, 255);
                        if (sIdx >= 0 && (size_t)(sIdx + 2) < sourcePixels.size()) {
                            col = IM_COL32(sourcePixels[sIdx], sourcePixels[sIdx + 1], sourcePixels[sIdx + 2], 255);
                        }

                        float cx = magPos.x + (gx + magRadius) * cellSize;
                        float cy = magPos.y + (gy + magRadius) * cellSize;
                        fg->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cellSize, cy + cellSize), col);
                    }
                }

                // Draw crosshair on center pixel
                float centerX = magPos.x + magRadius * cellSize;
                float centerY = magPos.y + magRadius * cellSize;
                fg->AddRect(ImVec2(centerX, centerY),
                            ImVec2(centerX + cellSize, centerY + cellSize),
                            IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
                fg->AddRect(ImVec2(centerX + 1, centerY + 1),
                            ImVec2(centerX + cellSize - 1, centerY + cellSize - 1),
                            IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.0f);
                const std::string& statusText = editor->GetCanvasToolStatusText();
                if (!statusText.empty()) {
                    fg->AddText(
                        ImVec2(magPos.x, magPos.y - 22.0f),
                        IM_COL32(230, 236, 242, 230),
                        statusText.c_str());
                }
            }
        }
    }

    // ── Comparison Logic (Hover Fade) ────────────────────────────────────────

    // 1) Draw checkerboard background so transparency is visible
    float checkerSize = 16.0f;
    float tilesX = dispW / checkerSize;
    float tilesY = dispH / checkerSize;
    constexpr float kImageRounding = 18.0f;
    ImGui::Dummy(ImVec2(dispW, dispH));
    const bool imageHovered = ImGui::IsItemHovered();
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    bool developSubjectRegionHandled = false;
    const bool developSubjectCanInteract =
        hasDevelopSubjectOverlay &&
        imageHovered &&
        !m_IsLocked &&
        !editor->IsPickingColor() &&
        !editor->IsToneCurveTargeting() &&
        !toneCurveProbeActive &&
        !editor->CanToggleActiveAutoGainMaskPreview();

    auto hitTestDevelopSubjectRegion =
        [&](float u, float v, DevelopSubjectRegionHandle& outHandle) -> const EditorModule::DevelopSubjectViewportRegion* {
            const EditorModule::DevelopSubjectViewportRegion* bestRegion = nullptr;
            DevelopSubjectRegionHandle bestHandle = DevelopSubjectRegionHandle::None;
            float bestScore = std::numeric_limits<float>::max();
            for (const EditorModule::DevelopSubjectViewportRegion& region : developSubjectState.regions) {
                const float rx = std::max(0.01f, region.radiusX);
                const float ry = std::max(0.01f, region.radiusY);
                const float dx = (u - region.centerX) / rx;
                const float dy = (v - region.centerY) / ry;
                const float normalizedDistance = std::sqrt(dx * dx + dy * dy);
                const float edgeDistance = std::abs(normalizedDistance - 1.0f);
                const bool nearEdge = edgeDistance <= 0.16f;
                const bool inside = normalizedDistance <= 1.0f;
                if (!nearEdge && !inside) {
                    continue;
                }
                const float activeBonus = region.id == developSubjectState.activeRegionId ? -0.18f : 0.0f;
                const float enabledPenalty = region.enabled ? 0.0f : 0.22f;
                const float score = activeBonus + enabledPenalty + (nearEdge ? edgeDistance : (0.18f + normalizedDistance * 0.04f));
                if (score < bestScore) {
                    bestScore = score;
                    bestRegion = &region;
                    bestHandle = nearEdge ? DevelopSubjectRegionHandle::Resize : DevelopSubjectRegionHandle::Move;
                }
            }
            outHandle = bestHandle;
            return bestRegion;
        };

    if ((!hasDevelopSubjectOverlay || m_ActiveDevelopSubjectNodeId != developSubjectState.nodeId) &&
        m_DevelopSubjectBrushStrokeActive) {
        if (m_ActiveDevelopSubjectNodeId > 0 && m_ActiveDevelopSubjectStrokeId > 0) {
            (void)editor->EndDevelopSubjectImportanceBrushStroke(
                m_ActiveDevelopSubjectNodeId,
                m_ActiveDevelopSubjectStrokeId);
        }
        m_DevelopSubjectBrushStrokeActive = false;
        m_ActiveDevelopSubjectNodeId = -1;
        m_ActiveDevelopSubjectStrokeId = -1;
    }

    if (!hasDevelopSubjectOverlay && m_ActiveDevelopSubjectHandle != DevelopSubjectRegionHandle::None) {
        m_ActiveDevelopSubjectHandle = DevelopSubjectRegionHandle::None;
        m_ActiveDevelopSubjectNodeId = -1;
        m_ActiveDevelopSubjectRegionId = -1;
    }

    if (m_DevelopSubjectBrushStrokeActive) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            hasDevelopSubjectOverlay &&
            m_ActiveDevelopSubjectNodeId == developSubjectState.nodeId &&
            m_ActiveDevelopSubjectStrokeId > 0) {
            developSubjectRegionHandled = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            (void)editor->AppendDevelopSubjectImportanceBrushStroke(
                m_ActiveDevelopSubjectNodeId,
                m_ActiveDevelopSubjectStrokeId,
                clampedImageU,
                clampedImageV);
        } else {
            if (m_ActiveDevelopSubjectNodeId > 0 && m_ActiveDevelopSubjectStrokeId > 0) {
                (void)editor->EndDevelopSubjectImportanceBrushStroke(
                    m_ActiveDevelopSubjectNodeId,
                    m_ActiveDevelopSubjectStrokeId);
            }
            m_DevelopSubjectBrushStrokeActive = false;
            m_ActiveDevelopSubjectNodeId = -1;
            m_ActiveDevelopSubjectStrokeId = -1;
        }
    }

    if (developSubjectCanInteract &&
        developSubjectState.brushEnabled &&
        !m_DevelopSubjectBrushStrokeActive &&
        m_ActiveDevelopSubjectHandle == DevelopSubjectRegionHandle::None) {
        developSubjectRegionHandled = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const int strokeId = editor->BeginDevelopSubjectImportanceBrushStroke(
                developSubjectState.nodeId,
                clampedImageU,
                clampedImageV);
            if (strokeId > 0) {
                m_DevelopSubjectBrushStrokeActive = true;
                m_ActiveDevelopSubjectNodeId = developSubjectState.nodeId;
                m_ActiveDevelopSubjectStrokeId = strokeId;
            }
        }
    }

    if (m_ActiveDevelopSubjectHandle != DevelopSubjectRegionHandle::None) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            hasDevelopSubjectOverlay &&
            m_ActiveDevelopSubjectNodeId == developSubjectState.nodeId) {
            developSubjectRegionHandled = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            float nextCenterX = m_DevelopSubjectStartCenterX;
            float nextCenterY = m_DevelopSubjectStartCenterY;
            float nextRadiusX = m_DevelopSubjectStartRadiusX;
            float nextRadiusY = m_DevelopSubjectStartRadiusY;
            if (m_ActiveDevelopSubjectHandle == DevelopSubjectRegionHandle::Move) {
                nextCenterX = std::clamp(
                    m_DevelopSubjectStartCenterX + (clampedImageU - m_DevelopSubjectDragStartU),
                    0.0f,
                    1.0f);
                nextCenterY = std::clamp(
                    m_DevelopSubjectStartCenterY + (clampedImageV - m_DevelopSubjectDragStartV),
                    0.0f,
                    1.0f);
            } else if (m_ActiveDevelopSubjectHandle == DevelopSubjectRegionHandle::Resize) {
                nextRadiusX = std::clamp(std::abs(clampedImageU - m_DevelopSubjectStartCenterX), 0.01f, 1.0f);
                nextRadiusY = std::clamp(std::abs(clampedImageV - m_DevelopSubjectStartCenterY), 0.01f, 1.0f);
                if (ImGui::GetIO().KeyShift) {
                    const float sharedRadius = std::max(nextRadiusX, nextRadiusY);
                    nextRadiusX = sharedRadius;
                    nextRadiusY = sharedRadius;
                }
            }
            (void)editor->UpdateDevelopSubjectImportanceRegionFromViewport(
                m_ActiveDevelopSubjectNodeId,
                m_ActiveDevelopSubjectRegionId,
                nextCenterX,
                nextCenterY,
                nextRadiusX,
                nextRadiusY);
        } else {
            m_ActiveDevelopSubjectHandle = DevelopSubjectRegionHandle::None;
            m_ActiveDevelopSubjectNodeId = -1;
            m_ActiveDevelopSubjectRegionId = -1;
        }
    }

    if (developSubjectCanInteract &&
        !developSubjectState.brushEnabled &&
        m_ActiveDevelopSubjectHandle == DevelopSubjectRegionHandle::None) {
        DevelopSubjectRegionHandle hoveredHandle = DevelopSubjectRegionHandle::None;
        const EditorModule::DevelopSubjectViewportRegion* hoveredRegion =
            hitTestDevelopSubjectRegion(imageU, imageV, hoveredHandle);
        if (hoveredRegion && hoveredHandle != DevelopSubjectRegionHandle::None) {
            developSubjectRegionHandled = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                (void)editor->SetDevelopSubjectImportanceActiveRegion(developSubjectState.nodeId, hoveredRegion->id);
                m_ActiveDevelopSubjectHandle = hoveredHandle;
                m_ActiveDevelopSubjectNodeId = developSubjectState.nodeId;
                m_ActiveDevelopSubjectRegionId = hoveredRegion->id;
                m_DevelopSubjectDragStartU = clampedImageU;
                m_DevelopSubjectDragStartV = clampedImageV;
                m_DevelopSubjectStartCenterX = hoveredRegion->centerX;
                m_DevelopSubjectStartCenterY = hoveredRegion->centerY;
                m_DevelopSubjectStartRadiusX = hoveredRegion->radiusX;
                m_DevelopSubjectStartRadiusY = hoveredRegion->radiusY;
            }
        }
    }

    if (imageHovered && !developSubjectRegionHandled && !editor->IsPickingColor() && !editor->IsToneCurveTargeting() && editor->CanToggleActiveAutoGainMaskPreview()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            editor->ToggleActiveAutoGainMaskPreview();
        }
    }
    const ImU32 workspaceFill = ImGui::ColorConvertFloat4ToU32(editor->GetWorkspaceBaseColor());
    if (!wallpaperSurfaces) {
        drawList->AddRectFilled(imageMin, imageMax, ApplyAlpha(workspaceFill, viewportRevealAlpha), kImageRounding);
    }
    drawList->AddImageRounded((ImTextureID)(intptr_t)m_CheckerTex, imageMin, imageMax,
                              ImVec2(0, 0), ImVec2(tilesX, tilesY), IM_COL32(255, 255, 255, static_cast<int>(170.0f * viewportRevealAlpha)), kImageRounding);

    // Handle Fade Factor (hover to compare with original)
    float hoverTarget = (imageHovered && !m_IsLocked && !toneCurveProbeActive && !editor->IsToneCurveTargeting()) ? 1.0f : 0.0f;
    float currentFactor = editor->GetHoverFade();
    const float fadeStep = 1.0f - std::exp(-ImGui::GetIO().DeltaTime / 0.2f);
    currentFactor += (hoverTarget - currentFactor) * fadeStep;
    currentFactor = std::max(0.0f, std::min(currentFactor, 1.0f));
    editor->SetHoverFade(currentFactor);

    // 2) Draw processed output fully opaque, then fade the original over it.
    const float uInset = imgW > 1 ? (0.5f / static_cast<float>(imgW)) : 0.0f;
    const float vInset = imgH > 1 ? (0.5f / static_cast<float>(imgH)) : 0.0f;
    auto drawViewportTileSet = [&](ImVec2 min, ImVec2 max, float alpha) {
        if (!hasViewportTiles || alpha <= 0.001f || viewportTiles.fullWidth <= 0 || viewportTiles.fullHeight <= 0) {
            return;
        }
        const float drawW = std::max(1.0f, max.x - min.x);
        const float drawH = std::max(1.0f, max.y - min.y);
        drawList->PushClipRect(min, max, true);
        for (const EditorRenderWorker::SharedTextureTile& tile : viewportTiles.tiles) {
            if (tile.texture == 0 || tile.width <= 0 || tile.height <= 0 || tile.haloWidth <= 0 || tile.haloHeight <= 0) {
                continue;
            }
            const float tileMinX = min.x + (static_cast<float>(tile.x) / static_cast<float>(viewportTiles.fullWidth)) * drawW;
            const float tileMaxX = min.x + (static_cast<float>(tile.x + tile.width) / static_cast<float>(viewportTiles.fullWidth)) * drawW;
            const float tileMinY = max.y - (static_cast<float>(tile.y + tile.height) / static_cast<float>(viewportTiles.fullHeight)) * drawH;
            const float tileMaxY = max.y - (static_cast<float>(tile.y) / static_cast<float>(viewportTiles.fullHeight)) * drawH;
            const float localX = static_cast<float>(tile.x - tile.haloX);
            const float localY = static_cast<float>(tile.y - tile.haloY);
            const float u0 = (localX + 0.5f) / static_cast<float>(tile.haloWidth);
            const float u1 = (localX + static_cast<float>(tile.width) - 0.5f) / static_cast<float>(tile.haloWidth);
            const float bottomV = (localY + 0.5f) / static_cast<float>(tile.haloHeight);
            const float topV = (localY + static_cast<float>(tile.height) - 0.5f) / static_cast<float>(tile.haloHeight);
            drawList->AddImage(
                (ImTextureID)(intptr_t)tile.texture,
                ImVec2(tileMinX, tileMinY),
                ImVec2(tileMaxX, tileMaxY),
                ImVec2(u0, 1.0f - topV),
                ImVec2(u1, 1.0f - bottomV),
                IM_COL32(255, 255, 255, static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f))));
            if (viewportTiles.debugOverlay) {
                drawList->AddRect(
                    ImVec2(tileMinX, tileMinY),
                    ImVec2(tileMaxX, tileMaxY),
                    IM_COL32(95, 190, 255, static_cast<int>(190.0f * std::clamp(alpha, 0.0f, 1.0f))),
                    0.0f,
                    0,
                    1.0f);
            }
        }
        drawList->PopClipRect();
    };
    if (hasViewportTiles) {
        drawViewportTileSet(imageMin, imageMax, viewportRevealAlpha);
    } else {
        drawList->AddImageRounded((ImTextureID)(intptr_t)outputTex, imageMin, imageMax,
                                  ImVec2(uInset, 1.0f - vInset), ImVec2(1.0f - uInset, vInset), IM_COL32(255, 255, 255, static_cast<int>(255.0f * viewportRevealAlpha)), kImageRounding);
    }

    if (currentFactor > 0.001f) {
        if (sourceTex != 0 && sourceTex != outputTex) {
            drawList->AddImageRounded((ImTextureID)(intptr_t)sourceTex, imageMin, imageMax,
                                      ImVec2(uInset, 1.0f - vInset), ImVec2(1.0f - uInset, vInset),
                                      IM_COL32(255, 255, 255, static_cast<int>(currentFactor * 255.0f * viewportRevealAlpha)), kImageRounding);
        }
    }

    if (hasDevelopSubjectOverlay) {
        drawList->PushClipRect(imageMin, imageMax, true);
        DrawDevelopSubjectInterpretedMapOverlay(
            drawList,
            developSubjectState,
            imageMin,
            imageMax);
        DrawDevelopSubjectRefinedMapOverlay(
            drawList,
            developSubjectState,
            imageMin,
            imageMax);
        for (const EditorModule::DevelopSubjectViewportStroke& stroke : developSubjectState.strokes) {
            DrawDevelopSubjectStrokeOverlay(
                drawList,
                stroke,
                developSubjectState.activeStrokeId,
                imageMin,
                imageMax,
                developSubjectState.overlayOpacity * viewportRevealAlpha);
        }
        for (const EditorModule::DevelopSubjectViewportRegion& region : developSubjectState.regions) {
            DrawDevelopSubjectRegionOverlay(
                drawList,
                region,
                developSubjectState.activeRegionId,
                imageMin,
                imageMax,
                developSubjectState.overlayOpacity * viewportRevealAlpha);
        }
        if (developSubjectCanInteract && developSubjectState.brushEnabled && imageHovered) {
            DrawDevelopSubjectBrushCursor(
                drawList,
                developSubjectState,
                imageMin,
                imageMax,
                clampedImageU,
                clampedImageV);
        }
        drawList->PopClipRect();
    }
}
