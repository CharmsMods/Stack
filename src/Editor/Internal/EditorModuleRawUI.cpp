#include "Editor/EditorModule.h"

#include "Editor/Layers/ToneLayers.h"
#include "NeuralDenoise/NeuralDenoiseManager.h"
#include "Raw/RawImageData.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

namespace {

std::string RawDisplayName(const EditorNodeGraph::RawSourcePayload& rawSource) {
    const std::string path = rawSource.sourcePath.empty()
        ? rawSource.metadata.sourcePath
        : rawSource.sourcePath;
    if (path.empty()) {
        return rawSource.label.empty() ? std::string("RAW") : rawSource.label;
    }
    try {
        const std::string filename = std::filesystem::path(path).filename().string();
        return filename.empty() ? path : filename;
    } catch (...) {
        return path;
    }
}

std::array<float, 3> EffectiveWhiteBalance(const Raw::RawMetadata& metadata, const Raw::RawDevelopSettings& settings) {
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual) {
        return settings.manualWhiteBalance;
    }
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Neutral) {
        return { 1.0f, 1.0f, 1.0f };
    }
    if (settings.whiteBalanceMode == Raw::WhiteBalanceMode::Auto) {
        const float r = std::max(0.001f, metadata.daylightWhiteBalance[0]);
        const float g = std::max(0.001f, metadata.daylightWhiteBalance[1]);
        const float b = std::max(0.001f, metadata.daylightWhiteBalance[2]);
        return { r / g, 1.0f, b / g };
    }

    const float r = std::max(0.001f, metadata.cameraWhiteBalance[0]);
    const float g = std::max(0.001f, metadata.cameraWhiteBalance[1]);
    const float b = std::max(0.001f, metadata.cameraWhiteBalance[2]);
    return { r / g, 1.0f, b / g };
}

bool SameRawMosaicDenoiseSettings(
    const Raw::RawMosaicDenoiseSettings& a,
    const Raw::RawMosaicDenoiseSettings& b) {
    return a.enabled == b.enabled &&
        a.hotPixelSuppression == b.hotPixelSuppression &&
        a.hotPixelThreshold == b.hotPixelThreshold &&
        a.lumaStrength == b.lumaStrength &&
        a.chromaStrength == b.chromaStrength &&
        a.radius == b.radius &&
        a.edgeProtection == b.edgeProtection &&
        a.iterations == b.iterations;
}

bool SameRawDevelopSettings(
    const Raw::RawDevelopSettings& a,
    const Raw::RawDevelopSettings& b) {
    return a.exposureStops == b.exposureStops &&
        a.whiteBalanceMode == b.whiteBalanceMode &&
        a.manualWhiteBalance == b.manualWhiteBalance &&
        a.overrideBlackLevel == b.overrideBlackLevel &&
        a.blackLevelOverride == b.blackLevelOverride &&
        a.overrideWhiteLevel == b.overrideWhiteLevel &&
        a.whiteLevelOverride == b.whiteLevelOverride &&
        a.highlightMode == b.highlightMode &&
        a.highlightStrength == b.highlightStrength &&
        a.highlightThreshold == b.highlightThreshold &&
        a.demosaicMethod == b.demosaicMethod &&
        a.cameraTransformEnabled == b.cameraTransformEnabled &&
        a.cameraTransformSource == b.cameraTransformSource &&
        a.debugBypassCameraTransform == b.debugBypassCameraTransform &&
        a.debugTransposeCameraMatrix == b.debugTransposeCameraMatrix &&
        a.debugView == b.debugView &&
        a.rotationDegrees == b.rotationDegrees &&
        a.rotateToFitFrame == b.rotateToFitFrame &&
        a.flipHorizontally == b.flipHorizontally &&
        a.flipVertically == b.flipVertically &&
        a.falseColorSuppression == b.falseColorSuppression &&
        a.defringeStrength == b.defringeStrength &&
        a.highlightEdgeCleanup == b.highlightEdgeCleanup &&
        a.chromaRadius == b.chromaRadius &&
        a.preserveRealColor == b.preserveRealColor &&
        a.lateralRedCyan == b.lateralRedCyan &&
        a.lateralBlueYellow == b.lateralBlueYellow &&
        SameRawMosaicDenoiseSettings(a.mosaicDenoise, b.mosaicDenoise);
}

bool SameRawDetailFusionSettings(
    const Raw::RawDetailFusionSettings& a,
    const Raw::RawDetailFusionSettings& b) {
    return a.mode == b.mode &&
        a.debugView == b.debugView &&
        a.autoSafetyEnabled == b.autoSafetyEnabled &&
        a.overrideMinEv == b.overrideMinEv &&
        a.overrideMaxEv == b.overrideMaxEv &&
        a.overrideBaseEv == b.overrideBaseEv &&
        a.overrideNoiseProtection == b.overrideNoiseProtection &&
        a.overrideHighlightProtection == b.overrideHighlightProtection &&
        a.overrideShadowLiftLimit == b.overrideShadowLiftLimit &&
        a.overrideWellExposedTarget == b.overrideWellExposedTarget &&
        a.minEvBias == b.minEvBias &&
        a.maxEvBias == b.maxEvBias &&
        a.baseEvBias == b.baseEvBias &&
        a.noiseProtectionBias == b.noiseProtectionBias &&
        a.highlightProtectionBias == b.highlightProtectionBias &&
        a.shadowLiftLimitBias == b.shadowLiftLimitBias &&
        a.wellExposedTargetBias == b.wellExposedTargetBias &&
        a.minEv == b.minEv &&
        a.maxEv == b.maxEv &&
        a.baseEv == b.baseEv &&
        a.strength == b.strength &&
        a.sampleCount == b.sampleCount &&
        a.baseRadiusPercent == b.baseRadiusPercent &&
        a.highlightProtection == b.highlightProtection &&
        a.shadowLiftLimit == b.shadowLiftLimit &&
        a.noiseProtection == b.noiseProtection &&
        a.detailWeight == b.detailWeight &&
        a.wellExposedTarget == b.wellExposedTarget &&
        a.smoothGradientProtection == b.smoothGradientProtection &&
        a.textureSensitivity == b.textureSensitivity &&
        a.skyBias == b.skyBias &&
        a.invertMask == b.invertMask &&
        a.maskBlackPoint == b.maskBlackPoint &&
        a.maskWhitePoint == b.maskWhitePoint &&
        a.maskGamma == b.maskGamma &&
        a.smoothnessRadius == b.smoothnessRadius &&
        a.smoothAreaRadius == b.smoothAreaRadius &&
        a.edgeAwareness == b.edgeAwareness &&
        a.haloGuard == b.haloGuard &&
        a.maskDebandDither == b.maskDebandDither &&
        a.manualBlend == b.manualBlend;
}

bool SameDevelopAutoGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b) {
    return a.intent == b.intent &&
        a.autoStrength == b.autoStrength &&
        a.exposureBias == b.exposureBias &&
        a.dynamicRange == b.dynamicRange &&
        a.shadowLift == b.shadowLift &&
        a.highlightGuard == b.highlightGuard &&
        a.highlightCharacter == b.highlightCharacter &&
        a.contrastBias == b.contrastBias &&
        a.subjectSceneBias == b.subjectSceneBias &&
        a.moodReadabilityBias == b.moodReadabilityBias;
}

bool SameDevelopSubjectImportanceRegion(
    const EditorNodeGraph::DevelopSubjectImportanceRegion& a,
    const EditorNodeGraph::DevelopSubjectImportanceRegion& b) {
    return a.id == b.id &&
        a.mode == b.mode &&
        a.enabled == b.enabled &&
        a.centerX == b.centerX &&
        a.centerY == b.centerY &&
        a.radiusX == b.radiusX &&
        a.radiusY == b.radiusY &&
        a.feather == b.feather &&
        a.strength == b.strength;
}

bool SameDevelopSubjectImportanceStroke(
    const EditorNodeGraph::DevelopSubjectImportanceStroke& a,
    const EditorNodeGraph::DevelopSubjectImportanceStroke& b) {
    if (a.id != b.id ||
        a.mode != b.mode ||
        a.enabled != b.enabled ||
        a.subtract != b.subtract ||
        a.radius != b.radius ||
        a.feather != b.feather ||
        a.strength != b.strength ||
        a.points.size() != b.points.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.points.size(); ++i) {
        if (a.points[i].x != b.points[i].x ||
            a.points[i].y != b.points[i].y) {
            return false;
        }
    }
    return true;
}

