#include "TimelineAnimation.h"

#include "Editor/Layers/LayerBase.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"

#include <algorithm>
#include <cmath>

namespace Stack::Timeline {
namespace {

struct LayerParameterSpec {
    LayerType layerType;
    const char* parameterId;
    const char* storageKey;
    const char* label;
    AnimatableValueType valueType;
    float defaultValue;
    float minValue;
    float maxValue;
    int arrayIndex = -1;
};

constexpr LayerParameterSpec kLayerParameters[] = {
    { LayerType::Brightness, "layer.brightness", "brightness", "Brightness", AnimatableValueType::Float, 0.0f, -1.0f, 1.0f },
    { LayerType::Contrast, "layer.contrast", "contrast", "Contrast", AnimatableValueType::Float, 0.0f, -1.0f, 1.0f },
    { LayerType::Saturation, "layer.saturation", "saturation", "Saturation", AnimatableValueType::Float, 0.0f, -1.0f, 1.0f },
    { LayerType::Warmth, "layer.warmth", "warmth", "Warmth", AnimatableValueType::Float, 0.0f, -1.0f, 1.0f },
    { LayerType::Sharpen, "layer.sharpening", "sharpening", "Sharpening", AnimatableValueType::Float, 0.0f, 0.0f, 1.0f },
    { LayerType::Sharpen, "layer.sharpenThreshold", "sharpenThreshold", "Sharpen Threshold", AnimatableValueType::Float, 0.0f, 0.0f, 1.0f },
    { LayerType::BoxBlur, "layer.blurAmount", "amount", "Blur Amount", AnimatableValueType::Float, 2.0f, 0.5f, 16.0f },
    { LayerType::GaussianBlur, "layer.blurAmount", "amount", "Blur Amount", AnimatableValueType::Float, 2.0f, 0.5f, 16.0f },
    { LayerType::HankelBlur, "layer.hankelRadius", "radius", "Blur Radius", AnimatableValueType::Float, 5.0f, 0.0f, 30.0f },
    { LayerType::HankelBlur, "layer.hankelQuality", "quality", "Quality", AnimatableValueType::Float, 8.0f, 2.0f, 16.0f },
    { LayerType::HankelBlur, "layer.hankelIntensity", "intensity", "Intensity", AnimatableValueType::Float, 1.0f, 0.0f, 1.0f },
    { LayerType::TiltShiftBlur, "layer.tiltShiftBlurType", "blurType", "Blur Filter", AnimatableValueType::Enum, 0.0f, 0.0f, 2.0f },
    { LayerType::TiltShiftBlur, "layer.tiltShiftBlurStrength", "amount", "Blur Strength", AnimatableValueType::Float, 10.0f, 0.0f, 100.0f },
    { LayerType::TiltShiftBlur, "layer.tiltShiftFocusRadius", "focusRadius", "Focus Radius", AnimatableValueType::Float, 30.0f, 0.0f, 150.0f },
    { LayerType::TiltShiftBlur, "layer.tiltShiftFocusFalloff", "transition", "Focus Falloff", AnimatableValueType::Float, 30.0f, 1.0f, 100.0f },
    { LayerType::TiltShiftBlur, "layer.tiltShiftFocusX", "centerX", "Focus X", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::TiltShiftBlur, "layer.tiltShiftFocusY", "centerY", "Focus Y", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::Pixelation, "layer.corruptionScale", "resScale", "Quality Scale", AnimatableValueType::Float, 50.0f, 1.0f, 100.0f },
    { LayerType::NonLocalMeansDenoise, "layer.denoiseSearchRadius", "searchRadius", "Search Radius", AnimatableValueType::Integer, 5.0f, 1.0f, 15.0f },
    { LayerType::NonLocalMeansDenoise, "layer.denoisePatchRadius", "patchRadius", "Patch Radius", AnimatableValueType::Integer, 2.0f, 1.0f, 5.0f },
    { LayerType::NonLocalMeansDenoise, "layer.denoiseFilterStrength", "h", "Filter Strength", AnimatableValueType::Float, 0.5f, 0.01f, 2.0f },
    { LayerType::NonLocalMeansDenoise, "layer.denoiseBlendStrength", "strength", "Blend Strength", AnimatableValueType::Float, 100.0f, 0.0f, 100.0f },
    { LayerType::MedianDenoise, "layer.denoiseSearchRadius", "searchRadius", "Search Radius", AnimatableValueType::Integer, 5.0f, 1.0f, 15.0f },
    { LayerType::MedianDenoise, "layer.denoiseBlendStrength", "strength", "Blend Strength", AnimatableValueType::Float, 100.0f, 0.0f, 100.0f },
    { LayerType::MeanDenoise, "layer.denoiseSearchRadius", "searchRadius", "Search Radius", AnimatableValueType::Integer, 5.0f, 1.0f, 15.0f },
    { LayerType::MeanDenoise, "layer.denoiseBlendStrength", "strength", "Blend Strength", AnimatableValueType::Float, 100.0f, 0.0f, 100.0f },
    { LayerType::Crop, "layer.cropLeft", "cropLeft", "Crop Left", AnimatableValueType::Float, 0.0f, 0.0f, 50.0f },
    { LayerType::Crop, "layer.cropRight", "cropRight", "Crop Right", AnimatableValueType::Float, 0.0f, 0.0f, 50.0f },
    { LayerType::Crop, "layer.cropTop", "cropTop", "Crop Top", AnimatableValueType::Float, 0.0f, 0.0f, 50.0f },
    { LayerType::Crop, "layer.cropBottom", "cropBottom", "Crop Bottom", AnimatableValueType::Float, 0.0f, 0.0f, 50.0f },
    { LayerType::Rotate, "layer.rotation", "rotation", "Rotation", AnimatableValueType::Float, 0.0f, -180.0f, 180.0f },
    { LayerType::Flip, "layer.flipHorizontal", "flipH", "Flip Horizontally", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::Flip, "layer.flipVertical", "flipV", "Flip Vertically", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::BilateralFilter, "layer.bilateralRadius", "radius", "Radius", AnimatableValueType::Integer, 3.0f, 1.0f, 30.0f },
    { LayerType::BilateralFilter, "layer.bilateralColorSigma", "sigmaCol", "Color Sigma", AnimatableValueType::Float, 0.1f, 0.01f, 1.0f },
    { LayerType::BilateralFilter, "layer.bilateralSpatialSigma", "sigmaSpace", "Spatial Sigma", AnimatableValueType::Float, 3.0f, 0.5f, 15.0f },
    { LayerType::BilateralFilter, "layer.bilateralKernel", "kernel", "Kernel", AnimatableValueType::Enum, 0.0f, 0.0f, 1.0f },
    { LayerType::BilateralFilter, "layer.bilateralEdgeMode", "edgeMode", "Edge Mode", AnimatableValueType::Enum, 0.0f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.noiseStrength", "strength", "Strength", AnimatableValueType::Float, 50.0f, 0.0f, 150.0f },
    { LayerType::Noise, "layer.noiseType", "noiseType", "Noise Type", AnimatableValueType::Enum, 13.0f, 0.0f, 13.0f },
    { LayerType::Noise, "layer.noiseBlendMode", "blendMode", "Blend Mode", AnimatableValueType::Enum, 0.0f, 0.0f, 5.0f },
    { LayerType::Noise, "layer.noiseBlurriness", "blurriness", "Blurriness", AnimatableValueType::Float, 0.0f, 0.0f, 100.0f },
    { LayerType::Noise, "layer.noiseSaturationStrength", "satStrength", "Sat Strength", AnimatableValueType::Float, 1.0f, 0.0f, 4.0f },
    { LayerType::Noise, "layer.noiseSaturationImpact", "satImpact", "Sat Impact", AnimatableValueType::Float, 0.0f, -100.0f, 100.0f },
    { LayerType::Noise, "layer.noiseScale", "scale", "Scale", AnimatableValueType::Float, 1.0f, 1.0f, 20.0f },
    { LayerType::Noise, "layer.noiseOpacity", "opacity", "Opacity", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.seed", "seed", "Legacy Seed", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.paramA", "paramA", "Legacy Parameter A", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.paramB", "paramB", "Legacy Parameter B", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.paramC", "paramC", "Legacy Parameter C", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.grainDensityDeviation", "grainDensityDeviation", "Density Deviation (OD)", AnimatableValueType::Float, 0.035f, 0.0f, 0.3f },
    { LayerType::Noise, "layer.grainRadiusMicrons", "grainRadiusMicrons", "Nominal Grain Radius (um)", AnimatableValueType::Float, 3.0f, 0.25f, 25.0f },
    { LayerType::Noise, "layer.grainRadiusVariation", "grainRadiusVariation", "Radius Variation", AnimatableValueType::Float, 0.35f, 0.0f, 0.8f },
    { LayerType::Noise, "layer.grainMaximumRadiusFactor", "grainMaximumRadiusFactor", "Maximum Radius (x mean)", AnimatableValueType::Float, 3.0f, 1.0f, 4.0f },
    { LayerType::Noise, "layer.grainFilmWidthMm", "grainFilmWidthMm", "Film Width (mm)", AnimatableValueType::Float, 36.0f, 8.0f, 70.0f },
    { LayerType::Noise, "layer.grainOpticalBlurMicrons", "grainOpticalBlurMicrons", "Optical Blur Sigma (um)", AnimatableValueType::Float, 1.5f, 0.0f, 20.0f },
    { LayerType::Noise, "layer.grainCoverage", "grainCoverage", "Developed Coverage", AnimatableValueType::Float, 0.5f, 0.05f, 0.9f },
    { LayerType::Noise, "layer.grainColorAmount", "grainColorAmount", "Color Grain Amount", AnimatableValueType::Float, 0.25f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.grainChannelCorrelation", "grainChannelCorrelation", "RGB Grain Correlation", AnimatableValueType::Float, 0.65f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.grainRadiusR", "grainRadiusR", "R Radius Multiplier", AnimatableValueType::Float, 1.0f, 0.5f, 2.0f },
    { LayerType::Noise, "layer.grainRadiusG", "grainRadiusG", "G Radius Multiplier", AnimatableValueType::Float, 1.0f, 0.5f, 2.0f },
    { LayerType::Noise, "layer.grainRadiusB", "grainRadiusB", "B Radius Multiplier", AnimatableValueType::Float, 1.0f, 0.5f, 2.0f },
    { LayerType::Noise, "layer.grainStrengthR", "grainStrengthR", "R Strength Multiplier", AnimatableValueType::Float, 1.0f, 0.0f, 3.0f },
    { LayerType::Noise, "layer.grainStrengthG", "grainStrengthG", "G Strength Multiplier", AnimatableValueType::Float, 1.0f, 0.0f, 3.0f },
    { LayerType::Noise, "layer.grainStrengthB", "grainStrengthB", "B Strength Multiplier", AnimatableValueType::Float, 1.0f, 0.0f, 3.0f },
    { LayerType::Noise, "layer.grainShadows", "grainShadows", "Grain Shadows", AnimatableValueType::Float, 1.2f, 0.0f, 3.0f },
    { LayerType::Noise, "layer.grainMidtones", "grainMidtones", "Grain Midtones", AnimatableValueType::Float, 1.0f, 0.0f, 3.0f },
    { LayerType::Noise, "layer.grainHighlights", "grainHighlights", "Grain Highlights", AnimatableValueType::Float, 0.6f, 0.0f, 3.0f },
    { LayerType::Noise, "layer.grainTonalPivot", "grainTonalPivot", "Grain Midtone Pivot (linear)", AnimatableValueType::Float, 0.18f, 0.01f, 0.9f },
    { LayerType::Noise, "layer.grainExposureFloor", "grainExposureFloor", "Shadow Exposure Floor", AnimatableValueType::Float, 0.005f, 0.0f, 0.1f },
    { LayerType::Noise, "layer.grainMix", "grainMix", "Grain Mix", AnimatableValueType::Float, 1.0f, 0.0f, 1.0f },
    { LayerType::Noise, "layer.grainEncoding", "grainEncoding", "Grain Input Encoding", AnimatableValueType::Enum, 0.0f, 0.0f, 2.0f },
    { LayerType::Noise, "layer.grainOutput", "grainOutput", "Grain Output", AnimatableValueType::Enum, 0.0f, 0.0f, 2.0f },
    { LayerType::Noise, "layer.grainSamples", "grainSamples", "Integration Samples", AnimatableValueType::Integer, 16.0f, 4.0f, 64.0f },
    { LayerType::Noise, "layer.grainSeed", "grainSeed", "Grain Seed", AnimatableValueType::Integer, 125775.0f, 0.0f, 16777215.0f },
    { LayerType::Noise, "layer.grainFrame", "grainFrame", "Grain Frame", AnimatableValueType::Integer, 0.0f, 0.0f, 1000000.0f },
    { LayerType::Noise, "layer.grainCompensateMean", "grainCompensateMean", "Compensate Density Mean", AnimatableValueType::Boolean, 1.0f, 0.0f, 1.0f },
    { LayerType::ColorGrade, "layer.colorGradeStrength", "strength", "Strength", AnimatableValueType::Float, 100.0f, 0.0f, 100.0f },
    { LayerType::Vignette, "layer.vignetteIntensity", "intensity", "Intensity", AnimatableValueType::Float, 0.3f, 0.0f, 1.0f },
    { LayerType::Vignette, "layer.vignetteRadius", "radius", "Radius", AnimatableValueType::Float, 0.75f, 0.0f, 1.5f },
    { LayerType::Vignette, "layer.vignetteSoftness", "softness", "Softness", AnimatableValueType::Float, 0.45f, 0.0f, 1.0f },
    { LayerType::ChromaticAberration, "layer.chromaticAmount", "amount", "Amount", AnimatableValueType::Float, 0.0f, 0.0f, 100.0f },
    { LayerType::ChromaticAberration, "layer.chromaticEdgeBlur", "edgeBlur", "Edge Blur", AnimatableValueType::Float, 0.0f, 0.0f, 100.0f },
    { LayerType::ChromaticAberration, "layer.chromaticZoomBlur", "zoomBlur", "Zoom Blur", AnimatableValueType::Float, 0.0f, 0.0f, 100.0f },
    { LayerType::ChromaticAberration, "layer.chromaticLinkFalloffToBlur", "linkFalloffToBlur", "Link Falloff To Blur", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::ChromaticAberration, "layer.chromaticRadius", "radius", "Radius", AnimatableValueType::Float, 50.0f, 0.0f, 100.0f },
    { LayerType::ChromaticAberration, "layer.chromaticFalloff", "falloff", "Falloff", AnimatableValueType::Float, 50.0f, 0.0f, 100.0f },
    { LayerType::ChromaticAberration, "layer.chromaticCenterX", "center", "Center X", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f, 0 },
    { LayerType::ChromaticAberration, "layer.chromaticCenterY", "center", "Center Y", AnimatableValueType::Float, 0.5f, 0.0f, 1.0f, 1 },
    { LayerType::LensDistortion, "layer.lensDistortionAmount", "amount", "Distortion Amount", AnimatableValueType::Float, 0.0f, -100.0f, 100.0f },
    { LayerType::LensDistortion, "layer.lensDistortionScale", "scale", "Scale Base", AnimatableValueType::Float, 100.0f, 50.0f, 150.0f },
    { LayerType::GlareRays, "layer.glareRaysIntensity", "intensity", "Intensity", AnimatableValueType::Float, 50.0f, 0.0f, 100.0f },
    { LayerType::GlareRays, "layer.glareRaysCount", "rays", "Ray Count", AnimatableValueType::Float, 4.0f, 2.0f, 12.0f },
    { LayerType::GlareRays, "layer.glareRaysLength", "length", "Ray Length", AnimatableValueType::Float, 50.0f, 1.0f, 100.0f },
    { LayerType::GlareRays, "layer.glareRaysSoftness", "softness", "Ray Softness", AnimatableValueType::Float, 20.0f, 0.0f, 100.0f },
    { LayerType::AiryBloom, "layer.airyBloomIntensity", "intensity", "Intensity", AnimatableValueType::Float, 0.5f, 0.0f, 2.0f },
    { LayerType::AiryBloom, "layer.airyBloomAperture", "aperture", "Aperture", AnimatableValueType::Float, 8.0f, 1.0f, 50.0f },
    { LayerType::AiryBloom, "layer.airyBloomThreshold", "threshold", "Threshold", AnimatableValueType::Float, 0.7f, 0.0f, 1.0f },
    { LayerType::AiryBloom, "layer.airyBloomThresholdFade", "thresholdFade", "Threshold Fade", AnimatableValueType::Float, 0.1f, 0.0f, 1.0f },
    { LayerType::AiryBloom, "layer.airyBloomCutoff", "cutoff", "Cutoff", AnimatableValueType::Float, 0.1f, 0.01f, 1.0f },
    { LayerType::Halftoning, "layer.halftoneSize", "size", "Cell Size", AnimatableValueType::Float, 4.0f, 1.0f, 20.0f },
    { LayerType::Halftoning, "layer.halftoneIntensity", "intensity", "Intensity", AnimatableValueType::Float, 1.0f, 0.0f, 1.0f },
    { LayerType::Halftoning, "layer.halftoneSharpness", "sharpness", "Sharpness", AnimatableValueType::Float, 0.8f, 0.0f, 1.0f },
    { LayerType::Halftoning, "layer.halftonePattern", "pattern", "Pattern", AnimatableValueType::Enum, 0.0f, 0.0f, 3.0f },
    { LayerType::Halftoning, "layer.halftoneColorMode", "colorMode", "Color Mode", AnimatableValueType::Enum, 0.0f, 0.0f, 3.0f },
    { LayerType::Halftoning, "layer.halftoneGray", "gray", "Grayscale Output", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::Halftoning, "layer.halftoneInvert", "invert", "Invert Pattern", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::CellShading, "layer.cellShadingLevels", "levels", "Shading Levels", AnimatableValueType::Integer, 4.0f, 2.0f, 12.0f },
    { LayerType::CellShading, "layer.cellShadingBias", "bias", "Contrast Bias", AnimatableValueType::Float, 0.0f, -1.0f, 1.0f },
    { LayerType::CellShading, "layer.cellShadingGamma", "gamma", "Gamma", AnimatableValueType::Float, 1.0f, 0.1f, 3.0f },
    { LayerType::CellShading, "layer.cellShadingQuantMode", "quantMode", "Quantization Mode", AnimatableValueType::Enum, 0.0f, 0.0f, 2.0f },
    { LayerType::CellShading, "layer.cellShadingBandMap", "bandMap", "Band Mapping", AnimatableValueType::Enum, 0.0f, 0.0f, 2.0f },
    { LayerType::CellShading, "layer.cellShadingEdgeMethod", "edgeMethod", "Edge Method", AnimatableValueType::Enum, 0.0f, 0.0f, 2.0f },
    { LayerType::CellShading, "layer.cellShadingEdgeStrength", "edgeStrength", "Edge Strength", AnimatableValueType::Float, 50.0f, 0.0f, 200.0f },
    { LayerType::CellShading, "layer.cellShadingEdgeThickness", "edgeThickness", "Edge Thickness", AnimatableValueType::Float, 1.0f, 0.5f, 5.0f },
    { LayerType::CellShading, "layer.cellShadingColorPreserve", "colorPreserve", "Color Preserve", AnimatableValueType::Float, 50.0f, 0.0f, 100.0f },
    { LayerType::CellShading, "layer.cellShadingShowEdges", "showEdges", "Show Edges", AnimatableValueType::Boolean, 1.0f, 0.0f, 1.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherTargetAlpha", "targetAlpha", "Removed Area Opacity", AnimatableValueType::Float, 0.0f, 0.0f, 1.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherTolerance", "tolerance", "Color Tolerance", AnimatableValueType::Float, 0.1f, 0.0f, 1.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherSmoothing", "smoothing", "Edge Smoothing", AnimatableValueType::Float, 0.05f, 0.0f, 1.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherEdgeShift", "edgeShift", "Edge Shift", AnimatableValueType::Float, 0.0f, -10.0f, 10.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherDefringe", "defringe", "Defringe", AnimatableValueType::Float, 0.0f, 0.0f, 1.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherKeepSelected", "keepSelected", "Keep Selected Range", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::BackgroundPatcher, "layer.backgroundPatcherShowDebugOverlay", "showDebugOverlay", "Visualizer", AnimatableValueType::Boolean, 0.0f, 0.0f, 1.0f },
    { LayerType::PaletteReconstructor, "layer.paletteBlend", "blend", "Global Blend", AnimatableValueType::Float, 100.0f, 0.0f, 100.0f },
    { LayerType::PaletteReconstructor, "layer.paletteSmoothing", "smoothing", "Palette Smoothing", AnimatableValueType::Float, 0.0f, 0.0f, 100.0f },
    { LayerType::PaletteReconstructor, "layer.paletteSmoothingType", "smoothingType", "Smoothing Type", AnimatableValueType::Enum, 0.0f, 0.0f, 1.0f },
};

std::string ResolveNodeLabel(const EditorNodeGraph::Node& node) {
    if (!node.title.empty()) {
        return node.title;
    }

    if (node.kind == EditorNodeGraph::NodeKind::Layer) {
        if (const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(node.layerType)) {
            return descriptor->displayName ? descriptor->displayName : "Layer";
        }
    }

    return "Node " + std::to_string(node.id);
}

bool ReadJsonScalar(const nlohmann::json& value, const LayerParameterSpec& spec, float& outValue) {
    if (spec.valueType == AnimatableValueType::Boolean) {
        if (value.is_boolean()) {
            outValue = value.get<bool>() ? 1.0f : 0.0f;
            return true;
        }
        if (value.is_number()) {
            outValue = value.get<float>() >= 0.5f ? 1.0f : 0.0f;
            return true;
        }
        return false;
    }

    if (!value.is_number()) {
        return false;
    }

    outValue = value.get<float>();
    return true;
}

bool ReadLayerJsonScalar(const LayerBase* layer, const LayerParameterSpec& spec, float& outValue) {
    if (!layer || !spec.storageKey) {
        return false;
    }

    const nlohmann::json layerJson = layer->Serialize();
    const auto it = layerJson.find(spec.storageKey);
    if (spec.arrayIndex >= 0) {
        if (it == layerJson.end() || !it->is_array() ||
            spec.arrayIndex >= static_cast<int>(it->size())) {
            return false;
        }

        return ReadJsonScalar((*it)[spec.arrayIndex], spec, outValue);
    }

    if (it == layerJson.end()) {
        return false;
    }

    return ReadJsonScalar(*it, spec, outValue);
}

const LayerParameterSpec* FindLayerParameterSpec(LayerType layerType, const std::string& parameterId) {
    for (const LayerParameterSpec& spec : kLayerParameters) {
        if (spec.layerType == layerType && parameterId == spec.parameterId) {
            return &spec;
        }
    }
    return nullptr;
}

bool ParameterAllowsLinearInterpolation(const std::string& parameterId) {
    for (const LayerParameterSpec& spec : kLayerParameters) {
        if (parameterId == spec.parameterId) {
            return spec.valueType == AnimatableValueType::Float ||
                spec.valueType == AnimatableValueType::Integer;
        }
    }
    return true;
}

nlohmann::json BuildLayerJsonValue(const LayerParameterSpec& spec, float value) {
    const float clampedValue = std::clamp(value, spec.minValue, spec.maxValue);
    switch (spec.valueType) {
        case AnimatableValueType::Float:
            return clampedValue;
        case AnimatableValueType::Integer:
        case AnimatableValueType::Enum:
            return static_cast<int>(std::lround(clampedValue));
        case AnimatableValueType::Boolean:
            return clampedValue >= 0.5f;
    }
    return clampedValue;
}

void WriteLayerJsonScalar(nlohmann::json& layerJson, const LayerParameterSpec& spec, float value) {
    if (spec.arrayIndex >= 0) {
        nlohmann::json& targetArray = layerJson[spec.storageKey];
        if (!targetArray.is_array()) {
            targetArray = nlohmann::json::array();
        }
        while (static_cast<int>(targetArray.size()) <= spec.arrayIndex) {
            targetArray.push_back(0.0f);
        }
        targetArray[spec.arrayIndex] = BuildLayerJsonValue(spec, value);
        return;
    }

    layerJson[spec.storageKey] = BuildLayerJsonValue(spec, value);
}

} // namespace

bool SameTarget(const AnimatableParameterTarget& a, const AnimatableParameterTarget& b) {
    return a.graphId == b.graphId && a.parameterId == b.parameterId &&
        ((!a.nodeUuid.empty() || !b.nodeUuid.empty()) ? a.nodeUuid == b.nodeUuid : a.nodeId == b.nodeId);
}

bool IsValidTarget(const AnimatableParameterTarget& target) {
    return target.nodeId > 0 && !target.parameterId.empty();
}

std::string BuildTargetKey(const AnimatableParameterTarget& target) {
    return target.graphId + ":" + (target.nodeUuid.empty() ? std::to_string(target.nodeId) : target.nodeUuid) + ":" + target.parameterId;
}

std::vector<AnimatableParameterDefinition> CollectAnimatableParametersForNode(
    const EditorNodeGraph::Node& node,
    const LayerBase* layer, const std::string& graphId) {
    std::vector<AnimatableParameterDefinition> parameters;
    if (node.kind == EditorNodeGraph::NodeKind::RawOperation) {
        if (const auto* registered = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node))
            for (const auto& parameter : registered->parameters) {
                if (parameter.animation == EditorNodeGraphDefinitions::LiveAnimationPolicy::NotAnimatable || !parameter.hasNumericDomain) continue;
                AnimatableParameterDefinition definition;
                definition.target = {node.id, parameter.id, graphId, node.instanceUuid};
                definition.nodeLabel = node.title; definition.parameterLabel = parameter.label;
                definition.storageKey = parameter.storageKey;
                definition.defaultValue = parameter.defaultValue.get<float>();
                definition.minValue = float(parameter.minimum); definition.maxValue = float(parameter.maximum);
                const nlohmann::json::json_pointer path(parameter.storageKey);
                definition.hasCurrentValue = node.rawOperation.parameters.contains(path);
                definition.currentValue = definition.hasCurrentValue ? node.rawOperation.parameters.at(path).get<float>() : definition.defaultValue;
                parameters.push_back(std::move(definition));
            }
        return parameters;
    }
    if (node.kind != EditorNodeGraph::NodeKind::Layer) return parameters;

    const std::string nodeLabel = ResolveNodeLabel(node);
    for (const LayerParameterSpec& spec : kLayerParameters) {
        if (spec.layerType != node.layerType) {
            continue;
        }

        AnimatableParameterDefinition definition;
        definition.target.nodeId = node.id;
        definition.target.graphId = graphId;
        definition.target.nodeUuid = node.instanceUuid;
        definition.target.parameterId = spec.parameterId;
        definition.nodeLabel = nodeLabel;
        definition.parameterLabel = spec.label;
        definition.storageKey = spec.storageKey;
        definition.storageArrayIndex = spec.arrayIndex;
        definition.valueType = spec.valueType;
        definition.defaultValue = spec.defaultValue;
        definition.minValue = spec.minValue;
        definition.maxValue = spec.maxValue;
        definition.currentValue = spec.defaultValue;
        definition.hasCurrentValue = ReadLayerJsonScalar(layer, spec, definition.currentValue);
        parameters.push_back(std::move(definition));
    }

    return parameters;
}

std::vector<AnimatableParameterDefinition> DescribeAnimatableParametersForLayer(
    LayerType layerType) {
    EditorNodeGraph::Node node;
    node.id = 0;
    node.kind = EditorNodeGraph::NodeKind::Layer;
    node.layerType = layerType;
    if (const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(layerType)) {
        node.typeId = descriptor->typeId ? descriptor->typeId : "";
        node.title = descriptor->displayName ? descriptor->displayName : "Layer";
    }
    return CollectAnimatableParametersForNode(node, nullptr);
}

bool TryReadAnimatableParameterValue(
    const EditorNodeGraph::Node& node,
    const LayerBase* layer,
    const std::string& parameterId,
    float& outValue) {
    if (node.kind == EditorNodeGraph::NodeKind::RawOperation) {
        for (const auto& definition : CollectAnimatableParametersForNode(node)) {
            if (definition.target.parameterId == parameterId) { outValue = definition.currentValue; return true; }
        }
        return false;
    }
    if (node.kind != EditorNodeGraph::NodeKind::Layer) return false;

    const LayerParameterSpec* spec = FindLayerParameterSpec(node.layerType, parameterId);
    if (!spec) {
        return false;
    }

    if (ReadLayerJsonScalar(layer, *spec, outValue)) {
        return true;
    }

    outValue = spec->defaultValue;
    return true;
}

TimelineTrack* FindTimelineTrack(TimelineAnimationState& state, const AnimatableParameterTarget& target) {
    auto it = std::find_if(state.tracks.begin(), state.tracks.end(), [&target](const TimelineTrack& track) {
        return SameTarget(track.target, target);
    });
    return it == state.tracks.end() ? nullptr : &(*it);
}

const TimelineTrack* FindTimelineTrack(const TimelineAnimationState& state, const AnimatableParameterTarget& target) {
    auto it = std::find_if(state.tracks.begin(), state.tracks.end(), [&target](const TimelineTrack& track) {
        return SameTarget(track.target, target);
    });
    return it == state.tracks.end() ? nullptr : &(*it);
}

TimelineTrack& EnsureTimelineTrack(TimelineAnimationState& state, const AnimatableParameterTarget& target) {
    if (TimelineTrack* track = FindTimelineTrack(state, target)) {
        return *track;
    }

    state.tracks.push_back(TimelineTrack{ target, {} });
    return state.tracks.back();
}

void SetOrReplaceKeyframe(
    TimelineAnimationState& state,
    const AnimatableParameterTarget& target,
    int frame,
    float value,
    TimelineInterpolation interpolation) {
    if (!IsValidTarget(target)) {
        return;
    }

    TimelineTrack& track = EnsureTimelineTrack(state, target);
    auto it = std::lower_bound(track.keyframes.begin(), track.keyframes.end(), frame, [](const TimelineKeyframe& keyframe, int frameValue) {
        return keyframe.frame < frameValue;
    });

    if (it != track.keyframes.end() && it->frame == frame) {
        it->value = value;
        it->interpolation = interpolation;
        return;
    }

    track.keyframes.insert(it, TimelineKeyframe{ frame, value, interpolation });
}

const TimelineKeyframe* FindKeyframeAtFrame(const TimelineTrack& track, int frame) {
    auto it = std::find_if(track.keyframes.begin(), track.keyframes.end(), [frame](const TimelineKeyframe& keyframe) {
        return keyframe.frame == frame;
    });
    return it == track.keyframes.end() ? nullptr : &(*it);
}

bool UpdateExistingKeyframeValue(
    TimelineAnimationState& state,
    const AnimatableParameterTarget& target,
    int frame,
    float value) {
    if (!IsValidTarget(target)) {
        return false;
    }

    TimelineTrack* track = FindTimelineTrack(state, target);
    if (!track) {
        return false;
    }

    auto it = std::lower_bound(
        track->keyframes.begin(),
        track->keyframes.end(),
        frame,
        [](const TimelineKeyframe& keyframe, int frameValue) {
            return keyframe.frame < frameValue;
        });
    if (it == track->keyframes.end() || it->frame != frame) {
        return false;
    }

    it->value = value;
    return true;
}

bool TargetAffectsCompletedChain(const AnimatableParameterTarget& target, const EditorNodeGraph::CompletedChainInfo& chain) {
    return std::find(chain.nodeIds.begin(), chain.nodeIds.end(), target.nodeId) != chain.nodeIds.end();
}

bool TrackAffectsCompletedChain(const TimelineTrack& track, const EditorNodeGraph::CompletedChainInfo& chain) {
    return TargetAffectsCompletedChain(track.target, chain);
}

int FindLastTimelineKeyframeFrame(const TimelineAnimationState& state) {
    int lastFrame = -1;
    for (const TimelineTrack& track : state.tracks) {
        for (const TimelineKeyframe& keyframe : track.keyframes) {
            lastFrame = std::max(lastFrame, keyframe.frame);
        }
    }
    return lastFrame;
}

std::size_t CountDistinctTimelineKeyframeFrames(const TimelineAnimationState& state) {
    std::vector<int> frames;
    for (const TimelineTrack& track : state.tracks) {
        for (const TimelineKeyframe& keyframe : track.keyframes) {
            frames.push_back(keyframe.frame);
        }
    }

    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames.size();
}

bool EvaluateTimelineTrackAtFrame(const TimelineTrack& track, int frame, float& outValue) {
    if (!IsValidTarget(track.target) || track.keyframes.empty()) {
        return false;
    }

    auto nextIt = std::lower_bound(
        track.keyframes.begin(),
        track.keyframes.end(),
        frame,
        [](const TimelineKeyframe& keyframe, int frameValue) {
            return keyframe.frame < frameValue;
        });

    if (nextIt == track.keyframes.begin()) {
        outValue = nextIt->value;
        return true;
    }

    if (nextIt == track.keyframes.end()) {
        outValue = track.keyframes.back().value;
        return true;
    }

    if (nextIt->frame == frame) {
        outValue = nextIt->value;
        return true;
    }

    const TimelineKeyframe& previous = *(nextIt - 1);
    const TimelineKeyframe& next = *nextIt;
    if (previous.interpolation == TimelineInterpolation::Hold ||
        !ParameterAllowsLinearInterpolation(track.target.parameterId) ||
        next.frame <= previous.frame) {
        outValue = previous.value;
        return true;
    }

    const float t =
        static_cast<float>(frame - previous.frame) /
        static_cast<float>(next.frame - previous.frame);
    outValue = previous.value + (next.value - previous.value) * std::clamp(t, 0.0f, 1.0f);
    return true;
}

FrameEvaluationContext BuildFrameEvaluationContext(const TimelineAnimationState& state, int frame) {
    FrameEvaluationContext context;
    context.frame = frame;

    for (const TimelineTrack& track : state.tracks) {
        float value = 0.0f;
        if (!EvaluateTimelineTrackAtFrame(track, frame, value)) {
            continue;
        }

        context.values.push_back(FrameParameterValue{ track.target, value });
    }

    return context;
}

bool TryGetFrameParameterValue(
    const FrameEvaluationContext& context,
    const AnimatableParameterTarget& target,
    float& outValue) {
    for (const FrameParameterValue& value : context.values) {
        if (SameTarget(value.target, target)) {
            outValue = value.value;
            return true;
        }
    }
    return false;
}

bool RemoveFrameParameterValue(
    FrameEvaluationContext& context,
    const AnimatableParameterTarget& target) {
    const std::size_t oldSize = context.values.size();
    context.values.erase(
        std::remove_if(
            context.values.begin(),
            context.values.end(),
            [&target](const FrameParameterValue& value) {
                return SameTarget(value.target, target);
            }),
        context.values.end());
    return context.values.size() != oldSize;
}

bool ApplyFrameEvaluationContextToLayerJson(
    const FrameEvaluationContext& context,
    int nodeId,
    LayerType layerType,
    nlohmann::json& layerJson, const std::string& graphId, const std::string& nodeUuid) {
    if (context.empty() || nodeId <= 0 || !layerJson.is_object()) {
        return false;
    }

    bool changed = false;
    for (const FrameParameterValue& value : context.values) {
        if (value.target.graphId != graphId || (!value.target.nodeUuid.empty()
            ? value.target.nodeUuid != nodeUuid : value.target.nodeId != nodeId)) {
            continue;
        }

        const LayerParameterSpec* spec = FindLayerParameterSpec(layerType, value.target.parameterId);
        if (!spec) {
            continue;
        }

        WriteLayerJsonScalar(layerJson, *spec, value.value);
        changed = true;
    }

    return changed;
}

} // namespace Stack::Timeline
