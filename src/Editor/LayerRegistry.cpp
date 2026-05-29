#include "LayerRegistry.h"

#include "Layers/AiryBloomLayer.h"
#include "Layers/AlphaHandlingLayer.h"
#include "Layers/AnalogVideoLayer.h"
#include "Layers/BackgroundPatcherLayer.h"
#include "Layers/BilateralFilterLayer.h"
#include "Layers/CellShadingLayer.h"
#include "Layers/ChromaticAberrationLayer.h"
#include "Layers/ColorGradeLayer.h"
#include "Layers/ExpanderLayer.h"
#include "Layers/GlareRaysLayer.h"
#include "Layers/HalftoningLayer.h"
#include "Layers/HankelBlurLayer.h"
#include "Layers/HDRLayer.h"
#include "Layers/ImageBreaksLayer.h"
#include "Layers/LensDistortionLayer.h"
#include "Layers/LinearRgbNeuralDenoiseLayer.h"
#include "Layers/NoiseLayer.h"
#include "Layers/PaletteReconstructorLayer.h"
#include "Layers/SceneDenoiseLayer.h"
#include "Layers/SplitAdjustmentsLayers.h"
#include "Layers/SplitBlurLayers.h"
#include "Layers/SplitCompressionLayers.h"
#include "Layers/SplitCorruptionLayers.h"
#include "Layers/SplitDenoisingLayers.h"
#include "Layers/SplitDitherLayers.h"
#include "Layers/SplitEdgeEffectsLayers.h"
#include "Layers/SplitHeatDistortionLayers.h"
#include "Layers/SplitTransformLayers.h"
#include "Layers/TextOverlayLayer.h"
#include "Layers/TiltShiftBlurLayer.h"
#include "Layers/ToneLayers.h"
#include "Layers/VignetteLayer.h"
#include <algorithm>
#include <map>
#include <string>

