#pragma once

#include "Raw/MultiFrameDenoise/Fusion.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <string>

namespace Raw::Mfd {

inline constexpr std::uint32_t kSharedBurstSettingsSchemaVersion = 1u;
inline constexpr std::uint32_t kSharedBurstAlgorithmVersion = 1u;
inline constexpr const char* kSharedBurstAlgorithmId = "shared-burst";
inline constexpr const char* kSharedBurstAlgorithmVersionId =
    "shared-burst-v1";
inline constexpr const char* kSharedBurstSettingsContractId =
    "stack-shared-burst-settings-v1";
inline constexpr const char* kSharedBurstFusionContractId =
    "stack-shared-burst-fusion-v1";
inline constexpr const char* kSharedBurstDiagnosticsContractId =
    "stack-shared-burst-diagnostics-v4";
inline constexpr const char* kSharedBurstResultCacheContractId =
    "stack-shared-burst-result-cache-v4";
inline constexpr std::size_t kSharedBurstMinimumEnabledCaptures = 2u;
inline constexpr std::size_t kSharedBurstMaximumEnabledCaptures = 30u;

struct SharedBurstSettings {
    std::uint32_t schemaVersion = kSharedBurstSettingsSchemaVersion;
    std::string algorithmId = kSharedBurstAlgorithmId;
    std::uint32_t algorithmVersion = kSharedBurstAlgorithmVersion;
    std::string profile = "static-maximum";
    double exposureGroupToleranceEv = 0.5;
    double huberThreshold = 1.345;
    std::uint32_t maximumHuberIterations = 12u;
    double absoluteHuberTolerance = 1.0e-12;
    double relativeHuberTolerance = 1.0e-10;
};

nlohmann::json SerializeSharedBurstSettings(
    const SharedBurstSettings& settings);
bool DeserializeSharedBurstSettings(
    const nlohmann::json& value,
    SharedBurstSettings& settings,
    std::string* error = nullptr);
bool ValidateSharedBurstSettings(
    const SharedBurstSettings& settings,
    std::string* error = nullptr);

bool FuseSharedBurstSample(
    const FusionReferenceSample& reference,
    const std::vector<FusionCandidateSample>& candidates,
    const Parameters& preparationParameters,
    const SharedBurstSettings& settings,
    FusionPixelResult& result,
    std::string* error = nullptr);

bool FuseSharedBurstTile(
    const FusionTileRequest& request,
    FusionTileResult& result,
    std::string* error = nullptr);
bool FuseSharedBurstTile(
    const FusionTileRequest& request,
    const SharedBurstSettings& settings,
    FusionTileResult& result,
    std::string* error = nullptr);

} // namespace Raw::Mfd
