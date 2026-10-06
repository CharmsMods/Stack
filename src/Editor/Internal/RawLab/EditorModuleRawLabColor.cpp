#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabColorSurface.h"
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

bool EditorModule::RenderRawWorkspaceLabColorSurface(
    RawWorkspaceEditContext& context, RawLabControlSection section) {
    const std::string previewIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    const RawDevelopmentGraphScopeReadback& cachedScope =
        m_RawWorkspaceColorWarpInputGraphScopeCache.readback;
    const bool cachedScopeMatches =
        !previewIdentity.empty() &&
        m_RawWorkspaceColorWarpInputGraphScopeCache.sourceKey ==
            previewIdentity &&
        cachedScope.valid &&
        cachedScope.stage ==
            RawDevelopmentGraphScopeStage::ColorWarpInput;
    const RawDevelopmentGraphScopeReadback& scope = cachedScopeMatches
        ? cachedScope
        : m_RawWorkspaceGraphScopeReadback;
    const std::size_t cloudInputFingerprint =
        m_RawWorkspaceColorWarpInputGraphScopeCache.sourceKey ==
                previewIdentity
            ? m_RawWorkspaceColorWarpInputGraphScopeCache.inputFingerprint
            : 0u;
    RawLabColorSurfaceArgs args {
        m_RawWorkspaceLabUi,
        context.recipe.colorWarp,
        context.recipe.technical.workingSpace,
        scope,
        cloudInputFingerprint,
        previewIdentity,
        [this]() { SaveRawWorkspaceAppState(); }
    };
    args.strengthDriven = IsRawParameterDriven("strength");
    bool changed = ShowsRawLabGraph(section) && Stack::Editor::RawLabInternal::RenderRawLabColorSurface(args);
    if (ShowsRawLabSettings(section)) changed |= Stack::Editor::RawLabInternal::RenderRawLabColorSettings(args);
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabColorSecondaryControls(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    context.recipe.colorWarp = Stack::RawRecipe::SanitizeColorWarpRecipe(
        std::move(context.recipe.colorWarp));
    auto& colorWarp = context.recipe.colorWarp;
    ImGui::BeginDisabled(IsRawParameterDriven("strength"));
    float overallStrength = colorWarp.strength;
    if (BareSliderFloat(
            "Overall Strength",
            "RawLabColorAdvancedOverallStrength",
            &overallStrength,
            0.0f,
            2.0f,
            "%.2f")) {
        colorWarp.strength = overallStrength;
        changed = true;
    }
    ImGui::EndDisabled();
    LabTooltip(
        "Master multiplier for every enabled Color Warp pin. 1.00 is authored "
        "strength; 0 preserves but visually bypasses the edit; values above 1 amplify it.");
    if (!colorWarp.pins.empty()) {
        m_RawWorkspaceLabUi.selectedColorWarpPin = std::clamp(
            m_RawWorkspaceLabUi.selectedColorWarpPin,
            0,
            static_cast<int>(colorWarp.pins.size() - 1u));
        auto& pin = colorWarp.pins[static_cast<std::size_t>(
            m_RawWorkspaceLabUi.selectedColorWarpPin)];
        ImGui::SeparatorText(pin.name.c_str());
        float reach = pin.radius;
        if (BareSliderFloat(
                "Reach",
                "RawLabColorAdvancedReach",
                &reach,
                0.005f,
                1.0f,
                "%.3f")) {
            pin.radius = reach;
            changed = true;
        }
        LabTooltip(
            "Color-distance radius around this pin's source on the OKLab a/b "
            "surface. This is not the image-space spatial Reach control.");
        float feather = pin.softness;
        if (BareSliderFloat(
                "Feather",
                "RawLabColorAdvancedFeather",
                &feather,
                0.0f,
                1.0f,
                "%.2f")) {
            pin.softness = feather;
            changed = true;
        }
        LabTooltip(
            "Softness across the outer edge of this pin's OKLab color "
            "qualifier. This is separate from EV and image-space boundary feathering.");
        float strength = pin.strength;
        if (BareSliderFloat(
                "Pin Strength",
                "RawLabColorAdvancedPinStrength",
                &strength,
                0.0f,
                2.0f,
                "%.2f")) {
            pin.strength = strength;
            changed = true;
        }
        LabTooltip(
            "Multiplier for only this pin's displacement and lightness shift, "
            "applied before the Color Warp master strength.");
        ImGui::TextDisabled("EV qualification");
        if (BareTextButton("All EV")) {
            pin.evCurve = Stack::RawRecipe::MakeUniformColorWarpEvCurve(1.0f);
            changed = true;
        }
        LabTooltip("Set the selected color to qualify at every EV.");
        ImGui::SameLine(0.0f, 2.0f);
        if (BareTextButton("None")) {
            pin.evCurve = Stack::RawRecipe::MakeUniformColorWarpEvCurve(0.0f);
            changed = true;
        }
        LabTooltip("Keep the color selection but make its EV qualification zero everywhere.");
    }
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Color Warp uses its graph input in the two-dimensional OKLab a/b plane. Hue is the angle, "
        "chroma is distance from neutral, and the disc is shown at "
        "one fixed lightness; the strip owns lightness.");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "Working RGB -> XYZ D65 -> OKLab a/b -> working RGB. Scene values remain unclamped.");
    ImGui::PopTextWrapPos();
    return changed;
}