namespace {

template <typename T>
std::shared_ptr<LayerBase> MakeLayer() {
    return std::make_shared<T>();
}

constexpr LayerLifecycleStatus Stable = LayerLifecycleStatus::Stable;
constexpr LayerLifecycleStatus NeedsFix = LayerLifecycleStatus::NeedsFix;
constexpr LayerLifecycleStatus Experimental = LayerLifecycleStatus::Experimental;
constexpr LayerLifecycleStatus Deprecated = LayerLifecycleStatus::Deprecated;
constexpr LayerLifecycleStatus Hidden = LayerLifecycleStatus::Hidden;

constexpr LayerChannelPolicy ChannelSafe = LayerChannelPolicy::ChannelSafe;
constexpr LayerChannelPolicy ChannelWarn = LayerChannelPolicy::ChannelUsefulWithWarning;
constexpr LayerChannelPolicy FullImagePreferred = LayerChannelPolicy::FullImagePreferred;
constexpr LayerChannelPolicy FullImageOnly = LayerChannelPolicy::FullImageOnly;
constexpr LayerChannelPolicy ReworkBeforeExpose = LayerChannelPolicy::ReworkBeforeExpose;

const std::vector<LayerDescriptor>& Descriptors() {
    static const std::vector<LayerDescriptor> descriptors = {
        { LayerType::Crop, "Crop", "Crop", "Crop", "Transform / Canvas", "Crop the source canvas.", {}, MakeLayer<CropLayer>, NeedsFix, ChannelWarn, true, "Fixed-canvas crop; does not resize the raster.", "Channel streams crop in place on the current reference canvas.", { "transform", "canvas", "channel-warning" } },
        { LayerType::Rotate, "Rotate", "Rotate", "Rotate", "Transform / Canvas", "Rotate the source canvas.", {}, MakeLayer<RotateLayer>, NeedsFix, ChannelWarn, true, "Fixed-canvas rotate; does not expand bounds.", "Channel streams rotate in place and can misregister after recombine.", { "transform", "canvas", "channel-warning" } },
        { LayerType::Flip, "Flip", "Flip", "Flip", "Transform / Canvas", "Flip the source canvas horizontally or vertically.", {}, MakeLayer<FlipLayer>, Stable, ChannelSafe, true, "", "Coordinate flip is valid for scalar channel streams.", { "transform", "channel-safe" } },
        { LayerType::Expander, "Expander", "Expand Canvas", "Expand Canvas", "Transform / Canvas", "Pad the canvas with a configurable fill.", {}, MakeLayer<ExpanderLayer>, Experimental, ReworkBeforeExpose, true, "Experimental: simulates padding inside the current fixed canvas.", "True canvas resizing belongs in a future Reformat/Canvas Resize node.", { "canvas", "experimental", "rework" } },
        { LayerType::BackgroundPatcher, "BackgroundPatcher", "Background Remover", "Background Remover", "Composite", "Remove or isolate background colors.", {}, MakeLayer<BackgroundPatcherLayer>, NeedsFix, FullImageOnly, true, "Useful advanced node, but brush/flood/AA/patch state is incomplete.", "Full-image matte operation; not a generic scalar channel effect.", { "alpha", "matte", "advanced-ui" } },
        { LayerType::AlphaHandling, "AlphaHandling", "Alpha Protect", "Alpha Protect", "Composite", "Control alpha preservation and protection.", {}, MakeLayer<AlphaHandlingLayer>, Deprecated, ReworkBeforeExpose, false, "Deprecated in the add-node browser; kept for saved project loading.", "Depends on original source alpha and conflicts with explicit alpha-channel workflows.", { "alpha", "deprecated", "hidden" } },

        { LayerType::Brightness, "Brightness", "Brightness", "Brightness", "Color", "Adjust image brightness.", {}, MakeLayer<BrightnessLayer>, Stable, ChannelSafe, true, "", "Scalar adjustment is valid on channel streams.", { "color", "adjust", "channel-safe" } },
        { LayerType::Contrast, "Contrast", "Contrast", "Contrast", "Color", "Adjust image contrast.", {}, MakeLayer<ContrastLayer>, Stable, ChannelSafe, true, "", "Scalar adjustment is valid on channel streams.", { "color", "adjust", "channel-safe" } },
        { LayerType::Saturation, "Saturation", "Saturation", "Saturation", "Color", "Adjust image saturation.", {}, MakeLayer<SaturationLayer>, NeedsFix, FullImagePreferred, true, "", "Saturation is a color relationship and can be misleading on one channel.", { "color", "full-image-preferred" } },
        { LayerType::Warmth, "Warmth", "Warmth", "Warmth", "Color", "Shift the image warmer or cooler.", {}, MakeLayer<WarmthLayer>, NeedsFix, FullImagePreferred, true, "", "Warmth shifts red/blue balance and is ambiguous on one channel.", { "color", "full-image-preferred" } },
        { LayerType::Sharpen, "Sharpen", "Sharpen", "Sharpen", "Color", "Sharpen edges with threshold control.", {}, MakeLayer<SharpenLayer>, Stable, ChannelWarn, true, "", "Works on channels, but may amplify noise or ringing.", { "detail", "channel-warning" } },
        { LayerType::ColorGrade, "ColorGrade", "3-Way Color Grade", "3-Way Color Grade", "Color", "Grade shadows, midtones, and highlights.", {}, MakeLayer<ColorGradeLayer>, Stable, FullImagePreferred, true, "", "Color wheels are full-image-first; channel streams use grayscale interpretation.", { "grade", "advanced-ui", "full-image-preferred" } },
        { LayerType::HDR, "HDR", "HDR Compressor", "HDR Compressor", "Color", "Emulate HDR bloom and highlight recovery.", {}, MakeLayer<HDRLayer>, NeedsFix, FullImagePreferred, true, "", "Highlight/luma behavior is full-image-first.", { "color", "tone", "full-image-preferred" } },
        { LayerType::ToneCurve, "ToneCurve", "Tone Curve", "Tone Curve", "Color / Tone", "Scene-referred curve control for RAW foundation edits and general tone shaping.", {}, MakeLayer<ToneCurveLayer>, Stable, FullImagePreferred, true, "", "Scene output can exceed display range; add View Transform for final preview/export.", { "tone", "curve", "scene-referred", "raw", "advanced-ui", "full-image-preferred" } },
        { LayerType::ToneEqualizer, "ToneEqualizer", "Tone Equalizer", "Tone Equalizer", "Color / Tone", "Adjust dynamic exposure by scene luminance bands without clamping HDR values.", {}, MakeLayer<ToneEqualizerLayer>, Hidden, FullImagePreferred, false, "", "Scene-referred EV gain; should generally feed a View Transform.", { "tone", "dynamic-range", "scene-referred", "raw", "advanced-ui", "full-image-preferred" } },
        { LayerType::ViewTransform, "ViewTransform", "View Transform", "View Transform", "Color / Tone", "Compress scene-linear RGB into display range for preview and output.", {}, MakeLayer<ViewTransformLayer>, Stable, FullImagePreferred, true, "", "Final display/output transform; place near the end of a scene-referred graph.", { "tone", "display", "view-transform", "scene-referred", "advanced-ui", "full-image-preferred" } },

        { LayerType::BoxBlur, "BoxBlur", "Box Blur", "Box Blur", "Blur / Focus", "Apply a box blur.", {}, MakeLayer<BoxBlurLayer>, Stable, ChannelSafe, true, "", "Blur is valid for scalar channel streams.", { "blur", "channel-safe" } },
        { LayerType::GaussianBlur, "GaussianBlur", "Gaussian Blur", "Gaussian Blur", "Blur / Focus", "Apply a gaussian blur.", {}, MakeLayer<GaussianBlurLayer>, Stable, ChannelSafe, true, "", "Blur is valid for scalar channel streams.", { "blur", "channel-safe" } },
        { LayerType::SceneDenoise, "SceneDenoise", "Scene Denoise", "Scene Denoise", "Blur / Focus", "High-quality scene-linear luminance/chroma denoise for RAW and HDR chains.", {}, MakeLayer<SceneDenoiseLayer>, Stable, FullImagePreferred, true, "", "Best before Tone Curve and View Transform; remains unclamped scene-linear RGB.", { "denoise", "raw", "scene-referred", "hdr", "recommended" } },
        { LayerType::LinearRgbNeuralDenoise, "LinearRgbNeuralDenoise", "Linear RGB Neural Denoise", "Linear RGB Neural Denoise", "Blur / Focus", "Optional external-model neural denoise for scene-linear or normal RGB.", {}, MakeLayer<LinearRgbNeuralDenoiseLayer>, Experimental, FullImagePreferred, true, "Uses external ONNX model packs. Install or generate a local denoise pack, then use Run/Refresh Denoise from the advanced node UI.", "Full-image RGB denoise should not be used as an isolated scalar channel effect.", { "denoise", "neural", "ai", "raw", "hdr", "advanced-ui", "full-image-preferred" } },
        { LayerType::NonLocalMeansDenoise, "NonLocalMeansDenoise", "Utility NLM Denoise", "Utility NLM Denoise", "Blur / Focus", "Simple utility non-local means filtering.", {}, MakeLayer<NonLocalMeansDenoiseLayer>, Stable, ChannelWarn, true, "", "Utility denoise; use Scene Denoise for photographic RAW/HDR cleanup.", { "denoise", "utility", "channel-warning" } },
        { LayerType::MedianDenoise, "MedianDenoise", "Utility Median Denoise", "Utility Median Denoise", "Blur / Focus", "Simple median filter for speckle and channel/mask cleanup.", {}, MakeLayer<MedianDenoiseLayer>, Stable, ChannelSafe, true, "", "Utility denoise; useful for scalar channels and masks.", { "denoise", "utility", "channel-safe" } },
        { LayerType::MeanDenoise, "MeanDenoise", "Utility Mean Denoise", "Utility Mean Denoise", "Blur / Focus", "Simple mean box filter for channel/mask smoothing.", {}, MakeLayer<MeanDenoiseLayer>, Stable, ChannelSafe, true, "", "Utility denoise; use Scene Denoise for photographic RAW/HDR cleanup.", { "denoise", "utility", "channel-safe" } },
        { LayerType::BilateralFilter, "BilateralFilter", "Bilateral Filter", "Bilateral Filter", "Blur / Focus", "Smooth detail while preserving edges.", {}, MakeLayer<BilateralFilterLayer>, Stable, ChannelWarn, true, "", "Edge-preserving behavior should be checked on scalar channels.", { "denoise", "edge", "channel-warning" } },
        { LayerType::Noise, "Noise", "Noise", "Noise", "Texture / Generate", "Add procedural noise and film grain.", {}, MakeLayer<NoiseLayer>, Stable, ChannelWarn, true, "", "Noise can be useful on channels; color noise modes need clear semantics.", { "generate", "noise", "channel-warning" } },
        { LayerType::TiltShiftBlur, "TiltShiftBlur", "Tilt-Shift Blur", "Tilt-Shift Blur", "Blur / Focus", "Apply directional depth-style blur.", {}, MakeLayer<TiltShiftBlurLayer>, Stable, ChannelWarn, true, "", "Focus blur can be applied to channels but may need canvas guides.", { "blur", "advanced-ui", "channel-warning" } },
        { LayerType::HankelBlur, "HankelBlur", "Optical Blur", "Optical Blur", "Blur / Focus", "Apply optical Hankel blur.", {}, MakeLayer<HankelBlurLayer>, Stable, ChannelWarn, true, "", "Optical blur is usable on channels but should be checked for artifacts.", { "blur", "channel-warning" } },

        { LayerType::OrderedDither8x8, "OrderedDither8x8", "Ordered Dither 8x8", "Ordered Dither 8x8", "Color", "Apply ordered Bayer 8x8 dithering.", {}, MakeLayer<OrderedDither8x8Layer>, Stable, ChannelWarn, true, "", "Scalar dithering is valid, but users should know it affects one channel.", { "dither", "channel-warning" } },
        { LayerType::ErrorDiffusionDither, "ErrorDiffusionDither", "Error Diffusion Dither", "Error Diffusion Dither", "Color", "Apply error diffusion style dithering.", {}, MakeLayer<ErrorDiffusionDitherLayer>, Stable, ChannelWarn, true, "", "Scalar error diffusion is valid but should be tested per channel.", { "dither", "channel-warning" } },
        { LayerType::WhiteNoiseDither, "WhiteNoiseDither", "White Noise Dither", "White Noise Dither", "Color", "Apply white noise dithering.", {}, MakeLayer<WhiteNoiseDitherLayer>, Stable, ChannelWarn, true, "", "Scalar dithering is valid, but users should know it affects one channel.", { "dither", "channel-warning" } },
        { LayerType::OrderedDither4x4, "OrderedDither4x4", "Ordered Dither 4x4", "Ordered Dither 4x4", "Color", "Apply ordered Bayer 4x4 dithering.", {}, MakeLayer<OrderedDither4x4Layer>, Stable, ChannelWarn, true, "", "Scalar dithering is valid, but users should know it affects one channel.", { "dither", "channel-warning" } },
        { LayerType::OrderedDither2x2, "OrderedDither2x2", "Ordered Dither 2x2", "Ordered Dither 2x2", "Color", "Apply ordered Bayer 2x2 dithering.", {}, MakeLayer<OrderedDither2x2Layer>, Stable, ChannelWarn, true, "", "Scalar dithering is valid, but users should know it affects one channel.", { "dither", "channel-warning" } },
        { LayerType::InterleavedGradientDither, "InterleavedGradientDither", "Interleaved Gradient Dither", "Interleaved Gradient Dither", "Color", "Apply interleaved gradient dithering.", {}, MakeLayer<InterleavedGradientDitherLayer>, Stable, ChannelWarn, true, "", "Scalar dithering is valid, but users should know it affects one channel.", { "dither", "channel-warning" } },
        { LayerType::Halftoning, "Halftoning", "Halftone", "Halftone", "Color", "Convert tone into dot patterns.", {}, MakeLayer<HalftoningLayer>, Stable, ChannelWarn, true, "", "Halftone can be scalar, but this should be presented clearly.", { "halftone", "channel-warning" } },
        { LayerType::CellShading, "CellShading", "Cell Shading", "Cell Shading", "Color", "Posterize lighting into cell-shaded bands.", { "Cell" }, MakeLayer<CellShadingLayer>, NeedsFix, FullImagePreferred, true, "", "Lighting/color band behavior is full-image-first.", { "stylize", "full-image-preferred" } },
        { LayerType::PaletteReconstructor, "PaletteReconstructor", "Palette Rebuild", "Palette Rebuild", "Color", "Rebuild the image against a reduced palette.", { "Palette" }, MakeLayer<PaletteReconstructorLayer>, NeedsFix, FullImageOnly, true, "", "Palette reconstruction is color-set based and not a scalar channel effect.", { "palette", "advanced-ui", "full-image-only" } },
        { LayerType::EdgeOverlay, "EdgeOverlay", "Edge Overlay", "Edge Overlay", "Effects / Damage", "Highlight detected edges as a bright overlay.", {}, MakeLayer<EdgeOverlayLayer>, Stable, ChannelWarn, true, "", "Edge detection can be useful on channels but should be labeled.", { "edge", "channel-warning" } },
        { LayerType::EdgeSaturationMask, "EdgeSaturationMask", "Edge Saturation Mask", "Edge Saturation Mask", "Effects / Damage", "Drive foreground and background saturation from edge structure.", {}, MakeLayer<EdgeSaturationMaskLayer>, NeedsFix, FullImagePreferred, true, "", "Saturation-driven edge behavior is full-image-first.", { "edge", "full-image-preferred" } },
        { LayerType::TextOverlay, "TextOverlay", "Text Overlay", "Text Overlay", "Texture / Generate", "Render text over the image.", {}, MakeLayer<TextOverlayLayer>, Hidden, FullImageOnly, false, "Hidden from add-node browser until C++ text texture generation is implemented.", "Text overlay channel behavior should wait until the node is complete.", { "text", "hidden", "advanced-ui" } },

        { LayerType::DctCompression, "DctCompression", "DCT Compression", "DCT Compression", "Effects / Damage", "Simulate DCT block compression artifacts.", {}, MakeLayer<DctCompressionLayer>, Stable, ChannelWarn, true, "", "Scalar artifacts can be useful but should be tested.", { "compression", "channel-warning" } },
        { LayerType::ChromaSubsampleCompression, "ChromaSubsampleCompression", "Chroma Subsample Compression", "Chroma Subsample Compression", "Effects / Damage", "Simulate chroma subsampling artifacts.", {}, MakeLayer<ChromaSubsampleCompressionLayer>, NeedsFix, FullImagePreferred, true, "", "Chroma behavior is full-color by nature.", { "compression", "full-image-preferred" } },
        { LayerType::WaveletCompression, "WaveletCompression", "Wavelet Compression", "Wavelet Compression", "Effects / Damage", "Simulate wavelet-style compression artifacts.", {}, MakeLayer<WaveletCompressionLayer>, Stable, ChannelWarn, true, "", "Scalar artifacts can be useful but should be tested.", { "compression", "channel-warning" } },
        { LayerType::JpegBlocks, "JpegBlocks", "JPEG Blocks", "JPEG Blocks", "Effects / Damage", "Break the image into coarse JPEG-style blocks.", {}, MakeLayer<JpegBlocksLayer>, Stable, ChannelWarn, true, "", "Block artifacts can be useful on channels but should be tested.", { "artifact", "channel-warning" } },
        { LayerType::Pixelation, "Pixelation", "Pixelation", "Pixelation", "Effects / Damage", "Pixelate the image into coarse cells.", {}, MakeLayer<PixelationLayer>, Stable, ChannelSafe, true, "", "Pixelation is valid on scalar channel streams.", { "artifact", "channel-safe" } },
        { LayerType::ColorBleed, "ColorBleed", "Color Bleed", "Color Bleed", "Effects / Damage", "Smear color channels horizontally.", {}, MakeLayer<ColorBleedLayer>, NeedsFix, FullImagePreferred, true, "", "Color bleed implies RGB channel relationships.", { "artifact", "full-image-preferred" } },
        { LayerType::ImageBreaks, "ImageBreaks", "Block Shift", "Block Shift", "Effects / Damage", "Slice and offset regions of the image.", {}, MakeLayer<ImageBreaksLayer>, Stable, ChannelWarn, true, "", "Per-channel block shifts can intentionally misregister recombined color.", { "artifact", "advanced-ui", "channel-warning" } },
        { LayerType::AnalogVideo, "AnalogVideo", "Analog Video (VHS/CRT)", "Analog Video (VHS/CRT)", "Effects / Damage", "Simulate analog video artifacts.", {}, MakeLayer<AnalogVideoLayer>, Stable, FullImagePreferred, true, "", "Analog artifact stack is full-image-first.", { "video", "advanced-ui", "full-image-preferred" } },

        { LayerType::Vignette, "Vignette", "Vignette", "Vignette", "Effects / Damage", "Darken or focus the image edges.", {}, MakeLayer<VignetteLayer>, Stable, ChannelWarn, true, "", "On channels this acts as a spatial channel multiplier.", { "focus", "channel-warning" } },
        { LayerType::GlareRays, "GlareRays", "Glare Rays", "Glare Rays", "Effects / Damage", "Add directional glare rays.", {}, MakeLayer<GlareRaysLayer>, Stable, FullImagePreferred, true, "", "Highlight/glare behavior is full-image-first.", { "glare", "full-image-preferred" } },
        { LayerType::ChromaticAberration, "ChromaticAberration", "Chromatic Aberration", "Chromatic Aberration", "Effects / Damage", "Offset color channels for lens fringing.", {}, MakeLayer<ChromaticAberrationLayer>, NeedsFix, FullImagePreferred, true, "", "Chromatic offset is ambiguous on a single channel.", { "lens", "full-image-preferred" } },
        { LayerType::LensDistortion, "LensDistortion", "Lens Distortion", "Lens Distortion", "Effects / Damage", "Warp the image with lens distortion.", {}, MakeLayer<LensDistortionLayer>, Stable, ChannelWarn, true, "", "Spatial warp is valid but can misregister recombined channels.", { "lens", "channel-warning" } },
        { LayerType::HeatwaveDistortion, "HeatwaveDistortion", "Heatwave Distortion", "Heatwave Distortion", "Effects / Damage", "Distort the image with directional heat shimmer.", {}, MakeLayer<HeatwaveDistortionLayer>, Stable, ChannelWarn, true, "", "Spatial warp is valid but can misregister recombined channels.", { "distortion", "channel-warning" } },
        { LayerType::RippleDistortion, "RippleDistortion", "Ripple Distortion", "Ripple Distortion", "Effects / Damage", "Distort the image with radial ripple waves.", {}, MakeLayer<RippleDistortionLayer>, Stable, ChannelWarn, true, "", "Spatial warp is valid but can misregister recombined channels.", { "distortion", "channel-warning" } },
        { LayerType::AiryBloom, "AiryBloom", "Airy Bloom", "Airy Bloom", "Blur / Focus", "Add airy disk bloom around highlights.", { "AiryDiskBloom" }, MakeLayer<AiryBloomLayer>, Stable, FullImagePreferred, true, "", "Highlight/bloom behavior is full-image-first.", { "bloom", "full-image-preferred" } },
    };
    return descriptors;
}

bool TypeIdMatches(const LayerDescriptor& descriptor, const std::string& typeId) {
    if (typeId == descriptor.typeId) {
        return true;
    }
    return std::any_of(
        descriptor.legacyTypeIds.begin(),
        descriptor.legacyTypeIds.end(),
        [&typeId](const char* legacyTypeId) {
            return typeId == legacyTypeId;
        });
}

} // namespace

