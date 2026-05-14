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
#include "Layers/NoiseLayer.h"
#include "Layers/PaletteReconstructorLayer.h"
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
#include "Layers/VignetteLayer.h"
#include <algorithm>
#include <map>
#include <string>

namespace {

template <typename T>
std::shared_ptr<LayerBase> MakeLayer() {
    return std::make_shared<T>();
}

const std::vector<LayerDescriptor>& Descriptors() {
    static const std::vector<LayerDescriptor> descriptors = {
        { LayerType::Crop, "Crop", "Crop", "Crop", "Transform / Canvas", "Crop the source canvas.", {}, MakeLayer<CropLayer> },
        { LayerType::Rotate, "Rotate", "Rotate", "Rotate", "Transform / Canvas", "Rotate the source canvas.", {}, MakeLayer<RotateLayer> },
        { LayerType::Flip, "Flip", "Flip", "Flip", "Transform / Canvas", "Flip the source canvas horizontally or vertically.", {}, MakeLayer<FlipLayer> },
        { LayerType::Expander, "Expander", "Expand Canvas", "Expand Canvas", "Transform / Canvas", "Pad the canvas with a configurable fill.", {}, MakeLayer<ExpanderLayer> },
        { LayerType::BackgroundPatcher, "BackgroundPatcher", "Background Remover", "Background Remover", "Composite", "Remove or isolate background colors.", {}, MakeLayer<BackgroundPatcherLayer> },
        { LayerType::AlphaHandling, "AlphaHandling", "Alpha Protect", "Alpha Protect", "Composite", "Control alpha preservation and protection.", {}, MakeLayer<AlphaHandlingLayer> },

        { LayerType::Brightness, "Brightness", "Brightness", "Brightness", "Color", "Adjust image brightness.", {}, MakeLayer<BrightnessLayer> },
        { LayerType::Contrast, "Contrast", "Contrast", "Contrast", "Color", "Adjust image contrast.", {}, MakeLayer<ContrastLayer> },
        { LayerType::Saturation, "Saturation", "Saturation", "Saturation", "Color", "Adjust image saturation.", {}, MakeLayer<SaturationLayer> },
        { LayerType::Warmth, "Warmth", "Warmth", "Warmth", "Color", "Shift the image warmer or cooler.", {}, MakeLayer<WarmthLayer> },
        { LayerType::Sharpen, "Sharpen", "Sharpen", "Sharpen", "Color", "Sharpen edges with threshold control.", {}, MakeLayer<SharpenLayer> },
        { LayerType::ColorGrade, "ColorGrade", "3-Way Color Grade", "3-Way Color Grade", "Color", "Grade shadows, midtones, and highlights.", {}, MakeLayer<ColorGradeLayer> },
        { LayerType::HDR, "HDR", "HDR Compressor", "HDR Compressor", "Color", "Emulate HDR bloom and highlight recovery.", {}, MakeLayer<HDRLayer> },

        { LayerType::BoxBlur, "BoxBlur", "Box Blur", "Box Blur", "Blur / Focus", "Apply a box blur.", {}, MakeLayer<BoxBlurLayer> },
        { LayerType::GaussianBlur, "GaussianBlur", "Gaussian Blur", "Gaussian Blur", "Blur / Focus", "Apply a gaussian blur.", {}, MakeLayer<GaussianBlurLayer> },
        { LayerType::NonLocalMeansDenoise, "NonLocalMeansDenoise", "Non-Local Means Denoise", "Non-Local Means Denoise", "Blur / Focus", "Reduce noise with non-local means filtering.", {}, MakeLayer<NonLocalMeansDenoiseLayer> },
        { LayerType::MedianDenoise, "MedianDenoise", "Median Denoise", "Median Denoise", "Blur / Focus", "Reduce noise with a median filter.", {}, MakeLayer<MedianDenoiseLayer> },
        { LayerType::MeanDenoise, "MeanDenoise", "Mean Denoise", "Mean Denoise", "Blur / Focus", "Reduce noise with a mean box filter.", {}, MakeLayer<MeanDenoiseLayer> },
        { LayerType::BilateralFilter, "BilateralFilter", "Bilateral Filter", "Bilateral Filter", "Blur / Focus", "Smooth detail while preserving edges.", {}, MakeLayer<BilateralFilterLayer> },
        { LayerType::Noise, "Noise", "Noise", "Noise", "Texture / Generate", "Add procedural noise and film grain.", {}, MakeLayer<NoiseLayer> },
        { LayerType::TiltShiftBlur, "TiltShiftBlur", "Tilt-Shift Blur", "Tilt-Shift Blur", "Blur / Focus", "Apply directional depth-style blur.", {}, MakeLayer<TiltShiftBlurLayer> },
        { LayerType::HankelBlur, "HankelBlur", "Optical Blur", "Optical Blur", "Blur / Focus", "Apply optical Hankel blur.", {}, MakeLayer<HankelBlurLayer> },

        { LayerType::OrderedDither8x8, "OrderedDither8x8", "Ordered Dither 8x8", "Ordered Dither 8x8", "Color", "Apply ordered Bayer 8x8 dithering.", {}, MakeLayer<OrderedDither8x8Layer> },
        { LayerType::ErrorDiffusionDither, "ErrorDiffusionDither", "Error Diffusion Dither", "Error Diffusion Dither", "Color", "Apply error diffusion style dithering.", {}, MakeLayer<ErrorDiffusionDitherLayer> },
        { LayerType::WhiteNoiseDither, "WhiteNoiseDither", "White Noise Dither", "White Noise Dither", "Color", "Apply white noise dithering.", {}, MakeLayer<WhiteNoiseDitherLayer> },
        { LayerType::OrderedDither4x4, "OrderedDither4x4", "Ordered Dither 4x4", "Ordered Dither 4x4", "Color", "Apply ordered Bayer 4x4 dithering.", {}, MakeLayer<OrderedDither4x4Layer> },
        { LayerType::OrderedDither2x2, "OrderedDither2x2", "Ordered Dither 2x2", "Ordered Dither 2x2", "Color", "Apply ordered Bayer 2x2 dithering.", {}, MakeLayer<OrderedDither2x2Layer> },
        { LayerType::InterleavedGradientDither, "InterleavedGradientDither", "Interleaved Gradient Dither", "Interleaved Gradient Dither", "Color", "Apply interleaved gradient dithering.", {}, MakeLayer<InterleavedGradientDitherLayer> },
        { LayerType::Halftoning, "Halftoning", "Halftone", "Halftone", "Color", "Convert tone into dot patterns.", {}, MakeLayer<HalftoningLayer> },
        { LayerType::CellShading, "CellShading", "Cell Shading", "Cell Shading", "Color", "Posterize lighting into cell-shaded bands.", { "Cell" }, MakeLayer<CellShadingLayer> },
        { LayerType::PaletteReconstructor, "PaletteReconstructor", "Palette Rebuild", "Palette Rebuild", "Color", "Rebuild the image against a reduced palette.", { "Palette" }, MakeLayer<PaletteReconstructorLayer> },
        { LayerType::EdgeOverlay, "EdgeOverlay", "Edge Overlay", "Edge Overlay", "Effects / Damage", "Highlight detected edges as a bright overlay.", {}, MakeLayer<EdgeOverlayLayer> },
        { LayerType::EdgeSaturationMask, "EdgeSaturationMask", "Edge Saturation Mask", "Edge Saturation Mask", "Effects / Damage", "Drive foreground and background saturation from edge structure.", {}, MakeLayer<EdgeSaturationMaskLayer> },
        { LayerType::TextOverlay, "TextOverlay", "Text Overlay", "Text Overlay", "Texture / Generate", "Render text over the image.", {}, MakeLayer<TextOverlayLayer> },

        { LayerType::DctCompression, "DctCompression", "DCT Compression", "DCT Compression", "Effects / Damage", "Simulate DCT block compression artifacts.", {}, MakeLayer<DctCompressionLayer> },
        { LayerType::ChromaSubsampleCompression, "ChromaSubsampleCompression", "Chroma Subsample Compression", "Chroma Subsample Compression", "Effects / Damage", "Simulate chroma subsampling artifacts.", {}, MakeLayer<ChromaSubsampleCompressionLayer> },
        { LayerType::WaveletCompression, "WaveletCompression", "Wavelet Compression", "Wavelet Compression", "Effects / Damage", "Simulate wavelet-style compression artifacts.", {}, MakeLayer<WaveletCompressionLayer> },
        { LayerType::JpegBlocks, "JpegBlocks", "JPEG Blocks", "JPEG Blocks", "Effects / Damage", "Break the image into coarse JPEG-style blocks.", {}, MakeLayer<JpegBlocksLayer> },
        { LayerType::Pixelation, "Pixelation", "Pixelation", "Pixelation", "Effects / Damage", "Pixelate the image into coarse cells.", {}, MakeLayer<PixelationLayer> },
        { LayerType::ColorBleed, "ColorBleed", "Color Bleed", "Color Bleed", "Effects / Damage", "Smear color channels horizontally.", {}, MakeLayer<ColorBleedLayer> },
        { LayerType::ImageBreaks, "ImageBreaks", "Block Shift", "Block Shift", "Effects / Damage", "Slice and offset regions of the image.", {}, MakeLayer<ImageBreaksLayer> },
        { LayerType::AnalogVideo, "AnalogVideo", "Analog Video (VHS/CRT)", "Analog Video (VHS/CRT)", "Effects / Damage", "Simulate analog video artifacts.", {}, MakeLayer<AnalogVideoLayer> },

        { LayerType::Vignette, "Vignette", "Vignette", "Vignette", "Effects / Damage", "Darken or focus the image edges.", {}, MakeLayer<VignetteLayer> },
        { LayerType::GlareRays, "GlareRays", "Glare Rays", "Glare Rays", "Effects / Damage", "Add directional glare rays.", {}, MakeLayer<GlareRaysLayer> },
        { LayerType::ChromaticAberration, "ChromaticAberration", "Chromatic Aberration", "Chromatic Aberration", "Effects / Damage", "Offset color channels for lens fringing.", {}, MakeLayer<ChromaticAberrationLayer> },
        { LayerType::LensDistortion, "LensDistortion", "Lens Distortion", "Lens Distortion", "Effects / Damage", "Warp the image with lens distortion.", {}, MakeLayer<LensDistortionLayer> },
        { LayerType::HeatwaveDistortion, "HeatwaveDistortion", "Heatwave Distortion", "Heatwave Distortion", "Effects / Damage", "Distort the image with directional heat shimmer.", {}, MakeLayer<HeatwaveDistortionLayer> },
        { LayerType::RippleDistortion, "RippleDistortion", "Ripple Distortion", "Ripple Distortion", "Effects / Damage", "Distort the image with radial ripple waves.", {}, MakeLayer<RippleDistortionLayer> },
        { LayerType::AiryBloom, "AiryBloom", "Airy Bloom", "Airy Bloom", "Blur / Focus", "Add airy disk bloom around highlights.", { "AiryDiskBloom" }, MakeLayer<AiryBloomLayer> },
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
