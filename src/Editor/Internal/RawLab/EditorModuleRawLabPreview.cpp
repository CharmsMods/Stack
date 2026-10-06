#include "Editor/UI/ViewportNavigation.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Raw/RawColorWarpMask.h"
#include "Renderer/GLHelpers.h"
#include "Utils/ImGuiExtras.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using Stack::Editor::RawLabInternal::FitLabImage;
using Stack::Editor::RawLabInternal::kRawLabFloatingScopesGap;
using Stack::Editor::RawLabInternal::kRawLabFloatingScopesHeight;
using Stack::Editor::RawLabInternal::LabTooltip;

namespace {

struct RawLabPreviewPresentationBounds {
    ImVec2 minimum;
    ImVec2 size;
};

RawLabPreviewPresentationBounds MakeRawLabPreviewPresentationBounds(
    const ImVec2& canvasMinimum,
    const ImVec2& canvasSize,
    bool addUnclutteredPadding) {
    if (!addUnclutteredPadding) {
        return { canvasMinimum, canvasSize };
    }

    // When neither auxiliary surface is visible, a fitted image otherwise
    // lands directly on the canvas edges. Keep this inset in the same bounds
    // used for fitting, navigation, and viewport resolution so every preview
    // subsystem agrees on the visible image rectangle.
    constexpr float kSidePadding = 28.0f;
    constexpr float kVerticalPadding = 22.0f;
    constexpr float kMinimumPreviewExtent = 120.0f;
    const float sidePadding = std::min(
        kSidePadding,
        std::max(0.0f, (canvasSize.x - kMinimumPreviewExtent) * 0.5f));
    const float verticalPadding = std::min(
        kVerticalPadding,
        std::max(0.0f, (canvasSize.y - kMinimumPreviewExtent) * 0.5f));
    return {
        ImVec2(
            canvasMinimum.x + sidePadding,
            canvasMinimum.y + verticalPadding),
        ImVec2(
            std::max(kMinimumPreviewExtent, canvasSize.x - sidePadding * 2.0f),
            std::max(kMinimumPreviewExtent, canvasSize.y - verticalPadding * 2.0f))
    };
}

const char* ColorWarpInterpretationLabel(
    Stack::RawRecipe::RawColorWarpInterpretationMode mode) {
    using Mode = Stack::RawRecipe::RawColorWarpInterpretationMode;
    switch (mode) {
        case Mode::DominantFamily: return "Dominant Family";
        case Mode::ConnectedFamily: return "Connected Family";
        case Mode::MultipleColors: return "Multiple Colors";
        case Mode::ColorOnly: return "Color Only";
        case Mode::ColorAndBrightness: return "Color + Brightness";
        case Mode::FullContents: return "Full Contents";
        case Mode::GuidedFamily:
        default: return "Guided Family";
    }
}

Stack::RawRecipe::RawColorWarpInterpretationMode CycleColorWarpInterpretation(
    Stack::RawRecipe::RawColorWarpInterpretationMode mode,
    int direction) {
    constexpr int count = 7;
    int index = static_cast<int>(mode);
    index = (index + (direction >= 0 ? 1 : count - 1)) % count;
    return static_cast<Stack::RawRecipe::RawColorWarpInterpretationMode>(index);
}

ImU32 PreviewColorWarpPinColor(
    const Stack::RawRecipe::RawColorWarpPin& pin,
    Raw::RawWorkingSpace workingSpace,
    float alpha = 1.0f) {
    Stack::RawRecipe::RawColorWarpCoordinate coordinate;
    coordinate.lightness = 0.70f;
    coordinate.a = pin.sourceA;
    coordinate.b = pin.sourceB;
    std::array<float, 3> rgb =
        Stack::RawRecipe::ColorWarpCoordinateToWorkingRgb(
            coordinate, workingSpace);
    const float minimum = std::min({ rgb[0], rgb[1], rgb[2] });
    if (minimum < 0.0f) {
        for (float& channel : rgb) channel -= minimum;
    }
    const float peak = std::max({ rgb[0], rgb[1], rgb[2], 1.0f });
    const auto display = [peak](float value) {
        return std::pow(std::clamp(value / peak, 0.0f, 1.0f), 1.0f / 2.2f);
    };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        display(rgb[0]), display(rgb[1]), display(rgb[2]), alpha));
}

void DrawPreviewDashedCircle(
    ImDrawList* drawList,
    const ImVec2& center,
    float radius,
    ImU32 color,
    float thickness = 1.5f) {
    constexpr int segments = 48;
    constexpr float twoPi = 6.28318530717958647692f;
    for (int segment = 0; segment < segments; segment += 2) {
        const float a0 = twoPi * segment / segments;
        const float a1 = twoPi * (segment + 1) / segments;
        drawList->AddLine(
            ImVec2(center.x + std::cos(a0) * radius,
                   center.y + std::sin(a0) * radius),
            ImVec2(center.x + std::cos(a1) * radius,
                   center.y + std::sin(a1) * radius),
            color,
            thickness);
    }
}

bool DrawLabTileSet(
    const EditorRenderWorker::SharedTextureTileSet& tiles,
    const ImRect& rect) {
    if (!tiles.tiled ||
        !tiles.complete ||
        tiles.tiles.empty() ||
        tiles.fullWidth <= 0 ||
        tiles.fullHeight <= 0) {
        return false;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float drawWidth = std::max(1.0f, rect.GetWidth());
    const float drawHeight = std::max(1.0f, rect.GetHeight());
    drawList->PushClipRect(rect.Min, rect.Max, true);
    for (const EditorRenderWorker::SharedTextureTile& tile : tiles.tiles) {
        if (tile.texture == 0 ||
            tile.width <= 0 ||
            tile.height <= 0 ||
            tile.haloWidth <= 0 ||
            tile.haloHeight <= 0) {
            continue;
        }
        const float tileMinX =
            rect.Min.x +
            (static_cast<float>(tile.x) / tiles.fullWidth) * drawWidth;
        const float tileMaxX =
            rect.Min.x +
            (static_cast<float>(tile.x + tile.width) / tiles.fullWidth) *
                drawWidth;
        const float tileMinY =
            rect.Max.y -
            (static_cast<float>(tile.y + tile.height) / tiles.fullHeight) *
                drawHeight;
        const float tileMaxY =
            rect.Max.y -
            (static_cast<float>(tile.y) / tiles.fullHeight) * drawHeight;
        const float localX = static_cast<float>(tile.x - tile.haloX);
        const float localY = static_cast<float>(tile.y - tile.haloY);
        const float u0 = (localX + 0.5f) / tile.haloWidth;
        const float u1 = (localX + tile.width - 0.5f) / tile.haloWidth;
        const float bottomV = (localY + 0.5f) / tile.haloHeight;
        const float topV = (localY + tile.height - 0.5f) / tile.haloHeight;
        drawList->AddImage(
            (ImTextureID)(intptr_t)tile.texture,
            ImVec2(tileMinX, tileMinY),
            ImVec2(tileMaxX, tileMaxY),
            ImVec2(u0, 1.0f - topV),
            ImVec2(u1, 1.0f - bottomV));
    }
    drawList->PopClipRect();
    return true;
}

} // namespace