namespace LayerRegistry {

std::shared_ptr<LayerBase> CreateLayer(LayerType type) {
    const LayerDescriptor* descriptor = GetDescriptor(type);
    return descriptor ? descriptor->create() : nullptr;
}

std::shared_ptr<LayerBase> CreateLayerFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->create() : nullptr;
}

const LayerDescriptor* GetDescriptor(LayerType type) {
    const auto& descriptors = Descriptors();
    auto it = std::find_if(descriptors.begin(), descriptors.end(), [type](const LayerDescriptor& descriptor) {
        return descriptor.type == type;
    });
    return it != descriptors.end() ? &(*it) : nullptr;
}

const LayerDescriptor* FindDescriptorByTypeId(const std::string& typeId) {
    const auto& descriptors = Descriptors();
    auto it = std::find_if(descriptors.begin(), descriptors.end(), [&typeId](const LayerDescriptor& descriptor) {
        return TypeIdMatches(descriptor, typeId);
    });
    return it != descriptors.end() ? &(*it) : nullptr;
}

const std::vector<LayerDescriptor>& GetAllDescriptors() {
    return Descriptors();
}

std::map<std::string, std::vector<const LayerDescriptor*>> GetDescriptorsByCategory() {
    std::map<std::string, std::vector<const LayerDescriptor*>> byCategory;
    for (const LayerDescriptor& descriptor : Descriptors()) {
        byCategory[descriptor.categoryName].push_back(&descriptor);
    }
    return byCategory;
}

