#pragma once

#include "ThirdParty/json.hpp"

#include <string>

namespace NeuralDenoise {

// Compatibility-only schema for projects saved with Stack's retired neural
// denoise experiments. This file deliberately contains no runtime/model-pack
// contract. New denoise work must define a new versioned interface.

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
