#include "Editor/Internal/RawDemosaicControls.h"
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
    const float wheelHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    const float whiteBalanceHeight = std::max(1.0f, availableHeight - wheelHeight - sectionGap);
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);

    const bool reconstructed=context.multiFrameResult&&m_HdrAdoptedRawResult&&m_HdrAdoptedRawResult->rawData&&
        m_HdrAdoptedRawResult->rawData->reconstructedCameraRgb;
    const bool canDemosaic=context.canEdit&&!reconstructed;
    if(reconstructed)ImGui::TextDisabled("Color reconstruction supplied by Bracketing");
    ImGui::TextUnformatted("Demosaicing");
    ImGui::BeginDisabled(!canDemosaic);
    changed |= Stack::Editor::DrawDemosaicCombo("##RawDemosaic",context.recipe.technical.demosaicMethod,-1.f,
        true,&m_RawFloatingSurface.imageNavigationMenuOpen);
    state.lightInteractionActive |= ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f,sectionGap));
    ImDrawList* drawList=ImGui::GetWindowDrawList();
    ImFont* wheelFont=ImGui::GetFont();
    const ImVec4 disabled=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

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