std::string GetDisplayNameFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->displayName : std::string();
}

std::string GetLibraryDisplayNameFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->libraryDisplayName : std::string();
}

const char* LifecycleStatusLabel(LayerLifecycleStatus status) {
    switch (status) {
        case LayerLifecycleStatus::Stable: return "Stable";
        case LayerLifecycleStatus::NeedsFix: return "Needs Fix";
        case LayerLifecycleStatus::Experimental: return "Experimental";
        case LayerLifecycleStatus::Deprecated: return "Deprecated";
        case LayerLifecycleStatus::Hidden: return "Hidden";
    }
    return "Unknown";
}

const char* ChannelPolicyLabel(LayerChannelPolicy policy) {
    switch (policy) {
        case LayerChannelPolicy::ChannelSafe: return "Channel Safe";
        case LayerChannelPolicy::ChannelUsefulWithWarning: return "Channel Useful With Warning";
        case LayerChannelPolicy::FullImagePreferred: return "Full Image Preferred";
        case LayerChannelPolicy::FullImageOnly: return "Full Image Only";
        case LayerChannelPolicy::ReworkBeforeExpose: return "Rework Before Expose";
    }
    return "Unknown";
}

bool ShouldShowInNodeBrowser(const LayerDescriptor& descriptor) {
    return descriptor.visibleInNodeBrowser &&
        descriptor.lifecycleStatus != LayerLifecycleStatus::Hidden &&
        descriptor.lifecycleStatus != LayerLifecycleStatus::Deprecated;
}

