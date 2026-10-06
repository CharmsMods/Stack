#include "Raw/MultiFrame/MeasurementHandle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Raw::MultiFrame {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

std::size_t PixelCount(const PixelExtent& extent) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return 0u;
    }
    return static_cast<std::size_t>(extent.width * extent.height);
}

} // namespace

const char* MeasurementDomainName(MeasurementDomain domain) {
    switch (domain) {
    case MeasurementDomain::SensorCfa: return "sensor-cfa";
    case MeasurementDomain::VirtualCfa: return "virtual-cfa";
    case MeasurementDomain::SceneLinearCfa: return "scene-linear-cfa";
    case MeasurementDomain::SceneLinearRgb: return "scene-linear-rgb";
    }
    return "sensor-cfa";
}

bool ValidateEvidenceSignature(
    const EvidenceSignature& signature,
    std::string* error) {
    if (signature.schemaVersion != kEvidenceSignatureVersion) {
        return Fail(error, "Evidence signature version is unsupported.");
    }
    const std::size_t tileCount =
        static_cast<std::size_t>(signature.tileColumns) * signature.tileRows;
    std::unordered_set<std::string> frameIds;
    for (const EvidenceContribution& contribution : signature.contributions) {
        if (contribution.originalFrameId.empty() ||
            !frameIds.insert(contribution.originalFrameId).second) {
            return Fail(error,
                "Evidence contributions require unique original frame IDs.");
        }
        if (!std::isfinite(contribution.globalCoefficient) ||
            contribution.globalCoefficient < 0.0) {
            return Fail(error,
                "Evidence contribution coefficients must be finite and nonnegative.");
        }
        if (!contribution.tileCoefficients.empty() &&
            contribution.tileCoefficients.size() != tileCount) {
            return Fail(error,
                "Spatial evidence coefficients do not match their tile grid.");
        }
        if (!std::all_of(
                contribution.tileCoefficients.begin(),
                contribution.tileCoefficients.end(),
                [](float value) { return std::isfinite(value) && value >= 0.0f; })) {
            return Fail(error,
                "Spatial evidence coefficients must be finite and nonnegative.");
        }
    }
    if (error) error->clear();
    return true;
}

EvidenceOverlap EvaluateEvidenceOverlap(
    const EvidenceSignature& a,
    const EvidenceSignature& b) {
    std::unordered_map<std::string, double> aWeights;
    double aNorm = 0.0;
    double bNorm = 0.0;
    for (const EvidenceContribution& contribution : a.contributions) {
        aWeights[contribution.originalFrameId] = contribution.globalCoefficient;
        aNorm += contribution.globalCoefficient * contribution.globalCoefficient;
    }
    EvidenceOverlap result;
    double dot = 0.0;
    for (const EvidenceContribution& contribution : b.contributions) {
        bNorm += contribution.globalCoefficient * contribution.globalCoefficient;
        const auto found = aWeights.find(contribution.originalFrameId);
        if (found != aWeights.end()) {
            result.sharedOriginalFrameIds.push_back(contribution.originalFrameId);
            dot += found->second * contribution.globalCoefficient;
        }
    }
    result.overlaps = !result.sharedOriginalFrameIds.empty();
    if (aNorm > 0.0 && bNorm > 0.0) {
        result.normalizedGlobalOverlap = std::clamp(
            dot / std::sqrt(aNorm * bNorm), 0.0, 1.0);
    }
    return result;
}

