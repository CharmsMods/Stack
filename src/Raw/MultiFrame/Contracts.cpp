#include "Raw/MultiFrame/Contracts.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace Raw::MultiFrame {
namespace {

bool SetError(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

bool IsSha256(const std::string& value) {
    if (value.size() != 64u) return false;
    for (const char character : value) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'f';
        const bool upper = character >= 'A' && character <= 'F';
        if (!digit && !lower && !upper) return false;
    }
    return true;
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return false;
    }
    const std::uint64_t product = extent.width * extent.height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

std::array<CfaPhase, 4> PhasesFor(CfaPattern pattern) {
    switch (pattern) {
        case CfaPattern::RGGB:
            return { CfaPhase::R, CfaPhase::G0, CfaPhase::G1, CfaPhase::B };
        case CfaPattern::BGGR:
            return { CfaPhase::B, CfaPhase::G0, CfaPhase::G1, CfaPhase::R };
        case CfaPattern::GBRG:
            return { CfaPhase::G0, CfaPhase::B, CfaPhase::R, CfaPhase::G1 };
        case CfaPattern::GRBG:
            return { CfaPhase::G0, CfaPhase::R, CfaPhase::B, CfaPhase::G1 };
        case CfaPattern::Unknown:
        default:
            return {};
    }
}

bool FiniteNonnegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

const char* CfaPhaseName(CfaPhase phase) {
    switch (phase) {
        case CfaPhase::R: return "R";
        case CfaPhase::G0: return "G0";
        case CfaPhase::G1: return "G1";
        case CfaPhase::B: return "B";
        default: return "Unknown";
    }
}

bool TryCreateCfaDomainIdentity(
    CfaPattern activePattern,
    RawSensorRect sensorActiveArea,
    PixelExtent activeExtent,
    CfaDomainIdentity& result,
    std::string* error) {
    if (activePattern == CfaPattern::Unknown) {
        return SetError(error, "MultiFrame CFA domain requires a supported Bayer pattern.");
    }
    CfaDomainIdentity value;
    value.activePattern = activePattern;
    value.sensorActiveArea = sensorActiveArea;
    value.activeExtent = activeExtent;
    value.phaseByParity = PhasesFor(activePattern);
    if (!ValidateCfaDomainIdentity(value, error)) return false;
    result = value;
    return true;
}

bool ValidateCfaDomainIdentity(
    const CfaDomainIdentity& domain,
    std::string* error) {
    if (domain.activePattern == CfaPattern::Unknown ||
        domain.phaseByParity != PhasesFor(domain.activePattern)) {
        return SetError(error, "MultiFrame CFA phase identity is invalid.");
    }
    if (domain.activeExtent.width == 0u || domain.activeExtent.height == 0u) {
        return SetError(error, "MultiFrame CFA domain extent is empty.");
    }
    if (!domain.packedBayer || !domain.preserveSignedValues ||
        !domain.preservePositiveOverrange) {
        return SetError(error, "MultiFrame CFA domain weakened the Virtual Bayer numeric contract.");
    }
    const int sensorWidth = domain.sensorActiveArea.right - domain.sensorActiveArea.left;
    const int sensorHeight = domain.sensorActiveArea.bottom - domain.sensorActiveArea.top;
    if (sensorWidth <= 0 || sensorHeight <= 0 ||
        static_cast<std::uint64_t>(sensorWidth) != domain.activeExtent.width ||
        static_cast<std::uint64_t>(sensorHeight) != domain.activeExtent.height) {
        return SetError(error, "MultiFrame active-area coordinates disagree with the packed extent.");
    }
    return true;
}

CfaPhase PhaseAt(
    const CfaDomainIdentity& domain,
    std::uint64_t activeX,
    std::uint64_t activeY) {
    const std::size_t parity = static_cast<std::size_t>(
        (activeY & 1u) * 2u + (activeX & 1u));
    return domain.phaseByParity[parity];
}

bool ValidateSourceIdentity(
    const SourceIdentity& identity,
    std::string* error) {
    if (identity.stableFrameId.empty() || !IsSha256(identity.contentSha256) ||
        identity.byteLength == 0u) {
        return SetError(error, "MultiFrame source identity is incomplete.");
    }
    return true;
}

const char* SampleEvidenceStateName(SampleEvidenceState state) {
    switch (state) {
        case SampleEvidenceState::ExactMeasurement: return "exact-measurement";
        case SampleEvidenceState::RepairedMeasurement: return "repaired-measurement";
        case SampleEvidenceState::UpperCensored: return "upper-censored";
        case SampleEvidenceState::LowerCensored: return "lower-censored";
        case SampleEvidenceState::Defective: return "defective";
        case SampleEvidenceState::DecoderInvalid: return "decoder-invalid";
        case SampleEvidenceState::Unsupported: return "unsupported";
        case SampleEvidenceState::OutOfBounds: return "out-of-bounds";
        case SampleEvidenceState::UnknownInvalid: return "unknown-invalid";
        default: return "unknown-invalid";
    }
}

std::uint16_t SampleEvidenceCauseMask(SampleEvidenceCause cause) {
    return static_cast<std::uint16_t>(cause);
}

bool HasSampleEvidenceCause(
    std::uint16_t mask,
    SampleEvidenceCause cause) {
    return (mask & SampleEvidenceCauseMask(cause)) != 0u;
}

bool ValidateRadiometricBounds(
    const RadiometricBounds& bounds,
    std::string* error) {
    if ((bounds.lowerInclusive && !std::isfinite(*bounds.lowerInclusive)) ||
        (bounds.upperInclusive && !std::isfinite(*bounds.upperInclusive))) {
        return SetError(error, "MultiFrame radiometric bounds must be finite when present.");
    }
    if (bounds.lowerInclusive && bounds.upperInclusive &&
        *bounds.lowerInclusive > *bounds.upperInclusive) {
        return SetError(error, "MultiFrame radiometric lower bound exceeds its upper bound.");
    }
    return true;
}

bool IsNumericMeasurement(const SampleEvidence& evidence) {
    return evidence.state == SampleEvidenceState::ExactMeasurement ||
        evidence.state == SampleEvidenceState::RepairedMeasurement;
}

bool ValidateSampleEvidence(
    const SampleEvidence& evidence,
    std::string* error) {
    if (static_cast<std::uint8_t>(evidence.state) >
        static_cast<std::uint8_t>(SampleEvidenceState::UnknownInvalid)) {
        return SetError(error, "MultiFrame sample evidence state is unsupported.");
    }
    if (!ValidateRadiometricBounds(evidence.bounds, error)) return false;
    if (evidence.state == SampleEvidenceState::RepairedMeasurement &&
        !HasSampleEvidenceCause(evidence.causes, SampleEvidenceCause::DecoderRepair)) {
        return SetError(error, "MultiFrame repaired evidence lacks repair provenance.");
    }
    if (evidence.state == SampleEvidenceState::Defective &&
        !HasSampleEvidenceCause(evidence.causes, SampleEvidenceCause::KnownDefect)) {
        return SetError(error, "MultiFrame defective evidence lacks defect provenance.");
    }
    return true;
}

bool ValidatePreparedMeasurementTile(
    const PreparedMeasurementTile& tile,
    std::string* error) {
    std::size_t count = 0;
    if (!CheckedSampleCount(tile.extent, count) ||
        tile.normalizedMosaic.size() != count ||
        tile.comparisonGain.size() != count ||
        tile.sampleEvidence.size() != count) {
        return SetError(error, "MultiFrame prepared tile payload sizes are inconsistent.");
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!ValidateSampleEvidence(tile.sampleEvidence[index], error)) return false;
        if (IsNumericMeasurement(tile.sampleEvidence[index]) &&
            !std::isfinite(tile.normalizedMosaic[index])) {
            return SetError(error, "MultiFrame numeric measurement is not finite.");
        }
        if (!std::isfinite(tile.comparisonGain[index]) ||
            tile.comparisonGain[index] <= 0.0f) {
            return SetError(error, "MultiFrame prepared tile has an invalid comparison gain.");
        }
    }
    return true;
}