int EditorModule::GetRawWorkspaceColorWarpDisplayedPin(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const {
    const int hovered = m_RawWorkspaceLabUi.colorWarpHoveredSelection;
    if (hovered >= 0 &&
        hovered < static_cast<int>(recipe.colorWarp.pins.size())) {
        return hovered;
    }
    if (m_RawWorkspaceLabUi.selectedColorWarpPin >= 0 &&
        m_RawWorkspaceLabUi.selectedColorWarpPin <
            static_cast<int>(recipe.colorWarp.pins.size())) {
        return m_RawWorkspaceLabUi.selectedColorWarpPin;
    }
    return -1;
}

int EditorModule::GetRawWorkspaceColorWarpViewMode(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) const {
    const int displayed = GetRawWorkspaceColorWarpDisplayedPin(recipe);
    if (displayed < 0) return 0;
    if (m_RawWorkspaceLabUi.colorWarpHoveredSelection >= 0 &&
        displayed != m_RawWorkspaceLabUi.selectedColorWarpPin) {
        return std::clamp(
            m_RawWorkspaceLabUi.colorWarpTemporaryViewMode, 0, 2);
    }
    const std::string& pinId = recipe.colorWarp.pins[
        static_cast<std::size_t>(displayed)].id;
    const auto retained =
        m_RawWorkspaceLabUi.colorWarpSelectionViewModes.find(pinId);
    return retained == m_RawWorkspaceLabUi.colorWarpSelectionViewModes.end()
        ? 0
        : std::clamp(retained->second, 0, 2);
}

void EditorModule::ApplyRawWorkspaceColorWarpViewOverride(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe) const {
    if (m_RawWorkspaceLabUi.activeTool != RawLabTool::Color ||
        GetRawWorkspaceColorWarpViewMode(recipe) != 1) {
        return;
    }
    const int displayed = GetRawWorkspaceColorWarpDisplayedPin(recipe);
    if (displayed >= 0 &&
        displayed < static_cast<int>(recipe.colorWarp.pins.size())) {
        recipe.colorWarp.pins[static_cast<std::size_t>(displayed)].enabled = false;
    }
}

void EditorModule::RenderRawWorkspacePreviewCanvas(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    bool drawImageFrame,
    ImVec2* outImageMinimum,
    ImVec2* outImageMaximum) {
    if (outImageMinimum != nullptr) *outImageMinimum = ImVec2(0.0f, 0.0f);
    if (outImageMaximum != nullptr) *outImageMaximum = ImVec2(0.0f, 0.0f);
    const auto publishImageRect = [&](const ImRect& imageRect) {
        if (outImageMinimum != nullptr) *outImageMinimum = imageRect.Min;
        if (outImageMaximum != nullptr) *outImageMaximum = imageRect.Max;
    };
    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color) {
        m_RawWorkspaceLabUi.colorWarpPhotoHoverValid = false;
    }
    const float gradingDrawerAmount = std::clamp(
        m_RawWorkspaceLabAnimatedLowerShelfHeight /
            kRawLabFloatingScopesHeight,
        0.0f,
        1.0f);
    const float previewBottomClearance =
        (kRawLabFloatingScopesHeight + kRawLabFloatingScopesGap) *
        gradingDrawerAmount;
    const bool unclutteredPresentation =
        m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Closed &&
        !m_RawWorkspaceLabUi.lowerShelfOpen;
    const auto imageSizeFor = [&](float width, float height, const ImVec2& bounds) {
        if (m_RawWorkspaceLabUi.previewActualPixels) {
            const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
            const float previewPixelScale = std::max(
                1.0f,
                std::max(framebufferScale.x, framebufferScale.y));
            return ImVec2(
                std::max(1.0f, width / previewPixelScale),
                std::max(1.0f, height / previewPixelScale));
        }
        const ImVec2 fitBounds(
            bounds.x,
            std::max(1.0f, bounds.y - previewBottomClearance));
        const ImVec2 fitted = FitLabImage(width, height, fitBounds);
        return ImVec2(fitted.x * .80f, fitted.y * .80f);
    };
    const auto imageMinimumFor = [&](const ImVec2& start,
                                     const ImVec2& bounds,
                                     const ImVec2& imageSize) {
        const float usableHeight = std::max(
            1.0f,
            bounds.y - previewBottomClearance);
        const float verticalFreeSpace = usableHeight - imageSize.y;
        return ImVec2(
            start.x + (bounds.x - imageSize.x) * 0.5f,
            start.y + verticalFreeSpace * 0.5f);
    };
    const RawWorkspacePreviewContext preview = ResolveRawWorkspacePreviewContext(selectedSource);
    if (preview.identity.empty()) {
        // The Gallery workspace owns browsing and the empty-folder action.
        // Bracket/Edit previews only display an image once one is open.
        return;
    }

    const bool selectedProjectActive = preview.projectActive;
    if (m_RawWorkspaceLabUi.previewViewSourceKey !=
        preview.identity) {
        m_RawWorkspaceLabUi.previewViewSourceKey =
            preview.identity;
        m_RawWorkspaceLabUi.previewZoom = 1.0f;
        m_RawWorkspaceLabUi.previewZoomTarget = 1.0f;
        m_RawWorkspaceLabUi.previewZoomAnimating = false;
        m_RawWorkspaceLabUi.previewPanX = 0.0f;
        m_RawWorkspaceLabUi.previewPanY = 0.0f;
        m_RawWorkspaceLabUi.previewPanning = false;
        m_RawWorkspaceLabUi.previewLayout = {};
        m_RawWorkspaceLabUi.previewSurroundFrom = m_RawImageBackdrop.extensionOpacity;
        m_RawWorkspaceLabUi.previewSurroundTarget = false;
        m_RawWorkspaceLabUi.previewSurroundStarted = ImGui::GetTime();
        m_RawWorkspaceLabUi.selectedEvGradient = -1;
        m_RawWorkspaceLabUi.selectedToneGradient = -1;
        m_RawWorkspaceLabUi.hoveredEvGradient = -1;
        m_RawWorkspaceLabUi.hoveredToneGradient = -1;
        m_RawWorkspaceLabUi.gradientDrawShape = -1;
        m_RawWorkspaceLabUi.gradientDragHandle = -1;
    }
    const bool selectedProjectLoading = preview.loading;
    if (!selectedProjectActive) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        Stack::RawWorkspace::SourceRecord thumbnailSource = *selectedSource;
        if (!m_RawWorkspaceLabFocusedProjectPath.empty()) {
            const auto& presentation = GetRawWorkspaceGalleryPresentation();
            const auto project = std::find_if(
                presentation.projects.begin(),
                presentation.projects.end(),
                [&](const Stack::RawWorkspace::GalleryProjectView& candidate) {
                    return !candidate.projectPath.empty() &&
                        candidate.projectPath.lexically_normal() ==
                            m_RawWorkspaceLabFocusedProjectPath.lexically_normal();
                });
            if (project != presentation.projects.end() &&
                !project->coverThumbnailCachePath.empty()) {
                thumbnailSource.relativePathKey = "project-overlay:" +
                    (!project->projectId.empty()
                        ? project->projectId
                        : project->projectPath.lexically_normal().generic_string());
                thumbnailSource.thumbnail.absolutePath =
                    project->coverThumbnailCachePath;
                thumbnailSource.thumbnail.status =
                    Stack::RawWorkspace::ThumbnailStatus::Ready;
            }
        }
        int thumbnailWidth = 0;
        int thumbnailHeight = 0;
        const unsigned int thumbnailTexture = GetRawWorkspaceThumbnailTexture(
            thumbnailSource,
            &thumbnailWidth,
            &thumbnailHeight,
            true);
        if (thumbnailTexture != 0 && thumbnailWidth > 1 && thumbnailHeight > 1) {
            const ImVec2 canvasStart = ImGui::GetCursorScreenPos();
            const ImVec2 canvasBounds(
                std::max(120.0f, available.x),
                std::max(120.0f, available.y));
            const RawLabPreviewPresentationBounds presentationBounds =
                MakeRawLabPreviewPresentationBounds(
                    canvasStart,
                    canvasBounds,
                    unclutteredPresentation);
            const ImVec2& start = presentationBounds.minimum;
            const ImVec2& imageBounds = presentationBounds.size;
            const ImVec2 imageSize = imageSizeFor(
                static_cast<float>(thumbnailWidth),
                static_cast<float>(thumbnailHeight),
                imageBounds);
            const ImVec2 imageMinimum = imageMinimumFor(
                start,
                imageBounds,
                imageSize);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddImage(
                (ImTextureID)(intptr_t)thumbnailTexture,
                imageMinimum,
                ImVec2(imageMinimum.x + imageSize.x, imageMinimum.y + imageSize.y),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));
            ImGui::Dummy(canvasBounds);
            return;
        }
        if (selectedProjectLoading) return;
        const char* label = "Double-click an image in Gallery to open it.";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (available.x - textSize.x) * 0.5f),
            std::max(0.0f, (available.y - textSize.y) * 0.5f)));
        ImGui::TextDisabled("%s", label);
        return;
    }
    const auto& previewRecipe = preview.edit.recipe;
    const ImVec2 layoutStart = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const bool colorLayout =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Color;
    const bool gradientLayout =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones ||
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Tone;
    const bool floatingTools = m_RawFloatingSurface.frame == ImGui::GetFrameCount() &&
        ImGui::GetWindowViewport() == ImGui::GetMainViewport();
    const float colorSideMargin = !floatingTools && colorLayout ? 18.0f : 0.0f;
    const float colorVerticalMargin = floatingTools ? 0.f : gradientLayout ? 36.0f : colorLayout &&
            !previewRecipe.colorWarp.pins.empty()
        ? 34.0f
        : (colorLayout ? 18.0f : 0.0f);
    const ImVec2 canvasStart(
        layoutStart.x + colorSideMargin,
        layoutStart.y + colorVerticalMargin);
    const ImVec2 canvasBounds(
        std::max(120.0f, available.x - colorSideMargin * 2.0f),
        std::max(
            120.0f,
            available.y - colorVerticalMargin * 2.0f));
    const RawLabPreviewPresentationBounds presentationBounds =
        MakeRawLabPreviewPresentationBounds(
            canvasStart,
            canvasBounds,
            unclutteredPresentation);
    const ImVec2& start = presentationBounds.minimum;
    const ImVec2& imageBounds = presentationBounds.size;
    const ImRect bounds(
        start,
        ImVec2(start.x + imageBounds.x, start.y + imageBounds.y));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (gradientLayout)
        RenderRawWorkspaceLabGradientDots(selectedSource, layoutStart, available.x);
    const bool rawStagePreview = UsesRawWorkspaceStageRender();
    const bool currentRawPreview =
        selectedProjectActive &&
        m_ViewportOutputRawWorkspaceSourceKey == preview.identity;

    if (colorLayout && !previewRecipe.colorWarp.pins.empty()) {
        constexpr float circleRadius = 12.0f;
        constexpr float circleSpacing = 9.0f;
        const float totalWidth =
            static_cast<float>(previewRecipe.colorWarp.pins.size()) *
                circleRadius * 2.0f +
            static_cast<float>(previewRecipe.colorWarp.pins.size() - 1u) *
                circleSpacing;
        const float firstCenterX = layoutStart.x +
            (available.x - totalWidth) * 0.5f + circleRadius;
        const float centerY = layoutStart.y + (floatingTools ? 18.f : colorVerticalMargin * 0.5f);
        const int previousHovered =
            m_RawWorkspaceLabUi.colorWarpHoveredSelection;
        int hovered = -1;
        bool viewChanged = false;
        const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
        for (std::size_t index = 0;
             index < previewRecipe.colorWarp.pins.size();
             ++index) {
            const float centerX = firstCenterX +
                static_cast<float>(index) *
                    (circleRadius * 2.0f + circleSpacing);
            const ImVec2 center(centerX, centerY);
            ImGui::SetCursorScreenPos(ImVec2(
                center.x - circleRadius - 3.0f,
                center.y - circleRadius - 3.0f));
            ImGui::PushID(static_cast<int>(index));
            ImGui::InvisibleButton(
                "##ColorWarpSelection",
                ImVec2((circleRadius + 3.0f) * 2.0f,
                       (circleRadius + 3.0f) * 2.0f));
            const bool itemHovered = ImGui::IsItemHovered();
            if (itemHovered) hovered = static_cast<int>(index);
            const auto& pin = previewRecipe.colorWarp.pins[index];
            const bool selectedPin =
                static_cast<int>(index) ==
                m_RawWorkspaceLabUi.selectedColorWarpPin;
            if (itemHovered &&
                previousHovered != static_cast<int>(index) &&
                !selectedPin) {
                m_RawWorkspaceLabUi.colorWarpTemporaryViewMode = 0;
            }
            int mode = 0;
            const auto retained =
                m_RawWorkspaceLabUi.colorWarpSelectionViewModes.find(pin.id);
            if (retained !=
                m_RawWorkspaceLabUi.colorWarpSelectionViewModes.end()) {
                mode = std::clamp(retained->second, 0, 2);
            }
            if (itemHovered && !selectedPin) {
                mode = std::clamp(
                    m_RawWorkspaceLabUi.colorWarpTemporaryViewMode, 0, 2);
            }
            if (itemHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0001f) {
                const int direction = ImGui::GetIO().MouseWheel > 0.0f ? 1 : 2;
                mode = (mode + direction) % 3;
                if (selectedPin) {
                    m_RawWorkspaceLabUi.colorWarpSelectionViewModes[pin.id] = mode;
                } else {
                    m_RawWorkspaceLabUi.colorWarpTemporaryViewMode = mode;
                }
                viewChanged = true;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                m_RawWorkspaceLabUi.selectedColorWarpPin =
                    static_cast<int>(index);
                m_RawWorkspaceLabUi.selectedColorWarpGroupId.clear();
                m_RawWorkspaceLabUi.colorWarpInspectionActive = true;
                m_RawWorkspaceLabUi.colorWarpInspectedRegionId = pin.regionId;
                viewChanged = true;
            }
            const ImU32 fill = PreviewColorWarpPinColor(
                pin,
                previewRecipe.technical.workingSpace);
            drawList->AddCircleFilled(center, circleRadius, fill, 32);
            drawList->AddCircle(
                center,
                circleRadius + (selectedPin ? 2.0f : 0.0f),
                selectedPin
                    ? IM_COL32(250, 250, 250, 245)
                    : ImGui::GetColorU32(ImGuiCol_Border),
                32,
                selectedPin ? 2.0f : 1.0f);
            if (mode == 1) {
                drawList->AddLine(
                    ImVec2(center.x - 8.0f, center.y + 8.0f),
                    ImVec2(center.x + 8.0f, center.y - 8.0f),
                    IM_COL32(250, 250, 250, 235),
                    2.0f);
            } else if (mode == 2) {
                DrawPreviewDashedCircle(
                    drawList,
                    center,
                    circleRadius + 4.0f,
                    IM_COL32(90, 224, 238, 235),
                    1.5f);
            }
            if (itemHovered) {
                const char* modeName = mode == 1
                    ? "Selection hidden"
                    : mode == 2 ? "Qualification mask" : "Adjusted result";
                ImGui::SetTooltip(
                    "%s\n%s\nWheel changes the viewport view%s.",
                    pin.name.c_str(),
                    modeName,
                    selectedPin ? " and keeps it" : " temporarily");
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(savedCursor);
        if (hovered != previousHovered) {
            m_RawWorkspaceLabUi.colorWarpHoveredSelection = hovered;
            if (hovered >= 0 &&
                hovered != m_RawWorkspaceLabUi.selectedColorWarpPin) {
                m_RawWorkspaceLabUi.colorWarpTemporaryViewMode = 0;
                const auto& pin = previewRecipe.colorWarp.pins[
                    static_cast<std::size_t>(hovered)];
                m_RawWorkspaceLabUi.colorWarpInspectionActive = true;
                m_RawWorkspaceLabUi.colorWarpInspectedRegionId = pin.regionId;
            } else if (hovered < 0 &&
                       m_RawWorkspaceLabUi.selectedColorWarpPin >= 0 &&
                       m_RawWorkspaceLabUi.selectedColorWarpPin <
                           static_cast<int>(previewRecipe.colorWarp.pins.size())) {
                const auto& pin = previewRecipe.colorWarp.pins[
                    static_cast<std::size_t>(
                        m_RawWorkspaceLabUi.selectedColorWarpPin)];
                m_RawWorkspaceLabUi.colorWarpInspectedRegionId = pin.regionId;
            }
            viewChanged = true;
        }
        if (viewChanged) MarkRenderRefreshDirty();
    } else if (m_RawWorkspaceLabUi.colorWarpHoveredSelection != -1) {
        m_RawWorkspaceLabUi.colorWarpHoveredSelection = -1;
        MarkRenderRefreshDirty();
    }

    bool previewNavigationHandled = false;
    auto imageRectFor = [&](float width, float height, bool rememberLayout = true) {
        const ImVec2 baseImageSize = imageSizeFor(width, height, imageBounds);
        auto& ui = m_RawWorkspaceLabUi;
        auto& layout = ui.previewLayout;
        if (!layout.valid) {
            layout.fitting = !ui.previewActualPixels &&
                std::abs(ui.previewZoom - 1.0f) < 0.0001f &&
                std::abs(ui.previewZoomTarget - 1.0f) < 0.0001f;
        }
        auto scaledImageSize = [&]() {
            return ImVec2(
                baseImageSize.x * m_RawWorkspaceLabUi.previewZoom,
                baseImageSize.y * m_RawWorkspaceLabUi.previewZoom);
        };
        auto unpannedMinimumFor = [&](const ImVec2& size) {
            return imageMinimumFor(bounds.Min, imageBounds, size);
        };
        const auto differs = [](const ImVec2& a, const ImVec2& b) {
            return std::abs(a.x - b.x) > 0.001f || std::abs(a.y - b.y) > 0.001f;
        };
        const bool sameImage = !differs(layout.sourceSize, ImVec2(width, height));
        const bool layoutChanged = differs(layout.viewportMinimum, bounds.Min) ||
            differs(layout.viewportMaximum, bounds.Max) ||
            differs(layout.baseImageSize, baseImageSize);
        if (rememberLayout && !previewNavigationHandled && layout.valid && sameImage &&
            layout.actualPixels == ui.previewActualPixels && layoutChanged &&
            (!layout.fitting || ui.previewActualPixels)) {
            // Panel motion changes the fit scale. A manually chosen view keeps
            // its displayed scale and the image point at the viewport center.
            const ImVec2 oldCenter(
                (layout.viewportMinimum.x + layout.viewportMaximum.x) * 0.5f,
                (layout.viewportMinimum.y + layout.viewportMaximum.y) * 0.5f);
            const ImVec2 centerUv(
                (oldCenter.x - layout.imageMinimum.x) / std::max(.0001f, layout.imageSize.x),
                (oldCenter.y - layout.imageMinimum.y) / std::max(.0001f, layout.imageSize.y));
            if (!ui.previewActualPixels) {
                const float scale = layout.baseImageSize.x / std::max(1.0f, baseImageSize.x);
                ui.previewZoom *= scale;
                ui.previewZoomTarget *= scale;
            }
            const ImVec2 size = scaledImageSize();
            const ImVec2 origin = unpannedMinimumFor(size);
            const ImVec2 center = bounds.GetCenter();
            ui.previewPanX = center.x - centerUv.x * size.x - origin.x;
            ui.previewPanY = center.y - centerUv.y * size.y - origin.y;
            if (ui.previewZoomAnimating) {
                ui.previewZoomFocusScreen = ImVec2(
                    origin.x + ui.previewPanX + ui.previewZoomFocusUv.x * size.x,
                    origin.y + ui.previewPanY + ui.previewZoomFocusUv.y * size.y);
            }
        }
        Stack::ViewportNavigation::State navigation {
            m_RawWorkspaceLabUi.previewZoom, m_RawWorkspaceLabUi.previewZoomTarget,
            m_RawWorkspaceLabUi.previewPanX, m_RawWorkspaceLabUi.previewPanY,
            m_RawWorkspaceLabUi.previewZoomAnimating, m_RawWorkspaceLabUi.previewPanning,
            m_RawWorkspaceLabUi.previewZoomFocusScreen, m_RawWorkspaceLabUi.previewZoomFocusUv };
        ImVec2 imageSize = scaledImageSize();
        ImVec2 unpannedMinimum = unpannedMinimumFor(imageSize);
        ImVec2 imageMinimum(
            unpannedMinimum.x + m_RawWorkspaceLabUi.previewPanX,
            unpannedMinimum.y + m_RawWorkspaceLabUi.previewPanY);

        if (!previewNavigationHandled) {
            previewNavigationHandled = true;
            ImGuiIO& previewIo = ImGui::GetIO();
            const bool previewWindowHovered = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const bool previewHovered =
                previewWindowHovered && bounds.Contains(previewIo.MousePos);

            Stack::ViewportNavigation::Input navigationInput;
            navigationInput.panConstraint = Stack::ViewportNavigation::PanConstraint::BoundedSurround;
            navigationInput.mouse = previewIo.MousePos;
            navigationInput.delta = previewIo.MouseDelta;
            // Half the logarithmic wheel step: about 8.6% per notch instead
            // of 18%, preserving fractional scrolling and cursor anchoring.
            navigationInput.wheel = previewIo.MouseWheel * .5f;
            navigationInput.seconds = previewIo.DeltaTime;
            navigationInput.hovered = previewHovered;
            navigationInput.wheelAllowed = !colorLayout ||
                (!m_RawWorkspaceLabUi.colorWarpPendingCircleActive &&
                 !(m_RawWorkspaceLabUi.colorWarpInspectionActive &&
                   (previewIo.KeyCtrl || previewIo.KeyShift)));
            navigationInput.middleClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
            navigationInput.middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            navigationInput.reset = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle);
            // A resize may put the preserved scale outside the usual fit-relative
            // limits. The next wheel step must not jump back across that range.
            navigationInput.minimumZoom = std::min({.10f, navigation.zoom, navigation.target});
            navigationInput.maximumZoom = std::max({12.0f, navigation.zoom, navigation.target});
            if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
                m_RawWorkspaceLabUi.zoneAreas.active) {
                // Keep the image under an authored stroke fixed through release.
                navigation.target = navigation.zoom;
                navigation.animating = navigation.panning = false;
            } else {
                Stack::ViewportNavigation::Update(navigation, navigationInput, baseImageSize,
                    bounds.Min, bounds.Max, unpannedMinimumFor);
                if (previewHovered && navigationInput.reset) {
                    layout.fitting = !ui.previewActualPixels;
                } else if (previewHovered && navigationInput.wheelAllowed &&
                    !navigationInput.middleDown && std::abs(navigationInput.wheel) > 0.0001f) {
                    // Reaching fit scale under an off-photo cursor does not
                    // request Fit or discard a deliberately panned view.
                    layout.fitting = false;
                }
                if (navigation.panning && (std::abs(navigationInput.delta.x) > .001f ||
                    std::abs(navigationInput.delta.y) > .001f)) layout.fitting = false;
            }
            if (m_RawWorkspaceLabUi.previewPanning)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

            imageSize = scaledImageSize();
            unpannedMinimum = unpannedMinimumFor(imageSize);
            imageMinimum = ImVec2(
                unpannedMinimum.x + m_RawWorkspaceLabUi.previewPanX,
                unpannedMinimum.y + m_RawWorkspaceLabUi.previewPanY);
        }
        const ImRect fullRect(imageMinimum,
            ImVec2(imageMinimum.x + imageSize.x, imageMinimum.y + imageSize.y));
        if (rememberLayout) {
            layout.valid = true;
            layout.actualPixels = ui.previewActualPixels;
            layout.viewportMinimum = bounds.Min;
            layout.viewportMaximum = bounds.Max;
            layout.baseImageSize = baseImageSize;
            layout.sourceSize = ImVec2(width, height);
            layout.imageMinimum = imageMinimum;
            layout.imageSize = imageSize;
        }
        UpdateRawViewportCoverage(fullRect.Min, fullRect.Max, bounds.Min, bounds.Max, currentRawPreview);
        return fullRect;
    };
    auto drawLocalOverlay = [&](const ImRect& imageRect) {
        if (!HasRawWorkspaceLocalRangeOverlayForSource(preview.identity)) {
            return;
        }
        const float uInset = m_RawWorkspaceLocalRangeOverlayWidth > 1
            ? 0.5f / m_RawWorkspaceLocalRangeOverlayWidth
            : 0.0f;
        const float vInset = m_RawWorkspaceLocalRangeOverlayHeight > 1
            ? 0.5f / m_RawWorkspaceLocalRangeOverlayHeight
            : 0.0f;
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_RawWorkspaceLocalRangeOverlayTexture,
            imageRect.Min,
            imageRect.Max,
            ImVec2(uInset, 1.0f - vInset),
            ImVec2(1.0f - uInset, vInset));
    };
    auto finishImage = [&](const ImRect& imageRect) {
        if (currentRawPreview) {
            ImRect visible = bounds;
            visible.ClipWith(ImGui::GetCurrentWindow()->ClipRect);
            PublishRawImageBackdrop(imageRect.Min, imageRect.Max, visible.Min, visible.Max, rawStagePreview);
        }
        m_BracketingImageMin=imageRect.Min;m_BracketingImageMax=imageRect.Max;
        DrawMultiFrameRawDiagnosticOverlay(drawList, imageRect.Min, imageRect.Max);
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            auto& colorUi = m_RawWorkspaceLabUi;
            if (colorUi.colorWarpPendingCircleActive) {
                colorUi.colorWarpPendingCircleActive = false;
                colorUi.colorWarpPendingCircleDrawing = false;
                colorUi.colorWarpPendingCircleRefining = false;
                colorUi.colorWarpPendingCircleCommitRequested = false;
                colorUi.colorWarpPendingCircleAppend = false;
                colorUi.colorWarpPendingReplaceRegionId.clear();
                colorUi.colorWarpPendingReplaceCircleId.clear();
                ++colorUi.colorWarpAreaAnalysisGeneration;
                colorUi.colorWarpAreaAnalysisState.reset();
            }
            colorUi.colorWarpInspectionActive = false;
            colorUi.colorWarpDiagnosticLocked = false;
            colorUi.colorWarpInspectedRegionId.clear();
        }
        if (!(m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
              m_RawWorkspaceLabUi.selectedEvGradient >= 0))
            drawLocalOverlay(imageRect);
        if (currentRawPreview)
            RenderRawWorkspaceLabGradientImage(selectedSource,
                imageRect.Min, imageRect.Max);
        if (currentRawPreview) RenderRawWorkspaceLabImageAreas(selectedSource, imageRect.Min, imageRect.Max);
        const int colorWarpDisplayedPin =
            GetRawWorkspaceColorWarpDisplayedPin(previewRecipe);
        const int colorWarpSelectionViewMode =
            GetRawWorkspaceColorWarpViewMode(previewRecipe);
        const bool colorWarpDiagnosticHeld = ImGui::IsKeyDown(ImGuiKey_H);
        m_RawWorkspaceLabUi.colorWarpDiagnosticIsolateSelected =
            colorWarpDiagnosticHeld && ImGui::GetIO().KeyShift;
        const bool colorWarpDiagnosticRequested = colorWarpDiagnosticHeld ||
            m_RawWorkspaceLabUi.colorWarpDiagnosticLocked ||
            colorWarpSelectionViewMode == 2 ||
            m_RawWorkspaceLabUi.colorWarpLightnessRangePreviewActive;
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&
            currentRawPreview &&
            (colorWarpDiagnosticHeld ||
             m_RawWorkspaceLabUi.colorWarpDiagnosticLocked ||
             colorWarpSelectionViewMode == 2)) {
            int presentationLongEdge = static_cast<int>(std::ceil(std::max(
                imageRect.GetWidth(), imageRect.GetHeight())));
            if (m_RawWorkspacePreviewOutputKind ==
                    RawWorkspacePreviewOutputKind::Tiled &&
                HasViewportOutputTiles()) {
                const auto& tiles = GetViewportOutputTiles();
                presentationLongEdge = std::max(tiles.fullWidth, tiles.fullHeight);
            } else if (m_RawWorkspacePreviewOutputKind ==
                           RawWorkspacePreviewOutputKind::SingleTexture) {
                presentationLongEdge = std::max(
                    m_RawWorkspacePresentationTexture.width,
                    m_RawWorkspacePresentationTexture.height);
            }
            const int requestedLongEdge = std::clamp(
                presentationLongEdge, 192, 2048);
            if (requestedLongEdge >
                m_RawWorkspaceLabUi.colorWarpDiagnosticTargetLongEdge) {
                m_RawWorkspaceLabUi.colorWarpDiagnosticTargetLongEdge =
                    requestedLongEdge;
                m_RawWorkspaceColorWarpInputGraphScopeCache = {};
                m_RawWorkspaceGraphScopeReadback = {};
                m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint = 0;
                ++m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration;
                m_RawWorkspaceLabUi.colorWarpDiagnosticState.reset();
                MarkRenderRefreshDirty();
            }
        }
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&
            currentRawPreview &&
            colorWarpDiagnosticRequested &&
            colorWarpDisplayedPin >= 0 &&
            colorWarpDisplayedPin < static_cast<int>(
                previewRecipe.colorWarp.pins.size())) {
            const std::string previewIdentity =
                GetActiveRawWorkspacePreviewIdentity();
            const RawDevelopmentGraphScopeReadback& cachedColorScope =
                m_RawWorkspaceColorWarpInputGraphScopeCache.readback;
            const bool cachedColorScopeMatches =
                !previewIdentity.empty() &&
                m_RawWorkspaceColorWarpInputGraphScopeCache.sourceKey ==
                    previewIdentity &&
                cachedColorScope.valid &&
                cachedColorScope.stage ==
                    RawDevelopmentGraphScopeStage::ColorWarpInput;
            const RawDevelopmentGraphScopeReadback& colorScope =
                cachedColorScopeMatches
                ? cachedColorScope
                : m_RawWorkspaceGraphScopeReadback;
            const std::size_t colorScopePixelCount = colorScope.valid &&
                    colorScope.stage ==
                        RawDevelopmentGraphScopeStage::ColorWarpInput &&
                    colorScope.width > 0 && colorScope.height > 0
                ? static_cast<std::size_t>(colorScope.width) *
                    static_cast<std::size_t>(colorScope.height)
                : 0u;
            if (colorScopePixelCount > 0u &&
                colorScope.pixels.size() >= colorScopePixelCount * 3u) {
                const Stack::RawRecipe::RawColorWarpPin& selectedPin =
                    previewRecipe.colorWarp.pins[
                        static_cast<std::size_t>(colorWarpDisplayedPin)];
                std::vector<Stack::RawRecipe::RawColorWarpPin> diagnosticPins;
                const bool isolate =
                    m_RawWorkspaceLabUi.colorWarpDiagnosticIsolateSelected ||
                    colorWarpSelectionViewMode == 2;
                std::unordered_set<std::string> groupPinIds;
                if (!isolate) {
                    for (const auto& group : previewRecipe.colorWarp.linkGroups) {
                        if (std::find(group.pinIds.begin(), group.pinIds.end(), selectedPin.id) !=
                            group.pinIds.end()) {
                            groupPinIds.insert(group.pinIds.begin(), group.pinIds.end());
                            break;
                        }
                    }
                }
                for (const auto& pin : previewRecipe.colorWarp.pins) {
                    if (pin.id == selectedPin.id || groupPinIds.find(pin.id) != groupPinIds.end()) {
                        diagnosticPins.push_back(pin);
                    }
                }
                std::size_t overlayFingerprint = std::hash<std::string> {}(
                    Stack::RawRecipe::SerializeColorWarpRecipe(previewRecipe.colorWarp).dump());
                overlayFingerprint ^= std::hash<std::string> {}(selectedPin.id) +
                    0x9e3779b97f4a7c15ull + (overlayFingerprint << 6u) +
                    (overlayFingerprint >> 2u);
                overlayFingerprint ^= m_RawWorkspaceColorWarpInputGraphScopeCache.inputFingerprint +
                    0x9e3779b97f4a7c15ull + (overlayFingerprint << 6u) +
                    (overlayFingerprint >> 2u);
                overlayFingerprint ^= isolate ? 0xa531u : 0x5ca1u;
                const bool rebuildOverlay =
                    m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture == 0 ||
                    m_RawWorkspaceLabUi.colorWarpAffectedOverlaySourceKey !=
                        previewIdentity ||
                    m_RawWorkspaceLabUi.colorWarpAffectedOverlayFingerprint !=
                        overlayFingerprint ||
                    m_RawWorkspaceLabUi.colorWarpAffectedOverlayWidth !=
                        colorScope.width ||
                    m_RawWorkspaceLabUi.colorWarpAffectedOverlayHeight !=
                        colorScope.height;
                if (rebuildOverlay &&
                    m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint !=
                        overlayFingerprint) {
                    auto state = std::make_shared<
                        Stack::EditorModuleTypes::RawWorkspaceColorWarpDiagnosticState>();
                    state->generation = ++m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration;
                    state->fingerprint = overlayFingerprint;
                    state->width = colorScope.width;
                    state->height = colorScope.height;
                    m_RawWorkspaceLabUi.colorWarpDiagnosticState = state;
                    m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint =
                        overlayFingerprint;
                    const auto scopePixels = colorScope.pixels;
                    const int scopeWidth = colorScope.width;
                    const int scopeHeight = colorScope.height;
                    const int sourceWidth = colorScope.sourceWidth;
                    const int sourceHeight = colorScope.sourceHeight;
                    const auto regions = previewRecipe.colorWarp.regions;
                    const auto workingSpace = previewRecipe.technical.workingSpace;
                    const bool submitted = ProjectTasks().SubmitHighPriority("Analyzing colors",
                        [state, scopePixels, scopeWidth, scopeHeight,
                         sourceWidth, sourceHeight, diagnosticPins, regions,
                         workingSpace]() mutable {
                            Stack::RawRecipe::RawColorWarpMaskGuide guide;
                            guide.width = scopeWidth;
                            guide.height = scopeHeight;
                            guide.sourceWidth = sourceWidth;
                            guide.sourceHeight = sourceHeight;
                            const std::size_t count =
                                static_cast<std::size_t>(scopeWidth) * scopeHeight;
                            guide.pixels.resize(count);
                            for (std::size_t index = 0; index < count; ++index) {
                                const std::size_t offset = index * 3u;
                                guide.pixels[index] =
                                    Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                                        { scopePixels[offset + 0u],
                                          scopePixels[offset + 1u],
                                          scopePixels[offset + 2u] },
                                        workingSpace);
                            }
                            std::vector<unsigned char> pixels(count * 4u, 0u);
                            bool spatialAvailable = false;
                            for (const auto& pin : diagnosticPins) {
                                const auto region = std::find_if(
                                    regions.begin(), regions.end(),
                                    [&](const auto& candidate) {
                                        return candidate.id == pin.regionId;
                                    });
                                const auto* regionPtr = region == regions.end()
                                    ? nullptr
                                    : &*region;
                                auto fields = Stack::RawRecipe::BuildRawColorWarpMaskFields(
                                    guide, pin, regionPtr);
                                spatialAvailable = spatialAvailable || fields.spatialAvailable;
                                const float spatialStrength = regionPtr
                                    ? regionPtr->spatialSupport
                                    : 0.0f;
                                for (std::size_t index = 0; index < count; ++index) {
                                    const auto& coordinate = guide.pixels[index];
                                    const float direct =
                                        Stack::RawRecipe::EvaluateColorWarpPinShapeWeight(
                                            pin, coordinate.a, coordinate.b) *
                                        Stack::RawRecipe::EvaluateColorWarpPinLightnessWeight(
                                            pin, coordinate.sceneEv);
                                    const float gatedDirect = direct *
                                        (fields.gate[index] / 255.0f);
                                    const float spatial = fields.support[index] /
                                        255.0f * spatialStrength;
                                    const float boundary = fields.boundary[index] / 255.0f;
                                    const std::size_t destination = index * 4u;
                                    if (std::max(gatedDirect, spatial) * boundary <= 0.01f) {
                                        pixels[destination + 3u] = std::max<unsigned char>(
                                            pixels[destination + 3u], 205u);
                                    } else if (fields.edge[index] != 0u ||
                                               boundary < 0.98f) {
                                        pixels[destination + 0u] = 174u;
                                        pixels[destination + 1u] = 104u;
                                        pixels[destination + 2u] = 242u;
                                        pixels[destination + 3u] = 218u;
                                    } else if (spatial > gatedDirect) {
                                        pixels[destination + 0u] = 238u;
                                        pixels[destination + 1u] = 160u;
                                        pixels[destination + 2u] = 62u;
                                        pixels[destination + 3u] = 218u;
                                    } else {
                                        pixels[destination + 0u] = 48u;
                                        pixels[destination + 1u] = 210u;
                                        pixels[destination + 2u] = 224u;
                                        pixels[destination + 3u] = 218u;
                                    }
                                }
                            }
                            std::lock_guard<std::mutex> lock(state->mutex);
                            state->pixels = std::move(pixels);
                            state->spatialAvailable = spatialAvailable;
                            state->ready = true;
                        });
                    if (!submitted) {
                        m_RawWorkspaceLabUi.colorWarpDiagnosticState.reset();
                        m_RawWorkspaceLabUi.colorWarpDiagnosticRequestedFingerprint = 0;
                    }
                }
                if (m_RawWorkspaceLabUi.colorWarpDiagnosticState) {
                    const auto completedState =
                        m_RawWorkspaceLabUi.colorWarpDiagnosticState;
                    std::lock_guard<std::mutex> lock(completedState->mutex);
                    auto& state = *completedState;
                    if (state.ready && state.generation ==
                            m_RawWorkspaceLabUi.colorWarpDiagnosticGeneration &&
                        state.fingerprint == overlayFingerprint) {
                        const unsigned int replacement = GLHelpers::CreateTextureFromPixels(
                            state.pixels.data(), state.width, state.height, 4);
                        if (replacement != 0u) {
                            if (m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture != 0u) {
                                glDeleteTextures(1, &m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture);
                            }
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture = replacement;
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayWidth = state.width;
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayHeight = state.height;
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayFingerprint = state.fingerprint;
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlaySourceKey = previewIdentity;
                        }
                        m_RawWorkspaceLabUi.colorWarpDiagnosticState.reset();
                    }
                }
                if (m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture != 0) {
                    const float uInset =
                        m_RawWorkspaceLabUi.colorWarpAffectedOverlayWidth > 1
                        ? 0.5f / static_cast<float>(
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayWidth)
                        : 0.0f;
                    const float vInset =
                        m_RawWorkspaceLabUi.colorWarpAffectedOverlayHeight > 1
                        ? 0.5f / static_cast<float>(
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayHeight)
                        : 0.0f;
                    drawList->AddImage(
                        (ImTextureID)(intptr_t)
                            m_RawWorkspaceLabUi.colorWarpAffectedOverlayTexture,
                        imageRect.Min,
                        imageRect.Max,
                        // Graph-scope readbacks are normalized to top-left row
                        // order before this CPU texture is uploaded. Unlike a
                        // renderer-owned OpenGL output texture, it must not be
                        // vertically flipped again during ImGui presentation.
                        ImVec2(uInset, vInset),
                        ImVec2(1.0f - uInset, 1.0f - vInset));
                    const ImVec2 legendMin(imageRect.Min.x + 10.0f, imageRect.Min.y + 10.0f);
                    drawList->AddRectFilled(
                        legendMin,
                        ImVec2(legendMin.x + 330.0f, legendMin.y + 27.0f),
                        IM_COL32(12, 14, 18, 205), 5.0f);
                    drawList->AddText(
                        ImVec2(legendMin.x + 8.0f, legendMin.y + 6.0f),
                        IM_COL32(245, 245, 245, 235),
                        "Black untouched   Cyan direct   Amber spatial   Violet edge fade");
                }
            }
        }
        if (drawImageFrame && m_PermanentGalleryWorkspace) {
            drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_Border));
        }
        const bool nativeRefinementWorking =
            m_RawWorkspaceFullResolutionPreviewRequested &&
            m_RawWorkspaceFullResolutionPreviewRequestGeneration != 0;
        if (nativeRefinementWorking) {
            constexpr const char* kWorkingLabel = "Working";
            const ImVec2 labelSize = ImGui::CalcTextSize(kWorkingLabel);
            const ImVec2 labelPosition(
                imageRect.GetCenter().x - labelSize.x * 0.5f,
                imageRect.Max.y - labelSize.y - 12.0f);
            const ImVec2 padding(8.0f, 4.0f);
            drawList->AddRectFilled(
                ImVec2(
                    labelPosition.x - padding.x,
                    labelPosition.y - padding.y),
                ImVec2(
                    labelPosition.x + labelSize.x + padding.x,
                    labelPosition.y + labelSize.y + padding.y),
                IM_COL32(0, 0, 0, 150),
                5.0f);
            drawList->AddText(
                labelPosition,
                ImGui::GetColorU32(ImGuiCol_Text),
                kWorkingLabel);
        }
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Color &&
            currentRawPreview) {
            const std::string previewIdentity =
                GetActiveRawWorkspacePreviewIdentity();
            const RawDevelopmentGraphScopeReadback& cachedColorScope =
                m_RawWorkspaceColorWarpInputGraphScopeCache.readback;
            const bool cachedColorScopeMatches =
                !previewIdentity.empty() &&
                m_RawWorkspaceColorWarpInputGraphScopeCache.sourceKey ==
                    previewIdentity &&
                cachedColorScope.valid &&
                cachedColorScope.stage ==
                    RawDevelopmentGraphScopeStage::ColorWarpInput;
            const RawDevelopmentGraphScopeReadback& colorScope =
                cachedColorScopeMatches
                ? cachedColorScope
                : m_RawWorkspaceGraphScopeReadback;
            const std::size_t colorScopePixelCount = colorScope.valid &&
                    colorScope.stage ==
                        RawDevelopmentGraphScopeStage::ColorWarpInput &&
                    colorScope.width > 0 && colorScope.height > 0
                ? static_cast<std::size_t>(colorScope.width) *
                    static_cast<std::size_t>(colorScope.height)
                : 0u;
            if (colorScopePixelCount > 0u &&
                colorScope.pixels.size() >= colorScopePixelCount * 3u) {
                auto& colorUi = m_RawWorkspaceLabUi;
                const auto displayToSource = [&](float displayU, float displayV) {
                    const auto& crop = previewRecipe.cropRotation;
                    const bool cropEnabled = crop.cropEnabled;
                    return ImVec2(
                        cropEnabled ? crop.cropX + displayU * crop.cropWidth : displayU,
                        cropEnabled ? crop.cropY + displayV * crop.cropHeight : displayV);
                };
                const auto sourceToDisplay = [&](float sourceU, float sourceV) {
                    const auto& crop = previewRecipe.cropRotation;
                    const bool cropEnabled = crop.cropEnabled;
                    return ImVec2(
                        cropEnabled
                            ? (sourceU - crop.cropX) / std::max(0.000001f, crop.cropWidth)
                            : sourceU,
                        cropEnabled
                            ? (sourceV - crop.cropY) / std::max(0.000001f, crop.cropHeight)
                            : sourceV);
                };
                const auto submitAreaAnalysis = [&]() {
                    std::vector<Stack::RawRecipe::RawColorWarpAnalysisSample> samples;
                    samples.reserve(colorScopePixelCount);
                    for (int y = 0; y < colorScope.height; ++y) {
                        for (int x = 0; x < colorScope.width; ++x) {
                            const std::size_t index =
                                static_cast<std::size_t>(y) * colorScope.width + x;
                            const std::size_t offset = index * 3u;
                            Stack::RawRecipe::RawColorWarpAnalysisSample sample;
                            sample.sourceU = (static_cast<float>(x) + 0.5f) /
                                colorScope.width;
                            sample.sourceV = (static_cast<float>(y) + 0.5f) /
                                colorScope.height;
                            sample.color = Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                                { colorScope.pixels[offset + 0u],
                                  colorScope.pixels[offset + 1u],
                                  colorScope.pixels[offset + 2u] },
                                previewRecipe.technical.workingSpace);
                            sample.valid = std::isfinite(sample.color.a) &&
                                std::isfinite(sample.color.b) &&
                                std::isfinite(sample.color.sceneEv);
                            samples.push_back(sample);
                        }
                    }
                    const std::uint64_t generation =
                        ++colorUi.colorWarpAreaAnalysisGeneration;
                    const auto circle = colorUi.colorWarpPendingCircle;
                    std::size_t reclaimedSlots = 0u;
                    if (!colorUi.colorWarpPendingReplaceRegionId.empty()) {
                        reclaimedSlots = static_cast<std::size_t>(std::count_if(
                            previewRecipe.colorWarp.pins.begin(),
                            previewRecipe.colorWarp.pins.end(),
                            [&](const auto& pin) {
                                return pin.regionId ==
                                    colorUi.colorWarpPendingReplaceRegionId;
                            }));
                    }
                    const std::size_t occupiedSlots =
                        previewRecipe.colorWarp.pins.size() - reclaimedSlots;
                    const std::size_t availableSlots =
                        Stack::RawRecipe::kMaxRawColorWarpPins > occupiedSlots
                            ? Stack::RawRecipe::kMaxRawColorWarpPins - occupiedSlots
                            : 0u;
                    auto state = std::make_shared<
                        Stack::EditorModuleTypes::RawWorkspaceColorWarpAreaAnalysisState>();
                    state->generation = generation;
                    colorUi.colorWarpAreaAnalysisState = state;
                    colorUi.colorWarpPendingCircleRefining = true;
                    colorUi.colorWarpAreaStatus = "Refining";
                    const bool submitted = ProjectTasks().SubmitHighPriority("Analyzing colors",
                        [state, generation, samples = std::move(samples), circle,
                         availableSlots]() mutable {
                            auto analysis = Stack::RawRecipe::AnalyzeRawColorWarpArea(
                                samples,
                                circle,
                                availableSlots);
                            std::lock_guard<std::mutex> lock(state->mutex);
                            if (state->generation == generation) {
                                state->result = std::move(analysis);
                                state->ready = true;
                            }
                        });
                    if (!submitted) {
                        colorUi.colorWarpAreaAnalysisState.reset();
                        colorUi.colorWarpPendingCircleRefining = false;
                        colorUi.colorWarpAreaStatus = "Analysis unavailable";
                    }
                };

                if (colorUi.colorWarpAreaAnalysisState) {
                    const auto completedState = colorUi.colorWarpAreaAnalysisState;
                    std::lock_guard<std::mutex> lock(completedState->mutex);
                    if (completedState->ready &&
                        completedState->generation ==
                            colorUi.colorWarpAreaAnalysisGeneration) {
                        colorUi.colorWarpAreaAnalysisResult =
                            completedState->result;
                        colorUi.colorWarpPendingCircleRefining = false;
                        if (colorUi.colorWarpAreaAnalysisResult.proposals.empty()) {
                            colorUi.colorWarpAreaStatus =
                                colorUi.colorWarpAreaAnalysisResult.capacityLimited
                                ? "No pin slots available"
                                : "No usable pixels";
                        } else if (colorUi.colorWarpAreaAnalysisResult.capacityLimited) {
                            colorUi.colorWarpAreaStatus = "Result limited by remaining pin slots";
                        } else {
                            colorUi.colorWarpAreaStatus =
                                std::to_string(colorUi.colorWarpAreaAnalysisResult.proposals.size()) +
                                (colorUi.colorWarpAreaAnalysisResult.proposals.size() == 1u
                                    ? " color ready"
                                    : " colors ready");
                        }
                        colorUi.colorWarpAreaAnalysisState.reset();
                    }
                }

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const bool previewHovered = imageRect.Contains(mouse) &&
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                if (previewHovered) {
                    const float displayU = std::clamp(
                        (mouse.x - imageRect.Min.x) / std::max(1.0f, imageRect.GetWidth()),
                        0.0f, 0.999999f);
                    const float displayV = std::clamp(
                        (mouse.y - imageRect.Min.y) / std::max(1.0f, imageRect.GetHeight()),
                        0.0f, 0.999999f);
                    const ImVec2 source = displayToSource(displayU, displayV);
                    const int sampleX = std::clamp(
                        static_cast<int>(source.x * colorScope.width), 0, colorScope.width - 1);
                    const int sampleY = std::clamp(
                        static_cast<int>(source.y * colorScope.height), 0, colorScope.height - 1);
                    const std::size_t offset =
                        (static_cast<std::size_t>(sampleY) * colorScope.width + sampleX) * 3u;
                    const auto coordinate = Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                        { colorScope.pixels[offset + 0u],
                          colorScope.pixels[offset + 1u],
                          colorScope.pixels[offset + 2u] },
                        previewRecipe.technical.workingSpace);
                    colorUi.colorWarpPhotoHoverValid = std::isfinite(coordinate.a) &&
                        std::isfinite(coordinate.b) && std::isfinite(coordinate.sceneEv);
                    if (colorUi.colorWarpPhotoHoverValid) {
                        colorUi.colorWarpPhotoHoverA = coordinate.a;
                        colorUi.colorWarpPhotoHoverB = coordinate.b;
                        colorUi.colorWarpPhotoHoverSceneEv = coordinate.sceneEv;
                    }

                    const ImVec2 pendingDisplay = sourceToDisplay(
                        colorUi.colorWarpPendingCircle.centerU,
                        colorUi.colorWarpPendingCircle.centerV);
                    const ImVec2 pendingCenter(
                        imageRect.Min.x + pendingDisplay.x * imageRect.GetWidth(),
                        imageRect.Min.y + pendingDisplay.y * imageRect.GetHeight());
                    const float radiusX = colorUi.colorWarpPendingCircle.radiusU *
                        imageRect.GetWidth() /
                        std::max(0.000001f,
                            previewRecipe.cropRotation.cropEnabled
                                ? previewRecipe.cropRotation.cropWidth
                                : 1.0f);
                    const float radiusY = colorUi.colorWarpPendingCircle.radiusV *
                        imageRect.GetHeight() /
                        std::max(0.000001f,
                            previewRecipe.cropRotation.cropEnabled
                                ? previewRecipe.cropRotation.cropHeight
                                : 1.0f);
                    const float pendingRadius = std::max(4.0f, (radiusX + radiusY) * 0.5f);
                    const float pendingDistance = std::hypot(
                        mouse.x - pendingCenter.x, mouse.y - pendingCenter.y);

                    if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
                        colorUi.colorWarpPendingCircleActive) {
                        colorUi.colorWarpPendingCircleActive = false;
                        colorUi.colorWarpPendingCircleDrawing = false;
                        colorUi.colorWarpPendingCircleCommitRequested = false;
                        colorUi.colorWarpPendingCircleAppend = false;
                        colorUi.colorWarpPendingReplaceRegionId.clear();
                        colorUi.colorWarpPendingReplaceCircleId.clear();
                        ++colorUi.colorWarpAreaAnalysisGeneration;
                        colorUi.colorWarpAreaAnalysisState.reset();
                    }
                    if (!colorUi.colorWarpPendingCircleActive &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        bool recalledCircleHit = false;
                        const bool inspectingRecalledRegion =
                            colorUi.colorWarpInspectionActive &&
                            !colorUi.colorWarpInspectedRegionId.empty();
                        if (inspectingRecalledRegion) {
                            const auto inspected = std::find_if(
                                previewRecipe.colorWarp.regions.begin(),
                                previewRecipe.colorWarp.regions.end(),
                                [&](const auto& region) {
                                    return region.id == colorUi.colorWarpInspectedRegionId;
                                });
                            if (inspected != previewRecipe.colorWarp.regions.end()) {
                                const auto& crop = previewRecipe.cropRotation;
                                const float cropWidth = crop.cropEnabled ? crop.cropWidth : 1.0f;
                                const float cropHeight = crop.cropEnabled ? crop.cropHeight : 1.0f;
                                for (const auto& circle : inspected->circles) {
                                    const ImVec2 recalledDisplay = sourceToDisplay(
                                        circle.centerU, circle.centerV);
                                    const ImVec2 recalledCenter(
                                        imageRect.Min.x + recalledDisplay.x * imageRect.GetWidth(),
                                        imageRect.Min.y + recalledDisplay.y * imageRect.GetHeight());
                                    const float recalledRadius = 0.5f * (
                                        circle.radiusU * imageRect.GetWidth() /
                                            std::max(0.000001f, cropWidth) +
                                        circle.radiusV * imageRect.GetHeight() /
                                            std::max(0.000001f, cropHeight));
                                    if (std::hypot(mouse.x - recalledCenter.x,
                                                   mouse.y - recalledCenter.y) <=
                                        recalledRadius + 6.0f) {
                                        colorUi.colorWarpPendingCircle = circle;
                                        colorUi.colorWarpPendingCircleActive = true;
                                        colorUi.colorWarpPendingCircleDrawing = false;
                                        colorUi.colorWarpPendingCircleEditKind = 1;
                                        colorUi.colorWarpPendingCircleAppend = false;
                                        colorUi.colorWarpPendingReplaceRegionId = inspected->id;
                                        colorUi.colorWarpPendingReplaceCircleId = circle.id;
                                        colorUi.colorWarpAreaStatus = "Edit sampled area";
                                        submitAreaAnalysis();
                                        recalledCircleHit = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (recalledCircleHit) {
                            colorUi.colorWarpPendingCirclePressPosition = mouse;
                            colorUi.colorWarpPendingCirclePressCenterU =
                                colorUi.colorWarpPendingCircle.centerU;
                            colorUi.colorWarpPendingCirclePressCenterV =
                                colorUi.colorWarpPendingCircle.centerV;
                            colorUi.colorWarpPendingCirclePressRadiusU =
                                colorUi.colorWarpPendingCircle.radiusU;
                            colorUi.colorWarpPendingCirclePressRadiusV =
                                colorUi.colorWarpPendingCircle.radiusV;
                        } else if (inspectingRecalledRegion &&
                                   !colorUi.colorWarpDiagnosticLocked) {
                            colorUi.colorWarpInspectionActive = false;
                            colorUi.colorWarpInspectedRegionId.clear();
                        } else {
                        colorUi.colorWarpPendingCircleActive = true;
                        colorUi.colorWarpPendingCircleDrawing = true;
                        colorUi.colorWarpPendingCircleEditKind = 2;
                        colorUi.colorWarpPendingCirclePressPosition = mouse;
                        colorUi.colorWarpPendingCircle = {};
                        colorUi.colorWarpPendingReplaceRegionId.clear();
                        colorUi.colorWarpPendingReplaceCircleId.clear();
                        colorUi.colorWarpPendingCircleAppend = false;
                        if (ImGui::GetIO().KeyShift &&
                            colorUi.selectedColorWarpPin >= 0 &&
                            colorUi.selectedColorWarpPin < static_cast<int>(
                                previewRecipe.colorWarp.pins.size())) {
                            const auto& selectedPin = previewRecipe.colorWarp.pins[
                                static_cast<std::size_t>(colorUi.selectedColorWarpPin)];
                            if (!selectedPin.regionId.empty()) {
                                colorUi.colorWarpPendingCircleAppend = true;
                                colorUi.colorWarpPendingReplaceRegionId =
                                    selectedPin.regionId;
                                colorUi.colorWarpPendingCircle.polarity =
                                    Stack::RawRecipe::RawColorWarpSamplePolarity::Include;
                            }
                        }
                        colorUi.colorWarpPendingCircle.id = "sample-1";
                        colorUi.colorWarpPendingCircle.centerU = source.x;
                        colorUi.colorWarpPendingCircle.centerV = source.y;
                        const auto& crop = previewRecipe.cropRotation;
                        const float cropWidth = crop.cropEnabled ? crop.cropWidth : 1.0f;
                        const float cropHeight = crop.cropEnabled ? crop.cropHeight : 1.0f;
                        colorUi.colorWarpPendingCircle.radiusU =
                            12.0f / std::max(1.0f, imageRect.GetWidth()) * cropWidth;
                        colorUi.colorWarpPendingCircle.radiusV =
                            12.0f / std::max(1.0f, imageRect.GetHeight()) * cropHeight;
                        colorUi.colorWarpAreaStatus = "Drag to size";
                        }
                    } else if (colorUi.colorWarpPendingCircleActive &&
                               !colorUi.colorWarpPendingCircleDrawing &&
                               ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (std::abs(pendingDistance - pendingRadius) <= 7.0f) {
                            colorUi.colorWarpPendingCircleEditKind = 2;
                        } else if (pendingDistance <= pendingRadius) {
                            colorUi.colorWarpPendingCircleEditKind = 1;
                        } else {
                            colorUi.colorWarpPendingCircleActive = false;
                            colorUi.colorWarpPendingCircleAppend = false;
                            colorUi.colorWarpPendingReplaceRegionId.clear();
                            colorUi.colorWarpPendingReplaceCircleId.clear();
                            ++colorUi.colorWarpAreaAnalysisGeneration;
                            colorUi.colorWarpAreaAnalysisState.reset();
                        }
                        colorUi.colorWarpPendingCirclePressPosition = mouse;
                        colorUi.colorWarpPendingCirclePressCenterU =
                            colorUi.colorWarpPendingCircle.centerU;
                        colorUi.colorWarpPendingCirclePressCenterV =
                            colorUi.colorWarpPendingCircle.centerV;
                        colorUi.colorWarpPendingCirclePressRadiusU =
                            colorUi.colorWarpPendingCircle.radiusU;
                        colorUi.colorWarpPendingCirclePressRadiusV =
                            colorUi.colorWarpPendingCircle.radiusV;
                    }
                    if (colorUi.colorWarpPendingCircleActive &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        const float dragDistance = std::hypot(
                            mouse.x - colorUi.colorWarpPendingCirclePressPosition.x,
                            mouse.y - colorUi.colorWarpPendingCirclePressPosition.y);
                        const auto& crop = previewRecipe.cropRotation;
                        const float cropWidth = crop.cropEnabled ? crop.cropWidth : 1.0f;
                        const float cropHeight = crop.cropEnabled ? crop.cropHeight : 1.0f;
                        if (colorUi.colorWarpPendingCircleDrawing ||
                            (colorUi.colorWarpPendingCircleEditKind == 2 && dragDistance > 2.0f)) {
                            const float radiusPixels = std::max(
                                4.0f,
                                colorUi.colorWarpPendingCircleDrawing
                                    ? dragDistance
                                    : std::hypot(
                                        mouse.x - pendingCenter.x,
                                        mouse.y - pendingCenter.y));
                            colorUi.colorWarpPendingCircle.radiusU = radiusPixels /
                                std::max(1.0f, imageRect.GetWidth()) * cropWidth;
                            colorUi.colorWarpPendingCircle.radiusV = radiusPixels /
                                std::max(1.0f, imageRect.GetHeight()) * cropHeight;
                        } else if (colorUi.colorWarpPendingCircleEditKind == 1 && dragDistance > 2.0f) {
                            colorUi.colorWarpPendingCircle.centerU = std::clamp(
                                colorUi.colorWarpPendingCirclePressCenterU +
                                    (mouse.x - colorUi.colorWarpPendingCirclePressPosition.x) /
                                        std::max(1.0f, imageRect.GetWidth()) * cropWidth,
                                0.0f, 1.0f);
                            colorUi.colorWarpPendingCircle.centerV = std::clamp(
                                colorUi.colorWarpPendingCirclePressCenterV +
                                    (mouse.y - colorUi.colorWarpPendingCirclePressPosition.y) /
                                        std::max(1.0f, imageRect.GetHeight()) * cropHeight,
                                0.0f, 1.0f);
                        }
                    }
                    if (colorUi.colorWarpPendingCircleActive &&
                        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                        const float dragDistance = std::hypot(
                            mouse.x - colorUi.colorWarpPendingCirclePressPosition.x,
                            mouse.y - colorUi.colorWarpPendingCirclePressPosition.y);
                        const bool wasInitial = colorUi.colorWarpPendingCircleDrawing;
                        colorUi.colorWarpPendingCircleDrawing = false;
                        if (!wasInitial && colorUi.colorWarpPendingCircleEditKind == 1 &&
                            dragDistance <= 2.0f && !colorUi.colorWarpPendingCircleRefining &&
                            (colorUi.colorWarpPendingCircleAppend ||
                             !colorUi.colorWarpAreaAnalysisResult.proposals.empty())) {
                            colorUi.colorWarpPendingCircleCommitRequested = true;
                        } else if (!colorUi.colorWarpPendingCircleAppend) {
                            submitAreaAnalysis();
                        } else {
                            colorUi.colorWarpPendingCircleRefining = false;
                            colorUi.colorWarpAreaStatus =
                                colorUi.colorWarpPendingCircle.polarity ==
                                    Stack::RawRecipe::RawColorWarpSamplePolarity::Exclude
                                ? "Exclusion ready"
                                : "Include seed ready";
                        }
                        colorUi.colorWarpPendingCircleEditKind = 0;
                    }
                    if (colorUi.colorWarpPendingCircleActive &&
                        pendingDistance <= pendingRadius + 90.0f &&
                        std::abs(ImGui::GetIO().MouseWheel) > 0.0001f &&
                        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        colorUi.colorWarpPendingCircle.interpretation =
                            CycleColorWarpInterpretation(
                                colorUi.colorWarpPendingCircle.interpretation,
                                ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1);
                        submitAreaAnalysis();
                    }
                }

                if (colorUi.colorWarpPendingCircleActive) {
                    const ImVec2 display = sourceToDisplay(
                        colorUi.colorWarpPendingCircle.centerU,
                        colorUi.colorWarpPendingCircle.centerV);
                    const ImVec2 center(
                        imageRect.Min.x + display.x * imageRect.GetWidth(),
                        imageRect.Min.y + display.y * imageRect.GetHeight());
                    const auto& crop = previewRecipe.cropRotation;
                    const float cropWidth = crop.cropEnabled ? crop.cropWidth : 1.0f;
                    const float cropHeight = crop.cropEnabled ? crop.cropHeight : 1.0f;
                    const float radius = 0.5f * (
                        colorUi.colorWarpPendingCircle.radiusU * imageRect.GetWidth() /
                            std::max(0.000001f, cropWidth) +
                        colorUi.colorWarpPendingCircle.radiusV * imageRect.GetHeight() /
                            std::max(0.000001f, cropHeight));
                    drawList->AddCircle(center, std::max(4.0f, radius),
                        IM_COL32(245, 245, 245, 235), 0, 2.0f);
                    const float reachPixels = 24.0f *
                        imageRect.GetWidth() / std::max(1, colorScope.sourceWidth);
                    drawList->AddCircle(center, std::max(4.0f, radius + reachPixels),
                        IM_COL32(236, 164, 72, 210), 32, 1.5f);
                    const char* modeLabel = ColorWarpInterpretationLabel(
                        colorUi.colorWarpPendingCircle.interpretation);
                    const std::string polarityLabel =
                        colorUi.colorWarpPendingCircle.polarity ==
                            Stack::RawRecipe::RawColorWarpSamplePolarity::Exclude
                        ? "Exclude · "
                        : "";
                    const std::string tag = polarityLabel + std::string(modeLabel) +
                        (colorUi.colorWarpPendingCircleRefining
                            ? "  refining..."
                            : "  " + colorUi.colorWarpAreaStatus);
                    const ImVec2 tagPosition(center.x + radius + 8.0f, center.y - 10.0f);
                    const ImVec2 tagSize = ImGui::CalcTextSize(tag.c_str());
                    drawList->AddRectFilled(
                        ImVec2(tagPosition.x - 6.0f, tagPosition.y - 4.0f),
                        ImVec2(tagPosition.x + tagSize.x + 6.0f,
                               tagPosition.y + tagSize.y + 4.0f),
                        IM_COL32(18, 20, 24, 220), 5.0f);
                    drawList->AddText(tagPosition, IM_COL32(245, 245, 245, 245), tag.c_str());
                }
                if (colorUi.colorWarpInspectionActive &&
                    !colorUi.colorWarpInspectedRegionId.empty() &&
                    !colorUi.colorWarpPendingCircleActive) {
                    const auto inspected = std::find_if(
                        previewRecipe.colorWarp.regions.begin(),
                        previewRecipe.colorWarp.regions.end(),
                        [&](const auto& region) {
                            return region.id == colorUi.colorWarpInspectedRegionId;
                        });
                    if (inspected != previewRecipe.colorWarp.regions.end()) {
                        const auto& crop = previewRecipe.cropRotation;
                        const float cropWidth = crop.cropEnabled ? crop.cropWidth : 1.0f;
                        const float cropHeight = crop.cropEnabled ? crop.cropHeight : 1.0f;
                        const float inspectionWheel = ImGui::GetIO().MouseWheel;
                        if (previewHovered &&
                            std::abs(inspectionWheel) > 0.0001f) {
                            if (ImGui::GetIO().KeyCtrl) {
                                colorUi.colorWarpRegionReachWheelDelta +=
                                    inspectionWheel * 4.0f;
                            } else if (ImGui::GetIO().KeyShift) {
                                colorUi.colorWarpRegionFeatherWheelDelta +=
                                    inspectionWheel * 4.0f;
                            }
                        }
                        drawList->PushClipRect(imageRect.Min, imageRect.Max, true);
                        for (const auto& circle : inspected->circles) {
                            const ImVec2 display = sourceToDisplay(circle.centerU, circle.centerV);
                            const ImVec2 center(
                                imageRect.Min.x + display.x * imageRect.GetWidth(),
                                imageRect.Min.y + display.y * imageRect.GetHeight());
                            const float radius = 0.5f * (
                                circle.radiusU * imageRect.GetWidth() /
                                    std::max(0.000001f, cropWidth) +
                                circle.radiusV * imageRect.GetHeight() /
                                    std::max(0.000001f, cropHeight));
                            const float sourceToDisplayScale = 0.5f * (
                                imageRect.GetWidth() / std::max(1, colorScope.sourceWidth) +
                                imageRect.GetHeight() / std::max(1, colorScope.sourceHeight));
                            drawList->AddCircle(center, std::max(4.0f, radius),
                                IM_COL32(245, 245, 245, 225), 0, 2.0f);
                            DrawPreviewDashedCircle(
                                drawList, center,
                                std::max(4.0f, radius +
                                    inspected->reachPixels * sourceToDisplayScale),
                                IM_COL32(236, 164, 72, 215));
                            if (inspected->featherPixels > 0.0f) {
                                drawList->AddCircle(
                                    center,
                                    std::max(4.0f, radius +
                                        inspected->featherPixels * sourceToDisplayScale),
                                    IM_COL32(174, 104, 242, 220), 48, 1.5f);
                            }
                            const std::string tag = std::string(
                                ColorWarpInterpretationLabel(circle.interpretation)) +
                                " · " + Stack::RawRecipe::RawColorWarpSpatialModeStableString(
                                    inspected->spatialMode) +
                                " · Ctrl-wheel reach · Shift-wheel feather";
                            drawList->AddText(
                                ImVec2(center.x + radius + 7.0f, center.y - 9.0f),
                                IM_COL32(245, 245, 245, 235), tag.c_str());
                        }
                        drawList->PopClipRect();
                    }
                }
            }
        }
        if (selectedSource && !preview.edit.multiFrameResult) HandleRawWorkspaceLocalRangeTargetInteraction(
            *selectedSource,
            imageRect.Min,
            imageRect.Max,
            selectedProjectActive,
            currentRawPreview);
    };

    // Fence only photo draws, including overview, tiles, update fades and
    // retained detail. Overlays added by finishImage stay outside this range.
    const auto beginPhoto = [&]() {
        drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        return drawList->CmdBuffer.Size - 1;
    };
    const auto endPhoto = [&](const ImRect& rect, int first, bool drawn) {
        const int end = drawList->CmdBuffer.Size;
        drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        if (!drawn) return;
        finishImage(rect);
        if (m_RawImageBackdrop.frame == ImGui::GetFrameCount() && !m_RawImageBackdrop.sharpDrawList &&
            ImGui::GetWindowViewport() == ImGui::GetMainViewport()) {
            m_RawImageBackdrop.sharpDrawList = drawList;
            m_RawImageBackdrop.sharpCommandBegin = first;
            m_RawImageBackdrop.sharpCommandEnd = end;
        }
    };
    bool drewPreview = false;
    if (currentRawPreview &&
        m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled &&
        HasViewportOutputTiles()) {
        const auto& tiles = GetViewportOutputTiles();
        const ImRect imageRect = imageRectFor(
            static_cast<float>(tiles.fullWidth),
            static_cast<float>(tiles.fullHeight));
        publishImageRect(imageRect);
        const int firstPhotoCommand = beginPhoto();
        drewPreview = DrawLabTileSet(tiles, imageRect);
        endPhoto(imageRect, firstPhotoCommand, drewPreview);
    }
    if (!drewPreview &&
        currentRawPreview &&
        m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::SingleTexture &&
        IsViewportTextureSafeForDrawing(m_RawWorkspacePresentationTexture.texture) &&
        m_RawWorkspacePresentationTexture.width > 0 &&
        m_RawWorkspacePresentationTexture.height > 0) {
        const auto region = rawStagePreview ? m_RawViewportPresentedRegion : Raw::ViewportRegion{};
        const ImRect imageRect = imageRectFor(
            static_cast<float>(m_ViewportOutputExpectedNativeWidth > 0 ? m_ViewportOutputExpectedNativeWidth : region.Valid() ? region.fullWidth : m_RawWorkspacePresentationTexture.width),
            static_cast<float>(m_ViewportOutputExpectedNativeHeight > 0 ? m_ViewportOutputExpectedNativeHeight : region.Valid() ? region.fullHeight : m_RawWorkspacePresentationTexture.height));
        const int firstPhotoCommand = beginPhoto();
        if (region.Valid() && IsViewportTextureSafeForDrawing(m_RawViewportOverviewTexture.texture)) {
            drawList->AddImage((ImTextureID)(intptr_t)m_RawViewportOverviewTexture.texture,
                imageRect.Min, imageRect.Max, ImVec2(0, 1), ImVec2(1, 0));
        }
        const ImVec2 rasterMin = region.Valid() ? ImVec2(
            imageRect.Min.x + imageRect.GetWidth() * region.x / region.fullWidth,
            imageRect.Min.y + imageRect.GetHeight() * region.y / region.fullHeight) : imageRect.Min;
        const ImVec2 rasterMax = region.Valid() ? ImVec2(
            imageRect.Min.x + imageRect.GetWidth() * (region.x + region.width) / region.fullWidth,
            imageRect.Min.y + imageRect.GetHeight() * (region.y + region.height) / region.fullHeight) : imageRect.Max;
        publishImageRect(imageRect);
        // A standalone presentation texture is clamped at its edges, so it
        // must use the complete normalized domain. Insetting both UV edges
        // by half a texel compresses W source samples into W-1 texel spans;
        // at 1:1 that places screen samples between adjacent photo pixels and
        // makes a genuinely native raster look like a softened proxy.
        if (!DrawRawViewportTransition(drawList, rasterMin, rasterMax)) drawList->AddImage(
            (ImTextureID)(intptr_t)m_RawWorkspacePresentationTexture.texture,
            rasterMin,
            rasterMax,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        DrawRawViewportDetail(drawList, imageRect.Min, imageRect.Max);
        endPhoto(imageRect, firstPhotoCommand, true);
        drewPreview = true;
    }
    if (!drewPreview) {
        const ImRect placeholder = imageRectFor(4.0f, 3.0f, false);

        const ImU32 placeholderColor = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.12f));
        drawList->AddRectFilled(placeholder.Min, placeholder.Max, placeholderColor);
        const char* label = "RAW";
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(
                placeholder.GetCenter().x - labelSize.x * 0.5f,
                placeholder.GetCenter().y - labelSize.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            label);
    }
    ImGui::SetCursorScreenPos(layoutStart);
    ImGui::Dummy(ImVec2(
        std::max(120.0f, available.x),
        std::max(120.0f, available.y)));
}

