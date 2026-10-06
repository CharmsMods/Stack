#pragma once

#include "Raw/MultiFrame/Contracts.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

inline constexpr std::uint32_t kRawMeasurementHandleVersion = 1;
inline constexpr const char* kRawMeasurementHandleContractId =
    "stack-raw-measurement-handle-v1";
inline constexpr std::uint32_t kEvidenceSignatureVersion = 1;

enum class MeasurementDomain : std::uint8_t {
    SensorCfa = 0,
    VirtualCfa,
    SceneLinearCfa,
    SceneLinearRgb
};

const char* MeasurementDomainName(MeasurementDomain domain);

struct EvidenceContribution {
    std::string originalFrameId;
    double globalCoefficient = 0.0;
    // Optional tile-major coefficients permit HDR ownership and motion
    // decisions to vary spatially without pretending one global mixture is
    // exact. Empty means the global coefficient applies everywhere.
    std::vector<float> tileCoefficients;
};

struct EvidenceSignature {
    std::uint32_t schemaVersion = kEvidenceSignatureVersion;
    std::string identitySha256;
    std::uint32_t tileColumns = 0;
    std::uint32_t tileRows = 0;
    std::vector<EvidenceContribution> contributions;
};

struct EvidenceOverlap {
    bool overlaps = false;
    double normalizedGlobalOverlap = 0.0;
    std::vector<std::string> sharedOriginalFrameIds;
};

bool ValidateEvidenceSignature(
    const EvidenceSignature& signature,
    std::string* error = nullptr);
EvidenceOverlap EvaluateEvidenceOverlap(
    const EvidenceSignature& a,
    const EvidenceSignature& b);

struct RawMeasurementPlanes {
    PixelExtent extent;
    std::shared_ptr<const std::vector<float>> mosaic;
    std::shared_ptr<const std::vector<float>> variance;
    std::shared_ptr<const std::vector<float>> effectiveSupport;
    std::shared_ptr<const std::vector<std::uint8_t>> validity;
    std::shared_ptr<const std::vector<std::uint8_t>> clipping;
};

struct RawMeasurementHandle {
    std::uint32_t contractVersion = kRawMeasurementHandleVersion;
    std::string contractId = kRawMeasurementHandleContractId;
    std::string contentHash;
    std::string producerNodeId;
    std::string producerAlgorithmId;
    std::uint32_t producerAlgorithmVersion = 0;
    MeasurementDomain domain = MeasurementDomain::SensorCfa;
    CfaDomainIdentity cfa;
    double radiometricScaleToAnchor = 1.0;
    std::string radiometricAnchorId;
    std::string geometryId;
    RawMeasurementPlanes planes;
    EvidenceSignature evidence;
    std::vector<ReloadableSourceHandle> sourceReplay;
};

struct RawMeasurementSetHandle {
    std::vector<std::shared_ptr<const RawMeasurementHandle>> measurements;
};

bool ValidateRawMeasurementHandle(
    const RawMeasurementHandle& handle,
    std::string* error = nullptr);
bool ValidateRawMeasurementSetHandle(
    const RawMeasurementSetHandle& handle,
    std::string* error = nullptr);

bool ValidateFusionMeasurementCompatibility(
    const RawMeasurementSetHandle& inputs, std::string* error = nullptr);

// Overlapping inputs require covariance-aware estimation or explicit
// de-duplication. Reconnecting the same pixels never creates new SNR.
bool RequiresCovarianceAwareFusion(
    const RawMeasurementSetHandle& inputs,
    EvidenceOverlap* strongestOverlap = nullptr);

} // namespace Raw::MultiFrame
