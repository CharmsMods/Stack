#include "LayerRegistry.h"

#include "Layers/AdjustmentsLayer.h"
#include "Layers/AiryBloomLayer.h"
#include "Layers/AlphaHandlingLayer.h"
#include "Layers/AnalogVideoLayer.h"
#include "Layers/BackgroundPatcherLayer.h"
#include "Layers/BilateralFilterLayer.h"
#include "Layers/BlurLayer.h"
#include "Layers/CellShadingLayer.h"
#include "Layers/ChromaticAberrationLayer.h"
#include "Layers/ColorGradeLayer.h"
#include "Layers/CompressionLayer.h"
#include "Layers/CorruptionLayer.h"
#include "Layers/CropTransformLayer.h"
#include "Layers/DenoisingLayer.h"
#include "Layers/DitherLayer.h"
#include "Layers/EdgeEffectsLayer.h"
#include "Layers/ExpanderLayer.h"
#include "Layers/GlareRaysLayer.h"
#include "Layers/HalftoningLayer.h"
#include "Layers/HankelBlurLayer.h"
#include "Layers/HDRLayer.h"
#include "Layers/HeatwaveLayer.h"
#include "Layers/ImageBreaksLayer.h"
#include "Layers/LensDistortionLayer.h"
#include "Layers/NoiseLayer.h"
#include "Layers/PaletteReconstructorLayer.h"
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
        { LayerType::CropTransform, "CropTransform", "Crop / Rotate / Flip", "Crop / Rotate / Flip", "BASE", "Crop, rotate, and flip the source canvas.", {}, MakeLayer<CropTransformLayer> },
        { LayerType::Expander, "Expander", "Expander (Canvas Pad)", "Expander", "BASE", "Pad the canvas with a configurable fill.", {}, MakeLayer<ExpanderLayer> },
        { LayerType::BackgroundPatcher, "BackgroundPatcher", "Background Patcher", "Background Patcher", "BASE", "Patch or replace background colors.", {}, MakeLayer<BackgroundPatcherLayer> },
        { LayerType::AlphaHandling, "AlphaHandling", "Alpha Handling / Protect", "Alpha Handling", "BASE", "Control alpha preservation and protection.", {}, MakeLayer<AlphaHandlingLayer> },

        { LayerType::Adjustments, "Adjustments", "Adjustments (Color/Contrast)", "Adjustments", "COLOR", "Basic color and contrast adjustments.", {}, MakeLayer<AdjustmentsLayer> },
        { LayerType::ColorGrade, "ColorGrade", "3-Way Color Grade", "3-Way Color Grade", "COLOR", "Grade shadows, midtones, and highlights.", {}, MakeLayer<ColorGradeLayer> },
        { LayerType::HDR, "HDR", "HDR Emulation", "HDR Emulation", "COLOR", "Emulate HDR bloom and highlight recovery.", {}, MakeLayer<HDRLayer> },

        { LayerType::Blur, "Blur", "Blur (Box/Gaussian)", "Blur", "TEXTURE & BLUR", "Apply box or gaussian blur.", {}, MakeLayer<BlurLayer> },
        { LayerType::Denoising, "Denoising", "Denoising", "Denoising", "TEXTURE & BLUR", "Reduce image noise while preserving structure.", {}, MakeLayer<DenoisingLayer> },
        { LayerType::BilateralFilter, "BilateralFilter", "Bilateral Filter", "Bilateral Filter", "TEXTURE & BLUR", "Smooth detail while preserving edges.", {}, MakeLayer<BilateralFilterLayer> },
        { LayerType::Noise, "Noise", "Noise / Film Grain", "Noise / Film Grain", "TEXTURE & BLUR", "Add procedural noise and film grain.", {}, MakeLayer<NoiseLayer> },
        { LayerType::TiltShiftBlur, "TiltShiftBlur", "Tilt-Shift Blur", "Tilt-Shift Blur", "TEXTURE & BLUR", "Apply directional depth-style blur.", {}, MakeLayer<TiltShiftBlurLayer> },
        { LayerType::HankelBlur, "HankelBlur", "Hankel (Optical) Blur", "Hankel Blur", "TEXTURE & BLUR", "Apply optical Hankel blur.", {}, MakeLayer<HankelBlurLayer> },

        { LayerType::Dither, "Dither", "Dithering", "Dithering", "STYLIZE", "Apply ordered or palette dithering.", { "Dithering" }, MakeLayer<DitherLayer> },
        { LayerType::Halftoning, "Halftoning", "Halftoning (Dots)", "Halftoning", "STYLIZE", "Convert tone into dot patterns.", {}, MakeLayer<HalftoningLayer> },
        { LayerType::CellShading, "CellShading", "Cell Shading", "Cell Shading", "STYLIZE", "Posterize lighting into cell-shaded bands.", { "Cell" }, MakeLayer<CellShadingLayer> },
        { LayerType::PaletteReconstructor, "PaletteReconstructor", "Palette Reconstructor", "Palette Reconstructor", "STYLIZE", "Rebuild the image against a reduced palette.", { "Palette" }, MakeLayer<PaletteReconstructorLayer> },
        { LayerType::EdgeEffects, "EdgeEffects", "Edge Effects", "Edge Effects", "STYLIZE", "Enhance or stylize detected edges.", { "Edge" }, MakeLayer<EdgeEffectsLayer> },
        { LayerType::TextOverlay, "TextOverlay", "Text Overlay", "Text Overlay", "STYLIZE", "Render text over the image.", {}, MakeLayer<TextOverlayLayer> },

        { LayerType::Compression, "Compression", "Compression", "Compression", "DAMAGE & GLITCH", "Simulate image compression artifacts.", {}, MakeLayer<CompressionLayer> },
        { LayerType::Corruption, "Corruption", "Corruption (Digital)", "Corruption", "DAMAGE & GLITCH", "Apply digital corruption effects.", {}, MakeLayer<CorruptionLayer> },
        { LayerType::ImageBreaks, "ImageBreaks", "Image Breaks", "Image Breaks", "DAMAGE & GLITCH", "Slice and offset regions of the image.", {}, MakeLayer<ImageBreaksLayer> },
        { LayerType::AnalogVideo, "AnalogVideo", "Analog (VHS/CRT)", "Analog Video (VHS/CRT)", "DAMAGE & GLITCH", "Simulate analog video artifacts.", {}, MakeLayer<AnalogVideoLayer> },

        { LayerType::Vignette, "Vignette", "Vignette", "Vignette & Focus", "OPTICS", "Darken or focus the image edges.", {}, MakeLayer<VignetteLayer> },
        { LayerType::GlareRays, "GlareRays", "Glare Rays", "Glare Rays", "OPTICS", "Add directional glare rays.", {}, MakeLayer<GlareRaysLayer> },
        { LayerType::ChromaticAberration, "ChromaticAberration", "Chromatic Aberration", "Chromatic Aberration", "OPTICS", "Offset color channels for lens fringing.", {}, MakeLayer<ChromaticAberrationLayer> },
        { LayerType::LensDistortion, "LensDistortion", "Lens Distortion", "Lens Distortion", "OPTICS", "Warp the image with lens distortion.", {}, MakeLayer<LensDistortionLayer> },
        { LayerType::Heatwave, "Heatwave", "Heatwave & Ripples", "Heatwave & Ripples", "OPTICS", "Distort the image with heatwave ripples.", {}, MakeLayer<HeatwaveLayer> },
        { LayerType::AiryBloom, "AiryBloom", "Airy Bloom", "Airy Bloom", "OPTICS", "Add airy disk bloom around highlights.", { "AiryDiskBloom" }, MakeLayer<AiryBloomLayer> },
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