void EditorModule::RenderRawWorkspaceLabPreview(
    const Stack::RawWorkspace::SourceRecord* selectedSource) {
    if(IsBracketingToolActive()&&RenderBracketingViewport())return;
    const ImVec2 previewAvailable = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
    ImGui::BeginChild(
        "RawLabPreviewCanvas",
        ImVec2(0.0f, std::max(100.0f, previewAvailable.y)),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // This canvas repositions its cursor for floating scopes. Keep one regular
    // item anchored to the canvas work rect so absolute positions cannot grow the child
    // beyond its parent when an adjacent panel is hidden.
    const ImVec2 canvasMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImVec2 imageMinimum;
    ImVec2 imageMaximum;
    RenderRawWorkspacePreviewCanvas(
        selectedSource,
        false,
        &imageMinimum,
        &imageMaximum);
    const float scopeAmount = std::clamp(
        m_RawWorkspaceLabAnimatedLowerShelfHeight /
            kRawLabFloatingScopesHeight,
        0.0f,
        1.0f);
    const bool hasImageBounds =
        imageMaximum.x > imageMinimum.x &&
        imageMaximum.y > imageMinimum.y;
    if (hasImageBounds &&
        (m_RawWorkspaceLabUi.lowerShelfOpen || scopeAmount > 0.002f)) {
        const float availableScopeWidth = std::max(1.0f, previewAvailable.x - 16.0f);
        const float scopeWidth = std::min(
            780.0f,
            std::min(imageMaximum.x - imageMinimum.x, availableScopeWidth));
        const float visibleY = imageMaximum.y + kRawLabFloatingScopesGap;
        const float hiddenY =
            ImGui::GetWindowPos().y + ImGui::GetWindowSize().y +
            kRawLabFloatingScopesGap;
        const float animatedScopeY = hiddenY + (visibleY - hiddenY) * scopeAmount;
        // Keep the child window cursor inside RawLabPreviewCanvas while the
        // shelf is hidden. SetCursorScreenPos() is allowed to position a
        // cursor freely, but Dear ImGui warns when that position extends the
        // parent window's content bounds. The parent clips the hidden shelf
        // anyway, so clamping to its bottom edge preserves the animation
        // without growing the parent or producing a layout warning.
        const ImVec2 parentWindowPos = ImGui::GetWindowPos();
        const ImVec2 parentWindowSize = ImGui::GetWindowSize();
        const float parentBottom = parentWindowPos.y + parentWindowSize.y;
        const float scopeY = std::clamp(
            animatedScopeY,
            parentWindowPos.y,
            parentBottom - kRawLabFloatingScopesHeight);
        ImGui::SetCursorScreenPos(ImVec2(
            (imageMinimum.x + imageMaximum.x - scopeWidth) * 0.5f,
            scopeY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::BeginChild(
            "RawLabFloatingScopes",
            ImVec2(scopeWidth, kRawLabFloatingScopesHeight),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceLabGradingSurface();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    ImGui::SetCursorScreenPos(canvasMinimum);
    ImGui::Dummy(ImVec2(
        std::max(1.0f, canvasSize.x),
        std::max(1.0f, canvasSize.y)));
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
