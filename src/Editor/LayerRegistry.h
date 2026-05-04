#pragma once

#include "Layers/LayerBase.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

enum class LayerType {
    Adjustments,
    ColorGrade,
    HDR,
    CropTransform,
    Blur,
    Noise,
    Vignette,
    ChromaticAberration,
    LensDistortion,
    TiltShiftBlur,
    Dither,
    Compression,
    CellShading,
    Heatwave,
    PaletteReconstructor,
    EdgeEffects,
    AiryBloom,
    ImageBreaks,
    AnalogVideo,
    BilateralFilter,
    Denoising,
    Halftoning,
    HankelBlur,
    GlareRays,
    Corruption,
    AlphaHandling,
    BackgroundPatcher,
    Expander,
    TextOverlay
};

struct LayerDescriptor {
    LayerType type;
    const char* typeId;
    const char* displayName;
    const char* libraryDisplayName;
    const char* categoryName;
    const char* description;
    std::vector<const char*> legacyTypeIds;
    std::function<std::shared_ptr<LayerBase>()> create;
};

namespace LayerRegistry {

std::shared_ptr<LayerBase> CreateLayer(LayerType type);
std::shared_ptr<LayerBase> CreateLayerFromTypeId(const std::string& typeId);

const LayerDescriptor* GetDescriptor(LayerType type);
const LayerDescriptor* FindDescriptorByTypeId(const std::string& typeId);
const std::vector<LayerDescriptor>& GetAllDescriptors();
std::map<std::string, std::vector<const LayerDescriptor*>> GetDescriptorsByCategory();

std::string GetDisplayNameFromTypeId(const std::string& typeId);
std::string GetLibraryDisplayNameFromTypeId(const std::string& typeId);

bool ValidateRegistry(std::vector<std::string>* errors = nullptr);

} // namespace LayerRegistry