bool ReloadableSourceHandle::IsBound() const {
    return !identity.empty() && static_cast<bool>(readTile);
}

TileReadStatus ReloadableSourceHandle::Read(
    std::uint32_t tileX,
    std::uint32_t tileY,
    PreparedMeasurementTile& tile,
    std::string* error) const {
    if (!IsBound()) {
        if (error) *error = "MultiFrame source handle is not bound.";
        return TileReadStatus::IoError;
    }
    return readTile(tileX, tileY, tile, error);
}

bool ValidatePreparedMeasurementSource(
    const PreparedMeasurementSource& source,
    std::string* error) {
    if (source.contractVersion != kPreparedSourceContractVersion ||
        source.contractId != kPreparedSourceContractId) {
        return SetError(error, "MultiFrame prepared-source contract identity is unsupported.");
    }
    if (!ValidateSourceIdentity(source.source, error) ||
        !ValidateCfaDomainIdentity(source.domain, error)) {
        return false;
    }
    if (source.tileRawPixels == 0u || source.tileColumns == 0u ||
        source.tileRows == 0u || !source.tiles.IsBound()) {
        return SetError(error, "MultiFrame prepared-source tiling is incomplete.");
    }
    const PreparationProvenance& provenance = source.provenance;
    if (provenance.producerAlgorithmId.empty() ||
        provenance.producerAlgorithmVersion == 0u ||
        provenance.producerProcessorContractId.empty() ||
        provenance.producerProcessorContractVersion == 0u ||
        provenance.preparationContractId.empty() ||
        provenance.preparationContractVersion == 0u ||
        !IsSha256(provenance.preparedCacheKey) ||
        provenance.numericalValuesReinterpreted) {
        return SetError(error, "MultiFrame preparation provenance is incomplete or reinterpreted.");
    }
    return true;
}