bool ValidateRegistry(std::vector<std::string>* errors) {
    std::vector<std::string> localErrors;
    std::vector<std::string>& outErrors = errors ? *errors : localErrors;
    outErrors.clear();

    const auto& descriptors = Descriptors();
    if (descriptors.empty()) {
        outErrors.push_back("LayerRegistry has no descriptors.");
    }

    std::map<std::string, const LayerDescriptor*> typeIdOwners;
    std::map<std::string, const LayerDescriptor*> aliasOwners;

    for (const LayerDescriptor& descriptor : descriptors) {
        const char* typeId = descriptor.typeId ? descriptor.typeId : "";
        const char* displayName = descriptor.displayName ? descriptor.displayName : "";
        const char* categoryName = descriptor.categoryName ? descriptor.categoryName : "";

        if (std::string(typeId).empty()) {
            outErrors.push_back("Layer descriptor is missing a stable type ID.");
        }
        if (std::string(displayName).empty()) {
            outErrors.push_back(std::string("Layer descriptor is missing a display name for type ID '") + typeId + "'.");
        }
        if (std::string(categoryName).empty()) {
            outErrors.push_back(std::string("Layer descriptor is missing a category for type ID '") + typeId + "'.");
        }
        if (!descriptor.create) {
            outErrors.push_back(std::string("Layer descriptor is missing a factory for type ID '") + typeId + "'.");
        } else if (!descriptor.create()) {
            outErrors.push_back(std::string("Layer factory returned null for type ID '") + typeId + "'.");
        }

        if (!std::string(typeId).empty()) {
            auto inserted = typeIdOwners.emplace(typeId, &descriptor);
            if (!inserted.second) {
                outErrors.push_back(std::string("Duplicate stable layer type ID '") + typeId + "'.");
            }
        }
    }

    for (const LayerDescriptor& descriptor : descriptors) {
        const char* typeId = descriptor.typeId ? descriptor.typeId : "";

        if (!std::string(typeId).empty()) {
            const LayerDescriptor* resolved = FindDescriptorByTypeId(typeId);
            if (resolved != &descriptor) {
                outErrors.push_back(std::string("Stable layer type ID does not resolve to its descriptor: '") + typeId + "'.");
            }
        }

        const LayerDescriptor* descriptorByType = GetDescriptor(descriptor.type);
        if (descriptorByType != &descriptor) {
            outErrors.push_back(std::string("LayerType enum does not resolve to the expected descriptor for type ID '") + typeId + "'.");
        }

        for (const char* aliasPtr : descriptor.legacyTypeIds) {
            const std::string alias = aliasPtr ? aliasPtr : "";
            if (alias.empty()) {
                outErrors.push_back(std::string("Layer descriptor has an empty legacy alias for type ID '") + typeId + "'.");
                continue;
            }

            if (typeIdOwners.find(alias) != typeIdOwners.end()) {
                outErrors.push_back(std::string("Legacy alias conflicts with a stable type ID: '") + alias + "'.");
            }

            auto inserted = aliasOwners.emplace(alias, &descriptor);
            if (!inserted.second && inserted.first->second != &descriptor) {
                outErrors.push_back(std::string("Legacy alias maps to multiple descriptors: '") + alias + "'.");
            }

            const LayerDescriptor* resolved = FindDescriptorByTypeId(alias);
            if (resolved != &descriptor) {
                outErrors.push_back(std::string("Legacy alias does not resolve to its descriptor: '") + alias + "'.");
            }
        }
    }

    return outErrors.empty();
}

} // namespace LayerRegistry
