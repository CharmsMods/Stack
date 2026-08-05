#include "Raw/MultiFrameDenoise/Reliability.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Raw::Mfd {
namespace {

constexpr std::array<CfaSite, 4> kSites {
    CfaSite::Red,
    CfaSite::Green0,
    CfaSite::Green1,
    CfaSite::Blue
};

bool Finite(double value) {
    return std::isfinite(value);
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

double Clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

double Smootherstep5(double value) {
    const double x = Clamp01(value);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

std::size_t CellIndex(PixelExtent extent, std::uint32_t x, std::uint32_t y) {
    return static_cast<std::size_t>(
        static_cast<std::uint64_t>(y) * extent.width + x);
}

std::size_t EvidenceIndex(
    PixelExtent extent,
    std::uint32_t x,
    std::uint32_t y,
    std::size_t siteIndex) {
    return 4u * CellIndex(extent, x, y) + siteIndex;
}

double Median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values[middle];
    if ((values.size() & 1u) != 0u) return upper;
    std::nth_element(values.begin(), values.begin() + middle - 1u, values.end());
    return 0.5 * (values[middle - 1u] + upper);
}

struct CachedResidualEvidence {
    bool motionValid = false;
    bool sampleValid = false;
    bool hardValid = false;
    double standardizedResidual = 0.0;
};

bool ValidateReliabilityParameters(const Parameters& parameters) {
    std::string error;
    return ValidateParameters(parameters, &error);
}

bool ValidStorageFormat(ReliabilityStorageFormat format) {
    return format == ReliabilityStorageFormat::Uint16Unorm ||
        format == ReliabilityStorageFormat::Float32;
}

} // namespace

double FlatTopQuinticGate(
    double magnitude,
    double fullWeightThreshold,
    double zeroWeightThreshold) {
    if (!Finite(magnitude) || !Finite(fullWeightThreshold) ||
        !Finite(zeroWeightThreshold) || fullWeightThreshold < 0.0 ||
        zeroWeightThreshold <= fullWeightThreshold) {
        return 0.0;
    }
    const double absolute = std::abs(magnitude);
    if (absolute <= fullWeightThreshold) return 1.0;
    if (absolute >= zeroWeightThreshold) return 0.0;
    return 1.0 - Smootherstep5(
        (absolute - fullWeightThreshold) /
        (zeroWeightThreshold - fullWeightThreshold));
}

double PatchReliabilityGate(
    double patchScale,
    const ReliabilityParameters& parameters) {
    return FlatTopQuinticGate(
        patchScale,
        parameters.patchFullWeightSigma,
        parameters.patchZeroWeightSigma);
}

double RemapReliabilityConfidence(
    double erodedConfidence,
    const ReliabilityParameters& parameters) {
    if (!Finite(erodedConfidence) ||
        !(parameters.reliabilityRemapMax > parameters.reliabilityRemapMin)) {
        return 0.0;
    }
    return Smootherstep5(
        (erodedConfidence - parameters.reliabilityRemapMin) /
        (parameters.reliabilityRemapMax - parameters.reliabilityRemapMin));
}

std::uint64_t MinimumUsableReliabilityCells(
    std::uint64_t totalCellCount,
    const ReliabilityParameters& parameters) {
    if (totalCellCount == 0u) return 0u;
    const long double total = static_cast<long double>(totalCellCount);
    const std::uint64_t maximumFractionCount = static_cast<std::uint64_t>(
        std::ceil(total * parameters.frameUsableMaximumFraction));
    const std::uint64_t minimumFractionCount = static_cast<std::uint64_t>(
        std::ceil(total * parameters.frameUsableMinimumFraction));
    return std::min<std::uint64_t>(
        maximumFractionCount,
        std::max<std::uint64_t>(
            parameters.frameUsableMinimumCells,
            minimumFractionCount));
}

bool BuildReliabilityMap(
    const ReliabilityBuildRequest& request,
    ReliabilityMap& result,
    std::string* error) {
    result = {};
    result.rawExtent = request.rawExtent;
    result.noiseQuality = request.noiseQuality;
    if (request.rawExtent.width < 2u || request.rawExtent.height < 2u ||
        !request.layout.IsValid() || !request.motionGrid ||
        !request.motionGrid->valid || !request.residualEvaluator ||
        !ValidateReliabilityParameters(request.parameters)) {
        result.message = "MFD reliability request is invalid.";
        return Fail(error, result.message);
    }
    result.cellExtent = {
        request.rawExtent.width / 2u,
        request.rawExtent.height / 2u
    };
    if (result.cellExtent.width == 0u || result.cellExtent.height == 0u ||
        result.cellExtent.width > static_cast<std::uint64_t>(
            std::numeric_limits<std::int32_t>::max()) ||
        result.cellExtent.height > static_cast<std::uint64_t>(
            std::numeric_limits<std::int32_t>::max()) ||
        result.cellExtent.width >
            std::numeric_limits<std::uint64_t>::max() /
                result.cellExtent.height) {
        result.message = "MFD reliability cell dimensions are invalid.";
        return Fail(error, result.message);
    }
    const std::uint64_t cellCount64 =
        result.cellExtent.width * result.cellExtent.height;
    if (cellCount64 > std::numeric_limits<std::size_t>::max() ||
        cellCount64 > std::numeric_limits<std::size_t>::max() / 4u) {
        result.message = "MFD reliability allocation would overflow.";
        return Fail(error, result.message);
    }
    const std::size_t cellCount = static_cast<std::size_t>(cellCount64);
    result.cells.resize(cellCount);
    std::vector<CachedResidualEvidence> evidence(cellCount * 4u);
    std::vector<double> alignmentConfidence(cellCount, 0.0);
    std::vector<std::uint8_t> alignmentValid(cellCount, 0u);
    result.noiseConfidence = NoiseModelConfidence(
        request.noiseQuality, request.parameters);
    const std::uint64_t totalProgressRows =
        result.cellExtent.height * 3u;
    std::uint64_t completedProgressRows = 0u;
    const auto reportProgress = [&]() {
        if (request.reportProgress) {
            request.reportProgress(totalProgressRows == 0u
                ? 1.0
                : static_cast<double>(completedProgressRows) /
                    static_cast<double>(totalProgressRows));
        }
    };
    const auto canceled = [&]() {
        return request.shouldCancel && request.shouldCancel();
    };
    reportProgress();

    for (std::uint32_t cellY = 0u;
         cellY < result.cellExtent.height;
         ++cellY) {
        if (canceled()) {
            result.message = "MFD reliability processing was canceled.";
            return Fail(error, result.message);
        }
        for (std::uint32_t cellX = 0u;
             cellX < result.cellExtent.width;
             ++cellX) {
            const std::size_t cell = CellIndex(result.cellExtent, cellX, cellY);
            ReliabilityCell& diagnostic = result.cells[cell];
            diagnostic.cellX = cellX;
            diagnostic.cellY = cellY;
            LocalMotionFieldSample centerMotion;
            std::string ignored;
            const RawCoordinate centerRaw {
                2.0 * static_cast<double>(cellX) + 0.5,
                2.0 * static_cast<double>(cellY) + 0.5
            };
            if (EvaluateLocalMotionField(
                    *request.motionGrid,
                    centerRaw,
                    request.motionOptions,
                    centerMotion,
                    &ignored)) {
                alignmentValid[cell] = 1u;
                alignmentConfidence[cell] = Clamp01(
                    centerMotion.alignmentConfidence);
            }
            for (std::size_t siteIndex = 0u;
                 siteIndex < kSites.size();
                 ++siteIndex) {
                const CfaSite site = kSites[siteIndex];
                const CfaOffset offset = request.layout.OffsetFor(site);
                const RawCoordinate referenceRaw {
                    2.0 * static_cast<double>(cellX) + offset.x,
                    2.0 * static_cast<double>(cellY) + offset.y
                };
                CachedResidualEvidence& cached = evidence[EvidenceIndex(
                    result.cellExtent, cellX, cellY, siteIndex)];
                LocalMotionFieldSample motion;
                ignored.clear();
                if (!EvaluateLocalMotionField(
                        *request.motionGrid,
                        referenceRaw,
                        request.motionOptions,
                        motion,
                        &ignored)) {
                    continue;
                }
                cached.motionValid = true;
                ReliabilityResidualSample sample;
                if (!request.residualEvaluator(
                        referenceRaw, site, motion, sample)) {
                    continue;
                }
                cached.hardValid = sample.hardValid;
                const double variance = sample.referenceGateVariance +
                    sample.alternateGateVariance +
                    request.parameters.fusion.numericalVarianceFloor;
                if (!sample.valid || !sample.hardValid ||
                    !Finite(sample.referenceValue) ||
                    !Finite(sample.alternateValue) ||
                    !Finite(sample.referenceGateVariance) ||
                    !Finite(sample.alternateGateVariance) ||
                    sample.referenceGateVariance < 0.0 ||
                    sample.alternateGateVariance < 0.0 ||
                    !Finite(variance) || variance <= 0.0) {
                    continue;
                }
                cached.standardizedResidual =
                    (sample.alternateValue - sample.referenceValue) /
                    std::sqrt(variance);
                cached.sampleValid = Finite(cached.standardizedResidual);
            }
        }
        ++completedProgressRows;
        reportProgress();
    }

    const std::int32_t patchRadius = static_cast<std::int32_t>(
        request.parameters.reliability.patchSupportBayerCells / 2u);
    for (std::uint32_t cellY = 0u;
         cellY < result.cellExtent.height;
         ++cellY) {
        if (canceled()) {
            result.message = "MFD reliability processing was canceled.";
            return Fail(error, result.message);
        }
        for (std::uint32_t cellX = 0u;
             cellX < result.cellExtent.width;
             ++cellX) {
            const std::size_t cell = CellIndex(result.cellExtent, cellX, cellY);
            ReliabilityCell& diagnostic = result.cells[cell];
            diagnostic.alignmentConfidence = alignmentConfidence[cell];
            bool centralSamplesHardValid = true;
            bool centralSamplesFinite = true;
            for (std::size_t siteIndex = 0u;
                 siteIndex < kSites.size();
                 ++siteIndex) {
                const CachedResidualEvidence& central = evidence[EvidenceIndex(
                    result.cellExtent, cellX, cellY, siteIndex)];
                centralSamplesHardValid = centralSamplesHardValid &&
                    central.motionValid && central.hardValid;
                centralSamplesFinite = centralSamplesFinite && central.sampleValid;
            }
            if (alignmentValid[cell] == 0u) {
                diagnostic.rejectionBits |= ReliabilityRejectMask(
                    ReliabilityRejectBit::AlignmentInvalid);
            }
            if (!centralSamplesHardValid) {
                diagnostic.rejectionBits |= ReliabilityRejectMask(
                    ReliabilityRejectBit::SampleInvalid);
            } else if (!centralSamplesFinite) {
                diagnostic.rejectionBits |= ReliabilityRejectMask(
                    ReliabilityRejectBit::NonFiniteEvidence);
            }
            if (result.noiseConfidence <= 0.0) {
                diagnostic.rejectionBits |= ReliabilityRejectMask(
                    ReliabilityRejectBit::NoiseUnavailable);
            }

            std::vector<double> absoluteResiduals;
            const std::uint32_t nominalResiduals =
                request.parameters.reliability.patchSupportBayerCells *
                request.parameters.reliability.patchSupportBayerCells * 4u;
            absoluteResiduals.reserve(nominalResiduals);
            for (std::int32_t dy = -patchRadius; dy <= patchRadius; ++dy) {
                const std::int32_t patchY =
                    static_cast<std::int32_t>(cellY) + dy;
                if (patchY < 0 ||
                    patchY >= static_cast<std::int32_t>(result.cellExtent.height)) {
                    continue;
                }
                for (std::int32_t dx = -patchRadius; dx <= patchRadius; ++dx) {
                    const std::int32_t patchX =
                        static_cast<std::int32_t>(cellX) + dx;
                    if (patchX < 0 || patchX >=
                        static_cast<std::int32_t>(result.cellExtent.width)) {
                        continue;
                    }
                    for (std::size_t siteIndex = 0u;
                         siteIndex < kSites.size();
                         ++siteIndex) {
                        const CachedResidualEvidence& cached = evidence[EvidenceIndex(
                            result.cellExtent,
                            static_cast<std::uint32_t>(patchX),
                            static_cast<std::uint32_t>(patchY),
                            siteIndex)];
                        if (cached.sampleValid && cached.hardValid) {
                            absoluteResiduals.push_back(
                                std::abs(cached.standardizedResidual));
                        }
                    }
                }
            }
            diagnostic.validResidualCount = static_cast<std::uint32_t>(
                absoluteResiduals.size());
            if (diagnostic.validResidualCount <
                request.parameters.reliability.patchMinimumValidResiduals) {
                diagnostic.rejectionBits |= ReliabilityRejectMask(
                    ReliabilityRejectBit::InsufficientPatchSupport);
            } else {
                diagnostic.patchScale = Median(std::move(absoluteResiduals)) /
                    kStandardNormalMedianAbsoluteValue;
                diagnostic.patchGate = PatchReliabilityGate(
                    diagnostic.patchScale,
                    request.parameters.reliability);
                if (diagnostic.patchGate <= 0.0) {
                    diagnostic.rejectionBits |= ReliabilityRejectMask(
                        ReliabilityRejectBit::PatchRejected);
                }
            }
            const bool hardValid = alignmentValid[cell] != 0u &&
                centralSamplesHardValid && centralSamplesFinite &&
                diagnostic.validResidualCount >=
                    request.parameters.reliability.patchMinimumValidResiduals &&
                result.noiseConfidence > 0.0;
            diagnostic.rawConfidence = hardValid
                ? Clamp01(result.noiseConfidence *
                    diagnostic.alignmentConfidence * diagnostic.patchGate)
                : 0.0;
        }
        ++completedProgressRows;
        reportProgress();
    }

    const std::int32_t erosionRadius = static_cast<std::int32_t>(
        request.parameters.reliability.minimumFilterRadiusCells);
    for (std::uint32_t cellY = 0u;
         cellY < result.cellExtent.height;
         ++cellY) {
        if (canceled()) {
            result.message = "MFD reliability processing was canceled.";
            return Fail(error, result.message);
        }
        for (std::uint32_t cellX = 0u;
             cellX < result.cellExtent.width;
             ++cellX) {
            double minimum = 1.0;
            for (std::int32_t dy = -erosionRadius;
                 dy <= erosionRadius;
                 ++dy) {
                for (std::int32_t dx = -erosionRadius;
                     dx <= erosionRadius;
                     ++dx) {
                    const std::int32_t neighborX =
                        static_cast<std::int32_t>(cellX) + dx;
                    const std::int32_t neighborY =
                        static_cast<std::int32_t>(cellY) + dy;
                    if (neighborX < 0 || neighborY < 0 ||
                        neighborX >= static_cast<std::int32_t>(
                            result.cellExtent.width) ||
                        neighborY >= static_cast<std::int32_t>(
                            result.cellExtent.height)) {
                        minimum = 0.0;
                        continue;
                    }
                    minimum = std::min(
                        minimum,
                        result.cells[CellIndex(
                            result.cellExtent,
                            static_cast<std::uint32_t>(neighborX),
                            static_cast<std::uint32_t>(neighborY))].rawConfidence);
                }
            }
            ReliabilityCell& diagnostic = result.cells[CellIndex(
                result.cellExtent, cellX, cellY)];
            diagnostic.erodedConfidence = Clamp01(minimum);
            diagnostic.reliability = RemapReliabilityConfidence(
                diagnostic.erodedConfidence,
                request.parameters.reliability);
        }
        ++completedProgressRows;
        reportProgress();
    }

    result.minimumUsableCellCount = MinimumUsableReliabilityCells(
        cellCount64, request.parameters.reliability);
    result.usableCellCount = static_cast<std::uint64_t>(std::count_if(
        result.cells.begin(), result.cells.end(),
        [&](const ReliabilityCell& cell) {
            // Frame admission answers whether enough of the alternate has
            // trustworthy spatial evidence. Noise-model quality is retained
            // in cell.reliability so it still reduces fusion weight, but it
            // must not make a valid low-confidence model mathematically
            // incapable of reaching the fusion stage.
            if (result.noiseConfidence <= 0.0) return false;
            const double spatialConfidence = Clamp01(
                cell.erodedConfidence / result.noiseConfidence);
            const double spatialReliability = RemapReliabilityConfidence(
                spatialConfidence,
                request.parameters.reliability);
            return spatialReliability > request.parameters.reliability.
                frameUsableReliabilityThreshold;
        }));
    result.frameUsable = result.usableCellCount >=
        result.minimumUsableCellCount;
    result.valid = true;
    result.message = result.frameUsable
        ? "MFD reliability map is usable."
        : "MFD reliability map has insufficient spatially usable area.";
    return true;
}

std::uint16_t QuantizeReliabilityUnorm16(double reliability) {
    if (!Finite(reliability)) return 0u;
    const double scaled = Clamp01(reliability) * 65535.0;
    return static_cast<std::uint16_t>(std::floor(scaled + 0.5));
}

double DecodeReliabilityUnorm16(std::uint16_t reliability) {
    return static_cast<double>(reliability) / 65535.0;
}

bool BuildReliabilityStore(
    const ReliabilityMap& map,
    ReliabilityStorageFormat format,
    std::uint32_t tileCells,
    ReliabilityStore& store,
    std::string* error) {
    store = {};
    if (!map.valid || map.cellExtent.width == 0u ||
        map.cellExtent.height == 0u || tileCells == 0u ||
        !ValidStorageFormat(format) ||
        map.cellExtent.width > std::numeric_limits<std::uint32_t>::max() ||
        map.cellExtent.height > std::numeric_limits<std::uint32_t>::max() ||
        map.cellExtent.width >
            std::numeric_limits<std::uint64_t>::max() /
                map.cellExtent.height) {
        return Fail(error, "MFD reliability store request is invalid.");
    }
    const std::uint64_t cellCount =
        map.cellExtent.width * map.cellExtent.height;
    if (cellCount > std::numeric_limits<std::size_t>::max() ||
        map.cells.size() != static_cast<std::size_t>(cellCount)) {
        return Fail(error, "MFD reliability store cell count is invalid.");
    }
    store.format = format;
    store.cellExtent = map.cellExtent;
    store.tileCells = tileCells;
    store.tilesX = static_cast<std::uint32_t>(
        (map.cellExtent.width + tileCells - 1u) / tileCells);
    store.tilesY = static_cast<std::uint32_t>(
        (map.cellExtent.height + tileCells - 1u) / tileCells);
    store.tiles.reserve(static_cast<std::size_t>(
        static_cast<std::uint64_t>(store.tilesX) * store.tilesY));
    for (std::uint32_t tileY = 0u; tileY < store.tilesY; ++tileY) {
        for (std::uint32_t tileX = 0u; tileX < store.tilesX; ++tileX) {
            ReliabilityTile tile;
            tile.tileX = tileX;
            tile.tileY = tileY;
            tile.originCellX = tileX * tileCells;
            tile.originCellY = tileY * tileCells;
            tile.extent = {
                std::min<std::uint64_t>(
                    tileCells, map.cellExtent.width - tile.originCellX),
                std::min<std::uint64_t>(
                    tileCells, map.cellExtent.height - tile.originCellY)
            };
            const std::size_t tileCount = static_cast<std::size_t>(
                tile.extent.width * tile.extent.height);
            tile.rejectionBits.resize(tileCount);
            if (format == ReliabilityStorageFormat::Uint16Unorm) {
                tile.uint16Values.resize(tileCount);
            } else {
                tile.floatValues.resize(tileCount);
            }
            for (std::uint32_t y = 0u; y < tile.extent.height; ++y) {
                for (std::uint32_t x = 0u; x < tile.extent.width; ++x) {
                    const ReliabilityCell& cell = map.cells[CellIndex(
                        map.cellExtent,
                        tile.originCellX + x,
                        tile.originCellY + y)];
                    const std::size_t index = static_cast<std::size_t>(
                        static_cast<std::uint64_t>(y) * tile.extent.width + x);
                    tile.rejectionBits[index] = cell.rejectionBits;
                    if (format == ReliabilityStorageFormat::Uint16Unorm) {
                        tile.uint16Values[index] = QuantizeReliabilityUnorm16(
                            cell.reliability);
                    } else {
                        tile.floatValues[index] = static_cast<float>(
                            Clamp01(cell.reliability));
                    }
                }
            }
            store.tiles.push_back(std::move(tile));
        }
    }
    return true;
}

bool ReadReliabilityStoreCell(
    const ReliabilityStore& store,
    std::uint32_t cellX,
    std::uint32_t cellY,
    double& reliability,
    std::uint16_t* rejectionBits,
    std::string* error) {
    reliability = 0.0;
    if (store.contractVersion != kReliabilityContractVersion ||
        store.contractId != kReliabilityContractId ||
        !ValidStorageFormat(store.format) || store.tileCells == 0u ||
        store.tilesX == 0u || store.tilesY == 0u ||
        cellX >= store.cellExtent.width || cellY >= store.cellExtent.height) {
        return Fail(error, "MFD reliability store coordinate is invalid.");
    }
    const std::uint32_t tileX = cellX / store.tileCells;
    const std::uint32_t tileY = cellY / store.tileCells;
    const std::size_t tileIndex = static_cast<std::size_t>(
        static_cast<std::uint64_t>(tileY) * store.tilesX + tileX);
    if (tileIndex >= store.tiles.size()) {
        return Fail(error, "MFD reliability store tile is missing.");
    }
    const ReliabilityTile& tile = store.tiles[tileIndex];
    const std::uint64_t expectedOriginX =
        static_cast<std::uint64_t>(tileX) * store.tileCells;
    const std::uint64_t expectedOriginY =
        static_cast<std::uint64_t>(tileY) * store.tileCells;
    if (tile.tileX != tileX || tile.tileY != tileY ||
        expectedOriginX != tile.originCellX ||
        expectedOriginY != tile.originCellY) {
        return Fail(error, "MFD reliability store tile metadata is malformed.");
    }
    const std::uint32_t localX = cellX - tile.originCellX;
    const std::uint32_t localY = cellY - tile.originCellY;
    if (localX >= tile.extent.width || localY >= tile.extent.height) {
        return Fail(error, "MFD reliability store tile coordinate is invalid.");
    }
    const std::size_t index = static_cast<std::size_t>(
        static_cast<std::uint64_t>(localY) * tile.extent.width + localX);
    if (index >= tile.rejectionBits.size()) {
        return Fail(error, "MFD reliability rejection tile is malformed.");
    }
    if (rejectionBits) *rejectionBits = tile.rejectionBits[index];
    if (store.format == ReliabilityStorageFormat::Uint16Unorm) {
        if (index >= tile.uint16Values.size()) {
            return Fail(error, "MFD uint16 reliability tile is malformed.");
        }
        reliability = DecodeReliabilityUnorm16(tile.uint16Values[index]);
    } else {
        if (index >= tile.floatValues.size() ||
            !std::isfinite(tile.floatValues[index])) {
            return Fail(error, "MFD float reliability tile is malformed.");
        }
        reliability = Clamp01(tile.floatValues[index]);
    }
    return true;
}

const char* CandidateGateFailureName(CandidateGateFailure failure) {
    switch (failure) {
        case CandidateGateFailure::None: return "none";
        case CandidateGateFailure::HardInvalid: return "hard-invalid";
        case CandidateGateFailure::NoiseUnavailable: return "noise-unavailable";
        case CandidateGateFailure::InvalidNumericInput: return "invalid-numeric-input";
        case CandidateGateFailure::ReliabilityZero: return "reliability-zero";
        case CandidateGateFailure::PixelOutlier: return "pixel-outlier";
        case CandidateGateFailure::AbsoluteSafetyFailure: return "absolute-safety-failure";
    }
    return "invalid-numeric-input";
}

bool EvaluateCandidateGate(
    const CandidateGateInput& input,
    const Parameters& parameters,
    CandidateGateResult& result,
    std::string* error) {
    result = {};
    if (!input.hardValid) {
        result.failure = CandidateGateFailure::HardInvalid;
        return Fail(error, "MFD candidate is hard-invalid.");
    }
    if (input.noiseQuality == NoiseModelQuality::Unavailable) {
        result.failure = CandidateGateFailure::NoiseUnavailable;
        return Fail(error, "MFD candidate has no usable noise model.");
    }
    if (!Finite(input.reliability) || input.reliability < 0.0 ||
        input.reliability > 1.0 || !Finite(input.referenceValue) ||
        !Finite(input.alternateValue) ||
        !Finite(input.referenceGateVariance) ||
        !Finite(input.alternateGateVariance) ||
        !Finite(input.referenceDarkVariance) ||
        !Finite(input.alternateDarkVariance) ||
        !Finite(input.referenceDnStep) || !Finite(input.alternateDnStep) ||
        input.referenceGateVariance < 0.0 ||
        input.alternateGateVariance < 0.0 ||
        input.referenceDarkVariance < 0.0 ||
        input.alternateDarkVariance < 0.0 ||
        input.referenceDnStep < 0.0 || input.alternateDnStep < 0.0) {
        result.failure = CandidateGateFailure::InvalidNumericInput;
        return Fail(error, "MFD candidate gate input is invalid.");
    }
    if (input.reliability <= 0.0) {
        result.failure = CandidateGateFailure::ReliabilityZero;
        return Fail(error, "MFD candidate reliability is zero.");
    }
    result.difference = input.alternateValue - input.referenceValue;
    const double variance = input.referenceGateVariance +
        input.alternateGateVariance + parameters.fusion.numericalVarianceFloor;
    if (!Finite(variance) || variance <= 0.0) {
        result.failure = CandidateGateFailure::InvalidNumericInput;
        return Fail(error, "MFD candidate gate variance is invalid.");
    }
    result.standardizedResidual = result.difference / std::sqrt(variance);
    const bool trusted = input.noiseQuality == NoiseModelQuality::TrustedMetadata ||
        input.noiseQuality == NoiseModelQuality::CalibratedCamera;
    const double inner = trusted
        ? parameters.reliability.trustedPixelFullWeightSigma
        : parameters.reliability.lowConfidencePixelFullWeightSigma;
    const double outer = trusted
        ? parameters.reliability.trustedPixelZeroWeightSigma
        : parameters.reliability.lowConfidencePixelZeroWeightSigma;
    result.pixelGate = FlatTopQuinticGate(
        result.standardizedResidual, inner, outer);
    if (result.pixelGate <= 0.0) {
        result.failure = CandidateGateFailure::PixelOutlier;
        return Fail(error, "MFD candidate failed the per-pixel redescending gate.");
    }

    result.absoluteSafetyApplied = !trusted;
    result.absoluteSafetyPassed = true;
    if (result.absoluteSafetyApplied) {
        const double dnSum =
            input.referenceDnStep + input.alternateDnStep;
        const double darkVariance =
            input.referenceDarkVariance + input.alternateDarkVariance;
        if (!Finite(dnSum) || !Finite(darkVariance) || darkVariance < 0.0) {
            result.failure = CandidateGateFailure::InvalidNumericInput;
            return Fail(error, "MFD candidate absolute safety inputs overflowed.");
        }
        const double absoluteTerm = std::max(
            parameters.reliability.absoluteSafetyDnMultiplier *
                dnSum,
            parameters.reliability.absoluteSafetyNoiseSigmaMultiplier *
                std::sqrt(darkVariance));
        result.absoluteSafetyLimit = absoluteTerm +
            parameters.reliability.absoluteSafetyRelativeFraction *
                std::max(
                    std::abs(input.referenceValue),
                    std::abs(input.alternateValue));
        if (!Finite(absoluteTerm) || !Finite(result.absoluteSafetyLimit)) {
            result.failure = CandidateGateFailure::InvalidNumericInput;
            return Fail(error, "MFD candidate absolute safety limit is invalid.");
        }
        result.absoluteSafetyPassed =
            std::abs(result.difference) <= result.absoluteSafetyLimit;
        if (!result.absoluteSafetyPassed) {
            result.failure = CandidateGateFailure::AbsoluteSafetyFailure;
            return Fail(error, "MFD candidate failed the low-confidence absolute safety gate.");
        }
    }
    result.gate = Clamp01(input.reliability * result.pixelGate);
    result.valid = result.gate > 0.0;
    result.failure = result.valid
        ? CandidateGateFailure::None
        : CandidateGateFailure::ReliabilityZero;
    if (!result.valid) return Fail(error, "MFD candidate final gate is zero.");
    return true;
}

} // namespace Raw::Mfd
