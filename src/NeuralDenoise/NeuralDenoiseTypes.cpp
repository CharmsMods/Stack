#include "NeuralDenoiseTypes.h"

#include <algorithm>

namespace NeuralDenoise {
namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

TilePlan DeserializeTilePlan(const nlohmann::json& value) {
    TilePlan plan;
    if (!value.is_object()) {
        return plan;
    }
    plan.tileSize = std::clamp(value.value("tileSize", plan.tileSize), 64, 4096);
    plan.overlap = std::clamp(value.value("overlap", plan.overlap), 0, plan.tileSize / 2);
    plan.featherMerge = value.value("featherMerge", plan.featherMerge);
    return plan;
}

nlohmann::json SerializeTilePlan(const TilePlan& plan) {
    return {
        { "tileSize", plan.tileSize },
        { "overlap", plan.overlap },
        { "featherMerge", plan.featherMerge }
    };
}

} // namespace

const char* ModelTypeToToken(ModelType value) {
    switch (value) {
        case ModelType::LinearRgb: return "linear_rgb";
        case ModelType::GenericRgb: return "generic_rgb";
        case ModelType::RawBayerPacked4Ch: return "raw_bayer_packed_4ch";
        case ModelType::Unknown:
        default: return "unknown";
    }
}

ModelType ModelTypeFromToken(const std::string& value) {
    if (value == "linear_rgb") return ModelType::LinearRgb;
    if (value == "generic_rgb") return ModelType::GenericRgb;
    if (value == "raw_bayer_packed_4ch") return ModelType::RawBayerPacked4Ch;
    return ModelType::Unknown;
}

const char* ModelTypeLabel(ModelType value) {
    switch (value) {
        case ModelType::LinearRgb: return "Linear RGB";
        case ModelType::GenericRgb: return "Generic RGB";
        case ModelType::RawBayerPacked4Ch: return "RAW Bayer / Packed 4ch";
        case ModelType::Unknown:
        default: return "Unknown";
    }
}

const char* RuntimePreferenceToToken(RuntimePreference value) {
    switch (value) {
        case RuntimePreference::Cuda: return "cuda";
        case RuntimePreference::Cpu: return "cpu";
        case RuntimePreference::DirectML: return "directml";
        case RuntimePreference::TensorRT: return "tensorrt";
        case RuntimePreference::Auto:
        default: return "auto";
    }
}

RuntimePreference RuntimePreferenceFromToken(const std::string& value) {
    if (value == "cuda") return RuntimePreference::Cuda;
    if (value == "cpu") return RuntimePreference::Cpu;
    if (value == "directml") return RuntimePreference::DirectML;
    if (value == "tensorrt") return RuntimePreference::TensorRT;
    return RuntimePreference::Auto;
}

const char* RuntimePreferenceLabel(RuntimePreference value) {
    switch (value) {
        case RuntimePreference::Cuda: return "CUDA";
        case RuntimePreference::Cpu: return "CPU placeholder";
        case RuntimePreference::DirectML: return "DirectML future";
        case RuntimePreference::TensorRT: return "TensorRT future";
        case RuntimePreference::Auto:
        default: return "Auto";
    }
}

const char* QualityModeToToken(QualityMode value) {
    switch (value) {
        case QualityMode::Balanced: return "balanced";
        case QualityMode::Fast: return "fast";
        case QualityMode::Quality:
        default: return "quality";
    }
}

QualityMode QualityModeFromToken(const std::string& value) {
    if (value == "balanced") return QualityMode::Balanced;
    if (value == "fast") return QualityMode::Fast;
    return QualityMode::Quality;
}

const char* QualityModeLabel(QualityMode value) {
    switch (value) {
        case QualityMode::Balanced: return "Balanced";
        case QualityMode::Fast: return "Fast";
        case QualityMode::Quality:
        default: return "Quality";
    }
}

const char* PreviewModeToToken(PreviewMode value) {
    switch (value) {
        case PreviewMode::Original: return "original";
        case PreviewMode::Difference: return "difference";
        case PreviewMode::Split: return "split";
        case PreviewMode::ChromaDifference: return "chroma_difference";
        case PreviewMode::LumaDifference: return "luma_difference";
        case PreviewMode::Denoised:
        default: return "denoised";
    }
}

PreviewMode PreviewModeFromToken(const std::string& value) {
    if (value == "original") return PreviewMode::Original;
    if (value == "difference") return PreviewMode::Difference;
    if (value == "split") return PreviewMode::Split;
    if (value == "chroma_difference") return PreviewMode::ChromaDifference;
    if (value == "luma_difference") return PreviewMode::LumaDifference;
    return PreviewMode::Denoised;
}

const char* PreviewModeLabel(PreviewMode value) {
    switch (value) {
        case PreviewMode::Original: return "Original";
        case PreviewMode::Difference: return "Difference";
        case PreviewMode::Split: return "Split view";
        case PreviewMode::ChromaDifference: return "Chroma-only difference";
        case PreviewMode::LumaDifference: return "Luma-only difference";
        case PreviewMode::Denoised:
        default: return "Denoised";
    }
}

