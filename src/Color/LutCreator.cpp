#include "Color/LutCreator.h"

#include "ThirdParty/json.hpp"
#include "ThirdParty/stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ColorLut {

namespace {

using json = nlohmann::json;

constexpr float kByteToUnit = 1.0f / 255.0f;

struct VoxelAccumulator {
    std::array<float, 3> sum { 0.0f, 0.0f, 0.0f };
    float weight = 0.0f;
};

std::string FileStem(const std::string& path) {
    try {
        return std::filesystem::path(path).stem().string();
    } catch (...) {
        return {};
    }
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::array<float, 3> LerpColor(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b,
    float t) {
    const float clamped = Clamp01(t);
    return {
        a[0] + (b[0] - a[0]) * clamped,
        a[1] + (b[1] - a[1]) * clamped,
        a[2] + (b[2] - a[2]) * clamped
    };
}

void ClampColor(std::array<float, 3>& color) {
    color[0] = Clamp01(color[0]);
    color[1] = Clamp01(color[1]);
    color[2] = Clamp01(color[2]);
}

std::size_t Index3D(int x, int y, int z, int size) {
    return (static_cast<std::size_t>(z) * static_cast<std::size_t>(size) +
            static_cast<std::size_t>(y)) * static_cast<std::size_t>(size) +
        static_cast<std::size_t>(x);
}

std::array<float, 3> IdentityColorForVoxel(int x, int y, int z, int size) {
    if (size <= 1) {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float denominator = static_cast<float>(size - 1);
    return {
        static_cast<float>(x) / denominator,
        static_cast<float>(y) / denominator,
        static_cast<float>(z) / denominator
    };
}

std::array<float, 3> ReadColorTriplet(const std::vector<float>& values, std::size_t index) {
    const std::size_t base = index * 3u;
    if (base + 2u >= values.size()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return { values[base], values[base + 1u], values[base + 2u] };
}

void WriteColorTriplet(std::vector<float>& values, std::size_t index, const std::array<float, 3>& color) {
    const std::size_t base = index * 3u;
    if (base + 2u >= values.size()) {
        return;
    }
    values[base] = color[0];
    values[base + 1u] = color[1];
    values[base + 2u] = color[2];
}

std::array<float, 3> ReadPixelRgb(const LutCreatorImage& image, int x, int y) {
    const std::size_t base =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) * 4u;
    if (base + 2u >= image.pixels.size()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return {
        static_cast<float>(image.pixels[base]) * kByteToUnit,
        static_cast<float>(image.pixels[base + 1u]) * kByteToUnit,
        static_cast<float>(image.pixels[base + 2u]) * kByteToUnit
    };
}

const char* LutUseModeToString(LutUseMode mode) {
    switch (mode) {
        case LutUseMode::PreViewTransform: return "PreViewTransform";
        case LutUseMode::PostViewTransform:
        default:
            return "PostViewTransform";
    }
}

LutUseMode LutUseModeFromString(const std::string& value) {
    if (value == "PreViewTransform") {
        return LutUseMode::PreViewTransform;
    }
    return LutUseMode::PostViewTransform;
}

const char* LutTransferFunctionToString(LutTransferFunction transform) {
    switch (transform) {
        case LutTransferFunction::SrgbEncode: return "SrgbEncode";
        case LutTransferFunction::Gamma22Encode: return "Gamma22Encode";
        case LutTransferFunction::SrgbDecode: return "SrgbDecode";
        case LutTransferFunction::Gamma22Decode: return "Gamma22Decode";
        case LutTransferFunction::None:
        default:
            return "None";
    }
}

LutTransferFunction LutTransferFunctionFromString(const std::string& value) {
    if (value == "SrgbEncode") return LutTransferFunction::SrgbEncode;
    if (value == "Gamma22Encode") return LutTransferFunction::Gamma22Encode;
    if (value == "SrgbDecode") return LutTransferFunction::SrgbDecode;
    if (value == "Gamma22Decode") return LutTransferFunction::Gamma22Decode;
    return LutTransferFunction::None;
}

std::string BuildDefaultGeneratedTitle(const LutCreatorImage& sourceImage, const LutCreatorImage& targetImage) {
    const std::string sourceStem = FileStem(sourceImage.sourcePath);
    const std::string targetStem = FileStem(targetImage.sourcePath);
    if (!sourceStem.empty() && !targetStem.empty()) {
        return sourceStem + " to " + targetStem;
    }
    if (!targetStem.empty()) {
        return targetStem;
    }
    if (!sourceStem.empty()) {
        return sourceStem + " LUT";
    }
    return "Generated LUT";
}

int ComputeEffectiveStride(const LutCreatorImage& image, const LutCreatorSettings& settings) {
    if (settings.manualStride > 0) {
        return std::max(1, settings.manualStride);
    }

    if (settings.maxSamples <= 0 || image.width <= 0 || image.height <= 0) {
        return 1;
    }

    const double totalPixels =
        static_cast<double>(image.width) * static_cast<double>(image.height);
    if (totalPixels <= static_cast<double>(settings.maxSamples)) {
        return 1;
    }

    const double ratio = totalPixels / static_cast<double>(settings.maxSamples);
    return std::max(1, static_cast<int>(std::ceil(std::sqrt(ratio))));
}

std::array<float, 3> AverageNeighborColors(
    const std::vector<float>& values,
    int x,
    int y,
    int z,
    int size) {
    std::array<float, 3> sum { 0.0f, 0.0f, 0.0f };
    int count = 0;

    const auto add = [&](int nx, int ny, int nz) {
        if (nx < 0 || ny < 0 || nz < 0 || nx >= size || ny >= size || nz >= size) {
            return;
        }
        const std::array<float, 3> color = ReadColorTriplet(values, Index3D(nx, ny, nz, size));
        sum[0] += color[0];
        sum[1] += color[1];
        sum[2] += color[2];
        ++count;
    };

    add(x, y, z);
    add(x - 1, y, z);
    add(x + 1, y, z);
    add(x, y - 1, z);
    add(x, y + 1, z);
    add(x, y, z - 1);
    add(x, y, z + 1);

    if (count <= 0) {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float invCount = 1.0f / static_cast<float>(count);
    sum[0] *= invCount;
    sum[1] *= invCount;
    sum[2] *= invCount;
    return sum;
}

std::string EscapeCubeTitle(const std::string& title) {
    std::string escaped = title;
    std::replace(escaped.begin(), escaped.end(), '"', '\'');
    return escaped;
}

json BuildSidecarDocument(
    const LutPayload& payload,
    const LutCreatorSettings& settings,
    const LutCreatorStats& stats,
    const std::string& sourceImagePath,
    const std::string& targetImagePath) {
    return json{
        { "stackLutSidecarVersion", 1 },
        { "title", payload.importedTitle },
        { "label", payload.label },
        { "useMode", LutUseModeToString(payload.useMode) },
        { "inputTransform", LutTransferFunctionToString(payload.inputTransform) },
        { "outputTransform", LutTransferFunctionToString(payload.outputTransform) },
        { "creator",
            {
                { "sourceImagePath", sourceImagePath },
                { "targetImagePath", targetImagePath },
                { "lutSize", settings.lutSize },
                { "maxSamples", settings.maxSamples },
                { "manualStride", settings.manualStride },
                { "smoothPasses", settings.smoothPasses },
                { "smoothStrength", settings.smoothStrength },
                { "identityBias", settings.identityBias },
                { "observationThreshold", settings.observationThreshold },
                { "totalPixelCount", stats.totalPixelCount },
                { "sampledPixelCount", stats.sampledPixelCount },
                { "effectiveStride", stats.effectiveStride },
                { "voxelCoverage", stats.voxelCoverage },
                { "meanAbsoluteError", stats.meanAbsoluteError },
                { "maxAbsoluteError", stats.maxAbsoluteError },
                { "rootMeanSquareError", stats.rootMeanSquareError }
            }
        }
    };
}

bool WriteCubeFile(const std::string& cubePath, const LutPayload& payload, std::string* outMessage) {
    if (!HasLut3D(payload)) {
        if (outMessage) {
            *outMessage = "The generated LUT does not contain 3D data.";
        }
        return false;
    }

    std::ofstream output(cubePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (outMessage) {
            *outMessage = "Could not open the destination .cube file.";
        }
        return false;
    }

    output << "TITLE \"" << EscapeCubeTitle(payload.importedTitle.empty() ? "Generated LUT" : payload.importedTitle) << "\"\n";
    output << "DOMAIN_MIN "
           << payload.lut3D.domainMin[0] << ' '
           << payload.lut3D.domainMin[1] << ' '
           << payload.lut3D.domainMin[2] << "\n";
    output << "DOMAIN_MAX "
           << payload.lut3D.domainMax[0] << ' '
           << payload.lut3D.domainMax[1] << ' '
           << payload.lut3D.domainMax[2] << "\n";
    output << "LUT_3D_SIZE " << payload.lut3D.size << "\n";
    output << std::fixed << std::setprecision(8);

    const int size = payload.lut3D.size;
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const std::size_t index = Index3D(r, g, b, size) * 3u;
                output << Clamp01(payload.lut3D.values[index]) << ' '
                       << Clamp01(payload.lut3D.values[index + 1u]) << ' '
                       << Clamp01(payload.lut3D.values[index + 2u]) << "\n";
            }
        }
    }

    if (!output.good()) {
        if (outMessage) {
            *outMessage = "Failed while writing the .cube LUT file.";
        }
        return false;
    }

    if (outMessage) {
        outMessage->clear();
    }
    return true;
}

} // namespace

bool LoadRasterImageForLutCreator(const std::string& path, LutCreatorImage& outImage, std::string* outMessage) {
    outImage = {};
    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int originalChannels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &originalChannels, 4);
    if (!pixels) {
        if (outMessage) {
            *outMessage = "Could not decode the selected raster image.";
        }
        return false;
    }

