#include "App/Validation/ValidationSuites.h"

#include "Raw/Denoise/RawRgbNoiseModel.h"
#include "Raw/RawGpuPipeline.h"
#include "Raw/RawProcessingMath.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace Stack::Validation {
namespace {

bool ValidateFusedRawGainMapRange() {
    constexpr int width = 8;
    constexpr int height = 8;
    Raw::RawImageData raw;
    raw.metadata.rawWidth = raw.metadata.visibleWidth = width;
    raw.metadata.rawHeight = raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.orientation = 1;
    auto mosaic = std::make_shared<std::vector<float>>(width * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            (*mosaic)[y * width + x] = x < width / 2 ? -0.1f : 1.4f;
        }
    }
    raw.normalizedMosaicBuffer = mosaic;
    Raw::DngGainMapOpcode gainMap;
    gainMap.bottom = height;
    gainMap.right = width;
    gainMap.mapPointsV = gainMap.mapPointsH = 1;
    gainMap.mapSpacingV = gainMap.mapSpacingH = 1.0;
    gainMap.gains = { 2.0f };
    raw.metadata.dngGainMaps.push_back(gainMap);

    Raw::RawDevelopSettings settings;
    settings.debugView = Raw::RawDebugView::PreDenoiseMosaic;
    Raw::RawGpuPipeline pipeline;
    for (const auto contract : {
            Raw::NormalizedMosaicInputContract::HdrVirtualAnchorPreGain,
            Raw::NormalizedMosaicInputContract::BracketingPreGain }) {
        raw.normalizedMosaicInputContract = contract;
        const unsigned int texture = pipeline.Render(raw, settings);
        if (texture == 0) {
            std::cerr << "Fused RAW GainMap range validation failed: "
                      << pipeline.GetLastError() << std::endl;
            return false;
        }
        std::vector<float> rgba(width * height * 4);
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int pixel = 0; pixel < width * height; ++pixel) {
            const float expected = pixel % width < width / 2 ? -0.2f : 2.8f;
            for (int channel = 0; channel < 3; ++channel) {
                const float actual = rgba[pixel * 4 + channel];
                if (!std::isfinite(actual) || std::abs(actual - expected) > 0.002f) {
                    std::cerr << "Fused RAW GainMap range validation failed for "
                              << Raw::NormalizedMosaicInputContractName(contract)
                              << ": expected " << expected << ", got " << actual
                              << std::endl;
                    return false;
                }
            }
        }
    }
    return true;
}

bool ValidateNearestNeighborDemosaic() {
    constexpr int width = 8;
    constexpr int height = 8;
    Raw::RawImageData raw;
    raw.metadata.rawWidth = raw.metadata.visibleWidth = width;
    raw.metadata.rawHeight = raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.orientation = 1;
    raw.normalizedMosaicInputContract =
        Raw::NormalizedMosaicInputContract::HdrVirtualAnchorPreGain;
    auto mosaic = std::make_shared<std::vector<float>>(width * height);
    for (int pixel = 0; pixel < width * height; ++pixel) {
        (*mosaic)[pixel] = static_cast<float>(pixel % 19 - 9) * 0.1f;
    }
    raw.normalizedMosaicBuffer = mosaic;
    Raw::RawDevelopSettings settings;
    settings.debugView = Raw::RawDebugView::DemosaicedCameraRgb;
    settings.demosaicMethod = Raw::DemosaicMethod::NearestNeighbor;
    settings.whiteBalanceMode = Raw::WhiteBalanceMode::Neutral;
    settings.highlightMode = Raw::HighlightReconstructionMode::Off;
    settings.mosaicDenoise.enabled = false;
    settings.lateralRedCyan = settings.lateralBlueYellow = 0.0f;
    Raw::RawGpuPipeline pipeline;
    for (const auto pattern : { Raw::CfaPattern::RGGB, Raw::CfaPattern::BGGR,
            Raw::CfaPattern::GBRG, Raw::CfaPattern::GRBG }) {
        raw.metadata.cfaPattern = pattern;
        const unsigned int texture = pipeline.Render(raw, settings);
        if (texture == 0) {
            std::cerr << "Nearest-neighbor GPU validation failed: "
                      << pipeline.GetLastError() << std::endl;
            return false;
        }
        std::vector<float> rgba(width * height * 4);
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto expected = Raw::Processing::DemosaicNearestNeighborAt(
                    *mosaic, width, height, pattern, x, y);
                for (int channel = 0; channel < 3; ++channel) {
                    const float actual = rgba[(y * width + x) * 4 + channel];
                    if (!std::isfinite(actual) ||
                        std::abs(actual - expected[channel]) > 0.001f) {
                        std::cerr << "Nearest-neighbor GPU validation failed for CFA "
                                  << static_cast<int>(pattern) << " at " << x << ',' << y
                                  << " channel " << channel << ": expected "
                                  << expected[channel] << ", got " << actual << std::endl;
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

} // namespace

bool ValidateRawRgbDenoiseGpuShaders() {
    Raw::RawImageData raw;
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = {
        { 0.010, 0.000025 },
        { 0.008, 0.000016 },
        { 0.012, 0.000036 }
    };
    Raw::RawDevelopSettings settings;
    settings.whiteBalanceMode = Raw::WhiteBalanceMode::Neutral;
    settings.cameraTransformEnabled = false;
    settings.applyBaselineExposure = false;
    const Raw::Denoise::RawRgbNoiseModel noiseModel =
        Raw::Denoise::BuildRawRgbNoiseModel(raw, settings);
    if (!noiseModel.profiled ||
        noiseModel.shotScale[0] <= 0.0f ||
        noiseModel.shotScale[1] <= 0.0f ||
        noiseModel.shotScale[2] <= 0.0f ||
        noiseModel.readNoiseVariance[0] <= 0.0f ||
        noiseModel.readNoiseVariance[1] <= 0.0f ||
        noiseModel.readNoiseVariance[2] <= 0.0f) {
        std::cerr
            << "RAW RGB denoise GPU validation failed: DNG noise propagation is invalid."
            << std::endl;
        return false;
    }
    raw.normalizedMosaicInputContract =
        Raw::NormalizedMosaicInputContract::HdrVirtualAnchorPreGain;
    if (Raw::Denoise::BuildRawRgbNoiseModel(raw, settings).profiled) {
        std::cerr
            << "RAW RGB denoise GPU validation failed: a single-frame profile was reused for a fused virtual mosaic."
            << std::endl;
        return false;
    }

    if (!glfwInit()) {
        std::cerr
            << "RAW RGB denoise GPU validation failed: GLFW could not initialize."
            << std::endl;
        return false;
    }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(
        16,
        16,
        "Stack RAW RGB Denoise GPU Validation",
        nullptr,
        nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr
            << "RAW RGB denoise GPU validation failed: OpenGL 3.3 is unavailable."
            << std::endl;
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr
            << "RAW RGB denoise GPU validation failed: OpenGL functions could not be loaded."
            << std::endl;
        return false;
    }

    bool valid = false;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        valid = pipeline.ValidateRawRgbDenoiseProgramsForTesting();
        valid = ValidateFusedRawGainMapRange() && valid;
        valid = ValidateNearestNeighborDemosaic() && valid;
    }
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!valid) {
        std::cerr
            << "RAW RGB denoise GPU validation failed: shader setup, neutral-stage skip, or active-map execution failed."
            << std::endl;
        return false;
    }
    std::cout
        << "RAW RGB denoise GPU shader, neutral-stage skip, active-map, fused GainMap range, and nearest-neighbor validation passed."
        << std::endl;
    return true;
}

} // namespace Stack::Validation