const char* AlphaModeToToken(AlphaMode value) {
    switch (value) {
        case AlphaMode::Ignore: return "ignore";
        case AlphaMode::UseAsMask: return "use_as_mask";
        case AlphaMode::Preserve:
        default: return "preserve";
    }
}

AlphaMode AlphaModeFromToken(const std::string& value) {
    if (value == "ignore") return AlphaMode::Ignore;
    if (value == "use_as_mask") return AlphaMode::UseAsMask;
    return AlphaMode::Preserve;
}

const char* AlphaModeLabel(AlphaMode value) {
    switch (value) {
        case AlphaMode::Ignore: return "Ignore";
        case AlphaMode::UseAsMask: return "Use as mask";
        case AlphaMode::Preserve:
        default: return "Preserve";
    }
}

const char* CfaOverrideToToken(CfaOverride value) {
    switch (value) {
        case CfaOverride::RGGB: return "RGGB";
        case CfaOverride::BGGR: return "BGGR";
        case CfaOverride::GRBG: return "GRBG";
        case CfaOverride::GBRG: return "GBRG";
        case CfaOverride::FromMetadata:
        default: return "from_metadata";
    }
}

CfaOverride CfaOverrideFromToken(const std::string& value) {
    if (value == "RGGB") return CfaOverride::RGGB;
    if (value == "BGGR") return CfaOverride::BGGR;
    if (value == "GRBG") return CfaOverride::GRBG;
    if (value == "GBRG") return CfaOverride::GBRG;
    return CfaOverride::FromMetadata;
}

const char* CfaOverrideLabel(CfaOverride value) {
    switch (value) {
        case CfaOverride::RGGB: return "RGGB";
        case CfaOverride::BGGR: return "BGGR";
        case CfaOverride::GRBG: return "GRBG";
        case CfaOverride::GBRG: return "GBRG";
        case CfaOverride::FromMetadata:
        default: return "From metadata";
    }
}

const char* NoiseEstimateModeToToken(NoiseEstimateMode value) {
    return value == NoiseEstimateMode::Manual ? "manual" : "metadata_auto";
}

NoiseEstimateMode NoiseEstimateModeFromToken(const std::string& value) {
    return value == "manual" ? NoiseEstimateMode::Manual : NoiseEstimateMode::MetadataAuto;
}

const char* NoiseEstimateModeLabel(NoiseEstimateMode value) {
    return value == NoiseEstimateMode::Manual ? "Manual" : "Auto from metadata";
}

const char* RawWhiteBalanceStageToToken(RawWhiteBalanceStage value) {
    return value == RawWhiteBalanceStage::AfterWhiteBalance ? "after_white_balance" : "before_white_balance";
}

RawWhiteBalanceStage RawWhiteBalanceStageFromToken(const std::string& value) {
    return value == "after_white_balance" ? RawWhiteBalanceStage::AfterWhiteBalance : RawWhiteBalanceStage::BeforeWhiteBalance;
}

const char* RawWhiteBalanceStageLabel(RawWhiteBalanceStage value) {
    return value == RawWhiteBalanceStage::AfterWhiteBalance ? "After white balance" : "Before white balance";
}

const char* RawOutputModeToToken(RawOutputMode value) {
    return value == RawOutputMode::DenoisedCfa ? "denoised_cfa" : "continue_to_demosaic";
}

RawOutputMode RawOutputModeFromToken(const std::string& value) {
    return value == "denoised_cfa" ? RawOutputMode::DenoisedCfa : RawOutputMode::ContinueToDemosaic;
}

const char* RawOutputModeLabel(RawOutputMode value) {
    return value == RawOutputMode::DenoisedCfa ? "Denoised CFA" : "Continue to demosaic";
}

nlohmann::json SerializeSettings(const NeuralDenoiseSettings& settings) {
    return {
        { "enabled", settings.enabled },
        { "selectedModelId", settings.selectedModelId },
        { "runtimePreference", RuntimePreferenceToToken(settings.runtimePreference) },
        { "qualityMode", QualityModeToToken(settings.qualityMode) },
        { "previewMode", PreviewModeToToken(settings.previewMode) },
        { "alphaMode", AlphaModeToToken(settings.alphaMode) },
        { "strength", settings.strength },
        { "detailPreservation", settings.detailPreservation },
        { "shadowsStrength", settings.shadowsStrength },
        { "highlightProtection", settings.highlightProtection },
        { "differenceAmount", settings.differenceAmount },
        { "chromaStrength", settings.chromaStrength },
        { "lumaStrength", settings.lumaStrength },
        { "fineGrainStrength", settings.fineGrainStrength },
        { "blotchStrength", settings.blotchStrength },
        { "hotDeadPixelCleanup", settings.hotDeadPixelCleanup },
        { "shadowBiasedDenoise", settings.shadowBiasedDenoise },
        { "workInLinearRgb", settings.workInLinearRgb },
        { "preserveAlpha", settings.preserveAlpha },
        { "allowCpuFallback", settings.allowCpuFallback },
        { "externalMaskInfluence", settings.externalMaskInfluence },
        { "runRequestRevision", settings.runRequestRevision },
        { "runRequestAllowLargeCpu", settings.runRequestAllowLargeCpu },
        { "renderNodeId", settings.renderNodeId },
        { "tilePlan", SerializeTilePlan(settings.tilePlan) },
        { "cfaOverride", CfaOverrideToToken(settings.cfaOverride) },
        { "overrideBlackLevel", settings.overrideBlackLevel },
        { "blackLevel", settings.blackLevel },
        { "overrideWhiteLevel", settings.overrideWhiteLevel },
        { "whiteLevel", settings.whiteLevel },
        { "noiseEstimateMode", NoiseEstimateModeToToken(settings.noiseEstimateMode) },
        { "manualNoiseEstimate", settings.manualNoiseEstimate },
        { "rawWhiteBalanceStage", RawWhiteBalanceStageToToken(settings.rawWhiteBalanceStage) },
        { "rawOutputMode", RawOutputModeToToken(settings.rawOutputMode) }
    };
}

