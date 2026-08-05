#pragma once

#include "Raw/MultiFrameDenoise/Streaming.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kInspectionContractVersion = 1;
inline constexpr const char* kInspectionContractId =
    "ra-cfa-offline-inspection-v1";

struct MfdInspectionInput {
    std::string sampleId;
    CfaPattern cfaPattern = CfaPattern::Unknown;
    PixelExtent extent;
    std::vector<float> referenceNormalizedMosaic;
    std::vector<float> outputNormalizedMosaic;
    std::vector<FusionPixelDiagnostics> fusionDiagnostics;
};

struct MfdInspectionInputView {
    std::string sampleId;
    CfaPattern cfaPattern = CfaPattern::Unknown;
    PixelExtent extent;
    const std::vector<float>* referenceNormalizedMosaic = nullptr;
    const std::vector<float>* outputNormalizedMosaic = nullptr;
    const std::vector<FusionPixelDiagnostics>* fusionDiagnostics = nullptr;
};

struct MfdInspectionSummary {
    bool valid = false;
    std::string message;
    std::uint64_t pixelCount = 0u;
    std::uint64_t contributingPixelCount = 0u;
    std::uint64_t exactReferencePixelCount = 0u;
    double contributingPixelFraction = 0.0;
    double exactReferencePixelFraction = 0.0;
    double meanAbsoluteDelta = 0.0;
    double percentile95AbsoluteDelta = 0.0;
    double percentile99AbsoluteDelta = 0.0;
    double maximumAbsoluteDelta = 0.0;
    double meanEffectiveSampleCount = 1.0;
    double previewLinearScale = 1.0;
    std::vector<std::filesystem::path> writtenFiles;
};

bool WriteMfdInspectionPacket(
    const MfdInspectionInput& input,
    const std::filesystem::path& outputDirectory,
    MfdInspectionSummary& summary,
    std::string* error = nullptr);

bool WriteMfdInspectionPacketView(
    const MfdInspectionInputView& input,
    const std::filesystem::path& outputDirectory,
    MfdInspectionSummary& summary,
    std::string* error = nullptr);

nlohmann::json SerializeMfdInspectionSummary(
    const MfdInspectionSummary& summary);

} // namespace Raw::Mfd
