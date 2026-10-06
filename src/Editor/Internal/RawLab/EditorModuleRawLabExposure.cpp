#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;

namespace {

constexpr float kRawLabPi = 3.14159265358979323846f;
constexpr double kRawLabDemosaicIdleSettleSeconds = 0.165;
constexpr double kRawLabDemosaicDirectionDebounceSeconds = 0.055;
constexpr float kRawLabDemosaicPursuitSeconds = 0.075f;

struct RawLabDemosaicChoice {
    const char* label = "";
    bool implemented = false;
    Raw::DemosaicMethod backendMethod = Raw::DemosaicMethod::Bilinear;
};

constexpr std::array<RawLabDemosaicChoice, 12> kRawLabDemosaicChoices {{
    { "Nearest Neighbor", true, Raw::DemosaicMethod::NearestNeighbor },
    { "Bilinear", true, Raw::DemosaicMethod::Bilinear },
    { "Malvar-He-Cutler 5x5", true, Raw::DemosaicMethod::MalvarHeCutler },
    { "Hamilton-Adams", true, Raw::DemosaicMethod::HamiltonAdams },
    { "PPG", false, Raw::DemosaicMethod::Bilinear },
    { "VNG4", false, Raw::DemosaicMethod::Bilinear },
    { "AHD", false, Raw::DemosaicMethod::Bilinear },
    { "DCB", false, Raw::DemosaicMethod::Bilinear },
    { "LMMSE", false, Raw::DemosaicMethod::Bilinear },
    { "IGV", false, Raw::DemosaicMethod::Bilinear },
    { "RCD", false, Raw::DemosaicMethod::Bilinear },
    { "AMaZE", false, Raw::DemosaicMethod::Bilinear }
}};

ImVec4 RawLabColorWithAlpha(ImVec4 color, float alpha) {
    color.w *= std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

float RawLabEaseToward(float current, float target, float responseSeconds) {
    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    const float response = 1.0f - std::exp(
        -deltaTime / std::max(0.001f, responseSeconds));
    const float next = current + (target - current) * response;
    return std::abs(next - target) <= 0.0001f ? target : next;
}

float RawLabWrapAngle(float angle) {
    return std::remainder(angle, 2.0f * kRawLabPi);
}

int RawLabWrapIndex(int index, int count) {
    if (count <= 0) {
        return 0;
    }
    const int remainder = index % count;
    return remainder < 0 ? remainder + count : remainder;
}

int RawLabDemosaicVisualIndex(Raw::DemosaicMethod method) {
    switch (method) {
        case Raw::DemosaicMethod::NearestNeighbor: return 0;
        case Raw::DemosaicMethod::MalvarHeCutler: return 2;
        case Raw::DemosaicMethod::HamiltonAdams: return 3;
        case Raw::DemosaicMethod::Bilinear:
        default: return 1;
    }
}

void DrawRawLabGlowingText(
    ImDrawList* drawList,
    ImFont* font,
    float fontSize,
    const ImVec2& position,
    ImU32 textColor,
    const ImVec4& glowColor,
    float glow,
    float opacity,
    const char* text) {
    if (drawList == nullptr || font == nullptr || text == nullptr) {
        return;
    }
    const float resolvedGlow = std::clamp(glow * opacity, 0.0f, 1.0f);
    if (resolvedGlow > 0.001f) {
        constexpr std::array<ImVec2, 8> directions {{
            ImVec2(-1.0f, 0.0f), ImVec2(1.0f, 0.0f),
            ImVec2(0.0f, -1.0f), ImVec2(0.0f, 1.0f),
            ImVec2(-0.7071f, -0.7071f), ImVec2(0.7071f, -0.7071f),
            ImVec2(-0.7071f, 0.7071f), ImVec2(0.7071f, 0.7071f)
        }};
        for (int ring = 3; ring >= 1; --ring) {
            const float radius = static_cast<float>(ring) * 1.55f;
            const float ringAlpha =
                resolvedGlow * (ring == 1 ? 0.19f : ring == 2 ? 0.10f : 0.045f);
            const ImU32 ringColor = ImGui::GetColorU32(
                RawLabColorWithAlpha(glowColor, ringAlpha));
            for (const ImVec2& direction : directions) {
                drawList->AddText(
                    font,
                    fontSize,
                    ImVec2(
                        position.x + direction.x * radius,
                        position.y + direction.y * radius),
                    ringColor,
                    text);
            }
        }
    }
    drawList->AddText(font, fontSize, position, textColor, text);
}

ImRect RawLabRadialTextBounds(
    const ImVec2& inwardStart,
    const ImVec2& textSize,
    float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const std::array<ImVec2, 4> localCorners {{
        ImVec2(0.0f, -textSize.y * 0.5f),
        ImVec2(textSize.x, -textSize.y * 0.5f),
        ImVec2(textSize.x, textSize.y * 0.5f),
        ImVec2(0.0f, textSize.y * 0.5f)
    }};
    ImVec2 minimum(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    ImVec2 maximum(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());
    for (const ImVec2& corner : localCorners) {
        const ImVec2 rotated(
            inwardStart.x + corner.x * cosine - corner.y * sine,
            inwardStart.y + corner.x * sine + corner.y * cosine);
        minimum.x = std::min(minimum.x, rotated.x);
        minimum.y = std::min(minimum.y, rotated.y);
        maximum.x = std::max(maximum.x, rotated.x);
        maximum.y = std::max(maximum.y, rotated.y);
    }
    return ImRect(minimum, maximum);
}

void DrawRawLabRadialText(
    ImDrawList* drawList,
    ImFont* font,
    float fontSize,
    const ImVec2& inwardStart,
    const ImVec2& textSize,
    float angle,
    ImU32 color,
    const char* text) {
    if (drawList == nullptr || font == nullptr || text == nullptr) {
        return;
    }
    const int vertexStart = drawList->VtxBuffer.Size;
    drawList->AddText(
        font,
        fontSize,
        ImVec2(inwardStart.x, inwardStart.y - textSize.y * 0.5f),
        color,
        text);
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    for (int index = vertexStart; index < drawList->VtxBuffer.Size; ++index) {
        const ImVec2 delta(
            drawList->VtxBuffer[index].pos.x - inwardStart.x,
            drawList->VtxBuffer[index].pos.y - inwardStart.y);
        drawList->VtxBuffer[index].pos = ImVec2(
            inwardStart.x + delta.x * cosine - delta.y * sine,
            inwardStart.y + delta.x * sine + delta.y * cosine);
    }
}

struct RawLabValueSurfaceResult {
    bool changed = false;
    bool interactionActive = false;
};

RawLabValueSurfaceResult DrawRawLabValueSurface(
    const char* id,
    const char* label,
    float& value,
    float minimum,
    float maximum,
    float resetValue,
    float dragGain,
    float wheelGain,
    bool logarithmic,
    const char* valueFormat,
    const ImVec2& size,
    float valueFontSize,
    const ImVec4& accent,
    float opacity,
    float& glow,
    bool enabled) {
    RawLabValueSurfaceResult result;
    const ImVec2 surfaceSize(
        std::max(1.0f, size.x),
        std::max(1.0f, size.y));
    ImGui::InvisibleButton(
        id,
        surfaceSize,
        ImGuiButtonFlags_MouseButtonLeft);
    const ImGuiID widgetId = ImGui::GetItemID();
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool hovered = ImGui::IsItemHovered(
        ImGuiHoveredFlags_AllowWhenDisabled);
    const bool active = enabled && ImGui::IsItemActive();
    if (hovered && enabled) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const float glowTarget = hovered && enabled ? 1.0f : 0.0f;
    glow = RawLabEaseToward(glow, glowTarget, 0.085f);
    const float safeOpacity = std::clamp(opacity, 0.0f, 1.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    if ((hovered || active) && enabled) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID anchorXKey = widgetId ^ static_cast<ImGuiID>(0x6a5f31d1u);
    const ImGuiID anchorYKey = widgetId ^ static_cast<ImGuiID>(0x2cb8e4a7u);
    if (enabled && ImGui::IsItemActivated()) {
        storage->SetFloat(anchorXKey, io.MousePos.x);
        storage->SetFloat(anchorYKey, io.MousePos.y);
    }
    if (active) {
        const ImVec2 anchor(
            storage->GetFloat(anchorXKey, rect.GetCenter().x),
            storage->GetFloat(anchorYKey, rect.GetCenter().y));
        const float pointerDelta =
            (io.MousePos.x - anchor.x) - (io.MousePos.y - anchor.y);
        if (std::abs(pointerDelta) > 0.0001f) {
            value = logarithmic
                ? value * std::exp(pointerDelta * dragGain)
                : value + pointerDelta * dragGain;
            value = std::clamp(value, minimum, maximum);
            result.changed = true;
        }
        ImGuiExtras::SubmitCursorCaptureRequest(
            ImGuiExtras::CursorCaptureRequest{
                ImGuiExtras::CursorCaptureMode::LockedScrub,
                anchor,
                anchor
            });
        result.interactionActive = true;
    }
    if (hovered && enabled && std::abs(io.MouseWheel) > 0.0001f) {
        const float wheel = std::clamp(io.MouseWheel, -4.0f, 4.0f);
        value = logarithmic
            ? value * std::pow(wheelGain, wheel)
            : value + wheel * wheelGain;
        value = std::clamp(value, minimum, maximum);
        result.changed = true;
        result.interactionActive = true;
    }
    if (hovered && enabled &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        value = std::clamp(resetValue, minimum, maximum);
        result.changed = true;
        result.interactionActive = true;
    }

    char valueText[64] = {};
    std::snprintf(valueText, sizeof(valueText), valueFormat, value);
    ImFont* font = ImGui::GetFont();
    const float resolvedValueFontSize = std::max(12.0f, valueFontSize);
    const ImVec2 valueTextSize = font->CalcTextSizeA(
        resolvedValueFontSize,
        std::numeric_limits<float>::max(),
        0.0f,
        valueText);
    const ImVec4 textColor = enabled
        ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImVec2 valuePosition(
        rect.GetCenter().x - valueTextSize.x * 0.5f,
        rect.GetCenter().y - valueTextSize.y * 0.47f);
    DrawRawLabGlowingText(
        drawList,
        font,
        resolvedValueFontSize,
        valuePosition,
        ImGui::GetColorU32(RawLabColorWithAlpha(
            textColor,
            safeOpacity)),
        accent,
        glow,
        safeOpacity,
        valueText);
    if (label != nullptr && label[0] != '\0') {
        const float labelFontSize = std::max(
            9.0f,
            ImGui::GetFontSize() * 0.72f);
        const ImVec2 labelSize = font->CalcTextSizeA(
            labelFontSize,
            std::numeric_limits<float>::max(),
            0.0f,
            label);
        DrawRawLabGlowingText(
            drawList,
            font,
            labelFontSize,
            ImVec2(
                rect.GetCenter().x - labelSize.x * 0.5f,
                rect.Max.y - labelFontSize - 9.0f),
            ImGui::GetColorU32(RawLabColorWithAlpha(
                accent,
                (0.72f + 0.18f * glow) * safeOpacity)),
            accent,
            glow * 0.72f,
            safeOpacity,
            label);
    }
    return result;
}

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif


} // namespace

bool EditorModule::RenderRawWorkspaceLabLightSurface(RawWorkspaceEditContext& context) {
    bool changed = false;
    auto& state = m_RawWorkspaceLabUi;
    state.lightInteractionActive = false;
    state.globalExposureInteractionActive = false;

    Stack::RawRecipe::RawWhiteBalanceRecipe& whiteBalance =
        context.recipe.whiteBalance;
    bool manualWhiteBalance =
        whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers &&
        whiteBalance.hasMultipliers;

    const std::string lightSourceKey = context.multiFrameResult
        ? std::string("multiframe:") + context.multiFrameSourceSetId
        : context.source != nullptr
            ? context.source->relativePathKey
            : std::string("raw-light");
    if (state.lightSurfaceSourceKey != lightSourceKey) {
        state.lightSurfaceSourceKey = lightSourceKey;
        state.lightDemosaicInitialized = false;
        state.lightExposureGlow = 0.0f;
        state.lightExposureGraphInitialized = false;
        state.lightWhiteBalanceGlow = { 0.0f, 0.0f, 0.0f };
        state.lightWhiteBalanceModeMix = manualWhiteBalance ? 1.0f : 0.0f;
        state.lightWhiteBalanceFieldsOpacity = manualWhiteBalance ? 1.0f : 0.0f;
    }

    const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float availableHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    constexpr float sectionGap = 10.0f;
    const float wheelHeight = std::clamp(availableHeight * 0.40f, 128.0f, 244.0f);
    const float whiteBalanceHeight = std::max(1.0f, availableHeight - wheelHeight - sectionGap);
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);

    const bool reconstructed=context.multiFrameResult&&m_HdrAdoptedRawResult&&m_HdrAdoptedRawResult->rawData&&
        m_HdrAdoptedRawResult->rawData->reconstructedCameraRgb;
    const bool canDemosaic=context.canEdit&&!reconstructed;
    if(reconstructed)ImGui::TextDisabled("Color reconstruction supplied by Bracketing");
    constexpr int demosaicChoiceCount =
        static_cast<int>(kRawLabDemosaicChoices.size());
    constexpr float demosaicStep =
        (2.0f * kRawLabPi) / static_cast<float>(demosaicChoiceCount);
    if (!state.lightDemosaicInitialized) {
        state.lightDemosaicSelectedItem = RawLabDemosaicVisualIndex(
            context.recipe.technical.demosaicMethod);
        state.lightDemosaicRotation =
            -static_cast<float>(state.lightDemosaicSelectedItem) * demosaicStep;
        state.lightDemosaicTargetRotation = state.lightDemosaicRotation;
        state.lightDemosaicLastInputTime = -1.0;
        state.lightDemosaicLastAcceptedScrollTime = -1.0;
        state.lightDemosaicLastAcceptedScrollDirection = 0;
        state.lightDemosaicIgnoredOppositeDirection = 0;
        state.lightDemosaicDragging = false;
        state.lightDemosaicPressedItem = -1;
        state.lightDemosaicInitialized = true;
    }

    ImGui::InvisibleButton(
        "##RawLabLightDemosaicWheel",
        ImVec2(availableWidth, wheelHeight),
        ImGuiButtonFlags_MouseButtonLeft);
    const ImRect wheelRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool wheelHovered = ImGui::IsItemHovered(
        ImGuiHoveredFlags_AllowWhenDisabled);
    const bool wheelActive = canDemosaic && ImGui::IsItemActive();
    if (wheelHovered && canDemosaic) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const double now = ImGui::GetTime();
    const ImGuiIO& io = ImGui::GetIO();
    const float wheelRadius = std::clamp(
        std::min(wheelRect.GetHeight() * 0.92f, availableWidth * 0.78f),
        96.0f,
        260.0f);
    const ImVec2 wheelCenter(
        wheelRect.Min.x - 2.0f,
        wheelRect.GetCenter().y + 6.0f);
    const int displayedSelection = RawLabWrapIndex(
        static_cast<int>(std::lround(
            -state.lightDemosaicRotation / demosaicStep)),
        demosaicChoiceCount);
    std::array<ImRect, demosaicChoiceCount> demosaicHitRects;
    std::array<bool, demosaicChoiceCount> demosaicHitVisible {};
    ImFont* wheelFont = ImGui::GetFont();
    for (int index = 0; index < demosaicChoiceCount; ++index) {
        const float angle = RawLabWrapAngle(
            static_cast<float>(index) * demosaicStep +
            state.lightDemosaicRotation);
        const float facing = std::cos(angle);
        if (facing <= 0.015f) {
            continue;
        }
        const float fontSize = index == displayedSelection
            ? std::clamp(availableWidth * 0.057f, 17.0f, 22.0f)
            : std::clamp(availableWidth * 0.045f, 13.0f, 17.0f);
        const ImVec2 textSize = wheelFont->CalcTextSizeA(
            fontSize,
            std::numeric_limits<float>::max(),
            0.0f,
            kRawLabDemosaicChoices[static_cast<std::size_t>(index)].label);
        const float inwardRadius = wheelRadius * 0.52f;
        const ImVec2 inwardStart(
            wheelCenter.x + std::cos(angle) * inwardRadius,
            wheelCenter.y + std::sin(angle) * inwardRadius);
        ImRect hitBounds = RawLabRadialTextBounds(
            inwardStart,
            textSize,
            angle);
        hitBounds.Expand(ImVec2(7.0f, 5.0f));
        demosaicHitRects[static_cast<std::size_t>(index)] = hitBounds;
        demosaicHitVisible[static_cast<std::size_t>(index)] = true;
    }

    if (canDemosaic && ImGui::IsItemActivated()) {
        state.lightDemosaicPressPosition = io.MousePos;
        state.lightDemosaicPreviousPointerAngle = std::atan2(
            io.MousePos.y - wheelCenter.y,
            io.MousePos.x - wheelCenter.x);
        state.lightDemosaicDragging = false;
        state.lightDemosaicPressedItem = -1;
        for (int index = 0; index < demosaicChoiceCount; ++index) {
            if (demosaicHitVisible[static_cast<std::size_t>(index)] &&
                demosaicHitRects[static_cast<std::size_t>(index)].Contains(
                    io.MousePos)) {
                state.lightDemosaicPressedItem = index;
                break;
            }
        }
    }
    if (wheelActive && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float dragX = io.MousePos.x - state.lightDemosaicPressPosition.x;
        const float dragY = io.MousePos.y - state.lightDemosaicPressPosition.y;
        if (!state.lightDemosaicDragging &&
            dragX * dragX + dragY * dragY >= 16.0f) {
            state.lightDemosaicDragging = true;
            state.lightDemosaicPressedItem = -1;
        }
        if (state.lightDemosaicDragging) {
            const float pointerAngle = std::atan2(
                io.MousePos.y - wheelCenter.y,
                io.MousePos.x - wheelCenter.x);
            const float pointerDelta = std::clamp(
                RawLabWrapAngle(
                    pointerAngle - state.lightDemosaicPreviousPointerAngle),
                -0.42f,
                0.42f);
            state.lightDemosaicTargetRotation += pointerDelta;
            state.lightDemosaicPreviousPointerAngle = pointerAngle;
            state.lightDemosaicLastInputTime = now;
            state.lightInteractionActive = true;
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }
    }
    if (canDemosaic && wheelHovered &&
        std::abs(io.MouseWheel) > 0.0001f) {
        const float wheelImpulse = std::clamp(io.MouseWheel, -4.0f, 4.0f);
        const int direction = wheelImpulse > 0.0f ? 1 : -1;
        const bool firstOppositeInsideDebounce =
            state.lightDemosaicLastAcceptedScrollDirection != 0 &&
            direction != state.lightDemosaicLastAcceptedScrollDirection &&
            now - state.lightDemosaicLastAcceptedScrollTime <=
                kRawLabDemosaicDirectionDebounceSeconds &&
            state.lightDemosaicIgnoredOppositeDirection != direction;
        if (firstOppositeInsideDebounce) {
            state.lightDemosaicIgnoredOppositeDirection = direction;
        } else {
            // The 1.18 multiplier is the requested modest gain increase over
            // the wheel's conservative 0.66-item base response.
            constexpr float scrollGain = demosaicStep * 0.66f * 1.18f;
            state.lightDemosaicTargetRotation += wheelImpulse * scrollGain;
            state.lightDemosaicLastInputTime = now;
            state.lightDemosaicLastAcceptedScrollTime = now;
            state.lightDemosaicLastAcceptedScrollDirection = direction;
            state.lightDemosaicIgnoredOppositeDirection = 0;
            state.lightInteractionActive = true;
        }
    }
    if (canDemosaic && ImGui::IsItemDeactivated()) {
        if (!state.lightDemosaicDragging &&
            state.lightDemosaicPressedItem >= 0) {
            const float pressedAngle = RawLabWrapAngle(
                static_cast<float>(state.lightDemosaicPressedItem) * demosaicStep +
                state.lightDemosaicRotation);
            state.lightDemosaicTargetRotation =
                state.lightDemosaicRotation - pressedAngle;
            state.lightDemosaicLastInputTime = now;
            state.lightInteractionActive = true;
        }
        state.lightDemosaicDragging = false;
        state.lightDemosaicPressedItem = -1;
    }

    if (!state.lightDemosaicDragging &&
        state.lightDemosaicLastInputTime >= 0.0 &&
        now - state.lightDemosaicLastInputTime >=
            kRawLabDemosaicIdleSettleSeconds) {
        const float nearestAlignedTarget = std::round(
            state.lightDemosaicTargetRotation / demosaicStep) * demosaicStep;
        const float alignmentDistance = std::abs(
            state.lightDemosaicTargetRotation - nearestAlignedTarget);
        if (alignmentDistance <= demosaicStep * 0.42f) {
            state.lightDemosaicTargetRotation = nearestAlignedTarget;
        }
    }
    state.lightDemosaicRotation = RawLabEaseToward(
        state.lightDemosaicRotation,
        state.lightDemosaicTargetRotation,
        kRawLabDemosaicPursuitSeconds);
    const bool wheelAnimating = std::abs(
        state.lightDemosaicRotation -
        state.lightDemosaicTargetRotation) > 0.0002f;
    state.lightInteractionActive |= wheelActive || wheelAnimating;
    const int nearestDemosaic = RawLabWrapIndex(
        static_cast<int>(std::lround(
            -state.lightDemosaicRotation / demosaicStep)),
        demosaicChoiceCount);
    if (nearestDemosaic != state.lightDemosaicSelectedItem) {
        state.lightDemosaicSelectedItem = nearestDemosaic;
        const RawLabDemosaicChoice& selectedChoice =
            kRawLabDemosaicChoices[static_cast<std::size_t>(nearestDemosaic)];
        if (canDemosaic && selectedChoice.implemented &&
            context.recipe.technical.demosaicMethod !=
                selectedChoice.backendMethod) {
            context.recipe.technical.demosaicMethod =
                selectedChoice.backendMethod;
            changed = true;
        }
    }
    if (std::abs(state.lightDemosaicRotation) > 200.0f * kRawLabPi &&
        !wheelAnimating && !state.lightDemosaicDragging) {
        const float turns = std::round(
            state.lightDemosaicRotation / (2.0f * kRawLabPi));
        const float reduction = turns * 2.0f * kRawLabPi;
        state.lightDemosaicRotation -= reduction;
        state.lightDemosaicTargetRotation -= reduction;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(wheelRect.Min, wheelRect.Max, true);
    const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    drawList->AddCircle(
        wheelCenter,
        wheelRadius,
        ImGui::GetColorU32(RawLabColorWithAlpha(disabled, 0.20f)),
        96,
        1.0f);
    drawList->AddCircle(
        wheelCenter,
        wheelRadius * 0.88f,
        ImGui::GetColorU32(RawLabColorWithAlpha(disabled, 0.075f)),
        96,
        1.0f);
    const ImVec2 selectionPoint(
        wheelCenter.x + wheelRadius,
        wheelCenter.y);
    drawList->AddCircleFilled(
        selectionPoint,
        3.25f,
        ImGui::GetColorU32(RawLabColorWithAlpha(accent, 0.92f)));
    drawList->AddLine(
        ImVec2(selectionPoint.x - 13.0f, selectionPoint.y),
        ImVec2(selectionPoint.x + 8.0f, selectionPoint.y),
        ImGui::GetColorU32(RawLabColorWithAlpha(accent, 0.40f)),
        1.0f);
    for (int index = 0; index < demosaicChoiceCount; ++index) {
        const float angle = RawLabWrapAngle(
            static_cast<float>(index) * demosaicStep +
            state.lightDemosaicRotation);
        const float facing = std::cos(angle);
        if (facing <= 0.015f) {
            continue;
        }
        const bool selected = index == nearestDemosaic;
        const RawLabDemosaicChoice& choice =
            kRawLabDemosaicChoices[static_cast<std::size_t>(index)];
        const float fontSize = selected
            ? std::clamp(availableWidth * 0.057f, 17.0f, 22.0f)
            : std::clamp(availableWidth * 0.045f, 13.0f, 17.0f);
        const ImVec2 textSize = wheelFont->CalcTextSizeA(
            fontSize,
            std::numeric_limits<float>::max(),
            0.0f,
            choice.label);
        const float inwardRadius = wheelRadius * 0.52f;
        const ImVec2 inwardStart(
            wheelCenter.x + std::cos(angle) * inwardRadius,
            wheelCenter.y + std::sin(angle) * inwardRadius);
        const float facingAlpha = std::pow(
            std::clamp(facing, 0.0f, 1.0f),
            0.70f);
        const ImVec4 labelColor = choice.implemented
            ? (selected
                ? accent
                : ImGui::GetStyleColorVec4(ImGuiCol_Text))
            : disabled;
        const float labelAlpha = facingAlpha * (choice.implemented
            ? (selected ? 1.0f : 0.54f)
            : (selected ? 0.58f : 0.34f));
        DrawRawLabRadialText(
            drawList,
            wheelFont,
            fontSize,
            inwardStart,
            textSize,
            angle,
            ImGui::GetColorU32(RawLabColorWithAlpha(
                labelColor,
                labelAlpha)),
            choice.label);
    }
    drawList->PopClipRect();
    ImGui::Dummy(ImVec2(0.0f, sectionGap));

    const ImVec2 whiteCardMinimum = ImGui::GetCursorScreenPos();
    const ImRect whiteCard(
        whiteCardMinimum,
        ImVec2(
            whiteCardMinimum.x + availableWidth,
            whiteCardMinimum.y + whiteBalanceHeight));
    const float compactModeHeight = std::clamp(
        whiteBalanceHeight * 0.31f,
        48.0f,
        68.0f);
    const float modeButtonHeight = manualWhiteBalance
        ? compactModeHeight
        : whiteBalanceHeight;
    ImGui::InvisibleButton(
        "##RawLabLightWhiteBalanceMode",
        ImVec2(availableWidth, modeButtonHeight),
        ImGuiButtonFlags_MouseButtonLeft);
    const ImRect modeRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool modeHovered = ImGui::IsItemHovered(
        ImGuiHoveredFlags_AllowWhenDisabled);
    if (modeHovered && context.canEdit) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (context.canEdit && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (manualWhiteBalance) {
            whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::AsShot;
            whiteBalance.hasMultipliers = false;
            manualWhiteBalance = false;
        } else {
            whiteBalance.mode =
                Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
            whiteBalance.hasMultipliers = true;
            for (float& multiplier : whiteBalance.multipliers) {
                multiplier = std::clamp(
                    std::isfinite(multiplier) ? multiplier : 1.0f,
                    0.05f,
                    16.0f);
            }
            manualWhiteBalance = true;
        }
        changed = true;
        state.lightInteractionActive = true;
    }
    state.lightInteractionActive |= context.canEdit && ImGui::IsItemActive();
    state.lightWhiteBalanceModeMix = RawLabEaseToward(
        state.lightWhiteBalanceModeMix,
        manualWhiteBalance ? 1.0f : 0.0f,
        0.105f);
    const float fieldFadeTarget = manualWhiteBalance
        ? std::clamp(
            (state.lightWhiteBalanceModeMix - 0.66f) / 0.34f,
            0.0f,
            1.0f)
        : 0.0f;
    state.lightWhiteBalanceFieldsOpacity = RawLabEaseToward(
        state.lightWhiteBalanceFieldsOpacity,
        fieldFadeTarget,
        0.115f);

    drawList->PushClipRect(whiteCard.Min, whiteCard.Max, true);
    const float modeTransition =
        state.lightWhiteBalanceModeMix *
        state.lightWhiteBalanceModeMix *
        (3.0f - 2.0f * state.lightWhiteBalanceModeMix);
    const float cameraModeCenterY = whiteCard.GetCenter().y;
    const float manualModeCenterY =
        whiteCard.Min.y + compactModeHeight * 0.52f;
    const float modeCenterY =
        cameraModeCenterY +
        (manualModeCenterY - cameraModeCenterY) * modeTransition;
    const float modeStep = std::clamp(
        whiteBalanceHeight * 0.20f,
        30.0f,
        42.0f);
    const char* cameraLabel = "CAMERA";
    const auto drawModeLabel = [&](const char* label, float logicalIndex) {
        const float distance = logicalIndex - state.lightWhiteBalanceModeMix;
        const float labelCenterY = modeCenterY + distance * modeStep;
        if (labelCenterY < whiteCard.Min.y - modeStep ||
            labelCenterY > whiteCard.Max.y + modeStep) {
            return;
        }
        const bool centered = std::abs(distance) < 0.48f;
        const float fontSize = centered
            ? std::clamp(availableWidth * 0.065f, 19.0f, 26.0f)
            : std::clamp(availableWidth * 0.050f, 14.0f, 19.0f);
        const ImVec2 textSize = wheelFont->CalcTextSizeA(
            fontSize,
            std::numeric_limits<float>::max(),
            0.0f,
            label);
        const float bottomFade = std::clamp(
            (whiteCard.Max.y - labelCenterY) /
                std::max(1.0f, modeStep * 0.92f),
            0.0f,
            1.0f);
        const float topFade = std::clamp(
            (labelCenterY - whiteCard.Min.y) /
                std::max(1.0f, modeStep * 0.72f),
            0.0f,
            1.0f);
        const float distanceFade = centered
            ? 1.0f
            : std::clamp(0.34f - 0.10f * (std::abs(distance) - 1.0f),
                0.08f,
                0.34f);
        drawList->AddText(
            wheelFont,
            fontSize,
            ImVec2(
                whiteCard.GetCenter().x - textSize.x * 0.5f,
                labelCenterY - textSize.y * 0.5f),
            ImGui::GetColorU32(RawLabColorWithAlpha(
                centered
                    ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                    : disabled,
                distanceFade * bottomFade * topFade)),
            label);
    };
    drawModeLabel(cameraLabel, 0.0f);
    drawModeLabel("MANUAL", 1.0f);
    drawList->PopClipRect();

    if (!manualWhiteBalance) {
        return changed;
    }

    const float fieldGap = 6.0f;
    const float fieldWidth = std::max(
        1.0f,
        (availableWidth - fieldGap * 2.0f) / 3.0f);
    const float fieldHeight = std::max(
        1.0f,
        whiteBalanceHeight - compactModeHeight - 7.0f);
    const float fieldOpacity = std::clamp(
        state.lightWhiteBalanceFieldsOpacity,
        0.0f,
        1.0f);
    const bool fieldsEnabled =
        context.canEdit && manualWhiteBalance && fieldOpacity >= 0.72f;
    ImGui::SetCursorScreenPos(ImVec2(
        whiteCard.Min.x,
        whiteCard.Min.y + compactModeHeight + 7.0f));
    const std::array<const char*, 3> multiplierLabels { "R", "G", "B" };
    const std::array<const char*, 3> multiplierIds {
        "##RawLabLightWbRedSurface",
        "##RawLabLightWbGreenSurface",
        "##RawLabLightWbBlueSurface"
    };
    const std::array<ImVec4, 3> multiplierAccents {{
        ImVec4(0.96f, 0.40f, 0.37f, 1.0f),
        ImVec4(0.43f, 0.91f, 0.62f, 1.0f),
        ImVec4(0.43f, 0.64f, 0.98f, 1.0f)
    }};
    for (int channel = 0; channel < 3; ++channel) {
        float multiplier = std::clamp(
            std::isfinite(whiteBalance.multipliers[
                static_cast<std::size_t>(channel)])
                ? whiteBalance.multipliers[static_cast<std::size_t>(channel)]
                : 1.0f,
            0.05f,
            16.0f);
        const RawLabValueSurfaceResult multiplierResult =
            DrawRawLabValueSurface(
                multiplierIds[static_cast<std::size_t>(channel)],
                multiplierLabels[static_cast<std::size_t>(channel)],
                multiplier,
                0.05f,
                16.0f,
                1.0f,
                0.006f,
                1.04f,
                true,
                "%.3f",
                ImVec2(fieldWidth, fieldHeight),
                std::clamp(fieldHeight * 0.32f, 19.0f, 34.0f),
                multiplierAccents[static_cast<std::size_t>(channel)],
                fieldOpacity,
                state.lightWhiteBalanceGlow[
                    static_cast<std::size_t>(channel)],
                fieldsEnabled);
        if (multiplierResult.changed) {
            whiteBalance.mode =
                Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
            whiteBalance.hasMultipliers = true;
            whiteBalance.multipliers[static_cast<std::size_t>(channel)] =
                multiplier;
            changed = true;
        }
        state.lightInteractionActive |= multiplierResult.interactionActive;
        if (channel < 2) {
            ImGui::SameLine(0.0f, fieldGap);
        }
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabLightSecondaryControls(
    RawWorkspaceEditContext& context) {
    ImGui::TextWrapped(
        "Camera white balance and Bayer demosaic. "
        "For global exposure, drag empty space in the EV graph up or down.");
    ImGui::Spacing();
    ImGui::TextDisabled(
        "Working space: %s",
        Raw::RawWorkingSpaceName(context.recipe.technical.workingSpace));
    ImGui::TextDisabled(
        "Baseline exposure: %s",
        context.recipe.technical.applyBaselineExposure
            ? "DNG metadata applied"
            : "off");
    ImGui::TextDisabled(
        "Output transfer: %s",
        context.recipe.technical.encodeSrgbOutput ? "sRGB" : "linear");
    return false;
}
