#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Validation {

struct ValidationColorStats {
    float avgR = 0.0f;
    float avgG = 0.0f;
    float avgB = 0.0f;
    float avgLuma = 0.0f;
    float avgPixelChroma = 0.0f;
    float channelSpread = 0.0f;
    float channelRatio = 1.0f;
    float warmCoolBias = 0.0f;
    float magentaGreenBias = 0.0f;
    float biasRisk = 0.0f;
};

struct ValidationFineNoiseStats {
    float lumaHighFrequency = 0.0f;
    float chromaHighFrequency = 0.0f;
    float combined = 0.0f;
};

std::size_t CountPixelsWithNonZeroAlpha(const std::vector<unsigned char>& pixels);
std::size_t CountPixelsWithNonZeroRgb(const std::vector<unsigned char>& pixels);
float ComputeAverageNormalizedLuma(const std::vector<unsigned char>& pixels);
ValidationColorStats ComputeValidationColorStats(const std::vector<unsigned char>& pixels);
ValidationFineNoiseStats ComputeValidationFineNoiseStats(
    const std::vector<unsigned char>& pixels,
    int width,
    int height);

std::vector<float> ReadTextureRgbaFloat(unsigned int texture, int width, int height);
float ReadTextureMaxRgb(unsigned int texture, int width, int height);

std::filesystem::path ResolveValidationInputPath(const char* rawPath);
std::string SanitizeValidationFileStem(std::string value);
bool WriteValidationPng(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& rgbaPixels,
    int width,
    int height);

} // namespace Stack::Validation