bool SameDevelopSubjectImportance(
    const EditorNodeGraph::DevelopSubjectImportanceMap& a,
    const EditorNodeGraph::DevelopSubjectImportanceMap& b) {
    if (a.schemaVersion != b.schemaVersion ||
        a.enabled != b.enabled ||
        a.showOverlay != b.showOverlay ||
        a.overlayOpacity != b.overlayOpacity ||
        a.showInterpretedMapOverlay != b.showInterpretedMapOverlay ||
        a.interpretedMapOpacity != b.interpretedMapOpacity ||
        a.showRefinedMapOverlay != b.showRefinedMapOverlay ||
        a.refinedMapOpacity != b.refinedMapOpacity ||
        a.brushEnabled != b.brushEnabled ||
        a.brushSubtract != b.brushSubtract ||
        a.brushMode != b.brushMode ||
        a.brushRadius != b.brushRadius ||
        a.brushFeather != b.brushFeather ||
        a.brushStrength != b.brushStrength ||
        a.activeRegionId != b.activeRegionId ||
        a.activeStrokeId != b.activeStrokeId ||
        a.nextRegionId != b.nextRegionId ||
        a.nextStrokeId != b.nextStrokeId ||
        a.regions.size() != b.regions.size() ||
        a.strokes.size() != b.strokes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        if (!SameDevelopSubjectImportanceRegion(a.regions[i], b.regions[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.strokes.size(); ++i) {
        if (!SameDevelopSubjectImportanceStroke(a.strokes[i], b.strokes[i])) {
            return false;
        }
    }
    return true;
}

bool SameRawDevelopPayload(
    const EditorNodeGraph::RawDevelopPayload& a,
    const EditorNodeGraph::RawDevelopPayload& b) {
    return SameRawDevelopSettings(a.settings, b.settings) &&
        a.scenePrepEnabled == b.scenePrepEnabled &&
        SameRawDetailFusionSettings(a.scenePrepSettings, b.scenePrepSettings) &&
        a.integratedToneEnabled == b.integratedToneEnabled &&
        a.integratedToneLayerJson == b.integratedToneLayerJson &&
        SameDevelopAutoGuidance(a.autoGuidance, b.autoGuidance) &&
        SameDevelopSubjectImportance(a.subjectImportance, b.subjectImportance) &&
        a.uiMode == b.uiMode;
}

nlohmann::json BuildDefaultIntegratedToneLayerJson() {
    ToneCurveLayer toneCurve;
    return toneCurve.Serialize();
}

ToneCurveLayer BuildIntegratedDevelopToneLayer(const nlohmann::json& layerJson) {
    ToneCurveLayer toneCurve;
    if (layerJson.is_object()) {
        toneCurve.Deserialize(layerJson);
    } else {
        toneCurve.Deserialize(BuildDefaultIntegratedToneLayerJson());
    }
    return toneCurve;
}

bool SameHdrMergeSettings(const Raw::HdrMergeSettings& a, const Raw::HdrMergeSettings& b) {
    return a.debugView == b.debugView &&
        a.alignmentMode == b.alignmentMode &&
        a.exposureMode == b.exposureMode &&
        a.referenceMode == b.referenceMode &&
        a.deghostMode == b.deghostMode &&
        a.motionPriority == b.motionPriority &&
        a.manualExposureEv[0] == b.manualExposureEv[0] &&
        a.manualExposureEv[1] == b.manualExposureEv[1] &&
        a.manualExposureEv[2] == b.manualExposureEv[2] &&
        a.exposureOffsetEv[0] == b.exposureOffsetEv[0] &&
        a.exposureOffsetEv[1] == b.exposureOffsetEv[1] &&
        a.exposureOffsetEv[2] == b.exposureOffsetEv[2] &&
        a.autoReliability == b.autoReliability &&
        a.clipThreshold == b.clipThreshold &&
        a.clipFeather == b.clipFeather &&
        a.blackThreshold == b.blackThreshold &&
        a.blackFeather == b.blackFeather &&
        a.readNoise == b.readNoise &&
        a.noiseAware == b.noiseAware;
}

void ClampHdrMergeSettings(Raw::HdrMergeSettings& settings) {
    for (float& ev : settings.manualExposureEv) {
        ev = std::clamp(ev, -12.0f, 12.0f);
    }
    for (float& ev : settings.exposureOffsetEv) {
        ev = std::clamp(ev, -4.0f, 4.0f);
    }
    settings.clipThreshold = std::clamp(settings.clipThreshold, 0.50f, 4.0f);
    settings.clipFeather = std::clamp(settings.clipFeather, 0.001f, 1.0f);
    settings.blackThreshold = std::clamp(settings.blackThreshold, 0.0f, 0.25f);
    settings.blackFeather = std::clamp(settings.blackFeather, 0.001f, 0.50f);
    settings.readNoise = std::clamp(settings.readNoise, 0.0f, 0.10f);
}

const char* HdrMergeDebugViewLabel(Raw::HdrMergeDebugView view) {
    switch (view) {
        case Raw::HdrMergeDebugView::Contribution: return "Contribution";
        case Raw::HdrMergeDebugView::Clipping: return "Clipping";
        case Raw::HdrMergeDebugView::NoiseLimited: return "Noise / Black Limited";
        case Raw::HdrMergeDebugView::AlignmentConfidence: return "Alignment Confidence";
        case Raw::HdrMergeDebugView::MotionMask: return "Motion Mask";
        case Raw::HdrMergeDebugView::RejectedSamples: return "Rejected Samples";
        case Raw::HdrMergeDebugView::FinalImage:
        default:
            return "Final Image";
    }
}

const char* HdrMergeAlignmentModeLabel(Raw::HdrMergeAlignmentMode mode) {
    switch (mode) {
        case Raw::HdrMergeAlignmentMode::Translation: return "Translation";
        case Raw::HdrMergeAlignmentMode::WideTranslation: return "Wide Translation";
        case Raw::HdrMergeAlignmentMode::Off:
        default:
            return "Off";
    }
}

const char* HdrMergeExposureModeLabel(Raw::HdrMergeExposureMode mode) {
    switch (mode) {
        case Raw::HdrMergeExposureMode::Manual: return "Manual";
        case Raw::HdrMergeExposureMode::Metadata:
        default:
            return "Metadata";
    }
}

const char* HdrMergeReferenceModeLabel(Raw::HdrMergeReferenceMode mode) {
    switch (mode) {
        case Raw::HdrMergeReferenceMode::Frame1: return "Frame 1";
        case Raw::HdrMergeReferenceMode::Frame2: return "Frame 2";
        case Raw::HdrMergeReferenceMode::Frame3: return "Frame 3";
        case Raw::HdrMergeReferenceMode::Auto:
        default:
            return "Auto";
    }
}

const char* HdrMergeDeghostModeLabel(Raw::HdrMergeDeghostMode mode) {
    switch (mode) {
        case Raw::HdrMergeDeghostMode::Off: return "Off";
        case Raw::HdrMergeDeghostMode::Low: return "Low";
        case Raw::HdrMergeDeghostMode::High: return "High";
        case Raw::HdrMergeDeghostMode::Medium:
        default:
            return "Medium";
    }
}

const char* HdrMergeMotionPriorityLabel(Raw::HdrMergeMotionPriority mode) {
    switch (mode) {
        case Raw::HdrMergeMotionPriority::AverageCleanAreas: return "Blend Static Consensus";
        case Raw::HdrMergeMotionPriority::PreserveReference:
        default:
            return "Prefer Reference";
    }
}

bool g_SuppressNextAutoGainHelpPopup = false;

bool ApplyResettableSliderFloat(float* value, float resetValue, bool changed) {
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (!state.lastHovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return changed;
    }
    g_SuppressNextAutoGainHelpPopup = true;
    if (std::abs(*value - resetValue) <= 0.0001f) {
        return changed;
    }
    *value = resetValue;
    return true;
}

bool ApplyResettableSliderInt(int* value, int resetValue, bool changed) {
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (!state.lastHovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return changed;
    }
    g_SuppressNextAutoGainHelpPopup = true;
    if (*value == resetValue) {
        return changed;
    }
    *value = resetValue;
    return true;
}

bool TryAutoGainDefaultFloat(const char* key, float& outValue) {
    const Raw::RawDetailFusionSettings defaults;
    const std::string value = key ? key : "";
    if (value == "MinEvBias" || value == "HighlightCompression") { outValue = defaults.minEvBias; return true; }
    if (value == "MaxEvBias" || value == "ShadowLift") { outValue = defaults.maxEvBias; return true; }
    if (value == "BaseEvBias" || value == "Baseline") { outValue = defaults.baseEvBias; return true; }
    if (value == "NoiseBias" || value == "ProtectNoiseBias") { outValue = defaults.noiseProtectionBias; return true; }
    if (value == "HighlightBias" || value == "ProtectHighlightBias") { outValue = defaults.highlightProtectionBias; return true; }
    if (value == "ShadowBias" || value == "ProtectShadowBias") { outValue = defaults.shadowLiftLimitBias; return true; }
    if (value == "TargetBias") { outValue = defaults.wellExposedTargetBias; return true; }
    if (value == "MinEv" || value == "MinEvOverride") { outValue = defaults.minEv; return true; }
    if (value == "MaxEv" || value == "MaxEvOverride") { outValue = defaults.maxEv; return true; }
    if (value == "BaseEv" || value == "BaseEvOverride") { outValue = defaults.baseEv; return true; }
    if (value == "Strength") { outValue = defaults.strength; return true; }
    if (value == "BaseRadius") { outValue = defaults.baseRadiusPercent; return true; }
    if (value == "HighlightProtection") { outValue = defaults.highlightProtection; return true; }
    if (value == "ShadowLimit") { outValue = defaults.shadowLiftLimit; return true; }
    if (value == "NoiseProtection") { outValue = defaults.noiseProtection; return true; }
    if (value == "Target") { outValue = defaults.wellExposedTarget; return true; }
    if (value == "DetailWeight") { outValue = defaults.detailWeight; return true; }
    if (value == "SmoothGradient") { outValue = defaults.smoothGradientProtection; return true; }
    if (value == "TextureSensitivity") { outValue = defaults.textureSensitivity; return true; }
    if (value == "SkyBias") { outValue = defaults.skyBias; return true; }
    if (value == "EdgeAwareness") { outValue = defaults.edgeAwareness; return true; }
    if (value == "HaloGuard") { outValue = defaults.haloGuard; return true; }
    if (value == "Dither") { outValue = defaults.maskDebandDither; return true; }
    if (value == "MaskBlack") { outValue = defaults.maskBlackPoint; return true; }
    if (value == "MaskWhite") { outValue = defaults.maskWhitePoint; return true; }
    if (value == "MaskGamma") { outValue = defaults.maskGamma; return true; }
    if (value == "ManualBlend") { outValue = defaults.manualBlend; return true; }
    return false;
}

bool TryAutoGainDefaultInt(const char* key, int& outValue) {
    const Raw::RawDetailFusionSettings defaults;
    const std::string value = key ? key : "";
    if (value == "Samples") { outValue = defaults.sampleCount; return true; }
    if (value == "SmoothRadius") { outValue = defaults.smoothnessRadius; return true; }
    if (value == "SmoothAreaRadius") { outValue = defaults.smoothAreaRadius; return true; }
    return false;
}

const char* AutoGainHelpBody(const char* key) {
    const std::string value = key ? key : "";
    if (value == "AutoSafety") return "Uses image statistics to keep local exposure shaping inside natural EV bounds with highlight, noise, shadow, and target-brightness protection. Keep this on for normal RAW work.";
    if (value == "OverrideMinEv") return "Stops Auto Safety from choosing the highlight darkening range. Use this only when you want exact local highlight compression control instead of scene-adaptive protection.";
    if (value == "MinEvBias") return "Nudges the automatic highlight/darkening range inside the natural safety envelope. Lower values allow stronger darkening/protection in bright regions. Higher values reduce highlight intervention.";
    if (value == "MinEvOverride") return "Absolute minimum EV used by the gain map. More negative values protect or darken highlights more, but can flatten bright clouds, windows, sunsets, and other luminous areas.";
    if (value == "MinEv") return "Manual minimum EV used by the gain map when Auto Safety is off. More negative values give the algorithm permission to darken bright areas more aggressively.";
    if (value == "OverrideMaxEv") return "Stops Auto Safety from choosing the shadow lift range. Use this when you need exact local lift limits instead of automatic noise-aware limits.";
    if (value == "MaxEvBias") return "Nudges the automatic shadow/detail lift inside the natural safety envelope. Higher values allow more lift. Lower values reduce noise, banding, and crunchy shadow artifacts.";
    if (value == "MaxEvOverride") return "Absolute maximum EV the mask may apply. High values can reveal deep detail, but can also turn noise, hot pixels, or compression into visible texture.";
    if (value == "MaxEv") return "Manual maximum EV used by the gain map when Auto Safety is off. Use carefully: this is the main permission for shadow lifting.";
    if (value == "OverrideBaseEv") return "Stops Auto Safety from choosing global midtone placement. Use this if you want the node's baseline brightness to be fully manual.";
    if (value == "BaseEvBias") return "Shifts the Scene Prep local exposure field brighter or darker without changing its relative mask structure. It sits after RAW exposure and before Prepared/Finish tone.";
    if (value == "BaseEvOverride") return "Absolute baseline exposure added to the generated field. Use carefully because it can make Pre-Local Exposure behave like normal exposure compensation.";
    if (value == "BaseEv") return "Manual baseline EV when Auto Safety is off. Positive values brighten the whole generated field; negative values darken it.";
    if (value == "Strength") return "Amount of Scene Prep local exposure. Use it to decide how strongly the RAW-developed image is prepared before Prepared Tone and the Final Graph.";
    if (value == "Preset") return "Applies a safe group of Pre-Local Exposure settings. Presets are starting points for local exposure shaping, not persistent modes or HDR reconstruction.";
    if (value == "Samples") return "Quality of the base exposure analysis. Higher values inspect more neighboring samples and can be smoother, but interaction is heavier.";
    if (value == "BaseRadius") return "Scale of the base exposure layer as a percent of the longer image edge. Higher values make broader, smoother local exposure decisions; lower values follow smaller regions but can look busier.";
    if (value == "OverrideHighlight") return "Makes highlight protection manual. Leave off when you want Auto Safety to react to clipped or saturated bright regions.";
    if (value == "HighlightBias") return "Nudges Scene Prep highlight protection after RAW highlight recovery. Raise it if bright skies, windows, powerlines, or saturated colors look harsh. Lower it if highlights become too muted.";
    if (value == "HighlightProtection") return "Absolute protection against clipping and saturated/channel-limited highlights. Higher reduces harsh bright edges but may reduce sparkle or local contrast.";
    if (value == "OverrideShadow") return "Makes shadow lift limiting manual. Leave off for normal RAW work so noisy shadows can be protected automatically.";
    if (value == "ShadowBias") return "Nudges how much Scene Prep is allowed to raise dark areas. Lower it if shadows get noisy. Raise it if shadows stay too heavy.";
    if (value == "ShadowLimit") return "Absolute limiter for lifting very dark pixels. Higher is more conservative against noise; lower allows more aggressive shadow brightening.";
    if (value == "OverrideNoise") return "Makes noise protection manual. Leave off when you want the node to estimate whether dark texture is real detail or mostly noise.";
    if (value == "NoiseBias") return "Nudges Scene Prep noise protection. Raise it if grain or chroma noise becomes detail. Lower it if real low-light texture is being suppressed.";
    if (value == "NoiseProtection") return "Absolute protection against treating noise as useful detail. Higher is cleaner but can soften fine low-light detail.";
    if (value == "OverrideTarget") return "Makes the well-exposed target manual. Leave off if you want Auto Safety to lower the target in highlight-heavy or noisy scenes.";
    if (value == "TargetBias") return "Nudges automatic midtone target brighter or darker. Raise for a brighter base edit; lower for darker mood or stronger highlight preservation.";
    if (value == "Target") return "Scene-linear luminance target used by the base exposure curve. Higher makes the algorithm prefer brighter results. Lower preserves darker mood and highlight headroom.";
    if (value == "DetailWeight") return "How much real texture and edges influence sample choice. Higher preserves foliage and texture. Lower gives smoother, cleaner tonal blending.";
    if (value == "SmoothGradient") return "Protects skies, fog, walls, and smooth ramps from banding or detail exaggeration. Raise it if skies show contouring; lower it if smooth areas become too flat.";
    if (value == "TextureSensitivity") return "Controls how easily small contrast is classified as real texture. Higher preserves fine detail but may catch noise or sky bands. Lower is cleaner.";
    if (value == "SkyBias") return "Adds a preference for treating sky-like smooth regions as gradients instead of detail. Keep moderate or high for sunsets, fog, and skies.";
    if (value == "SmoothRadius") return "Base smoothing radius for the EV map. Higher reduces speckle and harsh transitions; too high can cause halos or mushy local contrast.";
    if (value == "SmoothAreaRadius") return "Extra smoothing only in smooth-gradient regions. Useful for skies and walls. It should not smear tree lines when edge protection is working.";
    if (value == "EdgeAwareness") return "Controls how strongly smoothing avoids crossing edges. Higher protects silhouettes; too high can preserve colored zipper/fringe artifacts instead of blending them away.";
    if (value == "HaloGuard") return "Suppresses bright/dark transition halos around trees, horizons, powerlines, and buildings. Raise it when gain bleeds around high-contrast edges.";
    if (value == "Dither") return "Adds tiny mask dithering in banding-risk areas. Use as a last resort for visible banding; keep off or very low for clean RAW work.";
    if (value == "Diagnostics") return "Chooses what the viewport preview shows when Pre-Local Exposure preview is active. These diagnostic views explain local exposure shaping; they do not imply true recovery from missing highlight data.";
    if (value == "InvertMask") return "Flips an external or hybrid correction mask only. It does not invert the auto analysis itself.";
    if (value == "MaskBlack") return "Remaps a connected hybrid mask's dark endpoint. Raise it to make more of the mask count as black/no correction.";
    if (value == "MaskWhite") return "Remaps a connected hybrid mask's bright endpoint. Lower it to make more of the mask reach full correction.";
    if (value == "MaskGamma") return "Changes hybrid mask midtone response. Values above 1 push midtones darker; values below 1 make midtones more active.";
    if (value == "ManualBlend") return "Blends an external or hybrid mask correction into the generated auto field. 0 keeps the auto field; 1 follows the shaped mask.";
    return "This Pre-Local Exposure control affects how the generated exposure map is built or applied. Right-click other controls for more specific guidance.";
}

void RenderAutoGainHelpPopup(const char* key, const char* title) {
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    const std::string popupId = std::string("AutoGainHelp_") + (key ? key : "Unknown");
    if (g_SuppressNextAutoGainHelpPopup) {
        g_SuppressNextAutoGainHelpPopup = false;
    } else if (state.lastHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(popupId.c_str());
    }
    if (ImGui::BeginPopup(popupId.c_str())) {
        ImGui::TextUnformatted(title ? title : "Pre-Local Exposure Control");
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextWrapped("%s", AutoGainHelpBody(key));
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void RenderAutoGainItemHelpPopup(const char* key, const char* title) {
    const std::string popupId = std::string("AutoGainHelp_") + (key ? key : "Unknown");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(popupId.c_str());
    }
    if (ImGui::BeginPopup(popupId.c_str())) {
        ImGui::TextUnformatted(title ? title : "Pre-Local Exposure Control");
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextWrapped("%s", AutoGainHelpBody(key));
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

bool AutoGainSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float minValue,
    float maxValue,
    const char* format,
    float controlWidth,
    const char* helpKey) {
    bool changed = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
    float resetValue = 0.0f;
    if (TryAutoGainDefaultFloat(helpKey, resetValue)) {
        changed = ApplyResettableSliderFloat(value, resetValue, changed);
    }
    RenderAutoGainHelpPopup(helpKey, label);
    return changed;
}

bool AutoGainSliderInt(
    const char* label,
    const char* id,
    int* value,
    int minValue,
    int maxValue,
    const char* format,
    float controlWidth,
    const char* helpKey) {
    bool changed = ImGuiExtras::NodeSliderInt(label, id, value, minValue, maxValue, format, controlWidth);
    int resetValue = 0;
    if (TryAutoGainDefaultInt(helpKey, resetValue)) {
        changed = ApplyResettableSliderInt(value, resetValue, changed);
    }
    RenderAutoGainHelpPopup(helpKey, label);
    return changed;
}

bool AutoGainCheckbox(
    const char* label,
    const char* id,
    bool* value,
    float controlWidth,
    const char* helpKey) {
    const bool changed = ImGuiExtras::NodeCheckbox(label, id, value, controlWidth);
    RenderAutoGainHelpPopup(helpKey, label);
    return changed;
}

bool AutoGainCombo(
    const char* label,
    int* current,
    const char* const items[],
    int itemCount,
    float controlWidth,
    const char* helpKey) {
    ImGui::SetNextItemWidth(controlWidth);
    const bool changed = ImGui::Combo(label, current, items, itemCount);
    RenderAutoGainItemHelpPopup(helpKey, label);
    return changed;
}

float AutoGainClampedMetric(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float AutoGainEffectiveMetric(float base, float bias, bool overrideValue, bool inverseBias = false) {
    const float direction = inverseBias ? -1.0f : 1.0f;
    return overrideValue ? AutoGainClampedMetric(base) : AutoGainClampedMetric(base + bias * direction * 0.35f);
}

float AutoGainEffectiveEv(float base, float bias, bool overrideValue) {
    return overrideValue ? base : base + bias;
}

void DrawAutoGainMeter(const char* label, float value, float width) {
    const float clamped = AutoGainClampedMetric(value);
    ImGui::TextDisabled("%s  %.0f%%", label, clamped * 100.0f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(80.0f, width), 8.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), IM_COL32(40, 68, 76, 210), 4.0f);
    drawList->AddRectFilled(start, ImVec2(start.x + size.x * clamped, start.y + size.y), IM_COL32(76, 168, 214, 235), 4.0f);
    ImGui::Dummy(ImVec2(size.x, size.y + 4.0f));
}

void RenderAutoGainSafetyReadout(const Raw::RawDetailFusionSettings& settings, float controlWidth) {
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("AUTO SAFETY READOUT", 4.0f);
    if (!settings.autoSafetyEnabled) {
        ImGui::TextWrapped("Automatic analysis is off. The readout below shows your manual range, but image-statistics recommendations are bypassed.");
    } else {
        ImGui::TextWrapped("Live image statistics drive the render path. This panel previews the active control shape and safety tendencies.");
    }

    const float minEv = std::clamp(AutoGainEffectiveEv(settings.minEv, settings.minEvBias, settings.overrideMinEv), -2.5f, 0.5f);
    const float maxEv = std::clamp(std::max(minEv + 0.01f, AutoGainEffectiveEv(settings.maxEv, settings.maxEvBias, settings.overrideMaxEv)), std::max(0.25f, minEv + 0.01f), 2.5f);
    const float baseEv = std::clamp(AutoGainEffectiveEv(settings.baseEv, settings.baseEvBias, settings.overrideBaseEv), -1.0f, 1.0f);
    const float span = std::max(0.01f, maxEv - minEv);
    std::array<float, 64> evWindow{};
    std::array<float, 64> exposureCurve{};
    for (int i = 0; i < static_cast<int>(evWindow.size()); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(evWindow.size() - 1);
        const float ev = -2.5f + t * 5.0f;
        const float inside = (ev >= minEv && ev <= maxEv) ? 1.0f : 0.0f;
        const float center = std::clamp((baseEv - minEv) / span, 0.0f, 1.0f);
        const float distance = std::abs(t - center);
        evWindow[i] = inside * (0.35f + 0.65f * std::exp(-distance * distance * 42.0f));
        exposureCurve[i] = std::exp(-std::pow((ev - baseEv) * 0.32f, 2.0f)) * inside;
    }

    ImGui::TextDisabled("Natural EV window: %.2f to %.2f, base %.2f, strength %.2f", minEv, maxEv, baseEv, settings.strength);
    ImGui::PlotHistogram("EV Range Preview", evWindow.data(), static_cast<int>(evWindow.size()), 0, nullptr, 0.0f, 1.0f, ImVec2(controlWidth, 48.0f));
    ImGui::PlotLines("Sample Preference", exposureCurve.data(), static_cast<int>(exposureCurve.size()), 0, nullptr, 0.0f, 1.0f, ImVec2(controlWidth, 42.0f));

    const float effectiveNoise = AutoGainEffectiveMetric(settings.noiseProtection, settings.noiseProtectionBias, settings.overrideNoiseProtection);
    const float effectiveHighlight = AutoGainEffectiveMetric(settings.highlightProtection, settings.highlightProtectionBias, settings.overrideHighlightProtection);
    const float effectiveShadowLift = AutoGainEffectiveMetric(settings.shadowLiftLimit, settings.shadowLiftLimitBias, settings.overrideShadowLiftLimit, true);
    const float cleanShadowSafety = std::clamp(effectiveNoise * 0.70f + effectiveShadowLift * 0.30f, 0.0f, 1.0f);
    const float highlightHeadroom = effectiveHighlight;
    const float channelSaturation = std::clamp(effectiveHighlight * 0.70f + settings.smoothGradientProtection * 0.30f, 0.0f, 1.0f);
    const float textureConfidence = std::clamp(settings.detailWeight * settings.textureSensitivity * (1.0f - effectiveNoise * 0.35f), 0.0f, 1.0f);
    DrawAutoGainMeter("Noise Floor / SNR Safety", cleanShadowSafety, controlWidth);
    DrawAutoGainMeter("Highlight Headroom", highlightHeadroom, controlWidth);
    DrawAutoGainMeter("Channel Saturation Guard", channelSaturation, controlWidth);
    DrawAutoGainMeter("Texture Confidence", textureConfidence, controlWidth);

    std::array<float, 48> maskUse{};
    for (int i = 0; i < static_cast<int>(maskUse.size()); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(maskUse.size() - 1);
        const float darkLift = std::exp(-std::pow((t - 0.72f) * 3.0f, 2.0f)) * (1.0f - effectiveNoise * 0.35f);
        const float highlightProtect = std::exp(-std::pow((t - 0.20f) * 3.8f, 2.0f)) * effectiveHighlight * 0.75f;
        maskUse[i] = std::clamp((darkLift + highlightProtect) * settings.strength, 0.0f, 1.0f);
    }
    ImGui::PlotHistogram("EV Mask Use Preview", maskUse.data(), static_cast<int>(maskUse.size()), 0, nullptr, 0.0f, 1.0f, ImVec2(controlWidth, 42.0f));
}

void ClampRawDetailFusionSettings(Raw::RawDetailFusionSettings& settings) {
    settings.minEv = std::clamp(settings.minEv, -2.5f, 0.5f);
    settings.maxEv = std::clamp(settings.maxEv, std::max(settings.minEv + 0.01f, 0.25f), 2.5f);
    settings.baseEv = std::clamp(settings.baseEv, -1.0f, 1.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.baseRadiusPercent = std::clamp(settings.baseRadiusPercent, 0.002f, 0.030f);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.10f, 0.55f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = std::clamp(settings.manualBlend, 0.0f, 1.0f);
}

void NormalizeIntegratedScenePrepSettings(Raw::RawDetailFusionSettings& settings) {
    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
    settings.invertMask = false;
    settings.maskBlackPoint = 0.0f;
    settings.maskWhitePoint = 1.0f;
    settings.maskGamma = 1.0f;
    settings.manualBlend = 0.0f;
    ClampRawDetailFusionSettings(settings);
}

bool RenderAutoGainExposureControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool includeStrength) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    changed |= AutoGainCheckbox("Auto Safety", ("##" + prefix + "AutoSafety").c_str(), &settings.autoSafetyEnabled, controlWidth, "AutoSafety");
    if (settings.autoSafetyEnabled) {
        ImGui::TextWrapped("Auto Safety reads the connected image and derives safe EV bounds, midtone placement, highlight protection, and noise limits. Sliders below are offsets unless an override is enabled.");

        changed |= AutoGainCheckbox("Override Min EV", ("##" + prefix + "OverrideMinEv").c_str(), &settings.overrideMinEv, controlWidth, "OverrideMinEv");
        if (settings.overrideMinEv) {
            changed |= AutoGainSliderFloat("Min EV Override", ("##" + prefix + "MinEv").c_str(), &settings.minEv, -2.5f, 0.5f, "%.2f EV", controlWidth, "MinEvOverride");
        } else {
            changed |= AutoGainSliderFloat("Min EV Bias", ("##" + prefix + "MinEvBias").c_str(), &settings.minEvBias, -2.0f, 2.0f, "%+.2f EV", controlWidth, "MinEvBias");
        }

        changed |= AutoGainCheckbox("Override Max EV", ("##" + prefix + "OverrideMaxEv").c_str(), &settings.overrideMaxEv, controlWidth, "OverrideMaxEv");
        if (settings.overrideMaxEv) {
            changed |= AutoGainSliderFloat("Max EV Override", ("##" + prefix + "MaxEv").c_str(), &settings.maxEv, 0.25f, 2.5f, "%.2f EV", controlWidth, "MaxEvOverride");
        } else {
            changed |= AutoGainSliderFloat("Max EV Bias", ("##" + prefix + "MaxEvBias").c_str(), &settings.maxEvBias, -2.0f, 2.0f, "%+.2f EV", controlWidth, "MaxEvBias");
        }

        changed |= AutoGainCheckbox("Override Base EV", ("##" + prefix + "OverrideBaseEv").c_str(), &settings.overrideBaseEv, controlWidth, "OverrideBaseEv");
        if (settings.overrideBaseEv) {
            changed |= AutoGainSliderFloat("Base EV Override", ("##" + prefix + "BaseEv").c_str(), &settings.baseEv, -1.0f, 1.0f, "%.2f EV", controlWidth, "BaseEvOverride");
        } else {
            changed |= AutoGainSliderFloat("Base EV Bias", ("##" + prefix + "BaseEvBias").c_str(), &settings.baseEvBias, -1.25f, 1.25f, "%+.2f EV", controlWidth, "BaseEvBias");
        }
    } else {
        changed |= AutoGainSliderFloat("Min EV", ("##" + prefix + "MinEv").c_str(), &settings.minEv, -2.5f, 0.5f, "%.2f EV", controlWidth, "MinEv");
        changed |= AutoGainSliderFloat("Max EV", ("##" + prefix + "MaxEv").c_str(), &settings.maxEv, 0.25f, 2.5f, "%.2f EV", controlWidth, "MaxEv");
        changed |= AutoGainSliderFloat("Base EV", ("##" + prefix + "BaseEv").c_str(), &settings.baseEv, -1.0f, 1.0f, "%.2f EV", controlWidth, "BaseEv");
    }
    if (includeStrength) {
        changed |= AutoGainSliderFloat("Strength", ("##" + prefix + "Strength").c_str(), &settings.strength, 0.0f, 1.25f, "%.2f", controlWidth, "Strength");
    }
    changed |= AutoGainSliderInt("Base Samples", ("##" + prefix + "Samples").c_str(), &settings.sampleCount, 3, 33, "%d", controlWidth, "Samples");
    changed |= AutoGainSliderFloat("Base Radius", ("##" + prefix + "BaseRadius").c_str(), &settings.baseRadiusPercent, 0.002f, 0.030f, "%.3f", controlWidth, "BaseRadius");
    const float minAllowedMaxEv = std::max(0.25f, settings.minEv + 0.01f);
    if (settings.maxEv < minAllowedMaxEv) {
        settings.maxEv = minAllowedMaxEv;
        changed = true;
    }
    return changed;
}

bool RenderAutoGainAnalysisControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    if (settings.autoSafetyEnabled) {
        changed |= AutoGainCheckbox("Override Highlight", ("##" + prefix + "OverrideHighlight").c_str(), &settings.overrideHighlightProtection, controlWidth, "OverrideHighlight");
        if (settings.overrideHighlightProtection) {
            changed |= AutoGainSliderFloat("Highlight Protection", ("##" + prefix + "Highlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth, "HighlightProtection");
        } else {
            changed |= AutoGainSliderFloat("Highlight Bias", ("##" + prefix + "HighlightBias").c_str(), &settings.highlightProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth, "HighlightBias");
        }

        changed |= AutoGainCheckbox("Override Shadow Limit", ("##" + prefix + "OverrideShadow").c_str(), &settings.overrideShadowLiftLimit, controlWidth, "OverrideShadow");
        if (settings.overrideShadowLiftLimit) {
            changed |= AutoGainSliderFloat("Shadow Lift Limit", ("##" + prefix + "ShadowLimit").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth, "ShadowLimit");
        } else {
            changed |= AutoGainSliderFloat("Shadow Lift Bias", ("##" + prefix + "ShadowBias").c_str(), &settings.shadowLiftLimitBias, -1.0f, 1.0f, "%+.2f", controlWidth, "ShadowBias");
        }

        changed |= AutoGainCheckbox("Override Noise", ("##" + prefix + "OverrideNoise").c_str(), &settings.overrideNoiseProtection, controlWidth, "OverrideNoise");
        if (settings.overrideNoiseProtection) {
            changed |= AutoGainSliderFloat("Noise Protection", ("##" + prefix + "Noise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth, "NoiseProtection");
        } else {
            changed |= AutoGainSliderFloat("Noise Bias", ("##" + prefix + "NoiseBias").c_str(), &settings.noiseProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth, "NoiseBias");
        }

        changed |= AutoGainCheckbox("Override Target", ("##" + prefix + "OverrideTarget").c_str(), &settings.overrideWellExposedTarget, controlWidth, "OverrideTarget");
        if (settings.overrideWellExposedTarget) {
            changed |= AutoGainSliderFloat("Well-Exposed Target", ("##" + prefix + "Target").c_str(), &settings.wellExposedTarget, 0.10f, 0.55f, "%.2f", controlWidth, "Target");
        } else {
            changed |= AutoGainSliderFloat("Target Bias", ("##" + prefix + "TargetBias").c_str(), &settings.wellExposedTargetBias, -1.0f, 1.0f, "%+.2f", controlWidth, "TargetBias");
        }
    } else {
        changed |= AutoGainSliderFloat("Highlight Protection", ("##" + prefix + "Highlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth, "HighlightProtection");
        changed |= AutoGainSliderFloat("Shadow Lift Limit", ("##" + prefix + "ShadowLimit").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth, "ShadowLimit");
        changed |= AutoGainSliderFloat("Noise Protection", ("##" + prefix + "Noise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth, "NoiseProtection");
        changed |= AutoGainSliderFloat("Well-Exposed Target", ("##" + prefix + "Target").c_str(), &settings.wellExposedTarget, 0.10f, 0.55f, "%.2f", controlWidth, "Target");
    }
    changed |= AutoGainSliderFloat("Detail Weight", ("##" + prefix + "DetailWeight").c_str(), &settings.detailWeight, 0.0f, 1.0f, "%.2f", controlWidth, "DetailWeight");
    return changed;
}

enum class AutoGainPreset {
    Natural,
    ShadowDetail,
    HighlightControl,
    InteriorWindow,
    Developer
};

const char* AutoGainPresetName(AutoGainPreset preset) {
    switch (preset) {
        case AutoGainPreset::Natural: return "Natural";
        case AutoGainPreset::ShadowDetail: return "Shadow Detail";
        case AutoGainPreset::HighlightControl: return "Highlight Control";
        case AutoGainPreset::InteriorWindow: return "Interior Window";
        case AutoGainPreset::Developer: return "Developer";
    }
    return "Natural";
}

void ApplyAutoGainPreset(Raw::RawDetailFusionSettings& settings, AutoGainPreset preset, bool includeStrength) {
    const Raw::RawDetailFusionMode mode = settings.mode;
    const Raw::RawDetailFusionDebugView debugView = settings.debugView;
    const bool autoSafetyEnabled = settings.autoSafetyEnabled;
    const bool overrideMinEv = settings.overrideMinEv;
    const bool overrideMaxEv = settings.overrideMaxEv;
    const bool overrideBaseEv = settings.overrideBaseEv;
    const bool overrideNoiseProtection = settings.overrideNoiseProtection;
    const bool overrideHighlightProtection = settings.overrideHighlightProtection;
    const bool overrideShadowLiftLimit = settings.overrideShadowLiftLimit;
    const bool overrideWellExposedTarget = settings.overrideWellExposedTarget;
    const bool invertMask = settings.invertMask;
    const float maskBlackPoint = settings.maskBlackPoint;
    const float maskWhitePoint = settings.maskWhitePoint;
    const float maskGamma = settings.maskGamma;
    const float manualBlend = settings.manualBlend;

    settings = Raw::RawDetailFusionSettings{};
    settings.mode = mode;
    settings.debugView = debugView;
    settings.autoSafetyEnabled = autoSafetyEnabled;
    settings.overrideMinEv = overrideMinEv;
    settings.overrideMaxEv = overrideMaxEv;
    settings.overrideBaseEv = overrideBaseEv;
    settings.overrideNoiseProtection = overrideNoiseProtection;
    settings.overrideHighlightProtection = overrideHighlightProtection;
    settings.overrideShadowLiftLimit = overrideShadowLiftLimit;
    settings.overrideWellExposedTarget = overrideWellExposedTarget;
    settings.invertMask = invertMask;
    settings.maskBlackPoint = maskBlackPoint;
    settings.maskWhitePoint = maskWhitePoint;
    settings.maskGamma = maskGamma;
    settings.manualBlend = manualBlend;
    if (!includeStrength) {
        settings.strength = 1.0f;
    }

    switch (preset) {
        case AutoGainPreset::Natural:
            break;
        case AutoGainPreset::ShadowDetail:
            settings.strength = includeStrength ? 0.70f : 1.0f;
            settings.maxEvBias = 0.25f;
            settings.highlightProtectionBias = 0.10f;
            settings.noiseProtectionBias = 0.18f;
            settings.shadowLiftLimitBias = 0.15f;
            settings.smoothGradientProtection = 0.88f;
            settings.haloGuard = 0.92f;
            break;
        case AutoGainPreset::HighlightControl:
            settings.strength = includeStrength ? 0.62f : 1.0f;
            settings.minEvBias = -0.25f;
            settings.maxEvBias = -0.10f;
            settings.highlightProtectionBias = 0.22f;
            settings.wellExposedTargetBias = -0.08f;
            settings.smoothGradientProtection = 0.90f;
            settings.haloGuard = 0.94f;
            break;
        case AutoGainPreset::InteriorWindow:
            settings.strength = includeStrength ? 0.72f : 1.0f;
            settings.minEvBias = -0.20f;
            settings.maxEvBias = 0.20f;
            settings.highlightProtectionBias = 0.25f;
            settings.noiseProtectionBias = 0.18f;
            settings.shadowLiftLimitBias = 0.10f;
            settings.wellExposedTargetBias = -0.03f;
            settings.smoothGradientProtection = 0.92f;
            settings.edgeAwareness = 0.72f;
            settings.haloGuard = 0.96f;
            break;
        case AutoGainPreset::Developer:
            settings.strength = includeStrength ? 0.75f : 1.0f;
            settings.sampleCount = 25;
            settings.baseRadiusPercent = 0.016f;
            settings.detailWeight = 0.65f;
            settings.textureSensitivity = 0.65f;
            settings.skyBias = 0.50f;
            settings.edgeAwareness = 0.70f;
            settings.maskDebandDither = 0.05f;
            break;
    }
}

bool RenderAutoGainPresetControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool includeStrength) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::BeginCombo(("Apply Preset##" + prefix + "Preset").c_str(), "Choose preset...")) {
        for (int i = 0; i < 5; ++i) {
            const AutoGainPreset preset = static_cast<AutoGainPreset>(i);
            if (ImGui::Selectable(AutoGainPresetName(preset), false)) {
                ApplyAutoGainPreset(settings, preset, includeStrength);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    RenderAutoGainItemHelpPopup("Preset", "Apply Preset");
    ImGui::TextDisabled("Presets set safe local-exposure starting points; they are not HDR reconstruction.");
    return changed;
}

bool RenderAutoGainBasicControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool includeStrength) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    if (includeStrength) {
        changed |= AutoGainSliderFloat("Amount", ("##" + prefix + "Amount").c_str(), &settings.strength, 0.0f, 1.25f, "%.2f", controlWidth, "Strength");
    }
    if (settings.autoSafetyEnabled) {
        changed |= AutoGainSliderFloat("Shadow Lift", ("##" + prefix + "BasicShadowLift").c_str(), &settings.maxEvBias, -2.0f, 2.0f, "%+.2f EV", controlWidth, "MaxEvBias");
        changed |= AutoGainSliderFloat("Highlight Compression", ("##" + prefix + "BasicHighlightCompression").c_str(), &settings.minEvBias, -2.0f, 2.0f, "%+.2f EV", controlWidth, "MinEvBias");
        changed |= AutoGainSliderFloat("Baseline", ("##" + prefix + "BasicBaseline").c_str(), &settings.baseEvBias, -1.25f, 1.25f, "%+.2f EV", controlWidth, "BaseEvBias");
    } else {
        changed |= AutoGainSliderFloat("Shadow Lift", ("##" + prefix + "BasicMaxEv").c_str(), &settings.maxEv, 0.25f, 2.5f, "%.2f EV", controlWidth, "MaxEv");
        changed |= AutoGainSliderFloat("Highlight Compression", ("##" + prefix + "BasicMinEv").c_str(), &settings.minEv, -2.5f, 0.5f, "%.2f EV", controlWidth, "MinEv");
        changed |= AutoGainSliderFloat("Baseline", ("##" + prefix + "BasicBaseEv").c_str(), &settings.baseEv, -1.0f, 1.0f, "%.2f EV", controlWidth, "BaseEv");
    }
    return changed;
}

bool RenderAutoGainProtectionControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    if (settings.autoSafetyEnabled) {
        changed |= AutoGainSliderFloat("Noise Guard", ("##" + prefix + "ProtectNoiseBias").c_str(), &settings.noiseProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth, "NoiseBias");
        changed |= AutoGainSliderFloat("Highlight Guard", ("##" + prefix + "ProtectHighlightBias").c_str(), &settings.highlightProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth, "HighlightBias");
        changed |= AutoGainSliderFloat("Shadow Guard", ("##" + prefix + "ProtectShadowBias").c_str(), &settings.shadowLiftLimitBias, -1.0f, 1.0f, "%+.2f", controlWidth, "ShadowBias");
    } else {
        changed |= AutoGainSliderFloat("Noise Guard", ("##" + prefix + "ProtectNoise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth, "NoiseProtection");
        changed |= AutoGainSliderFloat("Highlight Guard", ("##" + prefix + "ProtectHighlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth, "HighlightProtection");
        changed |= AutoGainSliderFloat("Shadow Guard", ("##" + prefix + "ProtectShadow").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth, "ShadowLimit");
    }
    changed |= AutoGainSliderFloat("Smooth Gradient Protection", ("##" + prefix + "ProtectSmoothGradient").c_str(), &settings.smoothGradientProtection, 0.0f, 1.0f, "%.2f", controlWidth, "SmoothGradient");
    changed |= AutoGainSliderFloat("Halo Guard", ("##" + prefix + "ProtectHalo").c_str(), &settings.haloGuard, 0.0f, 1.0f, "%.2f", controlWidth, "HaloGuard");
    return changed;
}

bool RenderDevelopScenePrepNormalControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "DevelopScenePrep";
    changed |= AutoGainSliderFloat("Amount", ("##" + prefix + "Amount").c_str(), &settings.strength, 0.0f, 1.25f, "%.2f", controlWidth, "Strength");
    if (settings.autoSafetyEnabled) {
        changed |= AutoGainSliderFloat("Shadow Lift", ("##" + prefix + "ShadowLift").c_str(), &settings.maxEvBias, -2.0f, 2.0f, "%+.2f EV", controlWidth, "MaxEvBias");
        changed |= AutoGainSliderFloat("Highlight Compression", ("##" + prefix + "HighlightCompression").c_str(), &settings.minEvBias, -2.0f, 2.0f, "%+.2f EV", controlWidth, "MinEvBias");
        changed |= AutoGainSliderFloat("Noise Guard", ("##" + prefix + "NoiseGuard").c_str(), &settings.noiseProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth, "NoiseBias");
        changed |= AutoGainSliderFloat("Highlight Guard", ("##" + prefix + "HighlightGuard").c_str(), &settings.highlightProtectionBias, -1.0f, 1.0f, "%+.2f", controlWidth, "HighlightBias");
        changed |= AutoGainSliderFloat("Shadow Guard", ("##" + prefix + "ShadowGuard").c_str(), &settings.shadowLiftLimitBias, -1.0f, 1.0f, "%+.2f", controlWidth, "ShadowBias");
    } else {
        changed |= AutoGainSliderFloat("Shadow Lift", ("##" + prefix + "MaxEv").c_str(), &settings.maxEv, 0.25f, 2.5f, "%.2f EV", controlWidth, "MaxEv");
        changed |= AutoGainSliderFloat("Highlight Compression", ("##" + prefix + "MinEv").c_str(), &settings.minEv, -2.5f, 0.5f, "%.2f EV", controlWidth, "MinEv");
        changed |= AutoGainSliderFloat("Noise Guard", ("##" + prefix + "Noise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth, "NoiseProtection");
        changed |= AutoGainSliderFloat("Highlight Guard", ("##" + prefix + "Highlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth, "HighlightProtection");
        changed |= AutoGainSliderFloat("Shadow Guard", ("##" + prefix + "Shadow").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth, "ShadowLimit");
    }
    return changed;
}

bool RenderDevelopScenePrepAdvancedBiasControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "DevelopScenePrepAdvanced";
    if (settings.autoSafetyEnabled) {
        changed |= AutoGainSliderFloat("Local Baseline", ("##" + prefix + "Baseline").c_str(), &settings.baseEvBias, -1.25f, 1.25f, "%+.2f EV", controlWidth, "BaseEvBias");
        changed |= AutoGainSliderFloat("Midtone Target", ("##" + prefix + "TargetBias").c_str(), &settings.wellExposedTargetBias, -1.0f, 1.0f, "%+.2f", controlWidth, "TargetBias");
    } else {
        changed |= AutoGainSliderFloat("Local Baseline", ("##" + prefix + "BaseEv").c_str(), &settings.baseEv, -1.0f, 1.0f, "%.2f EV", controlWidth, "BaseEv");
        changed |= AutoGainSliderFloat("Midtone Target", ("##" + prefix + "Target").c_str(), &settings.wellExposedTarget, 0.10f, 0.55f, "%.2f", controlWidth, "Target");
    }
    changed |= AutoGainSliderFloat("Smooth Gradient Protection", ("##" + prefix + "SmoothGradient").c_str(), &settings.smoothGradientProtection, 0.0f, 1.0f, "%.2f", controlWidth, "SmoothGradient");
    changed |= AutoGainSliderFloat("Halo Guard", ("##" + prefix + "Halo").c_str(), &settings.haloGuard, 0.0f, 1.0f, "%.2f", controlWidth, "HaloGuard");
    return changed;
}

bool RenderAutoGainAdvancedControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool includeStrength) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    changed |= RenderAutoGainExposureControls(settings, controlWidth, (prefix + "Advanced").c_str(), includeStrength);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    changed |= RenderAutoGainAnalysisControls(settings, controlWidth, (prefix + "Advanced").c_str());

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("BASE / DETAIL AND HALO CONTROL", 4.0f);
    changed |= AutoGainSliderFloat("Texture Sensitivity", ("##" + prefix + "AdvancedTextureSensitivity").c_str(), &settings.textureSensitivity, 0.0f, 1.0f, "%.2f", controlWidth, "TextureSensitivity");
    changed |= AutoGainSliderFloat("Sky Bias", ("##" + prefix + "AdvancedSkyBias").c_str(), &settings.skyBias, 0.0f, 1.0f, "%.2f", controlWidth, "SkyBias");
    changed |= AutoGainSliderInt("Smoothness Radius", ("##" + prefix + "AdvancedSmoothRadius").c_str(), &settings.smoothnessRadius, 0, 16, "%d px", controlWidth, "SmoothRadius");
    changed |= AutoGainSliderInt("Smooth Area Radius", ("##" + prefix + "AdvancedSmoothAreaRadius").c_str(), &settings.smoothAreaRadius, 0, 32, "%d px", controlWidth, "SmoothAreaRadius");
    changed |= AutoGainSliderFloat("Edge Awareness", ("##" + prefix + "AdvancedEdgeAware").c_str(), &settings.edgeAwareness, 0.0f, 1.0f, "%.2f", controlWidth, "EdgeAwareness");
    changed |= AutoGainSliderFloat("Mask Deband Dither", ("##" + prefix + "AdvancedDither").c_str(), &settings.maskDebandDither, 0.0f, 1.0f, "%.2f", controlWidth, "Dither");
    return changed;
}

bool RenderAutoGainDiagnosticsControls(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix,
    bool advanced,
    const char* label) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "AutoGain";
    const char* publicLabels[] = {
        "EV Adjustment Map",
        "Protection / Confidence",
        "Clipping Limited",
        "Noise / SNR Limited"
    };
    const Raw::RawDetailFusionDebugView publicViews[] = {
        Raw::RawDetailFusionDebugView::ExposureMap,
        Raw::RawDetailFusionDebugView::Confidence,
        Raw::RawDetailFusionDebugView::HighlightSafety,
        Raw::RawDetailFusionDebugView::NoiseFloorSnr
    };
    int publicIndex = 0;
    bool isPublicView = false;
    for (int i = 0; i < 4; ++i) {
        if (settings.debugView == publicViews[i]) {
            publicIndex = i;
            isPublicView = true;
            break;
        }
    }
    if (!advanced && !isPublicView) {
        settings.debugView = Raw::RawDetailFusionDebugView::ExposureMap;
        publicIndex = 0;
        changed = true;
    }
    if (AutoGainCombo(label, &publicIndex, publicLabels, 4, controlWidth, "Diagnostics")) {
        settings.debugView = publicViews[std::clamp(publicIndex, 0, 3)];
        changed = true;
    }
    ImGui::TextDisabled("Preview answers where Pre-Local Exposure changes, how confident it is, and whether clipping or noise is limiting the result.");

    if (advanced) {
        const char* developerLabels[] = {
            "Effective EV Map",
            "Confidence Map",
            "Highlight Safety",
            "Shadow / Noise Protection",
            "Sample Selection",
            "Smooth Gradient Protection",
            "True Edge Map",
            "Texture Detail Map",
            "Deband / Chroma Risk",
            "Auto Range Map",
            "Noise Floor / SNR",
            "Highlight Headroom",
            "Channel Saturation",
            "Rejected Detail"
        };
        int debugView = static_cast<int>(settings.debugView);
        if (debugView <= static_cast<int>(Raw::RawDetailFusionDebugView::FinalImage)) {
            debugView = static_cast<int>(Raw::RawDetailFusionDebugView::ExposureMap);
        }
        int displayDebug = std::clamp(debugView - 1, 0, 13);
        if (AutoGainCombo("Developer Channel", &displayDebug, developerLabels, 14, controlWidth, "Diagnostics")) {
            settings.debugView = static_cast<Raw::RawDetailFusionDebugView>(std::clamp(displayDebug + 1, 1, 14));
            changed = true;
        }
    }

    return changed;
}

void RenderPreLocalExposureSummaryBadge(const char* label, const ImVec4& color, bool& firstBadge) {
    if (!firstBadge) {
        ImGui::SameLine(0.0f, 8.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    firstBadge = false;
}

void RenderPreLocalExposureSummarySection(
    const RenderPipeline::PreLocalExposureSummary* summary,
    bool hasImageInput,
    float controlWidth) {
    (void)controlWidth;
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("SUMMARY", 4.0f);
    if (!hasImageInput) {
        ImGui::TextWrapped("Connect the Image input to analyze scene-linear RGB from Develop or HDR Merge.");
        return;
    }
    if (!summary || !summary->valid) {
        ImGui::TextWrapped("Live summary appears when this node is part of the rendered output path.");
        return;
    }

    ImGui::TextDisabled(
        "Effective EV window: %.2f to %.2f",
        summary->effectiveSettings.minEv,
        summary->effectiveSettings.maxEv);
    ImGui::TextDisabled(
        "Baseline EV: %.2f    Target: %.2f    Amount: %.2f",
        summary->effectiveSettings.baseEv,
        summary->effectiveSettings.wellExposedTarget,
        summary->effectiveSettings.strength);
    ImGui::TextDisabled(
        "Noise floor: %.4f    Clip ratio: %.1f%%    Texture confidence: %.2f",
        summary->estimatedNoiseFloor,
        summary->clippingRatio * 100.0f,
        summary->textureConfidence);

    bool firstBadge = true;
    if (summary->noiseLimited) {
        RenderPreLocalExposureSummaryBadge("Noise Limited", ImVec4(0.96f, 0.80f, 0.35f, 1.0f), firstBadge);
    }
    if (summary->highlightLimited) {
        RenderPreLocalExposureSummaryBadge("Highlight Limited", ImVec4(0.96f, 0.58f, 0.40f, 1.0f), firstBadge);
    }
    if (summary->gradientProtected) {
        RenderPreLocalExposureSummaryBadge("Gradient Protected", ImVec4(0.55f, 0.82f, 0.96f, 1.0f), firstBadge);
    }
    if (firstBadge) {
        RenderPreLocalExposureSummaryBadge("Balanced", ImVec4(0.62f, 0.86f, 0.70f, 1.0f), firstBadge);
    }
}

bool RenderPreLocalExposureExpertOverrides(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "PreLocalExposure";
    if (!settings.autoSafetyEnabled) {
        ImGui::TextDisabled("Legacy direct mode is active from a saved graph. Reset the node to return to live automatic behavior.");
        changed |= AutoGainSliderFloat("Min EV", ("##" + prefix + "LegacyMinEv").c_str(), &settings.minEv, -2.5f, 0.5f, "%.2f EV", controlWidth, "MinEv");
        changed |= AutoGainSliderFloat("Max EV", ("##" + prefix + "LegacyMaxEv").c_str(), &settings.maxEv, 0.25f, 2.5f, "%.2f EV", controlWidth, "MaxEv");
        changed |= AutoGainSliderFloat("Base EV", ("##" + prefix + "LegacyBaseEv").c_str(), &settings.baseEv, -1.0f, 1.0f, "%.2f EV", controlWidth, "BaseEv");
        changed |= AutoGainSliderFloat("Well-Exposed Target", ("##" + prefix + "LegacyTarget").c_str(), &settings.wellExposedTarget, 0.10f, 0.55f, "%.2f", controlWidth, "Target");
        changed |= AutoGainSliderFloat("Noise Guard", ("##" + prefix + "LegacyNoise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth, "NoiseProtection");
        changed |= AutoGainSliderFloat("Highlight Guard", ("##" + prefix + "LegacyHighlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth, "HighlightProtection");
        changed |= AutoGainSliderFloat("Shadow Guard", ("##" + prefix + "LegacyShadow").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth, "ShadowLimit");
        return changed;
    }

    ImGui::TextDisabled("Live automatic analysis stays on. Use overrides only when you need exact limits.");

    changed |= AutoGainCheckbox("Override Min EV", ("##" + prefix + "OverrideMinEv").c_str(), &settings.overrideMinEv, controlWidth, "OverrideMinEv");
    if (settings.overrideMinEv) {
        changed |= AutoGainSliderFloat("Min EV", ("##" + prefix + "MinEv").c_str(), &settings.minEv, -2.5f, 0.5f, "%.2f EV", controlWidth, "MinEvOverride");
    }

    changed |= AutoGainCheckbox("Override Max EV", ("##" + prefix + "OverrideMaxEv").c_str(), &settings.overrideMaxEv, controlWidth, "OverrideMaxEv");
    if (settings.overrideMaxEv) {
        changed |= AutoGainSliderFloat("Max EV", ("##" + prefix + "MaxEv").c_str(), &settings.maxEv, 0.25f, 2.5f, "%.2f EV", controlWidth, "MaxEvOverride");
    }

    changed |= AutoGainCheckbox("Override Base EV", ("##" + prefix + "OverrideBaseEv").c_str(), &settings.overrideBaseEv, controlWidth, "OverrideBaseEv");
    if (settings.overrideBaseEv) {
        changed |= AutoGainSliderFloat("Base EV", ("##" + prefix + "BaseEv").c_str(), &settings.baseEv, -1.0f, 1.0f, "%.2f EV", controlWidth, "BaseEvOverride");
    }

    changed |= AutoGainCheckbox("Override Target", ("##" + prefix + "OverrideTarget").c_str(), &settings.overrideWellExposedTarget, controlWidth, "OverrideTarget");
    if (settings.overrideWellExposedTarget) {
        changed |= AutoGainSliderFloat("Well-Exposed Target", ("##" + prefix + "Target").c_str(), &settings.wellExposedTarget, 0.10f, 0.55f, "%.2f", controlWidth, "Target");
    }

    changed |= AutoGainCheckbox("Override Noise Guard", ("##" + prefix + "OverrideNoise").c_str(), &settings.overrideNoiseProtection, controlWidth, "OverrideNoise");
    if (settings.overrideNoiseProtection) {
        changed |= AutoGainSliderFloat("Noise Guard", ("##" + prefix + "Noise").c_str(), &settings.noiseProtection, 0.0f, 1.0f, "%.2f", controlWidth, "NoiseProtection");
    }

    changed |= AutoGainCheckbox("Override Highlight Guard", ("##" + prefix + "OverrideHighlight").c_str(), &settings.overrideHighlightProtection, controlWidth, "OverrideHighlight");
    if (settings.overrideHighlightProtection) {
        changed |= AutoGainSliderFloat("Highlight Guard", ("##" + prefix + "Highlight").c_str(), &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth, "HighlightProtection");
    }

    changed |= AutoGainCheckbox("Override Shadow Guard", ("##" + prefix + "OverrideShadow").c_str(), &settings.overrideShadowLiftLimit, controlWidth, "OverrideShadow");
    if (settings.overrideShadowLiftLimit) {
        changed |= AutoGainSliderFloat("Shadow Guard", ("##" + prefix + "Shadow").c_str(), &settings.shadowLiftLimit, 0.0f, 1.0f, "%.2f", controlWidth, "ShadowLimit");
    }

    return changed;
}

bool RenderPreLocalExposureSpatialModel(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "PreLocalExposure";
    changed |= AutoGainSliderInt("Base Samples", ("##" + prefix + "Samples").c_str(), &settings.sampleCount, 3, 33, "%d", controlWidth, "Samples");
    changed |= AutoGainSliderFloat("Base Radius", ("##" + prefix + "BaseRadius").c_str(), &settings.baseRadiusPercent, 0.002f, 0.030f, "%.3f", controlWidth, "BaseRadius");
    changed |= AutoGainSliderFloat("Detail Weight", ("##" + prefix + "DetailWeight").c_str(), &settings.detailWeight, 0.0f, 1.0f, "%.2f", controlWidth, "DetailWeight");
    changed |= AutoGainSliderFloat("Texture Sensitivity", ("##" + prefix + "TextureSensitivity").c_str(), &settings.textureSensitivity, 0.0f, 1.0f, "%.2f", controlWidth, "TextureSensitivity");
    changed |= AutoGainSliderFloat("Sky Bias", ("##" + prefix + "SkyBias").c_str(), &settings.skyBias, 0.0f, 1.0f, "%.2f", controlWidth, "SkyBias");
    return changed;
}

bool RenderPreLocalExposureSmoothing(
    Raw::RawDetailFusionSettings& settings,
    float controlWidth,
    const char* idPrefix) {
    bool changed = false;
    const std::string prefix = idPrefix ? idPrefix : "PreLocalExposure";
    changed |= AutoGainSliderInt("Smoothness Radius", ("##" + prefix + "SmoothRadius").c_str(), &settings.smoothnessRadius, 0, 16, "%d px", controlWidth, "SmoothRadius");
    changed |= AutoGainSliderInt("Smooth Area Radius", ("##" + prefix + "SmoothAreaRadius").c_str(), &settings.smoothAreaRadius, 0, 32, "%d px", controlWidth, "SmoothAreaRadius");
    changed |= AutoGainSliderFloat("Edge Awareness", ("##" + prefix + "EdgeAwareness").c_str(), &settings.edgeAwareness, 0.0f, 1.0f, "%.2f", controlWidth, "EdgeAwareness");
    changed |= AutoGainSliderFloat("Mask Deband Dither", ("##" + prefix + "MaskDeband").c_str(), &settings.maskDebandDither, 0.0f, 1.0f, "%.2f", controlWidth, "Dither");
    ImGui::TextDisabled("Smooth Gradient Protection and Halo Guard remain in Protection because they are part of the public look of the node.");
    return changed;
}

enum class RawDetailFusionWorkflow {
    MissingMask,
    Auto,
    Hybrid,
    ExternalMask
};

RawDetailFusionWorkflow ResolveRawDetailFusionWorkflow(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& fusionNode) {
    const EditorNodeGraph::Link* maskInput = graph.FindInputLink(fusionNode.id, EditorNodeGraph::kMaskInputSocketId);
    if (!maskInput) {
        return RawDetailFusionWorkflow::MissingMask;
    }

    const EditorNodeGraph::Node* source = graph.FindNode(maskInput->fromNodeId);
    if (!source) {
        return RawDetailFusionWorkflow::ExternalMask;
    }
    if (source->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask &&
        maskInput->fromSocketId == EditorNodeGraph::kMaskOutputSocketId) {
        return RawDetailFusionWorkflow::Auto;
    }

    std::vector<int> visited;
    const EditorNodeGraph::Link* current = maskInput;
    while (current) {
        if (std::find(visited.begin(), visited.end(), current->fromNodeId) != visited.end()) {
            break;
        }
        visited.push_back(current->fromNodeId);
        const EditorNodeGraph::Node* currentNode = graph.FindNode(current->fromNodeId);
        if (!currentNode) {
            break;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask &&
            current->fromSocketId == EditorNodeGraph::kMaskOutputSocketId) {
            return RawDetailFusionWorkflow::Hybrid;
        }
        if (currentNode->kind != EditorNodeGraph::NodeKind::MaskUtility ||
            current->fromSocketId != EditorNodeGraph::kMaskOutputSocketId) {
            break;
        }
        current = graph.FindInputLink(currentNode->id, EditorNodeGraph::kMaskInputSocketId);
    }

    return RawDetailFusionWorkflow::ExternalMask;
}

const EditorNodeGraph::Node* FindUpstreamRawSource(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
    std::vector<int> visited;
    while (input) {
        if (std::find(visited.begin(), visited.end(), input->fromNodeId) != visited.end()) {
            return nullptr;
        }
        visited.push_back(input->fromNodeId);
        const EditorNodeGraph::Node* upstream = graph.FindNode(input->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        input = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

} // namespace

void EditorModule::RenderRawSourceControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawSource) {
        return;
    }

    Raw::RawMetadata& metadata = node.rawSource.metadata;

    ImGuiExtras::RichSectionLabel("FILE / CAMERA", 4.0f);
    const std::string rawPath = node.rawSource.sourcePath.empty()
        ? metadata.sourcePath
        : node.rawSource.sourcePath;
    const std::string displayName = RawDisplayName(node.rawSource);
    if (advanced) {
        ImGui::TextDisabled("%s", rawPath.c_str());
    } else {
        ImGui::TextDisabled("%s", displayName.c_str());
    }
    if (!metadata.cameraMake.empty() || !metadata.cameraModel.empty()) {
        ImGui::TextDisabled("%s %s", metadata.cameraMake.c_str(), metadata.cameraModel.c_str());
    }
    if (metadata.hasExposureTime || metadata.hasApertureFNumber || metadata.hasIsoSpeed) {
        const float shutter = metadata.hasExposureTime ? metadata.exposureTimeSeconds : 0.0f;
        const float aperture = metadata.hasApertureFNumber ? metadata.apertureFNumber : 0.0f;
        const float iso = metadata.hasIsoSpeed ? metadata.isoSpeed : 0.0f;
        if (metadata.hasExposureTime && metadata.hasApertureFNumber && metadata.hasIsoSpeed) {
            ImGui::TextDisabled("Capture: %.4f s  f/%.2f  ISO %.0f", shutter, aperture, iso);
        } else if (metadata.hasExposureTime && metadata.hasIsoSpeed) {
            ImGui::TextDisabled("Capture: %.4f s  ISO %.0f", shutter, iso);
        } else if (metadata.hasExposureTime) {
            ImGui::TextDisabled("Capture: %.4f s", shutter);
        }
    }
    if (!metadata.dngUniqueCameraModel.empty()) {
        ImGui::TextDisabled("DNG camera: %s", metadata.dngUniqueCameraModel.c_str());
    }
    if (metadata.rawWidth > 0 && metadata.rawHeight > 0) {
        ImGui::TextDisabled("Raw: %d x %d", metadata.rawWidth, metadata.rawHeight);
    }
    if (metadata.visibleWidth > 0 && metadata.visibleHeight > 0) {
        ImGui::TextDisabled("Visible: %d x %d", metadata.visibleWidth, metadata.visibleHeight);
    }
    ImGui::TextDisabled("Crop: %d, %d  Orientation: %d", metadata.leftMargin, metadata.topMargin, metadata.orientation);
    ImGui::TextDisabled("RAW type: %s", Raw::RawPixelLayoutName(metadata.pixelLayout));
    ImGui::TextDisabled("Output: RAW sensor data and metadata");
    if (!metadata.dngTypeStatus.empty()) {
        ImGui::TextDisabled("%s", metadata.dngTypeStatus.c_str());
    }
    if (metadata.isDng) {
        ImGui::TextDisabled("DNG compression: %d  Photometric: %d",
            metadata.dngCompression,
            metadata.dngPhotometricInterpretation);
    }
    if (metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
        ImGui::TextDisabled("Demosaic: skipped  Bit depth: %d", metadata.bitDepth);
        ImGui::TextDisabled("Linear channels: %d  Sample: %s",
            metadata.linearChannels,
            Raw::RawSampleFormatName(metadata.linearSampleFormat));
    } else {
        ImGui::TextDisabled("CFA: %s  Bit depth: %d", Raw::CfaPatternName(metadata.cfaPattern), metadata.bitDepth);
        if (metadata.isDng && metadata.dngCfaRepeatPatternDim[0] > 0) {
            ImGui::TextDisabled("DNG CFA: repeat %dx%d  planes [%d %d %d]  pattern [%d %d %d %d]",
                metadata.dngCfaRepeatPatternDim[0],
                metadata.dngCfaRepeatPatternDim[1],
                metadata.dngCfaPlaneColor[0],
                metadata.dngCfaPlaneColor[1],
                metadata.dngCfaPlaneColor[2],
                metadata.dngCfaPattern[0],
                metadata.dngCfaPattern[1],
                metadata.dngCfaPattern[2],
                metadata.dngCfaPattern[3]);
        }
    }
    if (!metadata.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.38f, 0.34f, 1.0f));
        ImGui::TextWrapped("%s", metadata.error.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Status: ready");
    }
    ImGui::TextDisabled("Denoise starts in Develop; RAW Source is not RGB image data.");

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextWrapped("Connect this RAW source to a Develop node to produce unclamped scene-linear RGB.");
        if (ImGui::Button("Add Develop", ImVec2(controlWidth, 0.0f))) {
            const EditorNodeGraph::Vec2 position{ node.position.x + 250.0f, node.position.y };
            const int sourceNodeId = node.id;
            AddRawDevelopNodeAt(position);
            const int developNodeId = m_NodeGraph.GetSelectedNodeId();
            std::string errorMessage;
            ConnectGraphSockets(
                sourceNodeId,
                EditorNodeGraph::kRawOutputSocketId,
                developNodeId,
                EditorNodeGraph::kRawInputSocketId,
                &errorMessage);
            MarkRenderDirty(sourceNodeId);
        }
    }
}

void EditorModule::RenderRawNeuralDenoiseControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
        return;
    }

    using namespace NeuralDenoise;
    NeuralDenoiseManager& manager = NeuralDenoiseManager::Instance();
    NeuralDenoiseSettings& settings = node.rawNeuralDenoise.settings;
    const nlohmann::json before = SerializeSettings(settings);
    const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSource(m_NodeGraph, node);
    const Raw::RawMetadata* metadata = rawSourceNode ? &rawSourceNode->rawSource.metadata : nullptr;
    const bool hasMosaicedCfa = metadata &&
        metadata->pixelLayout == Raw::RawPixelLayout::MosaicBayer &&
        metadata->mosaiced &&
        metadata->cfaPattern != Raw::CfaPattern::Unknown;

    std::vector<const NeuralDenoiseModelInfo*> models = manager.ModelsOfType(ModelType::RawBayerPacked4Ch);
    if (settings.selectedModelId.empty() && !models.empty()) {
        settings.selectedModelId = models.front()->id;
    }
    const NeuralDenoiseModelInfo* selected = manager.FindModel(settings.selectedModelId);
    const ModelAvailability availability = manager.GetAvailability(selected);

    ImGuiExtras::RichSectionLabel("RAW/CFA NEURAL DENOISE", 4.0f);
    ImGuiExtras::NodeCheckbox("Enable", "##RawNeuralEnabled", &settings.enabled, controlWidth);
    if (!rawSourceNode) {
        ImGui::TextWrapped("RAW neural denoise unavailable: connect a RAW source before this node.");
    } else if (!hasMosaicedCfa) {
        ImGui::TextWrapped("RAW neural denoise unavailable: current input is not mosaiced CFA RAW.");
    } else {
        ImGui::TextDisabled("Input: mosaiced CFA RAW, %s", Raw::CfaPatternName(metadata->cfaPattern));
    }
    ImGui::TextDisabled("Execution: bypass / pass-through until real inference is implemented.");
    ImGui::TextDisabled("Status: %s", availability.status.c_str());

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("MODEL", 4.0f);
        const std::string currentLabel = selected ? selected->displayName : std::string("No model selected");
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::BeginCombo("Selected Model", currentLabel.c_str())) {
            for (const NeuralDenoiseModelInfo* model : models) {
                const bool isSelected = model && model->id == settings.selectedModelId;
                if (model && ImGui::Selectable(model->displayName.c_str(), isSelected)) {
                    settings.selectedModelId = model->id;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if (models.empty()) {
                ImGui::TextDisabled("No RAW Bayer neural models in manifest.");
            }
            ImGui::EndCombo();
        }

        const char* runtimeLabels[] = { "Auto", "CUDA", "CPU placeholder", "DirectML future", "TensorRT future" };
        int runtime = static_cast<int>(settings.runtimePreference);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Runtime / Provider", &runtime, runtimeLabels, 5)) {
            settings.runtimePreference = static_cast<RuntimePreference>(std::clamp(runtime, 0, 4));
        }
        const char* qualityLabels[] = { "Quality", "Balanced", "Fast" };
        int quality = static_cast<int>(settings.qualityMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Quality Mode", &quality, qualityLabels, 3)) {
            settings.qualityMode = static_cast<QualityMode>(std::clamp(quality, 0, 2));
        }
        if (selected) {
            ImGui::TextDisabled("Model type: %s", ModelTypeLabel(selected->type));
            ImGui::TextDisabled("Packed layout: Bayer 2x2 -> 4 channels");
            ImGui::TextDisabled("File: %s", selected->relativeFile.c_str());
            if (!selected->license.empty()) {
                ImGui::TextWrapped("License: %s", selected->license.c_str());
            }
        }
        for (const std::string& warning : availability.warnings) {
            ImGui::TextWrapped("%s", warning.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("BLEND", 4.0f);
        ImGuiExtras::NodeSliderFloat("Overall Strength", "##RawNeuralStrength", &settings.strength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Detail Preservation", "##RawNeuralDetail", &settings.detailPreservation, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Shadows Strength", "##RawNeuralShadows", &settings.shadowsStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Highlight Protection", "##RawNeuralHighlights", &settings.highlightProtection, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Difference Amount", "##RawNeuralDifference", &settings.differenceAmount, 0.0f, 2.0f, "%.2f", controlWidth);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("NOISE TARGETING", 4.0f);
        ImGuiExtras::NodeSliderFloat("Chroma Noise", "##RawNeuralChroma", &settings.chromaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Luma Noise", "##RawNeuralLuma", &settings.lumaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Fine Grain", "##RawNeuralFine", &settings.fineGrainStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeSliderFloat("Blotch / Splotch", "##RawNeuralBlotch", &settings.blotchStrength, 0.0f, 1.0f, "%.2f", controlWidth);
        ImGuiExtras::NodeCheckbox("Hot / Dead Pixel Cleanup", "##RawNeuralHotDead", &settings.hotDeadPixelCleanup, controlWidth);
        ImGuiExtras::NodeCheckbox("Shadow-Biased Denoise", "##RawNeuralShadowBias", &settings.shadowBiasedDenoise, controlWidth);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RAW / DNG", 4.0f);
        const char* cfaLabels[] = { "From metadata", "RGGB", "BGGR", "GRBG", "GBRG" };
        int cfa = static_cast<int>(settings.cfaOverride);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("CFA Override", &cfa, cfaLabels, 5)) {
            settings.cfaOverride = static_cast<CfaOverride>(std::clamp(cfa, 0, 4));
        }
        if (metadata) {
            ImGui::TextDisabled("Metadata CFA: %s", Raw::CfaPatternName(metadata->cfaPattern));
            ImGui::TextDisabled("Black / white: %.1f / %.1f", metadata->blackLevel, metadata->whiteLevel);
            ImGui::TextDisabled("ISO metadata: not exposed by current RAW pipeline");
        }
        ImGuiExtras::NodeCheckbox("Override Black", "##RawNeuralOverrideBlack", &settings.overrideBlackLevel, controlWidth);
        if (settings.overrideBlackLevel) {
            ImGuiExtras::NodeInputFloat("Black Level", "##RawNeuralBlack", &settings.blackLevel, 1.0f, 16.0f, "%.1f", controlWidth);
        }
        ImGuiExtras::NodeCheckbox("Override White", "##RawNeuralOverrideWhite", &settings.overrideWhiteLevel, controlWidth);
        if (settings.overrideWhiteLevel) {
            ImGuiExtras::NodeInputFloat("White Level", "##RawNeuralWhite", &settings.whiteLevel, 16.0f, 256.0f, "%.1f", controlWidth);
        }
        const char* noiseLabels[] = { "Auto from metadata", "Manual" };
        int noise = static_cast<int>(settings.noiseEstimateMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Noise Estimate", &noise, noiseLabels, 2)) {
            settings.noiseEstimateMode = static_cast<NoiseEstimateMode>(std::clamp(noise, 0, 1));
        }
        if (settings.noiseEstimateMode == NoiseEstimateMode::Manual) {
            ImGuiExtras::NodeSliderFloat("Manual Noise", "##RawNeuralManualNoise", &settings.manualNoiseEstimate, 0.0f, 1.0f, "%.3f", controlWidth);
        }
        const char* wbLabels[] = { "Before white balance", "After white balance" };
        int wbStage = static_cast<int>(settings.rawWhiteBalanceStage);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("WB Stage", &wbStage, wbLabels, 2)) {
            settings.rawWhiteBalanceStage = static_cast<RawWhiteBalanceStage>(std::clamp(wbStage, 0, 1));
        }
        const char* outputLabels[] = { "Denoised CFA", "Continue to demosaic" };
        int outputMode = static_cast<int>(settings.rawOutputMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Output Mode", &outputMode, outputLabels, 2)) {
            settings.rawOutputMode = static_cast<RawOutputMode>(std::clamp(outputMode, 0, 1));
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("PREVIEW / TILING", 4.0f);
        const char* previewLabels[] = { "Denoised", "Original", "Difference", "Split view", "Chroma-only difference", "Luma-only difference" };
        int preview = static_cast<int>(settings.previewMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Preview", &preview, previewLabels, 6)) {
            settings.previewMode = static_cast<PreviewMode>(std::clamp(preview, 0, 5));
        }
        ImGuiExtras::NodeSliderInt("Tile Size", "##RawNeuralTile", &settings.tilePlan.tileSize, 128, 2048, "%d px", controlWidth);
        ImGuiExtras::NodeSliderInt("Overlap", "##RawNeuralOverlap", &settings.tilePlan.overlap, 0, 512, "%d px", controlWidth);
        ImGuiExtras::NodeCheckbox("Feather Merge", "##RawNeuralFeather", &settings.tilePlan.featherMerge, controlWidth);
        settings.tilePlan.tileSize = std::clamp(settings.tilePlan.tileSize, 64, 4096);
        settings.tilePlan.overlap = std::clamp(settings.tilePlan.overlap, 0, settings.tilePlan.tileSize / 2);
    }

    if (before.dump() != SerializeSettings(settings).dump()) {
        MarkRenderDirty(node.id);
    }
}

void EditorModule::RenderRawDevelopControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return;
    }
    if (node.title.empty() || node.title == "RAW Develop") {
        node.title = "Develop";
    }

    const EditorNodeGraph::Link* rawInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
    const EditorNodeGraph::Node* rawSourceNode = rawInput ? m_NodeGraph.FindNode(rawInput->fromNodeId) : nullptr;
    const Raw::RawMetadata emptyMetadata;
    const Raw::RawMetadata& metadata =
        (rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource)
            ? rawSourceNode->rawSource.metadata
            : emptyMetadata;

    EditorNodeGraph::RawDevelopPayload& payload = node.rawDevelop;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }
    const EditorNodeGraph::RawDevelopPayload payloadBefore = payload;

    Raw::RawDevelopSettings& settings = payload.settings;
    Raw::RawDetailFusionSettings& scenePrepSettings = payload.scenePrepSettings;
    Raw::RawDevelopSettings defaultSettings;
    if (metadata.hasDngBaselineExposure) {
        defaultSettings.exposureStops = metadata.dngBaselineExposure;
    }
    if (!metadata.isDng) {
        defaultSettings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    }
    const bool hasRawSourceInput = rawSourceNode && rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource;
    const RenderPipeline::PreLocalExposureSummary* liveScenePrepSummary = m_Pipeline.GetPreLocalExposureSummary(node.id);
    EditorNodeGraph::RawDevelopUiMode& uiMode = payload.uiMode;
    bool changed = false;
    const bool showAdvancedManualControls = advanced;
    if (uiMode != EditorNodeGraph::RawDevelopUiMode::Manual) {
        m_RawDevelopExposureDrafts.erase(node.id);
    }

    auto resettableDevelopSliderFloat = [&](const char* label,
                                            const char* id,
                                            float* value,
                                            float resetValue,
                                            float minValue,
                                            float maxValue,
                                            const char* format) {
        bool localChanged = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
        const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
        if (state.lastHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && std::abs(*value - resetValue) > 0.0001f) {
            *value = resetValue;
            localChanged = true;
        }
        return localChanged;
    };
    auto resettableDevelopSliderInt = [&](const char* label,
                                          const char* id,
                                          int* value,
                                          int resetValue,
                                          int minValue,
                                          int maxValue,
                                          const char* format) {
        bool localChanged = ImGuiExtras::NodeSliderInt(label, id, value, minValue, maxValue, format, controlWidth);
        const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
        if (state.lastHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && *value != resetValue) {
            *value = resetValue;
            localChanged = true;
        }
        return localChanged;
    };

    const float effectiveBlack = settings.overrideBlackLevel ? settings.blackLevelOverride : metadata.blackLevel;
    const float requestedWhite = settings.overrideWhiteLevel ? settings.whiteLevelOverride : metadata.whiteLevel;
    const float effectiveWhite = std::max(effectiveBlack + 1.0f, requestedWhite);
    const std::array<float, 3> effectiveWb = EffectiveWhiteBalance(metadata, settings);
    const bool demosaicEnabled = metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer;

    ImGuiExtras::RichSectionLabel("DEVELOP", 4.0f);
    if (hasRawSourceInput) {
        ImGui::TextDisabled("%s", RawDisplayName(rawSourceNode->rawSource).c_str());
        ImGui::TextDisabled("Output: unclamped scene-linear float RGB");
    } else {
        ImGui::TextWrapped("Connect a RAW Source node to develop sensor data.");
    }

    const char* modeLabels[] = { "Auto", "Manual" };
    int uiModeIndex = static_cast<int>(uiMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Mode", &uiModeIndex, modeLabels, 2)) {
        uiMode = static_cast<EditorNodeGraph::RawDevelopUiMode>(std::clamp(uiModeIndex, 0, 1));
        changed = true;
    }
    bool autoMode = uiMode == EditorNodeGraph::RawDevelopUiMode::Auto;
    bool manualMode = uiMode == EditorNodeGraph::RawDevelopUiMode::Manual;
    bool forceAutoReanalysis = payloadBefore.uiMode != payload.uiMode;
    ImGui::TextDisabled(
        autoMode
            ? "Auto is the default merged Develop workflow. It solves RAW conversion, scene prep, and finish tone together."
            : "Manual freezes the current auto-authored result and reveals the deeper RAW, prep, and finish controls.");

    if (!hasRawSourceInput) {
        ImGui::BeginDisabled();
    }

    if (manualMode) {
        m_DevelopAutoGuidanceDrafts.erase(node.id);
        const float buttonGap = 8.0f;
        const float buttonWidth = std::max(110.0f, (controlWidth - buttonGap) * 0.5f);
        if (ImGuiExtras::RichFullWidthButton("Return To Auto", buttonWidth, 0.0f)) {
            uiMode = EditorNodeGraph::RawDevelopUiMode::Auto;
            autoMode = true;
            manualMode = false;
            forceAutoReanalysis = true;
            changed = true;
        }
        ImGui::SameLine(0.0f, buttonGap);
        if (ImGuiExtras::RichFullWidthButton("Recalibrate Auto", buttonWidth, 0.0f)) {
            ApplyDevelopAutoSolve(payload, metadata, true);
            changed = true;
        }
        ImGui::TextDisabled("Manual keeps the current authored state in place while you refine the image directly.");
    }

    if (autoMode) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("AUTO DEVELOP", 4.0f);
        const float buttonGap = 8.0f;
        const float buttonWidth = std::max(110.0f, (controlWidth - buttonGap) * 0.5f);
        auto& draftState = m_DevelopAutoGuidanceDrafts[node.id];
        if (!draftState.editing) {
            draftState.guidance = payload.autoGuidance;
        }
        bool autoSliderChanged = false;
        bool autoSliderActive = false;
        auto renderAutoGuidanceSlider = [&](const char* label,
                                           const char* id,
                                           float* value,
                                           float resetValue,
                                           float minValue,
                                           float maxValue,
                                           const char* format) {
            const bool localChanged =
                resettableDevelopSliderFloat(label, id, value, resetValue, minValue, maxValue, format);
            const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
            autoSliderChanged |= localChanged;
            autoSliderActive |= state.lastActive;
        };
        bool forceFullAutoReanalysis = forceAutoReanalysis;
        const char* intentLabels[] = {
            "Natural Finished",
            "Clean Base",
            "Flat Editing Base",
            "Bright Natural",
            "Dark Natural",
            "Punchy / High Contrast",
            "Maximum Range / Detail"
        };
        int intentIndex = static_cast<int>(payload.autoGuidance.intent);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Auto Mode / Intent", &intentIndex, intentLabels, IM_ARRAYSIZE(intentLabels))) {
            RecordRawDevelopInteraction(node.id);
            payload.autoGuidance.intent = static_cast<EditorNodeGraph::DevelopAutoIntent>(
                std::clamp(intentIndex, 0, IM_ARRAYSIZE(intentLabels) - 1));
            draftState.guidance.intent = payload.autoGuidance.intent;
            draftState.editing = false;
            forceAutoReanalysis = true;
            forceFullAutoReanalysis = true;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", EditorNodeGraph::DevelopAutoIntentDescription(payload.autoGuidance.intent));
        }
        ImGui::TextWrapped("%s", EditorNodeGraph::DevelopAutoIntentDescription(payload.autoGuidance.intent));
        if (ImGuiExtras::RichFullWidthButton("Auto Calibrate", buttonWidth, 0.0f)) {
            RecordRawDevelopInteraction(node.id);
            forceAutoReanalysis = true;
            forceFullAutoReanalysis = true;
            changed = true;
        }
        ImGui::SameLine(0.0f, buttonGap);
        if (ImGuiExtras::RichFullWidthButton("Reset Auto", buttonWidth, 0.0f)) {
            RecordRawDevelopInteraction(node.id);
            const EditorNodeGraph::DevelopAutoIntent selectedIntent = payload.autoGuidance.intent;
            payload.autoGuidance = EditorNodeGraph::DevelopAutoGuidance{};
            payload.autoGuidance.intent = selectedIntent;
            draftState.guidance = payload.autoGuidance;
            draftState.editing = false;
            forceAutoReanalysis = true;
            forceFullAutoReanalysis = true;
            changed = true;
        }

        const EditorNodeGraph::DevelopAutoGuidance defaultGuidance;
        bool subjectImportanceChanged = false;
        bool subjectImportanceActive = false;
        auto markSubjectImportanceEdit = [&](bool localChanged) {
            if (localChanged) {
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
            if (ImGui::IsItemActive()) {
                subjectImportanceActive = true;
            }
        };

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGuiExtras::RichSectionLabel("SUBJECT IMPORTANCE", 3.0f);
        markSubjectImportanceEdit(ImGui::Checkbox("Use Importance Guidance", &payload.subjectImportance.enabled));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Marked regions and brush strokes bias Auto candidate scoring. They are not hard masks and do not replace the Finish Mask input.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d region%s  |  %d stroke%s",
            static_cast<int>(payload.subjectImportance.regions.size()),
            payload.subjectImportance.regions.size() == 1 ? "" : "s",
            static_cast<int>(payload.subjectImportance.strokes.size()),
            payload.subjectImportance.strokes.size() == 1 ? "" : "s");

        if (ImGui::Checkbox("Show Overlay", &payload.subjectImportance.showOverlay)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show the Develop subject-importance region overlay in the image viewport.");
        }
        if (ImGuiExtras::NodeSliderFloat("Overlay Opacity", "##SubjectRegionOverlayOpacity",
            &payload.subjectImportance.overlayOpacity, 0.05f, 1.0f, "%.2f", controlWidth)) {
            changed = true;
        }

        if (ImGui::Checkbox("Show Interpreted Map", &payload.subjectImportance.showInterpretedMapOverlay)) {
            if (payload.subjectImportance.showInterpretedMapOverlay) {
                payload.subjectImportance.showOverlay = true;
            }
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show the compact 5x5 SubjectImportanceMapV1 diagnostic derived from current regions and brush strokes. This is solver interpretation, not the future edge-aware map.");
        }
        if (ImGuiExtras::NodeSliderFloat("Map Opacity", "##SubjectInterpretedMapOpacity",
            &payload.subjectImportance.interpretedMapOpacity, 0.05f, 1.0f, "%.2f", controlWidth)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Adjust only the diagnostic map display opacity. This does not change Auto solving.");
        }

        if (ImGui::Checkbox("Show Refined Map", &payload.subjectImportance.showRefinedMapOverlay)) {
            if (payload.subjectImportance.showRefinedMapOverlay) {
                payload.subjectImportance.showOverlay = true;
            }
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show the SubjectRefinedMapV1 diagnostic confidence map derived from current marks and solved subject/readability/protection/mood evidence. It is not AI detection or final edge-aware refinement.");
        }
        if (ImGuiExtras::NodeSliderFloat("Refined Opacity", "##SubjectRefinedMapOpacity",
            &payload.subjectImportance.refinedMapOpacity, 0.05f, 1.0f, "%.2f", controlWidth)) {
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Adjust only the refined diagnostic map display opacity. This does not change Auto solving.");
        }

        bool brushEnabled = payload.subjectImportance.brushEnabled;
        if (ImGui::Checkbox("Brush Edit", &brushEnabled)) {
            payload.subjectImportance.brushEnabled = brushEnabled;
            if (brushEnabled) {
                payload.subjectImportance.enabled = true;
                payload.subjectImportance.showOverlay = true;
            }
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Paint Develop subject-importance strokes in the image viewport.");
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
        ImGui::SameLine();
        bool brushSubtract = payload.subjectImportance.brushSubtract;
        if (ImGui::Checkbox("Reduce", &brushSubtract)) {
            payload.subjectImportance.brushSubtract = brushSubtract;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Paint strokes that reduce or de-prioritize marked importance.");
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }

        int brushMode = static_cast<int>(payload.subjectImportance.brushMode);
        const char* subjectImportanceModeLabels[] = {
            "Important",
            "Reveal",
            "Protect",
            "Preserve Mood",
            "Ignore / Low Priority"
        };
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Brush Mode", &brushMode, subjectImportanceModeLabels, IM_ARRAYSIZE(subjectImportanceModeLabels))) {
            payload.subjectImportance.brushMode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(
                std::clamp(brushMode, 0, IM_ARRAYSIZE(subjectImportanceModeLabels) - 1));
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", EditorNodeGraph::DevelopSubjectImportanceModeDescription(payload.subjectImportance.brushMode));
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
        if (ImGuiExtras::NodeSliderFloat("Brush Size", "##SubjectBrushRadius", &payload.subjectImportance.brushRadius, 0.005f, 0.25f, "%.3f", controlWidth)) {
            changed = true;
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
        if (ImGuiExtras::NodeSliderFloat("Brush Strength", "##SubjectBrushStrength", &payload.subjectImportance.brushStrength, 0.0f, 1.0f, "%.2f", controlWidth)) {
            changed = true;
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
        if (ImGuiExtras::NodeSliderFloat("Brush Soft Edge", "##SubjectBrushFeather", &payload.subjectImportance.brushFeather, 0.0f, 1.0f, "%.2f", controlWidth)) {
            changed = true;
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }

        const float smallButtonWidth = std::max(92.0f, (controlWidth - buttonGap) * 0.5f);
        if (ImGuiExtras::RichFullWidthButton("Add Region", smallButtonWidth, 0.0f)) {
            EditorNodeGraph::DevelopSubjectImportanceRegion region;
            region.id = payload.subjectImportance.nextRegionId++;
            region.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Important;
            payload.subjectImportance.enabled = true;
            payload.subjectImportance.regions.push_back(region);
            payload.subjectImportance.activeRegionId = region.id;
            NormalizeDevelopSubjectImportance(payload.subjectImportance);
            subjectImportanceChanged = true;
            forceAutoReanalysis = true;
            changed = true;
        }
        ImGui::SameLine(0.0f, buttonGap);
        if (ImGuiExtras::RichFullWidthButton("Clear Regions", smallButtonWidth, 0.0f)) {
            if (!payload.subjectImportance.regions.empty()) {
                payload.subjectImportance.regions.clear();
                payload.subjectImportance.nextRegionId = 1;
                payload.subjectImportance.activeRegionId = 0;
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
        }
        if (ImGuiExtras::RichFullWidthButton("Clear Brush", smallButtonWidth, 0.0f)) {
            if (!payload.subjectImportance.strokes.empty()) {
                payload.subjectImportance.strokes.clear();
                payload.subjectImportance.nextStrokeId = 1;
                payload.subjectImportance.activeStrokeId = 0;
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
        }

        auto syncBrushToolFromStroke = [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
            payload.subjectImportance.brushMode = stroke.mode;
            payload.subjectImportance.brushSubtract = stroke.subtract;
            payload.subjectImportance.brushRadius = stroke.radius;
            payload.subjectImportance.brushFeather = stroke.feather;
            payload.subjectImportance.brushStrength = stroke.strength;
        };

        for (std::size_t strokeIndex = 0; strokeIndex < payload.subjectImportance.strokes.size(); ++strokeIndex) {
            EditorNodeGraph::DevelopSubjectImportanceStroke& stroke =
                payload.subjectImportance.strokes[strokeIndex];
            ImGui::PushID("SubjectStroke");
            ImGui::PushID(static_cast<int>(stroke.id));
            ImGui::Separator();
            bool strokeEnabled = stroke.enabled;
            if (ImGui::Checkbox("##SubjectStrokeEnabled", &strokeEnabled)) {
                stroke.enabled = strokeEnabled;
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
            if (ImGui::IsItemActive()) {
                subjectImportanceActive = true;
            }
            ImGui::SameLine();
            const bool activeStroke = payload.subjectImportance.activeStrokeId == stroke.id;
            if (ImGui::RadioButton("##SubjectStrokeActive", activeStroke)) {
                payload.subjectImportance.activeStrokeId = stroke.id;
                syncBrushToolFromStroke(stroke);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Select this brush stroke and copy its settings to the brush tool.");
            }
            ImGui::SameLine();
            ImGui::Text("Stroke %d  (%d pt%s)",
                stroke.id,
                static_cast<int>(stroke.points.size()),
                stroke.points.size() == 1 ? "" : "s");
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                const bool deletingActiveStroke = payload.subjectImportance.activeStrokeId == stroke.id;
                payload.subjectImportance.strokes.erase(
                    payload.subjectImportance.strokes.begin() + static_cast<std::ptrdiff_t>(strokeIndex));
                if (deletingActiveStroke) {
                    payload.subjectImportance.activeStrokeId =
                        payload.subjectImportance.strokes.empty() ? 0 : payload.subjectImportance.strokes.front().id;
                    if (!payload.subjectImportance.strokes.empty()) {
                        syncBrushToolFromStroke(payload.subjectImportance.strokes.front());
                    }
                }
                --strokeIndex;
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
                ImGui::PopID();
                ImGui::PopID();
                continue;
            }

            bool strokeSubtract = stroke.subtract;
            if (ImGui::Checkbox("Reduce Stroke", &strokeSubtract)) {
                stroke.subtract = strokeSubtract;
                if (payload.subjectImportance.activeStrokeId == stroke.id) {
                    payload.subjectImportance.brushSubtract = stroke.subtract;
                }
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reduce strokes de-prioritize the painted path instead of increasing subject importance.");
            }
            if (ImGui::IsItemActive()) {
                subjectImportanceActive = true;
            }

            int strokeMode = static_cast<int>(stroke.mode);
            ImGui::SetNextItemWidth(controlWidth);
            if (ImGui::Combo("Stroke Mode", &strokeMode, subjectImportanceModeLabels, IM_ARRAYSIZE(subjectImportanceModeLabels))) {
                stroke.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(
                    std::clamp(strokeMode, 0, IM_ARRAYSIZE(subjectImportanceModeLabels) - 1));
                if (payload.subjectImportance.activeStrokeId == stroke.id) {
                    payload.subjectImportance.brushMode = stroke.mode;
                }
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", EditorNodeGraph::DevelopSubjectImportanceModeDescription(stroke.mode));
            }
            if (ImGui::IsItemActive()) {
                subjectImportanceActive = true;
            }

            const bool strokeStrengthChanged = ImGuiExtras::NodeSliderFloat(
                "Stroke Strength",
                "##SubjectStrokeStrength",
                &stroke.strength,
                0.0f,
                1.0f,
                "%.2f",
                controlWidth);
            if (strokeStrengthChanged && payload.subjectImportance.activeStrokeId == stroke.id) {
                payload.subjectImportance.brushStrength = stroke.strength;
            }
            markSubjectImportanceEdit(strokeStrengthChanged);

            const bool strokeRadiusChanged = ImGuiExtras::NodeSliderFloat(
                "Stroke Size",
                "##SubjectStrokeRadius",
                &stroke.radius,
                0.005f,
                0.25f,
                "%.3f",
                controlWidth);
            if (strokeRadiusChanged && payload.subjectImportance.activeStrokeId == stroke.id) {
                payload.subjectImportance.brushRadius = stroke.radius;
            }
            markSubjectImportanceEdit(strokeRadiusChanged);

            const bool strokeFeatherChanged = ImGuiExtras::NodeSliderFloat(
                "Stroke Soft Edge",
                "##SubjectStrokeFeather",
                &stroke.feather,
                0.0f,
                1.0f,
                "%.2f",
                controlWidth);
            if (strokeFeatherChanged && payload.subjectImportance.activeStrokeId == stroke.id) {
                payload.subjectImportance.brushFeather = stroke.feather;
            }
            markSubjectImportanceEdit(strokeFeatherChanged);
            ImGui::PopID();
            ImGui::PopID();
        }

        for (std::size_t regionIndex = 0; regionIndex < payload.subjectImportance.regions.size(); ++regionIndex) {
            EditorNodeGraph::DevelopSubjectImportanceRegion& region =
                payload.subjectImportance.regions[regionIndex];
            ImGui::PushID(static_cast<int>(region.id));
            ImGui::Separator();
            bool regionEnabled = region.enabled;
            if (ImGui::Checkbox("##SubjectRegionEnabled", &regionEnabled)) {
                region.enabled = regionEnabled;
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
            if (ImGui::IsItemActive()) {
                subjectImportanceActive = true;
            }
            ImGui::SameLine();
            const bool activeRegion = payload.subjectImportance.activeRegionId == region.id;
            if (ImGui::RadioButton("##SubjectRegionActive", activeRegion)) {
                payload.subjectImportance.activeRegionId = region.id;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Select this region for viewport editing.");
            }
            ImGui::SameLine();
            ImGui::Text("Region %d", region.id);
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                const bool deletingActiveRegion = payload.subjectImportance.activeRegionId == region.id;
                payload.subjectImportance.regions.erase(payload.subjectImportance.regions.begin() + static_cast<std::ptrdiff_t>(regionIndex));
                if (deletingActiveRegion) {
                    payload.subjectImportance.activeRegionId =
                        payload.subjectImportance.regions.empty() ? 0 : payload.subjectImportance.regions.front().id;
                }
                --regionIndex;
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
                ImGui::PopID();
                continue;
            }

            int regionMode = static_cast<int>(region.mode);
            ImGui::SetNextItemWidth(controlWidth);
            if (ImGui::Combo("Mode", &regionMode, subjectImportanceModeLabels, IM_ARRAYSIZE(subjectImportanceModeLabels))) {
                region.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(
                    std::clamp(regionMode, 0, IM_ARRAYSIZE(subjectImportanceModeLabels) - 1));
                subjectImportanceChanged = true;
                forceAutoReanalysis = true;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", EditorNodeGraph::DevelopSubjectImportanceModeDescription(region.mode));
            }
            if (ImGui::IsItemActive()) {
                subjectImportanceActive = true;
            }

            markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Strength", "##SubjectRegionStrength", &region.strength, 0.0f, 1.0f, "%.2f", controlWidth));
            markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Center X", "##SubjectRegionCenterX", &region.centerX, 0.0f, 1.0f, "%.2f", controlWidth));
            markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Center Y", "##SubjectRegionCenterY", &region.centerY, 0.0f, 1.0f, "%.2f", controlWidth));
            markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Width", "##SubjectRegionRadiusX", &region.radiusX, 0.01f, 1.0f, "%.2f", controlWidth));
            markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Height", "##SubjectRegionRadiusY", &region.radiusY, 0.01f, 1.0f, "%.2f", controlWidth));
            markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Soft Edge", "##SubjectRegionFeather", &region.feather, 0.0f, 1.0f, "%.2f", controlWidth));
            ImGui::PopID();
        }
        if (subjectImportanceChanged) {
            NormalizeDevelopSubjectImportance(payload.subjectImportance);
            RecordRawDevelopInteraction(node.id);
        } else if (subjectImportanceActive) {
            RecordRawDevelopInteraction(node.id);
        }

        const bool subjectSceneIntentChanged = resettableDevelopSliderFloat(
            "Subject / Scene Intent",
            "##DevelopSubjectSceneBias",
            &draftState.guidance.subjectSceneBias,
            defaultGuidance.subjectSceneBias,
            -1.0f,
            1.0f,
            "%+.2f");
        const ImGuiExtras::NodeControlState& subjectSceneIntentState = ImGuiExtras::GetNodeControlState();
        autoSliderChanged |= subjectSceneIntentChanged;
        autoSliderActive |= subjectSceneIntentState.lastActive;
        if (subjectSceneIntentState.lastHovered) {
            ImGui::SetTooltip("Negative favors global scene integrity; positive gives likely or user-marked subject priority. This biases Auto solving, not a hard mask.");
        }
        const bool moodReadabilityChanged = resettableDevelopSliderFloat(
            "Mood / Readability",
            "##DevelopMoodReadabilityBias",
            &draftState.guidance.moodReadabilityBias,
            defaultGuidance.moodReadabilityBias,
            -1.0f,
            1.0f,
            "%+.2f");
        const ImGuiExtras::NodeControlState& moodReadabilityState = ImGuiExtras::GetNodeControlState();
        autoSliderChanged |= moodReadabilityChanged;
        autoSliderActive |= moodReadabilityState.lastActive;
        if (moodReadabilityState.lastHovered) {
            ImGui::SetTooltip("Negative preserves low-key mood; positive asks Auto to improve subject or midtone readability when quality allows.");
        }
        renderAutoGuidanceSlider("Auto Strength", "##DevelopAutoStrength", &draftState.guidance.autoStrength, defaultGuidance.autoStrength, 0.0f, 2.4f, "%.2f");
        const bool brightnessIntentChanged = resettableDevelopSliderFloat(
            "Brightness Intent",
            "##DevelopExposureBias",
            &draftState.guidance.exposureBias,
            defaultGuidance.exposureBias,
            -2.0f,
            2.0f,
            "%+.2f");
        const ImGuiExtras::NodeControlState& brightnessIntentState = ImGuiExtras::GetNodeControlState();
        autoSliderChanged |= brightnessIntentChanged;
        autoSliderActive |= brightnessIntentState.lastActive;
        if (brightnessIntentState.lastHovered) {
            ImGui::SetTooltip("Solver-facing rendered brightness intent. Auto may adjust RAW EV, local exposure, and tone together; Manual RAW Exposure is the literal EV control.");
        }
        renderAutoGuidanceSlider("Dynamic Range", "##DevelopDynamicRange", &draftState.guidance.dynamicRange, defaultGuidance.dynamicRange, 0.25f, 3.0f, "%.2f");
        renderAutoGuidanceSlider("Shadow Lift", "##DevelopShadowLift", &draftState.guidance.shadowLift, defaultGuidance.shadowLift, -1.25f, 1.25f, "%.2f");
        renderAutoGuidanceSlider("Highlight Guard", "##DevelopHighlightGuard", &draftState.guidance.highlightGuard, defaultGuidance.highlightGuard, -1.25f, 1.25f, "%.2f");
        renderAutoGuidanceSlider("Highlight Character", "##DevelopHighlightCharacter", &draftState.guidance.highlightCharacter, defaultGuidance.highlightCharacter, -1.25f, 1.25f, "%.2f");
        renderAutoGuidanceSlider("Contrast Bias", "##DevelopContrastBias", &draftState.guidance.contrastBias, defaultGuidance.contrastBias, -1.25f, 1.25f, "%.2f");
        ImGui::TextDisabled("Stage map: Subject / Scene Intent steers solver priority. Brightness Intent can move RAW EV, Scene Prep, and Prepared Tone together. Contrast Bias is tone.");

        if (autoSliderActive) {
            RecordRawDevelopInteraction(node.id);
        }

        const bool draftCommitRequested = !autoSliderActive &&
            (draftState.editing || autoSliderChanged) &&
            !SameDevelopAutoGuidance(draftState.guidance, payload.autoGuidance);
        if (draftCommitRequested) {
            RecordRawDevelopInteraction(node.id);
            forceAutoReanalysis = true;
            forceFullAutoReanalysis =
                forceFullAutoReanalysis ||
                std::abs(draftState.guidance.exposureBias - payload.autoGuidance.exposureBias) > 0.0001f;
            payload.autoGuidance = draftState.guidance;
            changed = true;
        }
        draftState.editing = autoSliderActive;
        if (!draftState.editing) {
            draftState.guidance = payload.autoGuidance;
        }

        changed |= UpdateDevelopAutoState(
            node.id,
            payload,
            metadata,
            forceAutoReanalysis || changed,
            forceFullAutoReanalysis);

        const nlohmann::json& toneJson = payload.integratedToneLayerJson;
        double candidateFeedbackQuietRemaining = 0.0;
        if (GetDevelopCandidateFeedbackDeferredStatus(
                node.id,
                ImGui::GetTime(),
                candidateFeedbackQuietRemaining)) {
            if (candidateFeedbackQuietRemaining > 0.01) {
                ImGui::TextDisabled(
                    "Candidate feedback: waiting for edits to settle (%.1fs)",
                    candidateFeedbackQuietRemaining);
            } else {
                ImGui::TextDisabled("Candidate feedback: queued after edits settled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Auto keeps the viewport responsive while pausing expensive candidate feedback during recent Develop edits.");
            }
        }
        if (toneJson.value("autoSceneStatsValid", false)) {
            ImGui::TextDisabled(
                "Auto mode: %s",
                EditorNodeGraph::DevelopAutoIntentLabel(payload.autoGuidance.intent));
            if (toneJson.contains("autoCandidateSelectedLabel")) {
                ImGui::TextDisabled(
                    "Auto candidate: %s  |  score %.2f  |  pass %d%s",
                    toneJson.value("autoCandidateSelectedLabel", std::string("Base Solve")).c_str(),
                    toneJson.value("autoCandidateSelectedScore", 0.0f),
                    toneJson.value("autoCandidateConvergencePass", 0),
                    toneJson.value("autoCandidateConverged", false) ? " converged" : "");
                const std::string renderMetricsStatus =
                    toneJson.value("autoCandidateRenderMetricsStatus", std::string());
                if (!renderMetricsStatus.empty()) {
                    ImGui::TextDisabled(
                        "Candidate renders: %s  |  measured %d  |  failed %d",
                        renderMetricsStatus.c_str(),
                        toneJson.value("autoCandidateRenderedCount", 0),
                        toneJson.value("autoCandidateRenderedFailureCount", 0));
                    if (toneJson.value("autoCandidateRenderedTimingVersion", std::string()) == "CandidateRenderTimingV1") {
                        const std::string slowestLabel =
                            toneJson.value("autoCandidateRenderedSlowestLabel", std::string());
                        ImGui::TextDisabled(
                            "Feedback timing: total %.0f ms  |  graph %.0f  |  readback %.0f  |  analysis %.0f%s%s",
                            toneJson.value("autoCandidateRenderedTotalElapsedMs", 0.0),
                            toneJson.value("autoCandidateRenderedFinalGraphMs", 0.0) +
                                toneJson.value("autoCandidateRenderedPreFinishGraphMs", 0.0),
                            toneJson.value("autoCandidateRenderedFinalReadbackMs", 0.0) +
                                toneJson.value("autoCandidateRenderedPreFinishReadbackMs", 0.0),
                            toneJson.value("autoCandidateRenderedFinalAnalysisMs", 0.0) +
                                toneJson.value("autoCandidateRenderedPreFinishAnalysisMs", 0.0),
                            slowestLabel.empty() ? "" : "  |  slowest ",
                            slowestLabel.c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "Diagnostic timing for rendered Auto candidate feedback. These numbers do not affect scoring.");
                        }
                    }
                    if (toneJson.value("autoCandidateRenderedMetricReadbackCapped", false)) {
                        ImGui::TextDisabled(
                            "Metric readback: capped to %d px  |  final %d  |  pre-finish %d",
                            toneJson.value("autoCandidateRenderedMetricReadbackMaxDimension", 0),
                            toneJson.value("autoCandidateRenderedMetricReadbackDownsampledCount", 0),
                            toneJson.value("autoCandidateRenderedPreFinishMetricReadbackDownsampledCount", 0));
                    }
                    const std::string renderedBestLabel =
                        toneJson.value("autoCandidateRenderedBestLabel", std::string());
                    if (!renderedBestLabel.empty()) {
                        ImGui::TextDisabled(
                            "Rendered best: %s  |  metric %.2f",
                            renderedBestLabel.c_str(),
                            toneJson.value("autoCandidateRenderedBestScore", 0.0f));
                    }
                }
            }
            ImGui::TextDisabled(
                "Current solve: exposure %+.2f EV  |  WB %s  |  highlights %s",
                settings.exposureStops,
                Raw::WhiteBalanceModeName(settings.whiteBalanceMode),
                Raw::HighlightReconstructionModeName(settings.highlightMode));
            ImGui::TextDisabled(
                "Scene prep: strength %.2f  |  shadow bias %+.2f EV  |  highlight guard %+.2f",
                scenePrepSettings.strength,
                scenePrepSettings.maxEvBias,
                scenePrepSettings.highlightProtectionBias);
            ImGui::TextDisabled(
                "Brightness distribution: intent %+.2f  |  RAW scale x%.2f  |  local EV %+.2f / %+.2f  |  tone contrast %+.2f",
                toneJson.value("autoBrightnessIntent", payload.autoGuidance.exposureBias),
                toneJson.value("autoAuthoredRawExposureScale", std::exp2(settings.exposureStops)),
                toneJson.value("autoAuthoredLocalMinEvBias", scenePrepSettings.minEvBias),
                toneJson.value("autoAuthoredLocalMaxEvBias", scenePrepSettings.maxEvBias),
                toneJson.value("autoContrastBias", payload.autoGuidance.contrastBias));
            ImGui::TextDisabled(
                "Exposure diagnostics: clipping %.2f%%  |  highlight pressure %.2f  |  noise risk %.2f  |  HDR spread %.2f EV",
                toneJson.value("autoExposureDiagnosticClippingRatio", 0.0f) * 100.0f,
                toneJson.value("autoExposureDiagnosticHighlightPressure", 0.0f),
                toneJson.value("autoExposureDiagnosticNoiseRisk", 0.0f),
                toneJson.value("autoExposureDiagnosticHdrSpreadEv", 0.0f));
            if (toneJson.value("autoDynamicRangeStrategyVersion", std::string()) == "DynamicRangeStrategyV1") {
                ImGui::TextDisabled(
                    "Range strategy: %s  |  highlight %.2f  |  shadow %.2f  |  noise %.2f",
                    toneJson.value("autoDynamicRangeStrategyLabel", std::string("Balanced Range")).c_str(),
                    toneJson.value("autoDynamicRangeHighlightImportance", 0.0f),
                    toneJson.value("autoDynamicRangeShadowReadability", 0.0f),
                    toneJson.value("autoDynamicRangeNoiseConstraint", 0.0f));
                ImGui::TextDisabled(
                    "Range probes: broad %.2f  |  lum %.2f  |  read %.2f  |  halo %.2f  |  sep %.2f  |  spec %.2f  |  floor %.2f",
                    toneJson.value("autoDynamicRangeBroadHighlightGuardNeed", 0.0f),
                    toneJson.value("autoDynamicRangeHighlightBrightnessAnchorNeed", 0.0f),
                    toneJson.value("autoDynamicRangeShadowReadabilityLiftNeed", 0.0f),
                    toneJson.value("autoDynamicRangeLocalHaloGuardNeed", 0.0f),
                    toneJson.value("autoDynamicRangeNaturalContrastGuardNeed", 0.0f),
                    toneJson.value("autoDynamicRangeSpecularHighlightToleranceNeed", 0.0f),
                    toneJson.value("autoDynamicRangeShadowNoiseFloorNeed", 0.0f));
                if (toneJson.value("autoDynamicRangeStrategyMapVersion", std::string()) == "DynamicRangeStrategyMapV1") {
                    ImGui::TextDisabled(
                        "Strategy map: highlight/shadow %+.2f  |  contrast/range %+.2f",
                        toneJson.value("autoDynamicRangeStrategyMapHighlightShadowAxis", 0.0f),
                        toneJson.value("autoDynamicRangeStrategyMapContrastRangeAxis", 0.0f));
                }
                if (toneJson.value("autoSubjectSceneIntentVersion", std::string()) == "SubjectSceneIntentV1") {
                    ImGui::TextDisabled(
                        "Subject / scene: %s  |  %s  |  regions %d  |  strokes %d  |  conf %.2f  |  subject/scene %+.2f  |  mood/read %+.2f",
                        toneJson.value("autoSubjectSceneIntentLabel", std::string("Automatic Scene Balance")).c_str(),
                        toneJson.value("autoSubjectSceneUserGuidanceStatus", std::string("notAvailable")).c_str(),
                        toneJson.value("autoSubjectSceneImportanceRegionCount", 0),
                        toneJson.value("autoSubjectSceneImportanceStrokeCount", 0),
                        toneJson.value("autoSubjectSceneAutomaticConfidence", 0.0f),
                        toneJson.value("autoSubjectSceneSubjectSceneAxis", 0.0f),
                        toneJson.value("autoSubjectSceneMoodReadabilityAxis", 0.0f));
                    if (toneJson.value("autoSubjectSceneImportanceMapVersion", std::string()) == "SubjectImportanceMapV1") {
                        ImGui::TextDisabled(
                            "Importance map: %s  |  cov %.2f  |  peak %.2f  |  low %.2f  |  center %.2f",
                            toneJson.value("autoSubjectSceneImportanceMapStatus", std::string("disabled")).c_str(),
                            toneJson.value("autoSubjectSceneImportanceMapCoverage", 0.0f),
                            toneJson.value("autoSubjectSceneImportanceMapPeak", 0.0f),
                            toneJson.value("autoSubjectSceneImportanceMapLowPriorityCoverage", 0.0f),
                            toneJson.value("autoSubjectSceneImportanceMapCenterBias", 0.0f));
                    }
                    if (toneJson.value("autoSubjectSceneRefinedMapVersion", std::string()) == "SubjectRefinedMapV1") {
                        ImGui::TextDisabled(
                            "Refined map: %s  |  cov %.2f  |  conf %.2f  |  read %.2f  |  prot %.2f  |  mood %.2f",
                            toneJson.value("autoSubjectSceneRefinedMapStatus", std::string("disabled")).c_str(),
                            toneJson.value("autoSubjectSceneRefinedMapCoverage", 0.0f),
                            toneJson.value("autoSubjectSceneRefinedMapConfidence", 0.0f),
                            toneJson.value("autoSubjectSceneRefinedMapReadabilityCoverage", 0.0f),
                            toneJson.value("autoSubjectSceneRefinedMapProtectionCoverage", 0.0f),
                            toneJson.value("autoSubjectSceneRefinedMapMoodCoverage", 0.0f));
                    }
                    const nlohmann::json subjectSolveNotes =
                        toneJson.value("autoSubjectSceneSolveNotes", nlohmann::json::array());
                    if (subjectSolveNotes.is_array() && !subjectSolveNotes.empty()) {
                        int shownSubjectNotes = 0;
                        for (const nlohmann::json& note : subjectSolveNotes) {
                            if (!note.is_object()) {
                                continue;
                            }
                            const std::string text = note.value("text", std::string());
                            if (text.empty()) {
                                continue;
                            }
                            ImGui::TextWrapped("Subject note: %s", text.c_str());
                            ++shownSubjectNotes;
                            if (shownSubjectNotes >= 2) {
                                break;
                            }
                        }
                    }
                }
                if (toneJson.value("autoDynamicRangeLocalExposureStrategyVersion", std::string()) == "LocalExposureStrategyV1") {
                    ImGui::TextDisabled(
                        "Local exposure: %s  |  range %.2f  |  high %.2f  |  shadow %.2f  |  guard %.2f / %.2f  |  damage %.2f",
                        toneJson.value("autoDynamicRangeLocalExposureStrategyLabel", std::string("Balanced Local Prep")).c_str(),
                        toneJson.value("autoDynamicRangeLocalExposureRangeRedistribution", 0.0f),
                        toneJson.value("autoDynamicRangeLocalExposureHighlightCompression", 0.0f),
                        toneJson.value("autoDynamicRangeLocalExposureShadowOpening", 0.0f),
                        toneJson.value("autoDynamicRangeLocalExposureNoiseGuard", 0.0f),
                        toneJson.value("autoDynamicRangeLocalExposureHaloGuard", 0.0f),
                        toneJson.value("autoDynamicRangeLocalExposureDamageRisk", 0.0f));
                }
                const std::string strategyReason =
                    toneJson.value("autoDynamicRangeStrategyReason", std::string());
                if (!strategyReason.empty()) {
                    ImGui::TextWrapped("%s", strategyReason.c_str());
                }
                if (toneJson.value("autoDynamicRangeRegionEvidenceValid", false)) {
                    ImGui::TextDisabled(
                        "Regional evidence: highlight %.2f  |  meaning %.2f  |  gray %.2f  |  shadow %.2f  |  local %.2f  |  EV %.2f / %.1f",
                        toneJson.value("autoDynamicRangeLocalHighlightHotspotRisk", 0.0f),
                        toneJson.value("autoDynamicRangeMeaningfulHighlightPressure", 0.0f),
                        toneJson.value("autoDynamicRangeHighlightGrayRisk", 0.0f),
                        toneJson.value("autoDynamicRangeLocalShadowHotspotRisk", 0.0f),
                        toneJson.value("autoDynamicRangeLocalRangeConflict", 0.0f),
                        toneJson.value("autoDynamicRangeLocalEvConflict", 0.0f),
                        toneJson.value("autoDynamicRangeLocalEvSpreadStops", 0.0f));
                }
            }
            ImGui::TextDisabled(
                "RAW placement: authored %+.2f EV  |  requested base %+.2f EV",
                toneJson.value("autoAuthoredRawExposureEv", settings.exposureStops),
                toneJson.value("autoExposureDiagnosticRecommendedBaseEv", 0.0f));
            if (toneJson.contains("middleGrey") || toneJson.contains("localBaselineStrength")) {
                ImGui::TextDisabled(
                    "Tone placement: middle grey %.3f  |  prepared local %.2f",
                    toneJson.value("middleGrey", 0.18f),
                    toneJson.value("localBaselineStrength", 0.0f));
            }
        } else {
            ImGui::TextDisabled("Develop will analyze the connected RAW image on the next render.");
        }
        ImGui::TextDisabled("Switch to Manual when the automatic result is close and you want direct curve, targeting, or RAW cleanup edits.");
    } else {
        m_DevelopAutoSolveTriggerHashes.erase(node.id);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RAW COLOR", 4.0f);
        const char* wbLabels[] = { "Camera WB", "Auto WB", "Neutral", "Manual" };
        int wbMode = static_cast<int>(settings.whiteBalanceMode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("White Balance", &wbMode, wbLabels, 4)) {
            settings.whiteBalanceMode = static_cast<Raw::WhiteBalanceMode>(std::clamp(wbMode, 0, 3));
            changed = true;
        }
        changed |= resettableDevelopSliderFloat("Red Mult", "##RawWbR", &settings.manualWhiteBalance[0], defaultSettings.manualWhiteBalance[0], 0.05f, 16.0f, "%.3f");
        changed |= resettableDevelopSliderFloat("Green Mult", "##RawWbG", &settings.manualWhiteBalance[1], defaultSettings.manualWhiteBalance[1], 0.05f, 16.0f, "%.3f");
        changed |= resettableDevelopSliderFloat("Blue Mult", "##RawWbB", &settings.manualWhiteBalance[2], defaultSettings.manualWhiteBalance[2], 0.05f, 16.0f, "%.3f");
        ImGui::TextDisabled("Effective WB RGB: %.3f %.3f %.3f", effectiveWb[0], effectiveWb[1], effectiveWb[2]);
        if (!metadata.whiteBalanceSource.empty()) {
            ImGui::TextDisabled("WB source: %s", metadata.whiteBalanceSource.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RAW EXPOSURE + RANGE", 4.0f);
        auto& exposureDraft = m_RawDevelopExposureDrafts[node.id];
        if (!exposureDraft.editing) {
            exposureDraft.exposureStops = settings.exposureStops;
        }
        const bool baselineExposureDraftChanged = resettableDevelopSliderFloat(
            "RAW Exposure / EV",
            "##RawExposure",
            &exposureDraft.exposureStops,
            defaultSettings.exposureStops,
            -8.0f,
            8.0f,
            "%+.2f EV");
        const ImGuiExtras::NodeControlState& baselineExposureState = ImGuiExtras::GetNodeControlState();
        if (baselineExposureState.lastHovered) {
            ImGui::SetTooltip("+1 EV multiplies scene-linear values by 2 before Scene Prep, Finish Tone, and View Transform. -1 EV multiplies by 0.5.");
        }
        if (baselineExposureState.lastActive) {
            exposureDraft.editing = true;
            RecordRawDevelopInteraction(node.id);
        } else if (exposureDraft.editing) {
            exposureDraft.editing = false;
            if (std::abs(exposureDraft.exposureStops - settings.exposureStops) > 0.0001f) {
                settings.exposureStops = exposureDraft.exposureStops;
                changed = true;
            }
        } else if (baselineExposureDraftChanged &&
                   std::abs(exposureDraft.exposureStops - settings.exposureStops) > 0.0001f) {
            settings.exposureStops = exposureDraft.exposureStops;
            changed = true;
        }
        if (metadata.hasDngBaselineExposure) {
            ImGui::TextDisabled("DNG baseline exposure metadata: %+.2f EV", metadata.dngBaselineExposure);
        }
        ImGui::TextDisabled(
            "Scene-linear scale: x%.2f. Rendered brightness is shaped later by Scene Prep and Finish Tone.",
            std::exp2(settings.exposureStops));

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("SCENE PREP: LOCAL EXPOSURE", 4.0f);
        changed |= RenderDevelopScenePrepNormalControls(scenePrepSettings, controlWidth, "RawDevelopScenePrep");
        RenderPreLocalExposureSummarySection(liveScenePrepSummary, hasRawSourceInput, controlWidth);

        if (showAdvancedManualControls) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGuiExtras::RichSectionLabel("SCENE PREP ADVANCED", 4.0f);
            changed |= RenderDevelopScenePrepAdvancedBiasControls(scenePrepSettings, controlWidth, "RawDevelopScenePrep");
            changed |= RenderAutoGainPresetControls(scenePrepSettings, controlWidth, "RawDevelopScenePrep", true);
            changed |= RenderPreLocalExposureExpertOverrides(scenePrepSettings, controlWidth, "RawDevelopScenePrep");
            changed |= RenderPreLocalExposureSpatialModel(scenePrepSettings, controlWidth, "RawDevelopScenePrep");
            changed |= RenderPreLocalExposureSmoothing(scenePrepSettings, controlWidth, "RawDevelopScenePrep");

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGuiExtras::RichSectionLabel("SCENE PREP DIAGNOSTICS", 4.0f);
            changed |= RenderAutoGainDiagnosticsControls(scenePrepSettings, controlWidth, "RawDevelopScenePrep", true, "Preview");
        }

        if (showAdvancedManualControls) {
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
            changed |= resettableDevelopSliderFloat("Strength", "##RawHighlightStrength", &settings.highlightStrength, defaultSettings.highlightStrength, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderFloat("Clip Threshold", "##RawHighlightThreshold", &settings.highlightThreshold, defaultSettings.highlightThreshold, 0.8f, 1.0f, "%.3f");
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
            changed |= resettableDevelopSliderFloat("False Color Suppression", "##RawFalseColorSuppression", &settings.falseColorSuppression, defaultSettings.falseColorSuppression, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderFloat("Defringe Strength", "##RawDefringeStrength", &settings.defringeStrength, defaultSettings.defringeStrength, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderFloat("Highlight Edge Cleanup", "##RawHighlightEdgeCleanup", &settings.highlightEdgeCleanup, defaultSettings.highlightEdgeCleanup, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderInt("Chroma Radius", "##RawChromaRadius", &settings.chromaRadius, defaultSettings.chromaRadius, 1, 3, "%d px");
            changed |= resettableDevelopSliderFloat("Preserve Real Color", "##RawPreserveRealColor", &settings.preserveRealColor, defaultSettings.preserveRealColor, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderFloat("Red / Cyan CA", "##RawLateralRedCyan", &settings.lateralRedCyan, defaultSettings.lateralRedCyan, -8.0f, 8.0f, "%.2f px");
            changed |= resettableDevelopSliderFloat("Blue / Yellow CA", "##RawLateralBlueYellow", &settings.lateralBlueYellow, defaultSettings.lateralBlueYellow, -8.0f, 8.0f, "%.2f px");
            ImGui::EndDisabled();

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGuiExtras::RichSectionLabel("RAW MOSAIC DENOISE", 4.0f);
            ImGui::BeginDisabled(!demosaicEnabled);
            changed |= ImGuiExtras::NodeCheckbox("Enable", "##RawMosaicDenoiseEnabled", &settings.mosaicDenoise.enabled, controlWidth);
            changed |= ImGuiExtras::NodeCheckbox("Hot Pixel Suppression", "##RawMosaicHotPixels", &settings.mosaicDenoise.hotPixelSuppression, controlWidth);
            changed |= resettableDevelopSliderFloat("Hot Pixel Threshold", "##RawMosaicHotThreshold", &settings.mosaicDenoise.hotPixelThreshold, defaultSettings.mosaicDenoise.hotPixelThreshold, 0.005f, 0.5f, "%.3f");
            changed |= resettableDevelopSliderFloat("Luminance Strength", "##RawMosaicLumaStrength", &settings.mosaicDenoise.lumaStrength, defaultSettings.mosaicDenoise.lumaStrength, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderFloat("Chroma Strength", "##RawMosaicChromaStrength", &settings.mosaicDenoise.chromaStrength, defaultSettings.mosaicDenoise.chromaStrength, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderInt("Radius", "##RawMosaicRadius", &settings.mosaicDenoise.radius, defaultSettings.mosaicDenoise.radius, 1, 4, "%d CFA steps");
            changed |= resettableDevelopSliderFloat("Edge Protection", "##RawMosaicEdgeProtection", &settings.mosaicDenoise.edgeProtection, defaultSettings.mosaicDenoise.edgeProtection, 0.0f, 1.0f, "%.2f");
            changed |= resettableDevelopSliderInt("Iterations", "##RawMosaicIterations", &settings.mosaicDenoise.iterations, defaultSettings.mosaicDenoise.iterations, 1, 2, "%d");
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
        } else {
            ImGui::TextDisabled("Advanced RAW cleanup, exact scene-prep controls, foundation tone, and diagnostics are in the sidebar.");
        }

        NormalizeIntegratedScenePrepSettings(scenePrepSettings);
        const bool developSettingsChangedBeforeFinishTone =
            !SameRawDevelopSettings(payloadBefore.settings, payload.settings) ||
            payloadBefore.scenePrepEnabled != payload.scenePrepEnabled ||
            !SameRawDetailFusionSettings(payloadBefore.scenePrepSettings, payload.scenePrepSettings);

        ToneCurveLayer integratedTone = BuildIntegratedDevelopToneLayer(payload.integratedToneLayerJson);
        RestoreIntegratedToneTransientState(node.id, integratedTone);

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("FINISH TONE: FINAL GRAPH", 4.0f);
        bool integratedToneChanged = integratedTone.RenderDevelopFinishGraphPanel(controlWidth, true, showAdvancedManualControls);
        if (showAdvancedManualControls) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGuiExtras::RichSectionLabel("PREPARED TONE ADVANCED", 4.0f);
            integratedToneChanged |= integratedTone.RenderDevelopPreparedControlsPanel(controlWidth, true);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGuiExtras::RichSectionLabel("FOUNDATION TONE ADVANCED", 4.0f);
            integratedToneChanged |= integratedTone.RenderDevelopFoundationControlsPanel(controlWidth, true);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGuiExtras::RichSectionLabel("LOCAL SCOPE / MASKING", 4.0f);
            integratedToneChanged |= integratedTone.RenderDevelopScopedMaskPanel(this, node.id, controlWidth, true);
        } else {
            ImGui::TextDisabled("Prepared Tone, Foundation Tone, and local masking controls are in the sidebar.");
        }

        if (developSettingsChangedBeforeFinishTone && integratedTone.HasAutoPreparedState()) {
            integratedTone.NotifyUpstreamDevelopChanged();
            integratedToneChanged = true;
        }

        const nlohmann::json integratedToneJson = integratedTone.Serialize();
        StoreIntegratedToneTransientState(node.id, integratedTone);
        if (payload.integratedToneLayerJson != integratedToneJson) {
            payload.integratedToneLayerJson = integratedToneJson;
            changed = true;
        } else if (integratedToneChanged) {
            changed = true;
        }
    }

    if (!hasRawSourceInput) {
        ImGui::EndDisabled();
    }

    const bool developPayloadChanged = changed || !SameRawDevelopPayload(payloadBefore, payload);
    if (developPayloadChanged) {
        RecordRawDevelopInteraction(node.id);
        MarkRenderDirty(node.id);
    }
}

void EditorModule::RenderRawDetailAutoMaskControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        return;
    }

    Raw::RawDetailFusionSettings& settings = node.rawDetailAutoMask.settings;
    const Raw::RawDetailFusionSettings settingsBefore = settings;
    const bool hasImageInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId) != nullptr;
    bool changed = false;

    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.strength = 1.0f;
    settings.invertMask = false;
    settings.maskBlackPoint = 0.0f;
    settings.maskWhitePoint = 1.0f;
    settings.maskGamma = 1.0f;
    settings.manualBlend = 0.0f;

    ImGuiExtras::RichSectionLabel("RAW DETAIL AUTO MASK", 4.0f);
    ImGui::TextDisabled("Input: scene-linear image after Develop");
    ImGui::TextDisabled("Output: generated RGBA16F EV/detail mask");
    ImGui::TextDisabled("Local exposure analysis only; clipped pixels cannot regain missing detail here.");
    ImGui::TextDisabled("Right-click any Pre-Local Exposure control for an explanation.");
    if (!hasImageInput) {
        ImGui::TextWrapped("Connect the Image input from Develop. The generated mask keeps that developed image's aspect and orientation.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("PRESET", 4.0f);
    changed |= RenderAutoGainPresetControls(settings, controlWidth, "RawAutoMask", false);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("BASIC", 4.0f);
    changed |= RenderAutoGainBasicControls(settings, controlWidth, "RawAutoMask", false);
    ImGui::TextDisabled("Auto Safety keeps generated EV ranges inside the natural envelope.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("PROTECTION", 4.0f);
    changed |= RenderAutoGainProtectionControls(settings, controlWidth, "RawAutoMask");

    RenderAutoGainSafetyReadout(settings, controlWidth);

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("ADVANCED", 4.0f);
        changed |= RenderAutoGainAdvancedControls(settings, controlWidth, "RawAutoMask", false);
    } else {
        ImGui::TextDisabled("Enable advanced controls for base radius, target, edge tuning, and developer diagnostics.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("DIAGNOSTICS", 4.0f);
    changed |= RenderAutoGainDiagnosticsControls(settings, controlWidth, "RawAutoMask", advanced, "Preview");
    ImGui::TextDisabled("The mask output remains reusable by Preview, Levels Mask, scopes, and Fusion.");

    ClampRawDetailFusionSettings(settings);

    if (changed || !SameRawDetailFusionSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }
}

void EditorModule::RenderRawDetailFusionControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::RawDetailFusion) {
        return;
    }

    Raw::RawDetailFusionSettings& settings = node.rawDetailFusion.settings;
    const Raw::RawDetailFusionSettings settingsBefore = settings;
    const bool hasImageInput = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId) != nullptr;
    const bool hasHybridMask = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kMaskInputSocketId) != nullptr;
    const RenderPipeline::PreLocalExposureSummary* liveSummary = m_Pipeline.GetPreLocalExposureSummary(node.id);
    bool changed = false;

    ImGuiExtras::RichSectionLabel("PRE-LOCAL EXPOSURE", 4.0f);
    ImGui::TextDisabled("Input: scene-linear image after Develop");
    ImGui::TextDisabled("Output: scene-linear RGB with local pre-exposure balancing");
    ImGui::TextDisabled("Use this to reduce aggressive Tone Curve moves later; it does not recover clipped detail.");
    ImGui::TextDisabled("Still scene-linear HDR; use View Transform downstream for display output.");
    ImGui::TextDisabled("Right-click any Pre-Local Exposure control for an explanation.");
    if (!hasImageInput) {
        ImGui::TextWrapped("Connect the Image input. Pre-Local Exposure analyzes and applies its local gain field automatically when connected.");
    }
    ImGui::TextWrapped("With this window open, click the single-output viewport image to toggle the generated preview map.");

    if (ImGui::Button("Reset Pre-Local Exposure", ImVec2(controlWidth, 0.0f))) {
        settings = Raw::RawDetailFusionSettings{};
        changed = true;
    }

    RenderPreLocalExposureSummarySection(liveSummary, hasImageInput, controlWidth);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("CORE CONTROLS", 4.0f);
    changed |= RenderAutoGainBasicControls(settings, controlWidth, "PreLocalExposure", true);
    ImGui::TextDisabled("Use this stage to set a better local baseline before Tone Curve finishes the tonal placement.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("PROTECTION", 4.0f);
    changed |= RenderAutoGainProtectionControls(settings, controlWidth, "PreLocalExposure");

    if (advanced) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("EXPERT OVERRIDES", 4.0f);
        changed |= RenderPreLocalExposureExpertOverrides(settings, controlWidth, "PreLocalExposure");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("SPATIAL MODEL", 4.0f);
        changed |= RenderPreLocalExposureSpatialModel(settings, controlWidth, "PreLocalExposure");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("SMOOTHING", 4.0f);
        changed |= RenderPreLocalExposureSmoothing(settings, controlWidth, "PreLocalExposure");

        if (hasHybridMask) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::TextDisabled("Legacy manual mask input active.");
        }
    } else {
        ImGui::TextDisabled("Enable advanced controls for expert overrides, spatial model tuning, and smoothing.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("DIAGNOSTICS", 4.0f);
    changed |= RenderAutoGainDiagnosticsControls(settings, controlWidth, "PreLocalExposure", advanced, "Preview");
    ImGui::TextDisabled("Viewport preview uses this channel; normal output remains the final image.");

    settings.mode = hasHybridMask ? Raw::RawDetailFusionMode::Hybrid : Raw::RawDetailFusionMode::AutoAnalyze;
    ClampRawDetailFusionSettings(settings);

    if (changed || !SameRawDetailFusionSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }
}

void EditorModule::RenderHdrMergeControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    if (node.kind != EditorNodeGraph::NodeKind::HdrMerge) {
        return;
    }

    Raw::HdrMergeSettings& settings = node.hdrMerge.settings;
    const Raw::HdrMergeSettings settingsBefore = settings;
    const HdrMergeNodeStatus status = GetHdrMergeNodeStatus(node.id);
    bool changed = false;
    const bool showImage3Controls = status.inputs[2].connected;

    ImGuiExtras::RichSectionLabel("HDR MERGE", 4.0f);
    ImGui::TextDisabled("Input: scene-linear bracket or burst frames");
    ImGui::TextDisabled("Output: scene-linear HDR radiance image");
    ImGui::TextDisabled("This is scene-linear HDR reconstruction. Alignment and deghosting are first-pass tools here; tone mapping stays downstream.");

    const ImVec4 statusColor =
        status.state == HdrMergeRenderState::Failed || status.state == HdrMergeRenderState::IncompatibleInput
            ? ImVec4(0.95f, 0.55f, 0.42f, 1.0f)
            : (status.state == HdrMergeRenderState::BlockedMissingInput
                ? ImVec4(0.92f, 0.76f, 0.42f, 1.0f)
                : ImVec4(0.78f, 0.82f, 0.86f, 1.0f));
    ImGui::TextColored(statusColor, "Status: %s", status.message.c_str());
    ImGui::TextDisabled("Inspection View: %s", HdrMergeDebugViewLabel(status.debugView));
    ImGui::TextDisabled("Alignment: %s", HdrMergeAlignmentModeLabel(settings.alignmentMode));
    ImGui::TextDisabled("Exposure Normalization: %s", HdrMergeExposureModeLabel(settings.exposureMode));
    ImGui::TextDisabled("Reference: %s", HdrMergeReferenceModeLabel(settings.referenceMode));
    ImGui::TextDisabled("Ghost Reduction: %s", HdrMergeDeghostModeLabel(settings.deghostMode));
    ImGui::TextDisabled("Motion Merge Policy: %s", HdrMergeMotionPriorityLabel(settings.motionPriority));
    ImGui::TextDisabled("Reliability Defaults: %s", settings.autoReliability ? "Automatic" : "Manual Override");
    if (!status.normalizationMessage.empty()) {
        ImGui::TextDisabled("Normalization Status: %s", status.normalizationMessage.c_str());
    }
    if (!status.reliabilityMessage.empty()) {
        ImGui::TextDisabled("Reliability Status: %s", status.reliabilityMessage.c_str());
    }
    if (!status.warningMessage.empty()) {
        ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.42f, 1.0f), "%s", status.warningMessage.c_str());
    }
    if (status.stale) {
        ImGui::TextDisabled("The current output is older than the latest HDR Merge settings or connections.");
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("INPUTS", 4.0f);
    for (const HdrMergeInputSummary& input : status.inputs) {
        if (input.label.empty() || (!input.active && input.socketId == EditorNodeGraph::kHdrMergeInput3SocketId)) {
            continue;
        }

        std::string secondary = input.active ? (input.connected ? "Connected" : "Missing") : "Inactive";
        if (input.active && input.connected && !input.compatible) {
            secondary = "Dimension mismatch";
        }
        if (input.width > 0 && input.height > 0) {
            secondary += "  " + std::to_string(input.width) + " x " + std::to_string(input.height);
        }
        ImGui::Text("%s: %s", input.label.c_str(), input.sourceLabel.c_str());
        ImGui::TextDisabled("%s", secondary.c_str());
        if (!input.metadataSummary.empty()) {
            ImGui::TextDisabled("%s", input.metadataSummary.c_str());
        }
        if (!input.normalizationSummary.empty()) {
            ImGui::TextDisabled("%s", input.normalizationSummary.c_str());
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel(advanced ? "ALIGNMENT / MOTION" : "NORMAL", 4.0f);
    int alignmentMode = static_cast<int>(settings.alignmentMode);
    const char* alignmentModes[] = { "Off", "Auto Translation", "Wide Translation" };
    if (ImGuiExtras::NodeCombo("Alignment", "##HdrMergeAlignmentMode", &alignmentMode, alignmentModes, IM_ARRAYSIZE(alignmentModes), controlWidth)) {
        settings.alignmentMode = static_cast<Raw::HdrMergeAlignmentMode>(std::clamp(alignmentMode, 0, 2));
        changed = true;
    }
    int deghostMode = static_cast<int>(settings.deghostMode);
    const char* deghostModes[] = { "Off", "Low", "Medium", "High" };
    if (ImGuiExtras::NodeCombo("Ghost Reduction", "##HdrMergeDeghostMode", &deghostMode, deghostModes, IM_ARRAYSIZE(deghostModes), controlWidth)) {
        settings.deghostMode = static_cast<Raw::HdrMergeDeghostMode>(std::clamp(deghostMode, 0, 3));
        changed = true;
    }
    int exposureMode = static_cast<int>(settings.exposureMode);
    const char* exposureModes[] = { "Metadata (Automatic)", "Manual" };
    if (ImGuiExtras::NodeCombo("Exposure Normalization", "##HdrMergeExposureMode", &exposureMode, exposureModes, IM_ARRAYSIZE(exposureModes), controlWidth)) {
        settings.exposureMode = static_cast<Raw::HdrMergeExposureMode>(std::clamp(exposureMode, 0, 1));
        changed = true;
    }

    if (!advanced) {
        ImGui::TextDisabled("Automatic mode reads shutter, ISO, aperture, and Develop exposure when those sources are available.");
        if (settings.exposureMode == Raw::HdrMergeExposureMode::Metadata && !status.metadataNormalizationReady) {
            ImGui::TextDisabled("Automatic normalization is not fully available for this stack. Open Advanced to inspect the fallback inputs.");
        }
        if (settings.autoReliability && !status.automaticReliabilityReady) {
            ImGui::TextDisabled("Automatic reliability is only partial here. Some inputs are using the manual fallback thresholds.");
        }
        ImGui::TextDisabled("Use the advanced editor for reference-frame override, exposure offsets, and manual reliability tuning.");
    } else {
        int referenceMode = static_cast<int>(settings.referenceMode);
        const char* referenceModes[] = { "Auto", "Frame 1", "Frame 2", "Frame 3" };
        if (ImGuiExtras::NodeCombo("Reference Frame", "##HdrMergeReferenceMode", &referenceMode, referenceModes, IM_ARRAYSIZE(referenceModes), controlWidth)) {
            settings.referenceMode = static_cast<Raw::HdrMergeReferenceMode>(std::clamp(referenceMode, 0, 3));
            changed = true;
        }

        int motionPriority = static_cast<int>(settings.motionPriority);
        const char* motionPriorities[] = { "Prefer Reference", "Blend Static Consensus" };
        if (ImGuiExtras::NodeCombo("Motion Merge Policy", "##HdrMergeMotionPriority", &motionPriority, motionPriorities, IM_ARRAYSIZE(motionPriorities), controlWidth)) {
            settings.motionPriority = static_cast<Raw::HdrMergeMotionPriority>(std::clamp(motionPriority, 0, 1));
            changed = true;
        }

        ImGui::TextDisabled("Wide Translation increases search range but is still translation-only. It is not a full handheld warp model yet.");
        ImGui::TextDisabled("Auto reference prefers the least clipped usable bracket or the sharper middle frame for burst-like inputs.");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("EXPOSURE NORMALIZATION", 4.0f);
        if (settings.exposureMode == Raw::HdrMergeExposureMode::Metadata) {
            changed |= ImGuiExtras::NodeSliderFloat("Frame 1 Offset", "##HdrMergeEvOffset1Advanced", &settings.exposureOffsetEv[0], -4.0f, 4.0f, "%+.2f EV", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Frame 2 Offset", "##HdrMergeEvOffset2Advanced", &settings.exposureOffsetEv[1], -4.0f, 4.0f, "%+.2f EV", controlWidth);
            if (showImage3Controls) {
                changed |= ImGuiExtras::NodeSliderFloat("Frame 3 Offset", "##HdrMergeEvOffset3Advanced", &settings.exposureOffsetEv[2], -4.0f, 4.0f, "%+.2f EV", controlWidth);
            }
            ImGui::TextDisabled("Automatic normalization uses capture exposure metadata plus Develop exposure. These offsets stay available as a precise fallback.");
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Frame 1 EV", "##HdrMergeManualEv1Advanced", &settings.manualExposureEv[0], -12.0f, 12.0f, "%.2f EV", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Frame 2 EV", "##HdrMergeManualEv2Advanced", &settings.manualExposureEv[1], -12.0f, 12.0f, "%.2f EV", controlWidth);
            if (showImage3Controls) {
                changed |= ImGuiExtras::NodeSliderFloat("Frame 3 EV", "##HdrMergeManualEv3Advanced", &settings.manualExposureEv[2], -12.0f, 12.0f, "%.2f EV", controlWidth);
            }
            ImGui::TextDisabled("Manual mode is the fallback for non-RAW inputs, incomplete metadata, or deliberate expert overrides.");
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("RELIABILITY / NOISE", 4.0f);
        changed |= ImGuiExtras::NodeCheckbox("Automatic Reliability", "##HdrMergeAutoReliabilityAdvanced", &settings.autoReliability, controlWidth);
        changed |= ImGuiExtras::NodeCheckbox("Use Noise Model", "##HdrMergeNoiseAwareAdvanced", &settings.noiseAware, controlWidth);
        if (settings.autoReliability) {
            ImGui::TextDisabled("Highlight, shadow, and read-noise limits are derived from RAW white/black levels, ISO, and Develop exposure when available.");
        } else {
            changed |= ImGuiExtras::NodeSliderFloat("Highlight Reliability Threshold", "##HdrMergeClipThresholdAdvanced", &settings.clipThreshold, 0.50f, 4.0f, "%.3f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Highlight Roll-off", "##HdrMergeClipFeatherAdvanced", &settings.clipFeather, 0.001f, 1.0f, "%.3f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Shadow Reliability Threshold", "##HdrMergeBlackThresholdAdvanced", &settings.blackThreshold, 0.0f, 0.25f, "%.4f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Shadow Roll-off", "##HdrMergeBlackFeatherAdvanced", &settings.blackFeather, 0.001f, 0.50f, "%.4f", controlWidth);
            changed |= ImGuiExtras::NodeSliderFloat("Read-noise Override", "##HdrMergeReadNoiseAdvanced", &settings.readNoise, 0.0f, 0.10f, "%.4f", controlWidth);
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("PREVIEW", 4.0f);
        int debugView = static_cast<int>(settings.debugView);
        const char* debugViews[] = {
            "Final Image",
            "Contribution",
            "Clipping",
            "Noise / Black Limited",
            "Alignment Confidence",
            "Motion Mask",
            "Rejected Samples"
        };
        if (ImGuiExtras::NodeCombo("Inspection View", "##HdrMergeDebugAdvanced", &debugView, debugViews, IM_ARRAYSIZE(debugViews), controlWidth)) {
            settings.debugView = static_cast<Raw::HdrMergeDebugView>(std::clamp(debugView, 0, 6));
            changed = true;
        }
    }

    ClampHdrMergeSettings(settings);
    if (changed || !SameHdrMergeSettings(settingsBefore, settings)) {
        MarkRenderDirty(node.id);
    }
}
