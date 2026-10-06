#pragma once

#include "RawGpuPreprocessTelemetry.h"
#include "RawImageData.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace Raw {

class RawGpuPreprocessor;

// Shared color-domain helpers used by processing stages that follow the RAW
// GPU pipeline. Keeping these here prevents downstream noise models from
// drifting away from the exact white-balance and camera transform used to
// produce the scene-linear RGB texture.
std::array<float, 3> ResolveRawWhiteBalance(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings);
std::array<float, 9> BuildRawCameraToWorkingTransform(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    bool inputAlreadyWhiteBalanced);

class RawGpuPipeline {
public:
    RawGpuPipeline();
    ~RawGpuPipeline();

    unsigned int Render(
        const RawImageData& raw,
        const RawDevelopSettings& settings,
        int previewMaxDimension = 0,
        const std::function<bool()>& shouldCancel = {});
    void Clear();
    void InvalidateProcessingOutputs();
    const std::string& GetLastError() const { return m_LastError; }
    int GetOutputWidth() const { return m_OutputWidth; }
    int GetOutputHeight() const { return m_OutputHeight; }
    const RawGpuPreprocessTelemetry& GetLastPreprocessTelemetry() const {
        return m_LastPreprocessTelemetry;
    }

private:
    static constexpr std::size_t kToneCurveUniformCount = 12;

    struct ToneCurveUniformLocations {
        int pointCount = -1;
        std::array<int, kToneCurveUniformCount> points {};
    };

    struct RawProgramUniformLocations {
        int raw = -1;
        int correctedRaw = -1;
        int useCorrectedRaw = -1;
        int rawNoiseVariance = -1;
        int useRawNoiseVariance = -1;
        int hdrVirtualAnchor = -1;
        int rawSize = -1;
        int visibleSize = -1;
        int cropOrigin = -1;
        int clampToActiveArea = -1;
        int orientation = -1;
        int rotateToFitFrame = -1;
        int flipHorizontally = -1;
        int flipVertically = -1;
        int cfaPattern = -1;
        int blackLevel = -1;
        int channelBlack = -1;
        int whiteLevel = -1;
        int whiteBalance = -1;
        int whiteBalanceBeforeDemosaic = -1;
        int cameraToWorking = -1;
        int useCameraTransform = -1;
        int debugView = -1;
        int exposure = -1;
        ToneCurveUniformLocations toneCurve;
        int highlightMode = -1;
        int highlightStrength = -1;
        int highlightThreshold = -1;
        int demosaicMethod = -1;
        int falseColorSuppression = -1;
        int defringeStrength = -1;
        int highlightEdgeCleanup = -1;
        int chromaRadius = -1;
        int preserveRealColor = -1;
        int lateralRedCyan = -1;
        int lateralBlueYellow = -1;
        int mosaicDenoiseEnabled = -1;
        int mosaicHotPixelSuppression = -1;
        int mosaicHotPixelThreshold = -1;
        int mosaicLumaStrength = -1;
        int mosaicChromaStrength = -1;
        int mosaicRadius = -1;
        int mosaicEdgeProtection = -1;
        int mosaicIterations = -1;
    };

    struct LinearProgramUniformLocations {
        int linearRgb = -1;
        int topDownInput = -1;
        int visibleSize = -1;
        int orientation = -1;
        int rotateToFitFrame = -1;
        int flipHorizontally = -1;
        int flipVertically = -1;
        int cameraToWorking = -1;
        int useCameraTransform = -1;
        int debugView = -1;
        int exposure = -1;
        int whiteBalance = -1;
        int applyWhiteBalance = -1;
        ToneCurveUniformLocations toneCurve;
    };

    unsigned int m_Program = 0;
    unsigned int m_LinearProgram = 0;
    std::unique_ptr<RawGpuPreprocessor> m_Preprocessor;
    RawGpuPreprocessTelemetry m_LastPreprocessTelemetry;
    RawProgramUniformLocations m_RawUniforms;
    LinearProgramUniformLocations m_LinearUniforms;
    unsigned int m_RawTexture = 0;
    unsigned int m_CorrectedRawTexture = 0;
    unsigned int m_RawNoiseVarianceTexture = 0;
    unsigned int m_LinearTexture = 0;
    unsigned int m_OutputTexture = 0;
    unsigned int m_OutputFbo = 0;
    unsigned int m_QuadVao = 0;
    unsigned int m_QuadVbo = 0;
    int m_RawWidth = 0;
    int m_RawHeight = 0;
    int m_OutputWidth = 0;
    int m_OutputHeight = 0;
    std::size_t m_RawFingerprint = 0;
    std::size_t m_CorrectedRawFingerprint = 0;
    std::size_t m_RawNoiseVarianceFingerprint = 0;
    std::size_t m_LinearFingerprint = 0;
    std::string m_LastError;

    bool EnsureProgram();
    bool EnsureLinearProgram();
    bool UploadRawTexture(const RawImageData& raw);
    bool UploadCorrectedRawTexture(
        const RawImageData& raw,
        const RawDevelopSettings& settings,
        bool& outHasCorrectedRaw,
        bool& outHasNoiseVariance);
    bool UploadLinearTexture(const RawImageData& raw, const RawDevelopSettings& settings);
    bool EnsureOutput(int width, int height);
    bool EnsureFullscreenQuad();
};

} // namespace Raw