NeuralDenoiseSettings DeserializeSettings(const nlohmann::json& value) {
    NeuralDenoiseSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.enabled = value.value("enabled", settings.enabled);
    settings.selectedModelId = value.value("selectedModelId", settings.selectedModelId);
    settings.runtimePreference = RuntimePreferenceFromToken(value.value("runtimePreference", std::string("auto")));
    settings.qualityMode = QualityModeFromToken(value.value("qualityMode", std::string("quality")));
    settings.previewMode = PreviewModeFromToken(value.value("previewMode", std::string("denoised")));
    settings.alphaMode = AlphaModeFromToken(value.value("alphaMode", std::string("preserve")));
    settings.strength = Clamp01(value.value("strength", settings.strength));
    settings.detailPreservation = Clamp01(value.value("detailPreservation", settings.detailPreservation));
    settings.shadowsStrength = Clamp01(value.value("shadowsStrength", settings.shadowsStrength));
    settings.highlightProtection = Clamp01(value.value("highlightProtection", settings.highlightProtection));
    settings.differenceAmount = std::clamp(value.value("differenceAmount", settings.differenceAmount), 0.0f, 2.0f);
    settings.chromaStrength = Clamp01(value.value("chromaStrength", settings.chromaStrength));
    settings.lumaStrength = Clamp01(value.value("lumaStrength", settings.lumaStrength));
    settings.fineGrainStrength = Clamp01(value.value("fineGrainStrength", settings.fineGrainStrength));
    settings.blotchStrength = Clamp01(value.value("blotchStrength", settings.blotchStrength));
    settings.hotDeadPixelCleanup = value.value("hotDeadPixelCleanup", settings.hotDeadPixelCleanup);
    settings.shadowBiasedDenoise = value.value("shadowBiasedDenoise", settings.shadowBiasedDenoise);
    settings.workInLinearRgb = value.value("workInLinearRgb", settings.workInLinearRgb);
    settings.preserveAlpha = value.value("preserveAlpha", settings.preserveAlpha);
    settings.allowCpuFallback = value.value("allowCpuFallback", settings.allowCpuFallback);
    settings.externalMaskInfluence = value.value("externalMaskInfluence", settings.externalMaskInfluence);
    settings.runRequestRevision = std::max(0, value.value("runRequestRevision", settings.runRequestRevision));
    settings.runRequestAllowLargeCpu = value.value("runRequestAllowLargeCpu", settings.runRequestAllowLargeCpu);
    settings.renderNodeId = value.value("renderNodeId", settings.renderNodeId);
    settings.tilePlan = DeserializeTilePlan(value.value("tilePlan", nlohmann::json::object()));
    settings.cfaOverride = CfaOverrideFromToken(value.value("cfaOverride", std::string("from_metadata")));
    settings.overrideBlackLevel = value.value("overrideBlackLevel", settings.overrideBlackLevel);
    settings.blackLevel = std::max(0.0f, value.value("blackLevel", settings.blackLevel));
    settings.overrideWhiteLevel = value.value("overrideWhiteLevel", settings.overrideWhiteLevel);
    settings.whiteLevel = std::max(settings.blackLevel + 1.0f, value.value("whiteLevel", settings.whiteLevel));
    settings.noiseEstimateMode = NoiseEstimateModeFromToken(value.value("noiseEstimateMode", std::string("metadata_auto")));
    settings.manualNoiseEstimate = Clamp01(value.value("manualNoiseEstimate", settings.manualNoiseEstimate));
    settings.rawWhiteBalanceStage = RawWhiteBalanceStageFromToken(value.value("rawWhiteBalanceStage", std::string("before_white_balance")));
    settings.rawOutputMode = RawOutputModeFromToken(value.value("rawOutputMode", std::string("continue_to_demosaic")));
    return settings;
}

} // namespace NeuralDenoise
