#pragma once

#include "Color/LutData.h"

#include <array>
#include <string>
#include <vector>

namespace ColorLut {

struct LutCreatorImage {
    std::string sourcePath;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
    std::vector<unsigned char> pixels;
};

struct LutCreatorSettings {
    int lutSize = 33;
    int maxSamples = 160000;
    int manualStride = 0;
    int smoothPasses = 4;
    float smoothStrength = 0.38f;
    float identityBias = 0.10f;
    float observationThreshold = 1.40f;
    std::string label;
    std::string importedTitle;
    LutUseMode useMode = LutUseMode::PostViewTransform;
    LutTransferFunction inputTransform = LutTransferFunction::None;
    LutTransferFunction outputTransform = LutTransferFunction::None;
};

struct LutCreatorStats {
    int totalPixelCount = 0;
    int sampledPixelCount = 0;
    int effectiveStride = 1;
    float voxelCoverage = 0.0f;
    float meanAbsoluteError = 0.0f;
    float maxAbsoluteError = 0.0f;
    float rootMeanSquareError = 0.0f;
};

struct LutCreatorResult {
    bool success = false;
    std::string message;
    LutPayload payload;
    LutCreatorStats stats;
};

bool LoadRasterImageForLutCreator(const std::string& path, LutCreatorImage& outImage, std::string* outMessage = nullptr);
std::array<float, 3> SampleLut3D(const Lut3DStage& stage, const std::array<float, 3>& rgb);
LutCreatorResult CreateLutFromImages(
    const LutCreatorImage& sourceImage,
    const LutCreatorImage& targetImage,
    const LutCreatorSettings& settings);
std::string LutCreatorSidecarPath(const std::string& cubePath);
bool ApplyLutCreatorSidecarMetadata(const std::string& cubePath, LutPayload& payload);
bool SaveCubeLutWithSidecar(
    const std::string& cubePath,
    const LutPayload& payload,
    const LutCreatorSettings& settings,
    const LutCreatorStats& stats,
    const std::string& sourceImagePath,
    const std::string& targetImagePath,
    std::string* outMessage = nullptr);

} // namespace ColorLut