const char* NoiseModelQualityName(NoiseModelQuality quality) {
    switch (quality) {
        case NoiseModelQuality::TrustedMetadata: return "trusted-metadata";
        case NoiseModelQuality::CalibratedCamera: return "calibrated-camera";
        case NoiseModelQuality::EstimatedBurst: return "estimated-burst";
        case NoiseModelQuality::GenericLowConfidence: return "generic-low-confidence";
        case NoiseModelQuality::Unavailable: return "unavailable";
        default: return "unavailable";
    }
}

bool ValidateNoiseModelReference(
    const NoiseModelReference& model,
    std::string* error) {
    if (model.contractVersion != kNoiseReferenceContractVersion ||
        model.contractId != kNoiseReferenceContractId ||
        model.sourceContractId.empty() || model.sourceContractVersion == 0u ||
        model.numericalValuesReinterpreted) {
        return SetError(error, "MultiFrame noise-reference identity is invalid.");
    }
    if (model.quality == NoiseModelQuality::Unavailable) return true;
    if (!IsSha256(model.preparedFrameCacheKey) ||
        !IsSha256(model.identitySha256)) {
        return SetError(error, "MultiFrame noise-reference hashes are invalid.");
    }
    for (const SiteNoiseProfile& site : model.sites) {
        if (!FiniteNonnegative(site.shotScale) ||
            !FiniteNonnegative(site.offsetVariance) ||
            !FiniteNonnegative(site.quantizationVariance) ||
            !FiniteNonnegative(site.residualModelTau0) ||
            !FiniteNonnegative(site.residualModelTau1) ||
            !site.quantizationIncluded) {
            return SetError(error, "MultiFrame noise-reference site profile is incomplete.");
        }
    }
    return true;
}

} // namespace Raw::MultiFrame
