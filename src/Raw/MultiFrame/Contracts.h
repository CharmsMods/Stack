#pragma once

#include "Raw/RawImageData.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

inline constexpr std::uint32_t kMeasurementContractVersion = 1;
inline constexpr const char* kMeasurementContractId =
    "stack-raw-multiframe-measurement-v1";
inline constexpr std::uint32_t kPreparedSourceContractVersion = 1;
inline constexpr const char* kPreparedSourceContractId =
    "stack-raw-multiframe-prepared-source-v1";
inline constexpr std::uint32_t kNoiseReferenceContractVersion = 1;
inline constexpr const char* kNoiseReferenceContractId =
    "stack-raw-multiframe-noise-reference-v1";

enum class CfaPhase : std::uint8_t {
    R = 0,
    G0 = 1,
    G1 = 2,
    B = 3
};

const char* CfaPhaseName(CfaPhase phase);

struct PixelExtent {
    std::uint64_t width = 0;
    std::uint64_t height = 0;
};

struct CfaDomainIdentity {
    CfaPattern activePattern = CfaPattern::Unknown;
    RawSensorRect sensorActiveArea;
    PixelExtent activeExtent;
    std::array<CfaPhase, 4> phaseByParity {
        CfaPhase::R,
        CfaPhase::G0,
        CfaPhase::G1,
        CfaPhase::B
    };
    bool packedBayer = true;
    bool preserveSignedValues = true;
    bool preservePositiveOverrange = true;
};

bool TryCreateCfaDomainIdentity(
    CfaPattern activePattern,
    RawSensorRect sensorActiveArea,
    PixelExtent activeExtent,
    CfaDomainIdentity& result,
    std::string* error = nullptr);
bool ValidateCfaDomainIdentity(
    const CfaDomainIdentity& domain,
    std::string* error = nullptr);
CfaPhase PhaseAt(
    const CfaDomainIdentity& domain,
    std::uint64_t activeX,
    std::uint64_t activeY);

struct SourceIdentity {
    std::string stableFrameId;
    std::string contentSha256;
    std::uint64_t byteLength = 0;
};

bool ValidateSourceIdentity(
    const SourceIdentity& identity,
    std::string* error = nullptr);

enum class SampleEvidenceState : std::uint8_t {
    ExactMeasurement = 0,
    RepairedMeasurement,
    UpperCensored,
    LowerCensored,
    Defective,
    DecoderInvalid,
    Unsupported,
    OutOfBounds,
    UnknownInvalid
};

const char* SampleEvidenceStateName(SampleEvidenceState state);

enum class SampleEvidenceCause : std::uint16_t {
    None = 0,
    SensorSaturation = 1u << 0u,
    KnownDefect = 1u << 1u,
    DecoderRepair = 1u << 2u,
    ExplicitDecoderClip = 1u << 3u
};

std::uint16_t SampleEvidenceCauseMask(SampleEvidenceCause cause);
bool HasSampleEvidenceCause(
    std::uint16_t mask,
    SampleEvidenceCause cause);

struct RadiometricBounds {
    std::optional<double> lowerInclusive;
    std::optional<double> upperInclusive;
};

bool ValidateRadiometricBounds(
    const RadiometricBounds& bounds,
    std::string* error = nullptr);

struct SampleEvidence {
    SampleEvidenceState state = SampleEvidenceState::ExactMeasurement;
    std::uint16_t causes = 0;
    RadiometricBounds bounds;
};

bool IsNumericMeasurement(const SampleEvidence& evidence);
bool ValidateSampleEvidence(
    const SampleEvidence& evidence,
    std::string* error = nullptr);

struct PreparedMeasurementTile {
    std::uint32_t tileX = 0;
    std::uint32_t tileY = 0;
    std::uint64_t originX = 0;
    std::uint64_t originY = 0;
    PixelExtent extent;
    std::vector<float> normalizedMosaic;
    std::vector<float> comparisonGain;
    std::vector<SampleEvidence> sampleEvidence;
};

bool ValidatePreparedMeasurementTile(
    const PreparedMeasurementTile& tile,
    std::string* error = nullptr);

enum class TileReadStatus : std::uint8_t {
    Hit = 0,
    Missing,
    Corrupt,
    IoError
};

using TileReader = std::function<TileReadStatus(
    std::uint32_t,
    std::uint32_t,
    PreparedMeasurementTile&,
    std::string*)>;

struct ReloadableSourceHandle {
    std::string identity;
    TileReader readTile;

    bool IsBound() const;
    TileReadStatus Read(
        std::uint32_t tileX,
        std::uint32_t tileY,
        PreparedMeasurementTile& tile,
        std::string* error = nullptr) const;
};

struct PreparationProvenance {
    std::string producerAlgorithmId;
    std::uint32_t producerAlgorithmVersion = 0;
    std::string producerProcessorContractId;
    std::uint32_t producerProcessorContractVersion = 0;
    std::string preparationContractId;
    std::uint32_t preparationContractVersion = 0;
    std::string preparedCacheKey;
    bool numericalValuesReinterpreted = false;
};

struct PreparedMeasurementSource {
    std::uint32_t contractVersion = kPreparedSourceContractVersion;
    std::string contractId = kPreparedSourceContractId;
    SourceIdentity source;
    CfaDomainIdentity domain;
    std::uint32_t tileRawPixels = 0;
    std::uint32_t tileColumns = 0;
    std::uint32_t tileRows = 0;
    PreparationProvenance provenance;
    ReloadableSourceHandle tiles;
};

bool ValidatePreparedMeasurementSource(
    const PreparedMeasurementSource& source,
    std::string* error = nullptr);

enum class NoiseModelQuality : std::uint8_t {
    TrustedMetadata = 0,
    CalibratedCamera,
    EstimatedBurst,
    GenericLowConfidence,
    Unavailable
};

const char* NoiseModelQualityName(NoiseModelQuality quality);

struct SiteNoiseProfile {
    double shotScale = 0.0;
    double offsetVariance = 0.0;
    double quantizationVariance = 0.0;
    double residualModelTau0 = 0.0;
    double residualModelTau1 = 0.0;
    bool quantizationIncluded = false;
};

struct NoiseModelReference {
    std::uint32_t contractVersion = kNoiseReferenceContractVersion;
    std::string contractId = kNoiseReferenceContractId;
    std::string sourceContractId;
    std::uint32_t sourceContractVersion = 0;
    std::string preparedFrameCacheKey;
    std::string identitySha256;
    NoiseModelQuality quality = NoiseModelQuality::Unavailable;
    std::string sourceDescription;
    std::string sourceRecordId;
    std::array<SiteNoiseProfile, 4> sites;
    bool numericalValuesReinterpreted = false;
};

bool ValidateNoiseModelReference(
    const NoiseModelReference& model,
    std::string* error = nullptr);

} // namespace Raw::MultiFrame