    outImage.sourcePath = path;
    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.originalChannels = originalChannels;
    outImage.pixels.assign(
        pixels,
        pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    stbi_image_free(pixels);

    if (outMessage) {
        outMessage->clear();
    }
    return true;
}

std::array<float, 3> SampleLut3D(const Lut3DStage& stage, const std::array<float, 3>& rgb) {
    if (stage.size <= 1 || stage.values.size() != static_cast<std::size_t>(stage.size) * static_cast<std::size_t>(stage.size) * static_cast<std::size_t>(stage.size) * 3u) {
        return rgb;
    }

    std::array<float, 3> normalized { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 3; ++i) {
        const float domainMin = stage.domainMin[static_cast<std::size_t>(i)];
        const float domainMax = stage.domainMax[static_cast<std::size_t>(i)];
        const float range = domainMax - domainMin;
        if (std::abs(range) <= 1e-6f) {
            normalized[static_cast<std::size_t>(i)] = Clamp01(rgb[static_cast<std::size_t>(i)]);
        } else {
            normalized[static_cast<std::size_t>(i)] = Clamp01((rgb[static_cast<std::size_t>(i)] - domainMin) / range);
        }
    }

    const float maxCoord = static_cast<float>(stage.size - 1);
    const float x = normalized[0] * maxCoord;
    const float y = normalized[1] * maxCoord;
    const float z = normalized[2] * maxCoord;

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = std::min(x0 + 1, stage.size - 1);
    const int y1 = std::min(y0 + 1, stage.size - 1);
    const int z1 = std::min(z0 + 1, stage.size - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float fz = z - static_cast<float>(z0);

    const auto sample = [&](int sx, int sy, int sz) {
        return ReadColorTriplet(stage.values, Index3D(sx, sy, sz, stage.size));
    };

    const std::array<float, 3> c000 = sample(x0, y0, z0);
    const std::array<float, 3> c100 = sample(x1, y0, z0);
    const std::array<float, 3> c010 = sample(x0, y1, z0);
    const std::array<float, 3> c110 = sample(x1, y1, z0);
    const std::array<float, 3> c001 = sample(x0, y0, z1);
    const std::array<float, 3> c101 = sample(x1, y0, z1);
    const std::array<float, 3> c011 = sample(x0, y1, z1);
    const std::array<float, 3> c111 = sample(x1, y1, z1);

    const std::array<float, 3> c00 = LerpColor(c000, c100, fx);
    const std::array<float, 3> c10 = LerpColor(c010, c110, fx);
    const std::array<float, 3> c01 = LerpColor(c001, c101, fx);
    const std::array<float, 3> c11 = LerpColor(c011, c111, fx);
    const std::array<float, 3> c0 = LerpColor(c00, c10, fy);
    const std::array<float, 3> c1 = LerpColor(c01, c11, fy);
    std::array<float, 3> result = LerpColor(c0, c1, fz);
    ClampColor(result);
    return result;
}

LutCreatorResult CreateLutFromImages(
    const LutCreatorImage& sourceImage,
    const LutCreatorImage& targetImage,
    const LutCreatorSettings& settings) {
    LutCreatorResult result;
    result.payload.importFormat = LutImportFormat::Cube;
    result.payload.useMode = settings.useMode;
    result.payload.inputTransform = settings.inputTransform;
    result.payload.outputTransform = settings.outputTransform;

    if (sourceImage.width <= 0 || sourceImage.height <= 0 || sourceImage.pixels.empty()) {
        result.message = "Choose a valid source image before generating a LUT.";
        result.payload.importError = result.message;
        return result;
    }
    if (targetImage.width <= 0 || targetImage.height <= 0 || targetImage.pixels.empty()) {
        result.message = "Choose a valid target image before generating a LUT.";
        result.payload.importError = result.message;
        return result;
    }
    if (sourceImage.width != targetImage.width || sourceImage.height != targetImage.height) {
        result.message = "Source and target images must have the exact same dimensions for LUT creation.";
        result.payload.importError = result.message;
        return result;
    }
    if (settings.lutSize <= 1) {
        result.message = "Choose a LUT size greater than 1.";
        result.payload.importError = result.message;
        return result;
    }

    const int effectiveStride = ComputeEffectiveStride(sourceImage, settings);
    const std::size_t voxelCount =
        static_cast<std::size_t>(settings.lutSize) *
        static_cast<std::size_t>(settings.lutSize) *
        static_cast<std::size_t>(settings.lutSize);
    std::vector<VoxelAccumulator> accumulators(voxelCount);
    int sampledPixelCount = 0;

    for (int y = 0; y < sourceImage.height; y += effectiveStride) {
        for (int x = 0; x < sourceImage.width; x += effectiveStride) {
            const std::array<float, 3> sourceRgb = ReadPixelRgb(sourceImage, x, y);
            const std::array<float, 3> targetRgb = ReadPixelRgb(targetImage, x, y);
            const float maxCoord = static_cast<float>(settings.lutSize - 1);
            const float gx = Clamp01(sourceRgb[0]) * maxCoord;
            const float gy = Clamp01(sourceRgb[1]) * maxCoord;
            const float gz = Clamp01(sourceRgb[2]) * maxCoord;

            const int x0 = static_cast<int>(std::floor(gx));
            const int y0 = static_cast<int>(std::floor(gy));
            const int z0 = static_cast<int>(std::floor(gz));
            const int x1 = std::min(x0 + 1, settings.lutSize - 1);
            const int y1 = std::min(y0 + 1, settings.lutSize - 1);
            const int z1 = std::min(z0 + 1, settings.lutSize - 1);
            const float fx = gx - static_cast<float>(x0);
            const float fy = gy - static_cast<float>(y0);
            const float fz = gz - static_cast<float>(z0);

            const float wx[2] = { 1.0f - fx, fx };
            const float wy[2] = { 1.0f - fy, fy };
            const float wz[2] = { 1.0f - fz, fz };
            const int xs[2] = { x0, x1 };
            const int ys[2] = { y0, y1 };
            const int zs[2] = { z0, z1 };

            for (int iz = 0; iz < 2; ++iz) {
                for (int iy = 0; iy < 2; ++iy) {
                    for (int ix = 0; ix < 2; ++ix) {
                        const float weight = wx[ix] * wy[iy] * wz[iz];
                        if (weight <= 0.0f) {
                            continue;
                        }
                        VoxelAccumulator& accumulator =
                            accumulators[Index3D(xs[ix], ys[iy], zs[iz], settings.lutSize)];
                        accumulator.sum[0] += targetRgb[0] * weight;
                        accumulator.sum[1] += targetRgb[1] * weight;
                        accumulator.sum[2] += targetRgb[2] * weight;
                        accumulator.weight += weight;
                    }
                }
            }

            ++sampledPixelCount;
        }
    }

    std::vector<float> values(voxelCount * 3u, 0.0f);
    std::vector<float> observedValues(voxelCount * 3u, 0.0f);
    std::vector<float> confidence(voxelCount, 0.0f);
    std::vector<unsigned char> observedMask(voxelCount, 0u);
    const float averageWeightPerVoxel =
        voxelCount > 0u ? static_cast<float>(sampledPixelCount) / static_cast<float>(voxelCount) : 0.0f;
    const float confidenceDenominator = std::max(
        1.0f,
        averageWeightPerVoxel * std::max(settings.observationThreshold, 0.05f));
    int coveredVoxelCount = 0;

    for (int z = 0; z < settings.lutSize; ++z) {
        for (int y = 0; y < settings.lutSize; ++y) {
            for (int x = 0; x < settings.lutSize; ++x) {
                const std::size_t index = Index3D(x, y, z, settings.lutSize);
                const std::array<float, 3> identity = IdentityColorForVoxel(x, y, z, settings.lutSize);
                const VoxelAccumulator& accumulator = accumulators[index];
                std::array<float, 3> observed = identity;
                float voxelConfidence = 0.0f;
                if (accumulator.weight > 1e-6f) {
                    observed = {
                        accumulator.sum[0] / accumulator.weight,
                        accumulator.sum[1] / accumulator.weight,
                        accumulator.sum[2] / accumulator.weight
                    };
                    ClampColor(observed);
                    voxelConfidence = Clamp01(accumulator.weight / confidenceDenominator);
                    observedMask[index] = 1u;
                    ++coveredVoxelCount;
                }

                const float initialBlend = observedMask[index] != 0u
                    ? std::max(0.35f, voxelConfidence)
                    : 0.0f;
                WriteColorTriplet(observedValues, index, observed);
                WriteColorTriplet(values, index, LerpColor(identity, observed, initialBlend));
                confidence[index] = voxelConfidence;
            }
        }
    }

    std::vector<float> nextValues(values.size(), 0.0f);
    for (int pass = 0; pass < std::max(0, settings.smoothPasses); ++pass) {
        for (int z = 0; z < settings.lutSize; ++z) {
            for (int y = 0; y < settings.lutSize; ++y) {
                for (int x = 0; x < settings.lutSize; ++x) {
                    const std::size_t index = Index3D(x, y, z, settings.lutSize);
                    const std::array<float, 3> identity = IdentityColorForVoxel(x, y, z, settings.lutSize);
                    const std::array<float, 3> current = ReadColorTriplet(values, index);
                    const std::array<float, 3> observed = ReadColorTriplet(observedValues, index);
                    const std::array<float, 3> neighbors =
                        AverageNeighborColors(values, x, y, z, settings.lutSize);
                    const float voxelConfidence = confidence[index];
                    const float unresolved = 1.0f - voxelConfidence;
                    const float neighborInfluence = Clamp01(
                        settings.smoothStrength * (0.20f + unresolved * 0.80f));

                    std::array<float, 3> blended = LerpColor(current, neighbors, neighborInfluence);
                    blended = LerpColor(blended, identity, Clamp01(settings.identityBias * unresolved));
                    if (observedMask[index] != 0u) {
                        blended = LerpColor(blended, observed, voxelConfidence);
                    }
                    ClampColor(blended);
                    WriteColorTriplet(nextValues, index, blended);
                }
            }
        }
        values.swap(nextValues);
    }

    LutPayload payload;
    payload.label = settings.label.empty()
        ? BuildDefaultGeneratedTitle(sourceImage, targetImage)
        : settings.label;
    payload.importedTitle = settings.importedTitle.empty()
        ? BuildDefaultGeneratedTitle(sourceImage, targetImage)
        : settings.importedTitle;
    payload.importError.clear();
    payload.importFormat = LutImportFormat::Cube;
    payload.useMode = settings.useMode;
    payload.inputTransform = settings.inputTransform;
    payload.outputTransform = settings.outputTransform;
    ClearCanonicalLutData(payload);
    payload.lut3D.size = settings.lutSize;
    payload.lut3D.domainMin = { 0.0f, 0.0f, 0.0f };
    payload.lut3D.domainMax = { 1.0f, 1.0f, 1.0f };
    payload.lut3D.values = std::move(values);

    result.stats.totalPixelCount = sourceImage.width * sourceImage.height;
    result.stats.sampledPixelCount = sampledPixelCount;
    result.stats.effectiveStride = effectiveStride;
    result.stats.voxelCoverage = voxelCount > 0u
        ? static_cast<float>(coveredVoxelCount) / static_cast<float>(voxelCount)
        : 0.0f;

    double maeSum = 0.0;
    double mseSum = 0.0;
    float maxError = 0.0f;
    int evaluatedSamples = 0;
    for (int y = 0; y < sourceImage.height; y += effectiveStride) {
        for (int x = 0; x < sourceImage.width; x += effectiveStride) {
            const std::array<float, 3> sourceRgb = ReadPixelRgb(sourceImage, x, y);
            const std::array<float, 3> targetRgb = ReadPixelRgb(targetImage, x, y);
            const std::array<float, 3> predicted = SampleLut3D(payload.lut3D, sourceRgb);
            const float er = std::abs(predicted[0] - targetRgb[0]);
            const float eg = std::abs(predicted[1] - targetRgb[1]);
            const float eb = std::abs(predicted[2] - targetRgb[2]);
            const float maxChannelError = std::max(er, std::max(eg, eb));
            maeSum += (static_cast<double>(er) + static_cast<double>(eg) + static_cast<double>(eb)) / 3.0;
            mseSum += (static_cast<double>(er * er) + static_cast<double>(eg * eg) + static_cast<double>(eb * eb)) / 3.0;
            maxError = std::max(maxError, maxChannelError);
            ++evaluatedSamples;
        }
    }

    if (evaluatedSamples > 0) {
        result.stats.meanAbsoluteError =
            static_cast<float>(maeSum / static_cast<double>(evaluatedSamples));
        result.stats.rootMeanSquareError =
            static_cast<float>(std::sqrt(mseSum / static_cast<double>(evaluatedSamples)));
        result.stats.maxAbsoluteError = maxError;
    }

    payload.importError.clear();
    result.success = true;
    result.message = "LUT generated successfully.";
    result.payload = std::move(payload);
    return result;
}

std::string LutCreatorSidecarPath(const std::string& cubePath) {
    try {
        std::filesystem::path path(cubePath);
        path.replace_extension(".stacklut.json");
        return path.string();
    } catch (...) {
        return cubePath + ".stacklut.json";
    }
}

bool ApplyLutCreatorSidecarMetadata(const std::string& cubePath, LutPayload& payload) {
    const std::string sidecarPath = LutCreatorSidecarPath(cubePath);
    std::ifstream input(sidecarPath);
    if (!input.is_open()) {
        return false;
    }

    json document;
    try {
        input >> document;
    } catch (...) {
        return false;
    }

    if (!document.is_object()) {
        return false;
    }

    payload.label = document.value("label", payload.label);
    payload.importedTitle = document.value(
        "title",
        document.value("importedTitle", payload.importedTitle));
    payload.useMode = LutUseModeFromString(
        document.value("useMode", std::string(LutUseModeToString(payload.useMode))));
    payload.inputTransform = LutTransferFunctionFromString(
        document.value("inputTransform", std::string(LutTransferFunctionToString(payload.inputTransform))));
    payload.outputTransform = LutTransferFunctionFromString(
        document.value("outputTransform", std::string(LutTransferFunctionToString(payload.outputTransform))));
    return true;
}

bool SaveCubeLutWithSidecar(
    const std::string& cubePath,
    const LutPayload& payload,
    const LutCreatorSettings& settings,
    const LutCreatorStats& stats,
    const std::string& sourceImagePath,
    const std::string& targetImagePath,
    std::string* outMessage) {
    std::string message;
    if (!WriteCubeFile(cubePath, payload, &message)) {
        if (outMessage) {
            *outMessage = message;
        }
        return false;
    }

    const json sidecar = BuildSidecarDocument(
        payload,
        settings,
        stats,
        sourceImagePath,
        targetImagePath);
    const std::string sidecarPath = LutCreatorSidecarPath(cubePath);
    std::ofstream sidecarOutput(sidecarPath, std::ios::binary | std::ios::trunc);
    if (!sidecarOutput.is_open()) {
        if (outMessage) {
            *outMessage = "The .cube LUT was saved, but the Stack sidecar file could not be opened for writing.";
        }
        return false;
    }

    sidecarOutput << sidecar.dump(2);
    if (!sidecarOutput.good()) {
        if (outMessage) {
            *outMessage = "The .cube LUT was saved, but writing the Stack sidecar file failed.";
        }
        return false;
    }

    if (outMessage) {
        *outMessage = "Saved .cube LUT and Stack sidecar metadata.";
    }
    return true;
}

} // namespace ColorLut