bool ValidateRawMeasurementHandle(
    const RawMeasurementHandle& handle,
    std::string* error) {
    if (handle.contractVersion != kRawMeasurementHandleVersion ||
        handle.contractId != kRawMeasurementHandleContractId) {
        return Fail(error, "RAW measurement handle identity is unsupported.");
    }
    if (handle.contentHash.empty() || handle.producerNodeId.empty() ||
        handle.producerAlgorithmId.empty() ||
        handle.producerAlgorithmVersion == 0u) {
        return Fail(error, "RAW measurement producer identity is incomplete.");
    }
    if (!std::isfinite(handle.radiometricScaleToAnchor) ||
        handle.radiometricScaleToAnchor <= 0.0 ||
        handle.radiometricAnchorId.empty()) {
        return Fail(error, "RAW measurement radiometric anchor is invalid.");
    }
    std::string domainError;
    if (handle.domain != MeasurementDomain::SceneLinearRgb &&
        !ValidateCfaDomainIdentity(handle.cfa, &domainError)) {
        return Fail(error, "RAW measurement CFA identity is invalid: " + domainError);
    }
    const std::size_t pixels = PixelCount(handle.planes.extent);
    if (pixels == 0u || !handle.planes.mosaic ||
        handle.planes.mosaic->size() != pixels) {
        return Fail(error, "RAW measurement mosaic extent or storage is invalid.");
    }
    if (handle.planes.variance && handle.planes.variance->size() != pixels) {
        return Fail(error, "RAW measurement variance sidecar size is invalid.");
    }
    if (handle.planes.effectiveSupport &&
        handle.planes.effectiveSupport->size() != pixels) {
        return Fail(error, "RAW measurement support sidecar size is invalid.");
    }
    if (handle.planes.validity && handle.planes.validity->size() != pixels) {
        return Fail(error, "RAW measurement validity sidecar size is invalid.");
    }
    if (handle.planes.clipping && handle.planes.clipping->size() != pixels) {
        return Fail(error, "RAW measurement clipping sidecar size is invalid.");
    }
    if (!ValidateEvidenceSignature(handle.evidence, error)) return false;
    if (error) error->clear();
    return true;
}

bool ValidateRawMeasurementSetHandle(
    const RawMeasurementSetHandle& handle,
    std::string* error) {
    if (handle.measurements.empty()) {
        return Fail(error, "RAW measurement set is empty.");
    }
    for (const auto& measurement : handle.measurements) {
        if (!measurement || !ValidateRawMeasurementHandle(*measurement, error)) {
            if (error && error->empty()) {
                *error = "RAW measurement set contains an invalid handle.";
            }
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool ValidateFusionMeasurementCompatibility(const RawMeasurementSetHandle& inputs, std::string* error) {
    const RawMeasurementHandle* first = nullptr;
    std::string geometry;
    for (const auto& input : inputs.measurements) {
        if (!input) return Fail(error, "Fusion input measurement is missing.");
        if (!input->geometryId.empty()) {
            if (!geometry.empty() && geometry != input->geometryId)
                return Fail(error, "Fusion inputs have different geometry IDs. Enable registration or use one common identity geometry before fusion.");
            geometry = input->geometryId;
        }
        if (!first) { first = input.get(); continue; }
        if (input->planes.extent.width != first->planes.extent.width ||
            input->planes.extent.height != first->planes.extent.height ||
            (input->domain == MeasurementDomain::SceneLinearRgb) != (first->domain == MeasurementDomain::SceneLinearRgb) ||
            input->cfa.activePattern != first->cfa.activePattern)
            return Fail(error, "Fusion inputs need one sample layout and matching dimensions.");
    }
    if (error) error->clear();
    return true;
}

bool RequiresCovarianceAwareFusion(
    const RawMeasurementSetHandle& inputs,
    EvidenceOverlap* strongestOverlap) {
    EvidenceOverlap strongest;
    for (std::size_t a = 0; a < inputs.measurements.size(); ++a) {
        if (!inputs.measurements[a]) continue;
        for (std::size_t b = a + 1u; b < inputs.measurements.size(); ++b) {
            if (!inputs.measurements[b]) continue;
            EvidenceOverlap overlap = EvaluateEvidenceOverlap(
                inputs.measurements[a]->evidence,
                inputs.measurements[b]->evidence);
            if (overlap.overlaps &&
                (!strongest.overlaps ||
                 overlap.normalizedGlobalOverlap > strongest.normalizedGlobalOverlap)) {
                strongest = std::move(overlap);
            }
        }
    }
    if (strongestOverlap) *strongestOverlap = strongest;
    return strongest.overlaps;
}

} // namespace Raw::MultiFrame
