#pragma once

#include "ThirdParty/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Raw::Hdr {

inline constexpr std::uint32_t kOperationSchemaVersion = 1;
inline constexpr std::uint32_t kAlgorithmVersion = 4;
inline constexpr const char* kAlgorithmId = "tripod-cfa-hdr";
inline constexpr const char* kInputDomain = "mosaic-cfa";
inline constexpr std::size_t kMinimumFrameCount = 2u;
inline constexpr std::size_t kMaximumFrameCount = 20u;

enum class AlignmentMode : std::uint8_t {
    AutoTranslation = 0,
    Identity,
    VerifyOnly
};

const char* AlignmentModeName(AlignmentMode mode);
bool ParseAlignmentMode(const std::string& value, AlignmentMode& mode);

struct Parameters {
    AlignmentMode alignmentMode = AlignmentMode::AutoTranslation;
    std::uint32_t tileRawPixels = 512;
    double minimumExposureSpanEv = 0.5;
    double mixedIsoWarningEv = 0.15;
    double mixedIsoStrongUncertaintyEv = 0.35;
    double unverifiedContributionCap = 0.25;
    double saturationHeadroomSigma = 4.0;
    double robustCutoffSigma = 4.5;
    double disagreementFallbackSigma = 8.0;
    double numericalVarianceFloor = 1.0e-10;
    std::uint64_t minimumExposureFitSamples = 64;
    std::uint32_t highlightNeighborhoodRadiusCfa = 2;
    std::uint32_t highlightHoleMinimumClippedNeighbors = 3;
    double highlightHoleNormalizedCeiling = 0.90;
    std::uint32_t colorCoherenceRadiusCfa = 1;
    std::uint32_t colorCoherenceMinimumSupportingChannels = 2;
    double colorCoherenceDarkRatio = 0.30;
    double colorCoherenceNeighborRatio = 0.55;
    double colorCoherenceMaximumRepairErrorEv = 0.75;
    double colorCoherenceMinimumImprovementEv = 0.75;
};

nlohmann::json SerializeParameters(const Parameters& parameters);
bool DeserializeParameters(
    const nlohmann::json& value,
    Parameters& parameters,
    std::string* error = nullptr);
bool ValidateParameters(const Parameters& parameters, std::string* error = nullptr);

} // namespace Raw::Hdr
