#include "Editor/Internal/EditorModulePreLocalExposureControls.h"

#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleRawControlShared.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <string>
#include <vector>

using Stack::Editor::RawControls::GraphSliderRightClickWasConsumed;

namespace Stack::Editor::PreLocalExposureControls {

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

bool g_SuppressNextAutoGainHelpPopup = false;

bool ApplyResettableSliderFloat(float* value, float resetValue, bool changed) {
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (GraphSliderRightClickWasConsumed() ||
        !state.lastHovered ||
        !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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
    if (GraphSliderRightClickWasConsumed() ||
        !state.lastHovered ||
        !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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
    } else if (!GraphSliderRightClickWasConsumed() &&
               state.lastHovered &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
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



} // namespace Stack::Editor::PreLocalExposureControls

using namespace Stack::Editor::PreLocalExposureControls;

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
