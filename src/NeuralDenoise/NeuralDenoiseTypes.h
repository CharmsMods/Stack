#pragma once

#include "ThirdParty/json.hpp"

#include <string>
#include <vector>

namespace NeuralDenoise {

enum class ModelType {
    Unknown,
    LinearRgb,
    GenericRgb,
    RawBayerPacked4Ch
};

enum class RuntimePreference {
    Auto,
    Cuda,
    Cpu,
    DirectML,
    TensorRT
};

enum class QualityMode {
    Quality,
    Balanced,
    Fast
};

enum class PreviewMode {
    Denoised,
    Original,
    Difference,
    Split,
    ChromaDifference,
    LumaDifference
};

enum class AlphaMode {
    Preserve,
    Ignore,
    UseAsMask
};

enum class CfaOverride {
    FromMetadata,
    RGGB,
    BGGR,
    GRBG,
    GBRG
};

enum class NoiseEstimateMode {
    MetadataAuto,
    Manual
};

enum class RawWhiteBalanceStage {
    BeforeWhiteBalance,
    AfterWhiteBalance
};

enum class RawOutputMode {
    DenoisedCfa,
    ContinueToDemosaic
};

struct TilePlan {
    int tileSize = 512;
    int overlap = 64;
    bool featherMerge = true;
};

struct NeuralDenoiseModelInfo {
    std::string id;
    std::string displayName;
    std::string relativeFile;
    std::string resolvedPath;
    ModelType type = ModelType::Unknown;
    std::string architecture;
    std::string preferredBackend;
    std::string inputFormat = "nchw";
    std::string inputRange = "0_1";
    std::string inputName;
    std::string outputName;
    std::vector<std::string> precision;
    int inputChannels = 0;
    int outputChannels = 0;
    bool supportsTiling = false;
    int requiredInputMultiple = 1;
    std::string license;
    std::string licenseFile;
    TilePlan tileHints;
};

struct ModelAvailability {
    bool manifestLoaded = false;
    bool runtimeAvailable = false;
    bool modelFileAvailable = false;
    bool licenseNoticeAvailable = true;
    bool supported = false;
    std::string status;
    std::vector<std::string> warnings;
};

struct NeuralDenoiseSettings {
    bool enabled = false;
    std::string selectedModelId;
    RuntimePreference runtimePreference = RuntimePreference::Auto;
    QualityMode qualityMode = QualityMode::Quality;
    PreviewMode previewMode = PreviewMode::Denoised;
    AlphaMode alphaMode = AlphaMode::Preserve;

    float strength = 0.75f;
    float detailPreservation = 0.75f;
    float shadowsStrength = 0.5f;
    float highlightProtection = 0.6f;
    float differenceAmount = 1.0f;
    float chromaStrength = 0.65f;
    float lumaStrength = 0.45f;
    float fineGrainStrength = 0.25f;
    float blotchStrength = 0.45f;
    bool hotDeadPixelCleanup = false;
    bool shadowBiasedDenoise = false;
    bool workInLinearRgb = true;
    bool preserveAlpha = true;
    bool allowCpuFallback = false;
    bool externalMaskInfluence = false;
    int runRequestRevision = 0;
    bool runRequestAllowLargeCpu = false;
    int renderNodeId = -1;

    TilePlan tilePlan;

    CfaOverride cfaOverride = CfaOverride::FromMetadata;
    bool overrideBlackLevel = false;
    float blackLevel = 0.0f;
    bool overrideWhiteLevel = false;
    float whiteLevel = 65535.0f;
    NoiseEstimateMode noiseEstimateMode = NoiseEstimateMode::MetadataAuto;
    float manualNoiseEstimate = 0.05f;
    RawWhiteBalanceStage rawWhiteBalanceStage = RawWhiteBalanceStage::BeforeWhiteBalance;
    RawOutputMode rawOutputMode = RawOutputMode::ContinueToDemosaic;
};

struct NeuralDenoiseImage {
    int width = 0;
    int height = 0;
    int channels = 4;
    std::vector<float> rgba;

    bool IsValid() const {
        return width > 0 && height > 0 && channels == 4 &&
            rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

struct NeuralDenoiseInferenceRequest {
    NeuralDenoiseModelInfo model;
    NeuralDenoiseSettings settings;
    NeuralDenoiseImage input;
};

struct NeuralDenoiseInferenceResult {
    bool success = false;
    bool usedCuda = false;
    bool usedCpu = false;
    std::string status;
    NeuralDenoiseImage output;
};

const char* ModelTypeToToken(ModelType value);
ModelType ModelTypeFromToken(const std::string& value);
const char* ModelTypeLabel(ModelType value);

const char* RuntimePreferenceToToken(RuntimePreference value);
RuntimePreference RuntimePreferenceFromToken(const std::string& value);
const char* RuntimePreferenceLabel(RuntimePreference value);

const char* QualityModeToToken(QualityMode value);
QualityMode QualityModeFromToken(const std::string& value);
const char* QualityModeLabel(QualityMode value);

const char* PreviewModeToToken(PreviewMode value);
PreviewMode PreviewModeFromToken(const std::string& value);
const char* PreviewModeLabel(PreviewMode value);

const char* AlphaModeToToken(AlphaMode value);
AlphaMode AlphaModeFromToken(const std::string& value);
const char* AlphaModeLabel(AlphaMode value);

const char* CfaOverrideToToken(CfaOverride value);
CfaOverride CfaOverrideFromToken(const std::string& value);
const char* CfaOverrideLabel(CfaOverride value);

const char* NoiseEstimateModeToToken(NoiseEstimateMode value);
NoiseEstimateMode NoiseEstimateModeFromToken(const std::string& value);
const char* NoiseEstimateModeLabel(NoiseEstimateMode value);

const char* RawWhiteBalanceStageToToken(RawWhiteBalanceStage value);
RawWhiteBalanceStage RawWhiteBalanceStageFromToken(const std::string& value);
const char* RawWhiteBalanceStageLabel(RawWhiteBalanceStage value);

const char* RawOutputModeToToken(RawOutputMode value);
RawOutputMode RawOutputModeFromToken(const std::string& value);
const char* RawOutputModeLabel(RawOutputMode value);

nlohmann::json SerializeSettings(const NeuralDenoiseSettings& settings);
NeuralDenoiseSettings DeserializeSettings(const nlohmann::json& value);

} // namespace NeuralDenoise
