#include "Editor/Internal/EditorModuleDevelopManualRawControls.h"

#include "Editor/Internal/EditorModuleRawControlShared.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace {

bool ResettableDevelopSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float resetValue,
    float minValue,
    float maxValue,
    const char* format,
    float controlWidth) {
    bool localChanged = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (!Stack::Editor::RawControls::GraphSliderRightClickWasConsumed() &&
        state.lastHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        std::abs(*value - resetValue) > 0.0001f) {
        *value = resetValue;
        localChanged = true;
    }
    return localChanged;
}

bool ResettableDevelopSliderInt(
    const char* label,
    const char* id,
    int* value,
    int resetValue,
    int minValue,
    int maxValue,
    const char* format,
    float controlWidth) {
    bool localChanged = ImGuiExtras::NodeSliderInt(label, id, value, minValue, maxValue, format, controlWidth);
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (!Stack::Editor::RawControls::GraphSliderRightClickWasConsumed() &&
        state.lastHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        *value != resetValue) {
        *value = resetValue;
        localChanged = true;
    }
    return localChanged;
}

} // namespace

namespace Stack::Editor::DevelopManualRawControls {

ManualRawBasicControlResult RenderDevelopManualRawBasicControls(
    Raw::RawDevelopSettings& settings,
    const Raw::RawDevelopSettings& defaultSettings,
    const Raw::RawMetadata& metadata,
    Stack::EditorModuleTypes::RawDevelopExposureDraftState& exposureDraft,
    float controlWidth) {
    ManualRawBasicControlResult result;
    const std::array<float, 3> effectiveWb = Stack::Editor::RawControls::EffectiveWhiteBalance(metadata, settings);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW COLOR", 4.0f);
    const char* wbLabels[] = { "Camera WB", "Auto WB", "Neutral", "Manual" };
    int wbMode = static_cast<int>(settings.whiteBalanceMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("White Balance", &wbMode, wbLabels, 4)) {
        settings.whiteBalanceMode = static_cast<Raw::WhiteBalanceMode>(std::clamp(wbMode, 0, 3));
        result.changed = true;
    }
    result.changed |= ResettableDevelopSliderFloat("Red Mult", "##RawWbR", &settings.manualWhiteBalance[0], defaultSettings.manualWhiteBalance[0], 0.05f, 16.0f, "%.3f", controlWidth);
    result.changed |= ResettableDevelopSliderFloat("Green Mult", "##RawWbG", &settings.manualWhiteBalance[1], defaultSettings.manualWhiteBalance[1], 0.05f, 16.0f, "%.3f", controlWidth);
    result.changed |= ResettableDevelopSliderFloat("Blue Mult", "##RawWbB", &settings.manualWhiteBalance[2], defaultSettings.manualWhiteBalance[2], 0.05f, 16.0f, "%.3f", controlWidth);
    ImGui::TextDisabled("Effective WB RGB: %.3f %.3f %.3f", effectiveWb[0], effectiveWb[1], effectiveWb[2]);
    if (!metadata.whiteBalanceSource.empty()) {
        ImGui::TextDisabled("WB source: %s", metadata.whiteBalanceSource.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW EXPOSURE + RANGE", 4.0f);
    if (!exposureDraft.editing) {
        exposureDraft.exposureStops = settings.exposureStops;
    }
    const bool baselineExposureDraftChanged = ResettableDevelopSliderFloat(
        "RAW Exposure / EV",
        "##RawExposure",
        &exposureDraft.exposureStops,
        defaultSettings.exposureStops,
        -8.0f,
        8.0f,
        "%+.2f EV",
        controlWidth);
    const ImGuiExtras::NodeControlState& baselineExposureState = ImGuiExtras::GetNodeControlState();
    if (baselineExposureState.lastHovered) {
        ImGui::SetTooltip("+1 EV multiplies scene-linear values by 2 before Scene Prep, Finish Tone, and View Transform. -1 EV multiplies by 0.5.");
    }
    if (baselineExposureState.lastActive) {
        exposureDraft.editing = true;
        result.recordInteraction = true;
    } else if (exposureDraft.editing) {
        exposureDraft.editing = false;
        if (std::abs(exposureDraft.exposureStops - settings.exposureStops) > 0.0001f) {
            settings.exposureStops = exposureDraft.exposureStops;
            result.changed = true;
        }
    } else if (baselineExposureDraftChanged &&
               std::abs(exposureDraft.exposureStops - settings.exposureStops) > 0.0001f) {
        settings.exposureStops = exposureDraft.exposureStops;
        result.changed = true;
    }
    if (metadata.hasDngBaselineExposure) {
        ImGui::TextDisabled("DNG baseline exposure metadata: %+.2f EV", metadata.dngBaselineExposure);
    }
    ImGui::TextDisabled(
        "Scene-linear scale: x%.2f. Rendered brightness is shaped later by Scene Prep and Finish Tone.",
        std::exp2(settings.exposureStops));

    return result;
}

bool RenderDevelopManualRawAdvancedControls(
    Raw::RawDevelopSettings& settings,
    const Raw::RawDevelopSettings& defaultSettings,
    const Raw::RawMetadata& metadata,
    float controlWidth,
    bool showAdvancedControls) {
    bool changed = false;
    if (!showAdvancedControls) {
        ImGui::TextDisabled("Advanced RAW cleanup, exact scene-prep controls, foundation tone, and diagnostics are in the sidebar.");
        return changed;
    }

    const bool demosaicEnabled = metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer;
    const float effectiveBlack = settings.overrideBlackLevel ? settings.blackLevelOverride : metadata.blackLevel;
    const float requestedWhite = settings.overrideWhiteLevel ? settings.whiteLevelOverride : metadata.whiteLevel;
    const float effectiveWhite = std::max(effectiveBlack + 1.0f, requestedWhite);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW HIGHLIGHT RECONSTRUCTION", 4.0f);
    ImGui::TextDisabled("RAW-stage recovery before scene prep; separate from Scene Prep Highlight Guard.");
    int highlightMode = static_cast<int>(settings.highlightMode);
    const char* highlightLabels[] = { "Off", "Clip / Neutral", "Luminance", "Color Reconstruction" };
    ImGui::BeginDisabled(!demosaicEnabled);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Mode", &highlightMode, highlightLabels, 4)) {
        settings.highlightMode = static_cast<Raw::HighlightReconstructionMode>(std::clamp(highlightMode, 0, 3));
        changed = true;
    }
    changed |= ResettableDevelopSliderFloat("Strength", "##RawHighlightStrength", &settings.highlightStrength, defaultSettings.highlightStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderFloat("Clip Threshold", "##RawHighlightThreshold", &settings.highlightThreshold, defaultSettings.highlightThreshold, 0.8f, 1.0f, "%.3f", controlWidth);
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW DEMOSAIC", 4.0f);
    ImGui::BeginDisabled(!demosaicEnabled);
    ImGui::TextDisabled("Method: Fast / Bilinear (preview-safe)");
    ImGui::TextDisabled("Only Bilinear is selectable in this build; unsafe demosaic modes are forced back to Bilinear.");
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW INPUT LEVELS", 4.0f);
    changed |= ImGuiExtras::NodeCheckbox("Override Black", "##RawOverrideBlack", &settings.overrideBlackLevel, controlWidth);
    if (settings.overrideBlackLevel) {
        changed |= ImGuiExtras::NodeInputFloat("Black Level", "##RawBlackLevel", &settings.blackLevelOverride, 1.0f, 16.0f, "%.1f", controlWidth);
    }
    changed |= ImGuiExtras::NodeCheckbox("Override White", "##RawOverrideWhite", &settings.overrideWhiteLevel, controlWidth);
    if (settings.overrideWhiteLevel) {
        changed |= ImGuiExtras::NodeInputFloat("White Level", "##RawWhiteLevel", &settings.whiteLevelOverride, 16.0f, 256.0f, "%.1f", controlWidth);
    }
    ImGui::TextDisabled("Effective black/white: %.1f / %.1f", effectiveBlack, effectiveWhite);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW ORIENTATION", 4.0f);
    const char* rotationLabels[] = { "0 Degrees", "90 Degrees CW", "180 Degrees", "270 Degrees CW" };
    int rotationIdx = settings.rotationDegrees / 90;
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Manual Rotation", &rotationIdx, rotationLabels, 4)) {
        settings.rotationDegrees = std::clamp(rotationIdx, 0, 3) * 90;
        changed = true;
    }
    changed |= ImGuiExtras::NodeCheckbox("Stretch To Fit Frame", "##RawRotateToFitFrame", &settings.rotateToFitFrame, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Flip Horizontally", "##RawFlipHorizontally", &settings.flipHorizontally, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Flip Vertically", "##RawFlipVertically", &settings.flipVertically, controlWidth);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW CLEANUP", 4.0f);
    ImGui::BeginDisabled(!demosaicEnabled);
    changed |= ResettableDevelopSliderFloat("False Color Suppression", "##RawFalseColorSuppression", &settings.falseColorSuppression, defaultSettings.falseColorSuppression, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderFloat("Defringe Strength", "##RawDefringeStrength", &settings.defringeStrength, defaultSettings.defringeStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderFloat("Highlight Edge Cleanup", "##RawHighlightEdgeCleanup", &settings.highlightEdgeCleanup, defaultSettings.highlightEdgeCleanup, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderInt("Chroma Radius", "##RawChromaRadius", &settings.chromaRadius, defaultSettings.chromaRadius, 1, 3, "%d px", controlWidth);
    changed |= ResettableDevelopSliderFloat("Preserve Real Color", "##RawPreserveRealColor", &settings.preserveRealColor, defaultSettings.preserveRealColor, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderFloat("Red / Cyan CA", "##RawLateralRedCyan", &settings.lateralRedCyan, defaultSettings.lateralRedCyan, -8.0f, 8.0f, "%.2f px", controlWidth);
    changed |= ResettableDevelopSliderFloat("Blue / Yellow CA", "##RawLateralBlueYellow", &settings.lateralBlueYellow, defaultSettings.lateralBlueYellow, -8.0f, 8.0f, "%.2f px", controlWidth);
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW MOSAIC DENOISE", 4.0f);
    ImGui::BeginDisabled(!demosaicEnabled);
    changed |= ImGuiExtras::NodeCheckbox("Enable", "##RawMosaicDenoiseEnabled", &settings.mosaicDenoise.enabled, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Hot Pixel Suppression", "##RawMosaicHotPixels", &settings.mosaicDenoise.hotPixelSuppression, controlWidth);
    changed |= ResettableDevelopSliderFloat("Hot Pixel Threshold", "##RawMosaicHotThreshold", &settings.mosaicDenoise.hotPixelThreshold, defaultSettings.mosaicDenoise.hotPixelThreshold, 0.005f, 0.5f, "%.3f", controlWidth);
    changed |= ResettableDevelopSliderFloat("Luminance Strength", "##RawMosaicLumaStrength", &settings.mosaicDenoise.lumaStrength, defaultSettings.mosaicDenoise.lumaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderFloat("Chroma Strength", "##RawMosaicChromaStrength", &settings.mosaicDenoise.chromaStrength, defaultSettings.mosaicDenoise.chromaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderInt("Radius", "##RawMosaicRadius", &settings.mosaicDenoise.radius, defaultSettings.mosaicDenoise.radius, 1, 4, "%d CFA steps", controlWidth);
    changed |= ResettableDevelopSliderFloat("Edge Protection", "##RawMosaicEdgeProtection", &settings.mosaicDenoise.edgeProtection, defaultSettings.mosaicDenoise.edgeProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ResettableDevelopSliderInt("Iterations", "##RawMosaicIterations", &settings.mosaicDenoise.iterations, defaultSettings.mosaicDenoise.iterations, 1, 2, "%d", controlWidth);
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("CAMERA COLOR TRANSFORM", 4.0f);
    changed |= ImGuiExtras::NodeCheckbox("Camera Transform", "##RawCameraTransform", &settings.cameraTransformEnabled, controlWidth);
    const char* transformLabels[] = {
        "LibRaw rgb_cam",
        "DNG Auto (approx)",
        "DNG ForwardMatrix 1",
        "DNG ForwardMatrix 2",
        "DNG ColorMatrix inverse"
    };
    int transformSource = static_cast<int>(settings.cameraTransformSource);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Matrix Source", &transformSource, transformLabels, 5)) {
        settings.cameraTransformSource = static_cast<Raw::RawCameraTransformSource>(std::clamp(transformSource, 0, 4));
        changed = true;
    }
    changed |= ImGuiExtras::NodeCheckbox("Bypass Transform", "##RawBypassTransform", &settings.debugBypassCameraTransform, controlWidth);
    changed |= ImGuiExtras::NodeCheckbox("Matrix Transpose Debug", "##RawMatrixTranspose", &settings.debugTransposeCameraMatrix, controlWidth);
    if (!metadata.cameraMatrixSource.empty()) {
        ImGui::TextDisabled("Metadata preferred: %s", metadata.cameraMatrixSource.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("RAW DEBUG", 4.0f);
    const char* debugLabels[] = {
        "Final Output",
        "Normalized Mosaic",
        "CFA False Color",
        "Demosaiced Camera RGB",
        "White Balanced RGB",
        "Camera Transformed RGB",
        "Clipped RAW Channels",
        "Pre-Denoise Mosaic",
        "Post-Denoise Mosaic",
        "Hot Pixel Mask",
        "Denoise Difference",
        "False Color Mask",
        "Defringe Mask",
        "Highlight Edge Mask"
    };
    constexpr int debugLabelCount = static_cast<int>(sizeof(debugLabels) / sizeof(debugLabels[0]));
    int debugView = static_cast<int>(settings.debugView);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Debug View", &debugView, debugLabels, debugLabelCount)) {
        debugView = std::clamp(debugView, 0, debugLabelCount - 1);
        settings.debugView = static_cast<Raw::RawDebugView>(debugView);
        changed = true;
    }

    return changed;
}

} // namespace Stack::Editor::DevelopManualRawControls
