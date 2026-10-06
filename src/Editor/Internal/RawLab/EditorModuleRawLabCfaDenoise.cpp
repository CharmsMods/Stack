#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include <imgui_internal.h>

#include <algorithm>

using namespace Stack::Editor::RawLabInternal;

bool EditorModule::RenderRawWorkspaceLabDenoiseSurface(
    RawWorkspaceEditContext& context) {
    if(context.multiFrameResult&&m_HdrAdoptedRawResult&&m_HdrAdoptedRawResult->rawData&&
       m_HdrAdoptedRawResult->rawData->reconstructedCameraRgb) {
        ImGui::TextWrapped("Super-resolution has already reconstructed the sensor colors. Use RGB Denoise for remaining noise.");
        return false;
    }
    bool changed = false;
    Raw::RawMosaicDenoiseSettings& denoise =
        context.recipe.technical.mosaicDenoise;

    ImGui::TextDisabled("Pre-demosaic sensor cleanup");
    ImGui::Spacing();
    bool enabled = denoise.enabled;
    if (ImGui::Checkbox("Denoise", &enabled)) {
        denoise.enabled = enabled;
        changed = true;
    }
    LabTooltip(
        "Filters same-color sensor samples before Stack reconstructs RGB pixels.");

    ImGui::Spacing();
    ImGui::TextDisabled("Noise model");
    const bool dngMode =
        denoise.mode == Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    if (BareTextButton("DNG Profile", dngMode) && !dngMode) {
        denoise.mode = Raw::RawMosaicDenoiseMode::DngNoiseProfile;
        changed = true;
    }
    LabTooltip(
        "Uses the camera's DNG shot/read NoiseProfile when valid; otherwise "
        "uses the fixed-threshold fallback.");
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Fixed", !dngMode) && dngMode) {
        denoise.mode = Raw::RawMosaicDenoiseMode::FixedThreshold;
        changed = true;
    }
    LabTooltip("Uses Stack's historical fixed-threshold same-CFA filter.");

    if (denoise.mode == Raw::RawMosaicDenoiseMode::DngNoiseProfile) {
        ImGui::TextWrapped(
            "DNG-aware filtering is used when the source supplies a valid "
            "NoiseProfile; otherwise Stack uses the fixed fallback.");
    } else {
        ImGui::TextWrapped(
            "Fixed mode uses authored thresholds instead of camera noise metadata.");
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!denoise.enabled);
    float greenStrength = denoise.lumaStrength;
    if (BareSliderFloat(
            "Green",
            "RawLabDenoiseGreen",
            &greenStrength,
            0.0f,
            1.0f,
            "%.2f",
            std::max(170.0f, ImGui::GetContentRegionAvail().x - 90.0f))) {
        denoise.lumaStrength = greenStrength;
        changed = true;
    }
    LabTooltip("Denoise strength for green CFA samples.");
    float redBlueStrength = denoise.chromaStrength;
    if (BareSliderFloat(
            "Red / Blue",
            "RawLabDenoiseRedBlue",
            &redBlueStrength,
            0.0f,
            1.0f,
            "%.2f",
            std::max(150.0f, ImGui::GetContentRegionAvail().x - 120.0f))) {
        denoise.chromaStrength = redBlueStrength;
        changed = true;
    }
    LabTooltip("Denoise strength for red and blue CFA samples.");
    float edgeProtection = denoise.edgeProtection;
    if (BareSliderFloat(
            "Protect Edges",
            "RawLabDenoiseEdges",
            &edgeProtection,
            0.0f,
            1.0f,
            "%.2f",
            std::max(150.0f, ImGui::GetContentRegionAvail().x - 130.0f))) {
        denoise.edgeProtection = edgeProtection;
        changed = true;
    }
    LabTooltip("Higher values reject neighboring samples more aggressively.");
    bool hotPixels = denoise.hotPixelSuppression;
    if (ImGui::Checkbox("Suppress Hot Pixels", &hotPixels)) {
        denoise.hotPixelSuppression = hotPixels;
        changed = true;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextDisabled(
        "Judge this at 100%% zoom; settled output remains full resolution.");
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabDenoiseSecondaryControls(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    Raw::RawMosaicDenoiseSettings& denoise =
        context.recipe.technical.mosaicDenoise;
    ImGui::BeginDisabled(!denoise.enabled);
    int radius = denoise.radius;
    if (BareSliderInt(
            "Radius",
            "RawLabDenoiseRadius",
            &radius,
            1,
            4,
            "%d CFA steps")) {
        denoise.radius = radius;
        changed = true;
    }
    int iterations = denoise.iterations;
    if (BareSliderInt(
            "Iterations",
            "RawLabDenoiseIterations",
            &iterations,
            1,
            2,
            "%d")) {
        denoise.iterations = iterations;
        changed = true;
    }
    float hotPixelThreshold = denoise.hotPixelThreshold;
    if (BareSliderFloat(
            "Hot Pixel Threshold",
            "RawLabDenoiseHotPixelThreshold",
            &hotPixelThreshold,
            0.005f,
            0.5f,
            "%.3f")) {
        denoise.hotPixelThreshold = hotPixelThreshold;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Experimental pre-demosaic controls. Two iterations currently repeat "
        "the center evaluation against the original neighborhood; "
        "they are not two separately materialized denoise passes.");
    return changed;
}
