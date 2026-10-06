#include "Raw/MultiFrame/LegacyAdapters.h"

#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/MultiFrameHdr/Processor.h"

#include <utility>

namespace Raw::MultiFrame {
namespace {

bool SetError(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

NoiseModelQuality AdaptNoiseQuality(Mfd::NoiseModelQuality quality) {
    switch (quality) {
        case Mfd::NoiseModelQuality::TrustedMetadata:
            return NoiseModelQuality::TrustedMetadata;
        case Mfd::NoiseModelQuality::CalibratedCamera:
            return NoiseModelQuality::CalibratedCamera;
        case Mfd::NoiseModelQuality::EstimatedBurst:
            return NoiseModelQuality::EstimatedBurst;
        case Mfd::NoiseModelQuality::GenericLowConfidence:
            return NoiseModelQuality::GenericLowConfidence;
        case Mfd::NoiseModelQuality::Unavailable:
        default:
            return NoiseModelQuality::Unavailable;
    }
}

TileReadStatus AdaptTileReadStatus(Mfd::TileCacheReadStatus status) {
    switch (status) {
        case Mfd::TileCacheReadStatus::Hit: return TileReadStatus::Hit;
        case Mfd::TileCacheReadStatus::Miss: return TileReadStatus::Missing;
        case Mfd::TileCacheReadStatus::Corrupt: return TileReadStatus::Corrupt;
        case Mfd::TileCacheReadStatus::IoError:
        default:
            return TileReadStatus::IoError;
    }
}

} // namespace

const char* LegacyProcessorKindName(LegacyProcessorKind kind) {
    switch (kind) {
        case LegacyProcessorKind::MultiFrameDenoise: return "multi-frame-denoise";
        case LegacyProcessorKind::RawBurstHdr: return "raw-burst-hdr";
        default: return "unknown";
    }
}

LegacyProcessorIdentity CaptureLegacyProcessorIdentity(
    LegacyProcessorKind kind) {
    LegacyProcessorIdentity result;
    result.kind = kind;
    result.preparationContractId = Mfd::kPreparationContractId;
    result.preparationContractVersion = Mfd::kPreparationContractVersion;
    result.noiseModelContractId = Mfd::kNoiseModelContractId;
    result.noiseModelContractVersion = Mfd::kNoiseModelContractVersion;
    if (kind == LegacyProcessorKind::RawBurstHdr) {
        result.algorithmId = Hdr::kAlgorithmId;
        result.algorithmVersion = Hdr::kAlgorithmVersion;
        result.processorContractId = Hdr::kProcessorContractId;
        result.processorContractVersion = Hdr::kProcessorContractVersion;
    } else {
        result.algorithmId = Mfd::kAlgorithmId;
        result.algorithmVersion = Mfd::kAlgorithmVersion;
        result.processorContractId = "ra-cfa-offline-processor-v1";
        result.processorContractVersion = 1u;
    }
    return result;
}

bool ValidateLegacyProcessorIdentity(
    const LegacyProcessorIdentity& identity,
    std::string* error) {
    if (identity.algorithmId.empty() || identity.algorithmVersion == 0u ||
        identity.processorContractId.empty() ||
        identity.processorContractVersion == 0u ||
        identity.preparationContractId.empty() ||
        identity.preparationContractVersion == 0u ||
        identity.noiseModelContractId.empty() ||
        identity.noiseModelContractVersion == 0u) {
        return SetError(error, "Legacy MultiFrame processor identity is incomplete.");
    }
    return true;
}

SampleEvidence AdaptLegacySampleEvidence(std::uint8_t preparedFlags) {
    SampleEvidence result;
    const auto addCause = [&result](SampleEvidenceCause cause) {
        result.causes |= SampleEvidenceCauseMask(cause);
    };
    if (Mfd::HasSampleFlag(preparedFlags, Mfd::PreparedSampleFlag::Saturated)) {
        addCause(SampleEvidenceCause::SensorSaturation);
    }
    if (Mfd::HasSampleFlag(preparedFlags, Mfd::PreparedSampleFlag::Defective)) {
        addCause(SampleEvidenceCause::KnownDefect);
    }
    if (Mfd::HasSampleFlag(preparedFlags, Mfd::PreparedSampleFlag::DecoderRepaired)) {
        addCause(SampleEvidenceCause::DecoderRepair);
    }
    if (Mfd::HasSampleFlag(
            preparedFlags,
            Mfd::PreparedSampleFlag::ExplicitDecoderClip)) {
        addCause(SampleEvidenceCause::ExplicitDecoderClip);
    }

    if (HasSampleEvidenceCause(result.causes, SampleEvidenceCause::KnownDefect)) {
        result.state = SampleEvidenceState::Defective;
    } else if (
        HasSampleEvidenceCause(result.causes, SampleEvidenceCause::SensorSaturation) ||
        HasSampleEvidenceCause(result.causes, SampleEvidenceCause::ExplicitDecoderClip)) {
        result.state = SampleEvidenceState::UpperCensored;
    } else if (
        HasSampleEvidenceCause(result.causes, SampleEvidenceCause::DecoderRepair)) {
        result.state = SampleEvidenceState::RepairedMeasurement;
    } else {
        result.state = SampleEvidenceState::ExactMeasurement;
    }
    return result;
}

bool AdaptLegacyPreparedFrame(
    LegacyProcessorKind producer,
    const std::string& stableFrameId,
    const Mfd::PreparedRawFrame& frame,
    std::shared_ptr<Mfd::NormalizedTileCache> tileCache,
    PreparedMeasurementSource& result,
    std::string* error) {
    if (frame.contractVersion != Mfd::kPreparationContractVersion ||
        frame.contractId != Mfd::kPreparationContractId || !tileCache) {
        return SetError(error, "Legacy MultiFrame preparation cannot be adapted.");
    }

    const LegacyProcessorIdentity producerIdentity =
        CaptureLegacyProcessorIdentity(producer);
    if (!ValidateLegacyProcessorIdentity(producerIdentity, error)) return false;

    PreparedMeasurementSource adapted;
    adapted.source.stableFrameId = stableFrameId;
    adapted.source.contentSha256 = frame.sourceContentSha256;
    adapted.source.byteLength = frame.sourceByteSize;
    if (!TryCreateCfaDomainIdentity(
            frame.activeCfaPattern,
            frame.sensorActiveArea,
            { frame.activeExtent.width, frame.activeExtent.height },
            adapted.domain,
            error)) {
        return false;
    }
    adapted.tileRawPixels = frame.tileRawPixels;
    adapted.tileColumns = frame.tileColumns;
    adapted.tileRows = frame.tileRows;
    adapted.provenance.producerAlgorithmId = producerIdentity.algorithmId;
    adapted.provenance.producerAlgorithmVersion = producerIdentity.algorithmVersion;
    adapted.provenance.producerProcessorContractId =
        producerIdentity.processorContractId;
    adapted.provenance.producerProcessorContractVersion =
        producerIdentity.processorContractVersion;
    adapted.provenance.preparationContractId = frame.contractId;
    adapted.provenance.preparationContractVersion = frame.contractVersion;
    adapted.provenance.preparedCacheKey = frame.cacheKey;
    adapted.provenance.numericalValuesReinterpreted = false;
    adapted.tiles.identity = frame.cacheKey;

    const Mfd::PreparedRawFrame capturedFrame = frame;
    adapted.tiles.readTile = [
        capturedFrame,
        tileCache = std::move(tileCache)
    ](
        std::uint32_t tileX,
        std::uint32_t tileY,
        PreparedMeasurementTile& output,
        std::string* readError) {
        Mfd::PreparedRawTile legacyTile;
        const Mfd::TileCacheReadStatus legacyStatus = Mfd::ReadPreparedTile(
            capturedFrame,
            *tileCache,
            tileX,
            tileY,
            legacyTile,
            readError);
        const TileReadStatus status = AdaptTileReadStatus(legacyStatus);
        if (status != TileReadStatus::Hit) return status;

        PreparedMeasurementTile adaptedTile;
        adaptedTile.tileX = legacyTile.tileX;
        adaptedTile.tileY = legacyTile.tileY;
        adaptedTile.originX = legacyTile.originX;
        adaptedTile.originY = legacyTile.originY;
        adaptedTile.extent = {
            legacyTile.extent.width,
            legacyTile.extent.height
        };
        adaptedTile.normalizedMosaic = legacyTile.normalizedMosaic;
        adaptedTile.comparisonGain = legacyTile.comparisonGain;
        adaptedTile.sampleEvidence.reserve(legacyTile.sampleFlags.size());
        for (const std::uint8_t flags : legacyTile.sampleFlags) {
            adaptedTile.sampleEvidence.push_back(AdaptLegacySampleEvidence(flags));
        }
        if (!ValidatePreparedMeasurementTile(adaptedTile, readError)) {
            return TileReadStatus::Corrupt;
        }
        output = std::move(adaptedTile);
        return TileReadStatus::Hit;
    };

    if (!ValidatePreparedMeasurementSource(adapted, error)) return false;
    result = std::move(adapted);
    return true;
}

bool AdaptLegacyNoiseModel(
    const Mfd::NoiseModel& model,
    NoiseModelReference& result,
    std::string* error) {
    if (!Mfd::ValidateNoiseModel(model, error)) return false;

    NoiseModelReference adapted;
    adapted.sourceContractId = model.contractId;
    adapted.sourceContractVersion = model.contractVersion;
    adapted.preparedFrameCacheKey = model.preparedFrameCacheKey;
    adapted.identitySha256 = model.identitySha256;
    adapted.quality = AdaptNoiseQuality(model.quality);
    adapted.sourceDescription = model.diagnostics.source;
    adapted.sourceRecordId = model.diagnostics.sourceRecordId;
    adapted.numericalValuesReinterpreted = false;
    for (std::size_t index = 0; index < adapted.sites.size(); ++index) {
        const Mfd::SiteNoiseProfile& source = model.sites[index];
        SiteNoiseProfile& destination = adapted.sites[index];
        destination.shotScale = source.shotScale;
        destination.offsetVariance = source.offsetVariance;
        destination.quantizationVariance = source.quantizationVariance;
        destination.residualModelTau0 = source.residualModelTau0;
        destination.residualModelTau1 = source.residualModelTau1;
        destination.quantizationIncluded = source.quantizationIncluded;
    }
    if (!ValidateNoiseModelReference(adapted, error)) return false;
    result = std::move(adapted);
    return true;
}

} // namespace Raw::MultiFrame
