#include "Raw/MultiFrameDenoise/Processor.h"

#include "Raw/MultiFrameDenoise/Preparation.h"
#include "Raw/RawLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

constexpr std::array<CfaSite, 4> kSites {
    CfaSite::Red,
    CfaSite::Green0,
    CfaSite::Green1,
    CfaSite::Blue
};

NoiseResolutionOptions MakeProcessorNoiseResolutionOptions() {
    NoiseResolutionOptions options;

    // A still-mosaiced frame can be too dark or too uniform for the
    // reference-frame signal-range fit even though it is otherwise a valid
    // MFD input. Keep the resolver's default fail-closed contract unchanged,
    // but explicitly opt the real processor into its final, conservative
    // fallback. GenericLowConfidence is already subject to the strictest
    // reliability gates and alternate-weight caps in the fusion pipeline.
    options.enableGenericLowConfidence = true;
    SiteNoiseProfile generic;
    generic.shotScale = 0.005;
    generic.offsetVariance = 0.000030;
    generic.quantizationIncluded = false;
    options.genericLowConfidenceSites.fill(generic);
    return options;
}

std::size_t SiteIndex(CfaSite site) {
    return static_cast<std::size_t>(site);
}

bool Finite(double value) {
    return std::isfinite(value);
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    count = 0u;
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::uint64_t>::max() /
            extent.height) {
        return false;
    }
    const std::uint64_t samples = extent.width * extent.height;
    if (samples > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(samples);
    return true;
}

std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

std::uint64_t SaturatingMultiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0u && right >
        std::numeric_limits<std::uint64_t>::max() / left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

bool IsCanceled(const MfdProcessingRequest& request) {
    return request.shouldCancel && request.shouldCancel();
}

void ReportProgress(
    const MfdProcessingRequest& request,
    MfdProcessingStage stage,
    double overallFraction,
    double stageFraction,
    std::string message,
    std::uint64_t completedUnits = 0u,
    std::uint64_t totalUnits = 0u,
    std::uint32_t frameOrdinal = 0u) {
    if (!request.reportProgress) return;
    MfdProcessingProgress progress;
    progress.stage = stage;
    progress.overallFraction = std::clamp(overallFraction, 0.0, 1.0);
    progress.stageFraction = std::clamp(stageFraction, 0.0, 1.0);
    progress.completedUnits = completedUnits;
    progress.totalUnits = totalUnits;
    progress.frameOrdinal = frameOrdinal;
    progress.frameCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(
            request.frames.size(),
            std::numeric_limits<std::uint32_t>::max()));
    progress.message = std::move(message);
    request.reportProgress(progress);
}

bool HardInvalid(std::uint8_t flags) {
    return HasSampleFlag(flags, PreparedSampleFlag::Saturated) ||
        HasSampleFlag(flags, PreparedSampleFlag::Defective) ||
        HasSampleFlag(flags, PreparedSampleFlag::DecoderRepaired) ||
        HasSampleFlag(flags, PreparedSampleFlag::ExplicitDecoderClip);
}

struct MaterializedFrame {
    MfdProcessingFrameInput input;
    RawMetadata metadata;
    PreparedRawFrame prepared;
    NoiseModel noise;
    CfaLayout layout;
    std::vector<float> normalized;
    std::vector<float> comparisonGain;
    std::vector<std::uint8_t> sampleFlags;
    CfaPlanePyramid pyramid;
};

struct AcceptedAlternate {
    MaterializedFrame frame;
    GlobalFrameAlignmentDecision alignment;
    BidirectionalLocalMotionResult motion;
    ReliabilityMap reliability;
};

bool DefaultLoadRawFrame(
    const std::filesystem::path& sourcePath,
    RawImageData& frame,
    const std::function<bool()>& shouldCancel,
    std::string& error) {
    if (!RawLoader::LoadFile(sourcePath.u8string(), frame, shouldCancel)) {
        error = frame.metadata.error.empty()
            ? "The RAW frame could not be decoded."
            : frame.metadata.error;
        return false;
    }
    error.clear();
    return true;
}

bool VerifyDecodedIdentity(
    const MfdProcessingFrameInput& input,
    const RawImageData& raw,
    std::string& error) {
    if (!LooksLikeSha256(input.expectedSourceSha256) ||
        input.expectedSourceByteLength == 0u ||
        raw.metadata.sourceContentSha256 != input.expectedSourceSha256 ||
        raw.metadata.sourceByteSize != input.expectedSourceByteLength ||
        raw.metadata.visibleWidth <= 0 || raw.metadata.visibleHeight <= 0 ||
        static_cast<std::uint64_t>(raw.metadata.visibleWidth) !=
            input.expectedVisibleExtent.width ||
        static_cast<std::uint64_t>(raw.metadata.visibleHeight) !=
            input.expectedVisibleExtent.height) {
        error = "Decoded RAW identity does not match the locked corpus frame.";
        return false;
    }
    return true;
}

bool PrepareFrame(
    const MfdProcessingFrameInput& input,
    const Parameters& parameters,
    const MfdProcessingServices& services,
    NormalizedTileCache& cache,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& reportProgress,
    MaterializedFrame& frame,
    std::string& error) {
    frame = {};
    frame.input = input;
    if (reportProgress) reportProgress(0.0);
    RawImageData raw;
    if (!services.loadRawFrame ||
        !services.loadRawFrame(input.sourcePath, raw, shouldCancel, error)) {
        return false;
    }
    if (reportProgress) reportProgress(0.1);
    if (!VerifyDecodedIdentity(input, raw, error)) return false;
    frame.metadata = raw.metadata;

    PreparationOptions preparation = MakePreparationOptions(parameters);
    preparation.shouldCancel = shouldCancel;
    preparation.reportProgress = [reportProgress](
        std::uint64_t completed,
        std::uint64_t total) {
        if (reportProgress) {
            const double fraction = total == 0u
                ? 1.0
                : static_cast<double>(completed) /
                    static_cast<double>(total);
            reportProgress(0.1 + 0.35 * fraction);
        }
    };
    PreparationResult prepared = PrepareRawFrame(raw, preparation, cache);
    if (!prepared.success) {
        error = prepared.message;
        return false;
    }

    const NoiseResolutionOptions noiseOptions =
        MakeProcessorNoiseResolutionOptions();
    NoiseResolutionResult noise = ResolveNoiseModel(
        raw.metadata, prepared.frame, &cache, noiseOptions);
    if (noise.resolved) {
        ApplyNoiseModelToPreparationOptions(noise.model, preparation);
        preparation.reportProgress = [reportProgress](
            std::uint64_t completed,
            std::uint64_t total) {
            if (reportProgress) {
                const double fraction = total == 0u
                    ? 1.0
                    : static_cast<double>(completed) /
                        static_cast<double>(total);
                reportProgress(0.45 + 0.55 * fraction);
            }
        };
        prepared = PrepareRawFrame(raw, preparation, cache);
        if (!prepared.success) {
            error = prepared.message;
            return false;
        }
        noise = ResolveNoiseModel(
            raw.metadata, prepared.frame, &cache, noiseOptions);
    } else if (reportProgress) {
        reportProgress(1.0);
    }

    frame.prepared = std::move(prepared.frame);
    frame.noise = std::move(noise.model);
    if (!CfaLayout::TryCreate(frame.prepared.activeCfaPattern, frame.layout)) {
        error = "The prepared active-area CFA layout is unsupported.";
        return false;
    }
    error.clear();
    if (reportProgress) reportProgress(1.0);
    return true;
}

bool MaterializePreparedFrame(
    MaterializedFrame& frame,
    NormalizedTileCache& cache,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& reportProgress,
    std::string& error) {
    std::size_t sampleCount = 0u;
    if (!CheckedSampleCount(frame.prepared.activeExtent, sampleCount)) {
        error = "The prepared frame dimensions overflow the processor.";
        return false;
    }
    try {
        frame.normalized.assign(sampleCount, 0.0f);
        frame.comparisonGain.assign(sampleCount, 1.0f);
        frame.sampleFlags.assign(sampleCount, 0u);
    } catch (const std::bad_alloc&) {
        error = "The normalized frame could not fit the memory budget.";
        return false;
    }

    const std::uint64_t totalTiles =
        static_cast<std::uint64_t>(frame.prepared.tileRows) *
        static_cast<std::uint64_t>(frame.prepared.tileColumns);
    std::uint64_t completedTiles = 0u;
    if (reportProgress) reportProgress(0.0);
    for (std::uint32_t tileY = 0u;
         tileY < frame.prepared.tileRows;
         ++tileY) {
        for (std::uint32_t tileX = 0u;
             tileX < frame.prepared.tileColumns;
             ++tileX) {
            if (shouldCancel && shouldCancel()) {
                error = "MFD frame materialization was canceled.";
                return false;
            }
            PreparedRawTile tile;
            std::string tileError;
            if (ReadPreparedTile(
                    frame.prepared,
                    cache,
                    tileX,
                    tileY,
                    tile,
                    &tileError) != TileCacheReadStatus::Hit) {
                error = tileError.empty()
                    ? "A prepared RAW tile is unavailable."
                    : tileError;
                return false;
            }
            for (std::uint64_t row = 0u; row < tile.extent.height; ++row) {
                const std::size_t source = static_cast<std::size_t>(
                    row * tile.extent.width);
                const std::size_t destination = static_cast<std::size_t>(
                    (tile.originY + row) * frame.prepared.activeExtent.width +
                    tile.originX);
                const std::size_t width = static_cast<std::size_t>(
                    tile.extent.width);
                std::copy_n(
                    tile.normalizedMosaic.begin() + source,
                    width,
                    frame.normalized.begin() + destination);
                std::copy_n(
                    tile.comparisonGain.begin() + source,
                    width,
                    frame.comparisonGain.begin() + destination);
                std::copy_n(
                    tile.sampleFlags.begin() + source,
                    width,
                    frame.sampleFlags.begin() + destination);
            }
            ++completedTiles;
            if (reportProgress) {
                reportProgress(totalTiles == 0u
                    ? 1.0
                    : static_cast<double>(completedTiles) /
                        static_cast<double>(totalTiles));
            }
        }
    }
    return true;
}

bool ExactVariance(
    const MaterializedFrame& frame,
    std::uint64_t rawX,
    std::uint64_t rawY,
    EffectiveVariance& effective,
    double& effectiveDnStep,
    std::string* error = nullptr) {
    if (rawX >= frame.prepared.activeExtent.width ||
        rawY >= frame.prepared.activeExtent.height ||
        frame.noise.quality == NoiseModelQuality::Unavailable) {
        if (error) *error = "The exact RAW variance request is invalid.";
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(
        rawY * frame.prepared.activeExtent.width + rawX);
    const CfaSite site = frame.layout.SiteAt(
        static_cast<std::int64_t>(rawX),
        static_cast<std::int64_t>(rawY));
    const std::size_t siteIndex = SiteIndex(site);
    const double gain = frame.comparisonGain[index];
    const double comparison = gain * frame.normalized[index];
    GainPropagatedVariance propagated;
    std::string varianceError;
    if (!PropagateKnownGainVariance(
            frame.noise.sites[siteIndex],
            gain,
            NonnegativeShotPilot(comparison),
            frame.prepared.calibration.usableSpanByCfaSite[siteIndex],
            propagated,
            &varianceError) ||
        !ComposeEffectiveVariance(
            propagated.variance,
            0.0,
            ResidualModelVariance(
                frame.noise.sites[siteIndex], comparison),
            0.0,
            propagated.darkVariance,
            propagated.effectiveDnStep * propagated.effectiveDnStep,
            1.0e-12,
            effective,
            &varianceError)) {
        if (error) *error = varianceError;
        return false;
    }
    effectiveDnStep = propagated.effectiveDnStep;
    return true;
}

bool SampleMaterializedSameCfa(
    const MaterializedFrame& frame,
    RawCoordinate sourceRaw,
    CfaSite site,
    const SameCfaScalarParameters& parameters,
    SameCfaSampleResult& result,
    std::string* error = nullptr) {
    result = {};
    result.site = site;
    result.sourceRaw = sourceRaw;
    if (frame.noise.quality == NoiseModelQuality::Unavailable ||
        !Finite(sourceRaw.x) || !Finite(sourceRaw.y)) {
        if (error) *error = "The materialized same-CFA request is invalid.";
        return false;
    }
    const CfaPlaneCoordinate sourcePlane =
        frame.layout.RawToPlane(sourceRaw, site);
    result.sourcePlane = sourcePlane;
    KeysBicubicFootprint footprint;
    std::string localError;
    if (!BuildKeysBicubicFootprint(sourcePlane, footprint, &localError) ||
        !ValidateKeysBicubicFootprint(
            footprint,
            frame.layout.PlaneExtent(site, frame.prepared.activeExtent),
            &localError)) {
        if (error) *error = localError;
        return false;
    }

    std::array<SameCfaTapInput, kSameCfaTapCount> taps;
    for (std::size_t tapIndex = 0u;
         tapIndex < footprint.taps.size();
         ++tapIndex) {
        const RawCoordinate raw = frame.layout.PlanePixelToRaw(
            footprint.taps[tapIndex]);
        if (!Finite(raw.x) || !Finite(raw.y) || raw.x < 0.0 || raw.y < 0.0 ||
            raw.x >= static_cast<double>(frame.prepared.activeExtent.width) ||
            raw.y >= static_cast<double>(frame.prepared.activeExtent.height)) {
            if (error) *error = "The same-CFA footprint leaves the active area.";
            return false;
        }
        const std::uint64_t rawX = static_cast<std::uint64_t>(raw.x);
        const std::uint64_t rawY = static_cast<std::uint64_t>(raw.y);
        const std::size_t packedIndex = static_cast<std::size_t>(
            rawY * frame.prepared.activeExtent.width + rawX);
        taps[tapIndex].normalizedSample = frame.normalized[packedIndex];
        taps[tapIndex].comparisonGain = frame.comparisonGain[packedIndex];
        taps[tapIndex].sampleFlags = frame.sampleFlags[packedIndex];
    }
    if (!EvaluateSameCfaFootprintScalar(
            footprint,
            taps,
            frame.noise.sites[SiteIndex(site)],
            frame.prepared.calibration.usableSpanByCfaSite[SiteIndex(site)],
            parameters,
            result,
            &localError)) {
        if (error) *error = localError;
        return false;
    }
    result.sourceRaw = sourceRaw;
    result.sourcePlane = sourcePlane;
    return true;
}

bool BuildFramePyramid(
    MaterializedFrame& frame,
    const Parameters& parameters,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& reportProgress,
    std::string& error) {
    std::array<CfaPyramidBasePlane, 4> base;
    try {
        for (CfaSite site : kSites) {
            CfaPyramidBasePlane& plane = base[SiteIndex(site)];
            plane.site = site;
            plane.extent = frame.layout.PlaneExtent(
                site, frame.prepared.activeExtent);
            std::size_t count = 0u;
            if (!CheckedSampleCount(plane.extent, count)) {
                error = "A CFA plane dimension overflowed.";
                return false;
            }
            plane.signal.assign(count, 0.0);
            plane.variance.assign(count, 0.0);
            plane.validMask.assign(count, 0u);
        }
    } catch (const std::bad_alloc&) {
        error = "The CFA pyramid base planes could not be allocated.";
        return false;
    }

    const PixelExtent extent = frame.prepared.activeExtent;
    if (reportProgress) reportProgress(0.0);
    for (std::uint64_t rawY = 0u; rawY < extent.height; ++rawY) {
        if (shouldCancel && shouldCancel()) {
            error = "MFD CFA pyramid construction was canceled.";
            return false;
        }
        for (std::uint64_t rawX = 0u; rawX < extent.width; ++rawX) {
            const std::size_t packedIndex = static_cast<std::size_t>(
                rawY * extent.width + rawX);
            const CfaSite site = frame.layout.SiteAt(
                static_cast<std::int64_t>(rawX),
                static_cast<std::int64_t>(rawY));
            CfaPyramidBasePlane& plane = base[SiteIndex(site)];
            const CfaPlaneCoordinate coordinate = frame.layout.RawToPlane(
                { static_cast<double>(rawX), static_cast<double>(rawY) }, site);
            const auto planeX = static_cast<std::uint64_t>(coordinate.x);
            const auto planeY = static_cast<std::uint64_t>(coordinate.y);
            const std::size_t planeIndex = static_cast<std::size_t>(
                planeY * plane.extent.width + planeX);
            const double gain = frame.comparisonGain[packedIndex];
            const double signal = gain * frame.normalized[packedIndex];
            plane.signal[planeIndex] = signal;
            EffectiveVariance effective;
            double dnStep = 0.0;
            plane.validMask[planeIndex] =
                !HardInvalid(frame.sampleFlags[packedIndex]) &&
                ExactVariance(
                    frame, rawX, rawY, effective, dnStep, nullptr)
                ? 1u
                : 0u;
            plane.variance[planeIndex] = plane.validMask[planeIndex]
                ? effective.gateVariance
                : 0.0;
        }
        if (reportProgress &&
            ((rawY + 1u) % 16u == 0u || rawY + 1u == extent.height)) {
            reportProgress(
                0.75 * static_cast<double>(rawY + 1u) /
                    static_cast<double>(extent.height));
        }
    }
    try {
        const bool built = BuildCfaPlanePyramid(
            frame.layout,
            extent,
            base,
            parameters.registration,
            frame.pyramid,
            &error);
        if (built && reportProgress) reportProgress(1.0);
        return built;
    } catch (const std::bad_alloc&) {
        error = "The CFA pyramid allocation exceeded available memory.";
        return false;
    }
}

std::uint64_t EstimatePyramidBytes(
    PixelExtent extent,
    std::uint32_t levels) {
    std::uint64_t totalSamples = 0u;
    for (std::uint32_t level = 0u; level < levels; ++level) {
        totalSamples = SaturatingAdd(
            totalSamples,
            SaturatingMultiply(extent.width, extent.height));
        extent.width = (extent.width + 1u) / 2u;
        extent.height = (extent.height + 1u) / 2u;
    }
    return SaturatingMultiply(totalSamples, 2u * sizeof(double) + 1u);
}

std::uint64_t EstimatePeakBytes(
    PixelExtent extent,
    std::size_t frameCount,
    const Parameters& parameters,
    std::uint32_t workerCount,
    MfdAlignmentMode alignmentMode) {
    const std::uint64_t samples = SaturatingMultiply(
        extent.width, extent.height);
    const std::uint64_t packed = SaturatingMultiply(
        SaturatingMultiply(samples, frameCount),
        2u * sizeof(float) + sizeof(std::uint8_t));
    const std::uint64_t pyramid = alignmentMode == MfdAlignmentMode::Identity
        ? 0u
        : EstimatePyramidBytes(
            extent, parameters.registration.pyramidLevels);
    const std::uint64_t output = SaturatingMultiply(
        samples,
        sizeof(float) + sizeof(FusionPixelDiagnostics));
    const std::uint64_t reliability = SaturatingMultiply(
        SaturatingMultiply(samples, frameCount > 0u ? frameCount - 1u : 0u),
        sizeof(ReliabilityCell) / 4u);
    const PixelExtent tileExtent {
        std::min<std::uint64_t>(
            extent.width, parameters.fusion.outputTileRawPixels),
        std::min<std::uint64_t>(
            extent.height, parameters.fusion.outputTileRawPixels)
    };
    const std::uint64_t streamingWorking = SaturatingMultiply(
        EstimateFusionTileWorkingBytes(
            tileExtent, frameCount > 0u ? frameCount - 1u : 0u),
        workerCount);
    return SaturatingAdd(SaturatingAdd(
        SaturatingAdd(packed, SaturatingMultiply(pyramid, 2u)),
        SaturatingAdd(
            output,
            SaturatingAdd(reliability, SaturatingMultiply(samples, 17u)))),
        streamingWorking);
}

SymmetricRawCovariance RegistrationFloor(const Parameters& parameters);

bool BuildFixedMotionField(
    PixelExtent extent,
    const AffineModel& globalWarp,
    const Parameters& parameters,
    const char* message,
    BidirectionalLocalMotionResult& motion,
    std::string& error) {
    motion = {};
    if (extent.width < 2u || extent.height < 2u) {
        error = "The RAW extent is too small for a fixed MFD motion field.";
        return false;
    }
    LocalMotionGrid& grid = motion.forward;
    grid.valid = true;
    grid.failure = LocalMotionFailure::None;
    grid.message = message;
    grid.referenceRawExtent = extent;
    grid.sourceRawExtent = extent;
    grid.globalWarp = globalWarp;
    grid.width = 2u;
    grid.height = 2u;
    grid.originRawX = 0.0;
    grid.originRawY = 0.0;
    grid.spacingRawX = static_cast<double>(extent.width - 1u);
    grid.spacingRawY = static_cast<double>(extent.height - 1u);
    grid.structuredCount = 4u;
    const SymmetricRawCovariance covariance = RegistrationFloor(parameters);
    const double positionalSigma = std::sqrt(std::max(
        covariance.xxRawPixelsSquared,
        covariance.yyRawPixelsSquared));
    try {
        grid.nodes.resize(4u);
    } catch (const std::bad_alloc&) {
        error = "The fixed MFD motion field could not be allocated.";
        motion = {};
        return false;
    }
    for (std::uint32_t y = 0u; y < 2u; ++y) {
        for (std::uint32_t x = 0u; x < 2u; ++x) {
            MotionNode& node = grid.nodes[y * 2u + x];
            node.gridX = x;
            node.gridY = y;
            node.centerRaw = {
                x * grid.spacingRawX,
                y * grid.spacingRawY
            };
            node.residualRaw = {};
            node.discreteResidualRaw = {};
            node.covarianceRaw = covariance;
            node.state = MotionNodeState::Structured;
            node.rejectReason = MotionNodeRejectReason::None;
            node.confidence = 1.0;
            node.validFraction = 1.0;
            node.positionalSigmaRaw = positionalSigma;
        }
    }
    motion.valid = true;
    motion.failure = LocalMotionFailure::None;
    motion.message = message;
    return true;
}

std::uint64_t FusionWorkingBudget(
    PixelExtent extent,
    std::size_t alternateCount,
    const Parameters& parameters,
    std::uint32_t workerCount) {
    const PixelExtent tileExtent {
        std::min<std::uint64_t>(
            extent.width, parameters.fusion.outputTileRawPixels),
        std::min<std::uint64_t>(
            extent.height, parameters.fusion.outputTileRawPixels)
    };
    return SaturatingMultiply(
        EstimateFusionTileWorkingBytes(tileExtent, alternateCount),
        std::max<std::uint32_t>(1u, workerCount));
}

bool BuildPhasePairs(
    const MaterializedFrame& reference,
    const MaterializedFrame& alternate,
    std::vector<GlobalPlanePair>& pairs,
    std::string& error) {
    std::uint32_t selectedLevel = 0u;
    const std::uint32_t levels = std::min(
        reference.pyramid.levelCount, alternate.pyramid.levelCount);
    for (std::uint32_t level = levels; level > 0u; --level) {
        const CfaPyramidLevel* candidate = FindCfaPyramidLevel(
            reference.pyramid, CfaSite::Red, level - 1u);
        if (candidate && candidate->extent.width >= 32u &&
            candidate->extent.height >= 32u) {
            selectedLevel = level - 1u;
            break;
        }
    }
    pairs.clear();
    try {
        for (CfaSite site : kSites) {
            const CfaPyramidLevel* ref = FindCfaPyramidLevel(
                reference.pyramid, site, selectedLevel);
            const CfaPyramidLevel* alt = FindCfaPyramidLevel(
                alternate.pyramid, site, selectedLevel);
            if (!ref || !alt || ref->extent.width != alt->extent.width ||
                ref->extent.height != alt->extent.height) {
                error = "The reference and alternate CFA pyramids do not match.";
                return false;
            }
            GlobalPlanePair pair;
            pair.site = site;
            pair.extent = ref->extent;
            pair.reference = ref->signal;
            pair.alternate = alt->signal;
            pair.rawPixelsPerPlanePixel = ref->rawPixelsPerLevelPixel;
            pair.validMask.resize(ref->validMask.size());
            for (std::size_t index = 0u; index < pair.validMask.size(); ++index) {
                pair.validMask[index] =
                    ref->validMask[index] && alt->validMask[index] ? 1u : 0u;
            }
            pairs.push_back(std::move(pair));
        }
    } catch (const std::bad_alloc&) {
        error = "The coarse global-registration planes could not be allocated.";
        return false;
    }
    return true;
}

SymmetricRawCovariance RegistrationFloor(const Parameters& parameters) {
    const double floor = std::max(
        parameters.registration.warpCovarianceFloorRawPixels,
        parameters.registration.warpCovarianceEigenvalueMinRawPixels);
    return { floor * floor, 0.0, floor * floor };
}

bool ExactComparisonSample(
    const MaterializedFrame& frame,
    std::uint64_t rawX,
    std::uint64_t rawY,
    double& comparison,
    EffectiveVariance& variance,
    double& effectiveDnStep) {
    if (rawX >= frame.prepared.activeExtent.width ||
        rawY >= frame.prepared.activeExtent.height) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(
        rawY * frame.prepared.activeExtent.width + rawX);
    if (HardInvalid(frame.sampleFlags[index])) return false;
    comparison = frame.normalized[index] * frame.comparisonGain[index];
    return Finite(comparison) && ExactVariance(
        frame, rawX, rawY, variance, effectiveDnStep, nullptr);
}

bool BuildExposureSamplesAtStride(
    const MaterializedFrame& reference,
    const MaterializedFrame& alternate,
    RawCoordinate translation,
    const Parameters& parameters,
    std::uint64_t stride,
    std::vector<ExposureSample>& samples) {
    samples.clear();
    const PixelExtent extent = reference.prepared.activeExtent;
    const SymmetricRawCovariance covariance = RegistrationFloor(parameters);
    const std::uint64_t cellStride = std::max<std::uint64_t>(1u, stride);
    for (std::uint64_t cellY = 2u;
         2u * cellY + 5u < extent.height;
         cellY += cellStride) {
        for (std::uint64_t cellX = 2u;
             2u * cellX + 5u < extent.width;
             cellX += cellStride) {
            for (std::uint64_t offsetY = 0u; offsetY < 2u; ++offsetY) {
                for (std::uint64_t offsetX = 0u; offsetX < 2u; ++offsetX) {
            const std::uint64_t x = 2u * cellX + offsetX;
            const std::uint64_t y = 2u * cellY + offsetY;
            double referenceValue = 0.0;
            EffectiveVariance referenceVariance;
            double referenceDnStep = 0.0;
            if (!ExactComparisonSample(
                    reference,
                    x,
                    y,
                    referenceValue,
                    referenceVariance,
                    referenceDnStep) ||
                referenceValue < parameters.radiometric.exposureFitSignalMin ||
                referenceValue > parameters.radiometric.exposureFitSignalMax) {
                continue;
            }
            const CfaSite site = reference.layout.SiteAt(
                static_cast<std::int64_t>(x),
                static_cast<std::int64_t>(y));
            SameCfaScalarParameters sampleParameters;
            sampleParameters.referenceComparisonPilot = referenceValue;
            sampleParameters.warpCovariance = covariance;
            sampleParameters.numericalVarianceFloor =
                parameters.fusion.numericalVarianceFloor;
            SameCfaSampleResult alternateSample;
            if (!SampleMaterializedSameCfa(
                    alternate,
                    { static_cast<double>(x) + translation.x,
                      static_cast<double>(y) + translation.y },
                    site,
                    sampleParameters,
                    alternateSample,
                    nullptr)) {
                continue;
            }
            ExposureSample sample;
            sample.referenceValue = referenceValue;
            sample.alternateValue = alternateSample.value;
            sample.referenceVariance = referenceVariance.gateVariance;
            sample.alternateVariance = alternateSample.gateVariance;
            sample.usableCodeSpanDn =
                reference.prepared.calibration.usableSpanByCfaSite[
                    SiteIndex(site)];
            sample.offsetVariance = referenceVariance.darkVariance;
            sample.site = site;
            samples.push_back(sample);
                }
            }
        }
    }
    return true;
}

bool BuildExposureSamples(
    const MaterializedFrame& reference,
    const MaterializedFrame& alternate,
    RawCoordinate translation,
    const Parameters& parameters,
    std::vector<ExposureSample>& samples) {
    const double rawSamples = static_cast<double>(
        reference.prepared.activeExtent.width) *
        static_cast<double>(reference.prepared.activeExtent.height);
    const std::uint64_t stride = std::max<std::uint64_t>(
        1u,
        static_cast<std::uint64_t>(std::ceil(std::sqrt(
            rawSamples / 50000.0))));
    BuildExposureSamplesAtStride(
        reference, alternate, translation, parameters, stride, samples);
    if (samples.size() < parameters.radiometric.exposureFitMinimumSamples &&
        stride > 1u) {
        BuildExposureSamplesAtStride(
            reference, alternate, translation, parameters, 1u, samples);
    }
    return !samples.empty();
}

std::vector<AffineReferenceSample> BuildAffineReferenceSamples(
    const MaterializedFrame& reference,
    std::uint64_t targetCount = 20000u) {
    std::vector<AffineReferenceSample> samples;
    const PixelExtent extent = reference.prepared.activeExtent;
    const double rawSamples = static_cast<double>(extent.width) *
        static_cast<double>(extent.height);
    const std::uint64_t stride = std::max<std::uint64_t>(
        2u,
        static_cast<std::uint64_t>(std::ceil(std::sqrt(
            rawSamples / static_cast<double>(targetCount)))));
    for (std::uint64_t y = 4u; y + 4u < extent.height; y += stride) {
        for (std::uint64_t x = 4u; x + 4u < extent.width; x += stride) {
            double value = 0.0;
            EffectiveVariance variance;
            double dnStep = 0.0;
            if (!ExactComparisonSample(
                    reference, x, y, value, variance, dnStep)) {
                continue;
            }
            AffineReferenceSample sample;
            sample.referenceRaw = {
                static_cast<double>(x), static_cast<double>(y) };
            sample.site = reference.layout.SiteAt(
                static_cast<std::int64_t>(x),
                static_cast<std::int64_t>(y));
            sample.referenceValue = value;
            sample.referenceVariance = variance.gateVariance;
            samples.push_back(sample);
        }
    }
    return samples;
}

GlobalWarpCovarianceEvaluator MakeForwardCovarianceEvaluator(
    const GlobalFrameAlignmentDecision& decision,
    const AffineRefinementResult& affine,
    const Parameters& parameters) {
    const SymmetricRawCovariance fallback = RegistrationFloor(parameters);
    if (decision.choice != GlobalFrameAlignmentChoice::Affine ||
        !affine.acceptedAffine) {
        return [fallback](RawCoordinate, SymmetricRawCovariance& covariance) {
            covariance = fallback;
            return true;
        };
    }
    const std::array<double, 36> covariance = affine.parameterCovariance;
    const RawCoordinate center = decision.model.centerRaw;
    return [covariance, center, fallback](
        RawCoordinate coordinate,
        SymmetricRawCovariance& output) {
        const double dx = coordinate.x - center.x;
        const double dy = coordinate.y - center.y;
        const std::array<double, 6> jx { dx, dy, 0.0, 0.0, 1.0, 0.0 };
        const std::array<double, 6> jy { 0.0, 0.0, dx, dy, 0.0, 1.0 };
        auto bilinear = [&covariance](
            const std::array<double, 6>& left,
            const std::array<double, 6>& right) {
            double value = 0.0;
            for (std::size_t row = 0u; row < 6u; ++row) {
                for (std::size_t column = 0u; column < 6u; ++column) {
                    value += left[row] * covariance[row * 6u + column] *
                        right[column];
                }
            }
            return value;
        };
        output = {
            bilinear(jx, jx) + fallback.xxRawPixelsSquared,
            bilinear(jx, jy),
            bilinear(jy, jy) + fallback.yyRawPixelsSquared
        };
        return Finite(output.xxRawPixelsSquared) &&
            Finite(output.xyRawPixelsSquared) &&
            Finite(output.yyRawPixelsSquared) &&
            output.xxRawPixelsSquared > 0.0 &&
            output.yyRawPixelsSquared > 0.0;
    };
}

bool RegisterAlternate(
    const MaterializedFrame& reference,
    MaterializedFrame& alternate,
    const Parameters& parameters,
    MfdAlignmentMode alignmentMode,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& reportProgress,
    GlobalFrameAlignmentDecision& decision,
    AffineRefinementResult& affine,
    BidirectionalLocalMotionResult& motion,
    ReliabilityMap& reliability,
    std::string& error) {
    const auto canceled = [&]() {
        return shouldCancel && shouldCancel();
    };
    const auto report = [&](double fraction) {
        if (reportProgress) reportProgress(std::clamp(fraction, 0.0, 1.0));
    };
    if (canceled()) {
        error = "MFD alternate registration was canceled.";
        return false;
    }
    report(0.0);
    const RawCoordinate referenceCenter {
        0.5 * static_cast<double>(reference.prepared.activeExtent.width - 1u),
        0.5 * static_cast<double>(reference.prepared.activeExtent.height - 1u)
    };
    AffineModel seed;
    seed.centerRaw = referenceCenter;
    PhaseTranslationResult translation;
    ExposureFitResult exposure;
    if (alignmentMode == MfdAlignmentMode::Identity) {
        decision.accepted = true;
        decision.choice = GlobalFrameAlignmentChoice::Identity;
        decision.message =
            "Identity coordinates were used; geometric and exposure registration were skipped.";
        decision.model = seed;
        decision.exposureScale = 1.0;
        decision.exposureScaleVariance = 0.0;
        if (!BuildFixedMotionField(
                reference.prepared.activeExtent,
                decision.model,
                parameters,
                "Identity-coordinate motion field; registration was skipped.",
                motion,
                error)) {
            return false;
        }
        report(0.85);
    } else {
        std::vector<GlobalPlanePair> pairs;
        if (!BuildPhasePairs(reference, alternate, pairs, error)) return false;
        report(0.03);
        PhaseCorrelationOptions phaseOptions;
        phaseOptions.minimumOverlapFraction =
            parameters.registration.minimumGlobalOverlapFraction;
        if (!EstimateMultichannelPhaseTranslation(
                pairs, phaseOptions, translation, &error)) {
            return false;
        }
        if (canceled()) {
            error = "MFD alternate registration was canceled.";
            return false;
        }
        report(0.15);

        std::vector<ExposureSample> exposureSamples;
        BuildExposureSamples(
            reference,
            alternate,
            translation.translationRaw,
            parameters,
            exposureSamples);
        ExposureMetadataPrior metadataPrior;
        if (!FitGlobalExposureScale(
                exposureSamples,
                SaturatingMultiply(
                    reference.prepared.activeExtent.width,
                    reference.prepared.activeExtent.height),
                metadataPrior,
                parameters.radiometric,
                exposure,
                &error)) {
            error += " Phase translation raw=(" +
                std::to_string(translation.translationRaw.x) + "," +
                std::to_string(translation.translationRaw.y) +
                "), eligible exposure samples=" +
                std::to_string(exposureSamples.size()) + ".";
            return false;
        }
        if (canceled()) {
            error = "MFD alternate registration was canceled.";
            return false;
        }
        report(0.25);
        seed.translationRaw = translation.translationRaw;

        if (alignmentMode == MfdAlignmentMode::TranslationOnly) {
            decision.accepted = true;
            decision.choice = GlobalFrameAlignmentChoice::Translation;
            decision.message =
                "Global translation was estimated; affine and local registration were skipped.";
            decision.model = seed;
            decision.exposureScale = exposure.scale;
            decision.exposureScaleVariance = exposure.scaleVariance;
            if (!BuildFixedMotionField(
                    reference.prepared.activeExtent,
                    decision.model,
                    parameters,
                    "Translation-only motion field; local registration was skipped.",
                    motion,
                    error)) {
                return false;
            }
            report(0.85);
        } else {
            AffineRefinementOptions affineOptions;
            affineOptions.referenceExtent = reference.prepared.activeExtent;
            affineOptions.alternateExtent = alternate.prepared.activeExtent;
            affineOptions.exposureScale = exposure.scale;
            affineOptions.iterations =
                parameters.registration.affineIterationsPerLevel;
            affineOptions.minimumOverlapFraction =
                parameters.registration.minimumGlobalOverlapFraction;
            affineOptions.singularValueMinimum =
                parameters.registration.affineSingularValueMin;
            affineOptions.singularValueMaximum =
                parameters.registration.affineSingularValueMax;
            affineOptions.maximumRotationDegrees =
                parameters.registration.affineRotationLimitDegrees;
            affineOptions.numericalVarianceFloor =
                parameters.fusion.numericalVarianceFloor;
            const std::vector<AffineReferenceSample> affineSamples =
                BuildAffineReferenceSamples(reference);
            const SymmetricRawCovariance registrationFloor =
                RegistrationFloor(parameters);
            const AffineSourceEvaluator evaluator = [
                &alternate,
                &parameters,
                registrationFloor](
                    CfaSite site,
                    RawCoordinate coordinate,
                    AffineSourceSample& sample) {
                SameCfaScalarParameters sampleParameters;
                sampleParameters.referenceComparisonPilot = 0.25;
                sampleParameters.warpCovariance = registrationFloor;
                sampleParameters.numericalVarianceFloor =
                    parameters.fusion.numericalVarianceFloor;
                SameCfaSampleResult sampled;
                if (!SampleMaterializedSameCfa(
                        alternate,
                        coordinate,
                        site,
                        sampleParameters,
                        sampled,
                        nullptr)) {
                    return false;
                }
                sample.value = sampled.value;
                sample.gradientRaw = sampled.gradientRaw;
                sample.variance = sampled.gateVariance;
                return true;
            };
            std::string affineError;
            const bool affineAccepted = RefineGlobalAffine(
                affineSamples,
                evaluator,
                seed,
                affineOptions,
                affine,
                &affineError);
            if (!affineAccepted) {
                affine.translationFallbackAvailable = true;
                if (affine.message.empty()) affine.message = affineError;
            }
            decision = DecideGlobalFrameAlignment(
                translation, exposure, affine, seed.centerRaw);
            if (!decision.accepted) {
                error = decision.message;
                return false;
            }
            report(0.4);

            AffineModel inverse;
            if (!InvertAffineModel(decision.model, inverse, &error)) return false;
            BidirectionalLocalMotionRequest motionRequest;
            motionRequest.reference = &reference.pyramid;
            motionRequest.alternate = &alternate.pyramid;
            motionRequest.referenceToAlternate = decision.model;
            motionRequest.exposureScale = decision.exposureScale;
            motionRequest.forwardGlobalCovariance =
                MakeForwardCovarianceEvaluator(decision, affine, parameters);
            const SymmetricRawCovariance reverseFloor =
                RegistrationFloor(parameters);
            motionRequest.reverseGlobalCovariance = [reverseFloor](
                RawCoordinate, SymmetricRawCovariance& covariance) {
                covariance = reverseFloor;
                return true;
            };
            motionRequest.options.registration = parameters.registration;
            motionRequest.shouldCancel = shouldCancel;
            motionRequest.reportProgress = [reportProgress](double fraction) {
                if (reportProgress) {
                    reportProgress(
                        0.4 + 0.45 * std::clamp(fraction, 0.0, 1.0));
                }
            };
            if (!EstimateBidirectionalLocalMotion(
                    motionRequest, motion, &error)) {
                return false;
            }
            report(0.85);
        }
    }

    LocalMotionOptions motionOptions;
    motionOptions.registration = parameters.registration;

    ReliabilityBuildRequest reliabilityRequest;
    reliabilityRequest.rawExtent = reference.prepared.activeExtent;
    reliabilityRequest.layout = reference.layout;
    reliabilityRequest.motionGrid = &motion.forward;
    reliabilityRequest.motionOptions = motionOptions;
    reliabilityRequest.noiseQuality = static_cast<NoiseModelQuality>(
        std::max(
            static_cast<std::uint8_t>(reference.noise.quality),
            static_cast<std::uint8_t>(alternate.noise.quality)));
    reliabilityRequest.parameters = parameters;
    reliabilityRequest.shouldCancel = shouldCancel;
    reliabilityRequest.reportProgress = [reportProgress](double fraction) {
        if (reportProgress) {
            reportProgress(0.85 + 0.15 * std::clamp(fraction, 0.0, 1.0));
        }
    };
    reliabilityRequest.residualEvaluator = [
        &reference,
        &alternate,
        &decision,
        &parameters](
            RawCoordinate referenceRaw,
            CfaSite site,
            const LocalMotionFieldSample& motionSample,
            ReliabilityResidualSample& sample) {
        if (referenceRaw.x < 0.0 || referenceRaw.y < 0.0) return false;
        const auto x = static_cast<std::uint64_t>(referenceRaw.x);
        const auto y = static_cast<std::uint64_t>(referenceRaw.y);
        double referenceValue = 0.0;
        EffectiveVariance referenceVariance;
        double referenceDnStep = 0.0;
        sample = {};
        sample.valid = true;
        if (!ExactComparisonSample(
                reference,
                x,
                y,
                referenceValue,
                referenceVariance,
                referenceDnStep)) {
            sample.hardValid = false;
            return true;
        }
        SameCfaScalarParameters sampleParameters;
        sampleParameters.exposureScale = decision.exposureScale;
        sampleParameters.exposureScaleVariance =
            decision.exposureScaleVariance;
        sampleParameters.referenceComparisonPilot = referenceValue;
        sampleParameters.warpCovariance = motionSample.covarianceRaw;
        sampleParameters.numericalVarianceFloor =
            parameters.fusion.numericalVarianceFloor;
        SameCfaSampleResult alternateSample;
        if (!SampleMaterializedSameCfa(
                alternate,
                motionSample.sourceRaw,
                site,
                sampleParameters,
                alternateSample,
                nullptr)) {
            sample.hardValid = false;
            return true;
        }
        sample.hardValid = true;
        sample.referenceValue = referenceValue;
        sample.alternateValue = alternateSample.value;
        sample.referenceGateVariance = referenceVariance.gateVariance;
        sample.alternateGateVariance = alternateSample.gateVariance;
        return true;
    };
    if (!BuildReliabilityMap(reliabilityRequest, reliability, &error) ||
        !reliability.frameUsable) {
        if (error.empty()) {
            error = "The alternate has no safely usable reliability area.";
        }
        return false;
    }
    report(1.0);
    return true;
}

std::string BuildResultCacheKey(
    const MaterializedFrame& reference,
    const std::vector<AcceptedAlternate>& alternates,
    const Parameters& parameters,
    MfdAlignmentMode alignmentMode) {
    MfdCacheKeyRequest keyRequest;
    keyRequest.stage = MfdCacheStage::FusedResult;
    std::vector<std::string> frameIds;
    frameIds.push_back(reference.input.stableFrameId);
    std::vector<std::string> alternateIdentities;
    for (const AcceptedAlternate& alternate : alternates) {
        frameIds.push_back(alternate.frame.input.stableFrameId);
        alternateIdentities.push_back(
            alternate.frame.prepared.cacheKey + ":" +
            std::to_string(alternate.reliability.usableCellCount));
    }
    keyRequest.dependencies = {
        { "ordered-frame-ids", EncodeOrderedIdentities(frameIds) },
        { "reference-frame-id", reference.input.stableFrameId },
        { "ordered-cache-e-keys", alternateIdentities.empty()
            ? std::string("none")
            : EncodeOrderedIdentities(alternateIdentities) },
        { "alignment-mode", MfdAlignmentModeId(alignmentMode) },
        { "fusion-parameters", SerializeParameters(parameters).dump() },
        { "output-contract", kOutputContractId }
    };
    std::string key;
    std::string ignored;
    if (!BuildMfdCacheKey(keyRequest, key, &ignored)) {
        return reference.prepared.cacheKey;
    }
    return key;
}

bool PublishExactReference(
    const MaterializedFrame& reference,
    const std::string& cacheKey,
    PublishedFusionSnapshot& published,
    std::string& error) {
    PublishedFusionResult output;
    output.fusedResultCacheKey = cacheKey;
    output.extent = reference.prepared.activeExtent;
    output.normalizedMosaic = reference.normalized;
    try {
        output.diagnostics.resize(output.normalizedMosaic.size());
    } catch (const std::bad_alloc&) {
        error = "The exact-reference diagnostic result could not be allocated.";
        return false;
    }
    for (FusionPixelDiagnostics& diagnostic : output.diagnostics) {
        diagnostic.decisionReason = DecisionReason::AllAlternatesRejected;
        diagnostic.exactReferenceCopy = true;
        diagnostic.referenceIncluded = true;
        diagnostic.effectiveSampleCount = 1.0;
    }
    AtomicFusionResultPublisher publisher;
    return publisher.Publish(0u, std::move(output), published, &error);
}

FusionReferenceProvider MakeReferenceProvider(
    const MaterializedFrame& reference,
    const Parameters&) {
    return [&reference](
        std::uint64_t x,
        std::uint64_t y,
        FusionReferenceSample& sample) {
        sample = {};
        if (x >= reference.prepared.activeExtent.width ||
            y >= reference.prepared.activeExtent.height) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(
            y * reference.prepared.activeExtent.width + x);
        EffectiveVariance variance;
        double dnStep = 0.0;
        if (!ExactVariance(reference, x, y, variance, dnStep, nullptr)) {
            return false;
        }
        const std::uint8_t flags = reference.sampleFlags[index];
        sample.valid = true;
        sample.clipped = HasSampleFlag(flags, PreparedSampleFlag::Saturated) ||
            HasSampleFlag(flags, PreparedSampleFlag::ExplicitDecoderClip);
        sample.defective = HasSampleFlag(flags, PreparedSampleFlag::Defective) ||
            HasSampleFlag(flags, PreparedSampleFlag::DecoderRepaired);
        sample.noiseQuality = reference.noise.quality;
        sample.normalizedValue = reference.normalized[index];
        sample.comparisonGain = reference.comparisonGain[index];
        sample.gateVariance = variance.gateVariance;
        sample.fusionVariance = variance.fusionVariance;
        sample.darkVariance = variance.darkVariance;
        sample.effectiveDnStep = dnStep;
        return Finite(sample.normalizedValue) &&
            Finite(sample.comparisonGain) && sample.comparisonGain > 0.0;
    };
}

FusionCandidateProvider MakeCandidateProvider(
    const MaterializedFrame& reference,
    const std::vector<AcceptedAlternate>& alternates,
    const Parameters& parameters) {
    return [&reference, &alternates, &parameters](
        std::size_t stableAlternateIndex,
        std::uint64_t x,
        std::uint64_t y,
        FusionCandidateSample& sample) {
        sample = {};
        if (stableAlternateIndex >= alternates.size() ||
            x >= reference.prepared.activeExtent.width ||
            y >= reference.prepared.activeExtent.height) {
            return false;
        }
        const AcceptedAlternate& alternate = alternates[stableAlternateIndex];
        LocalMotionFieldSample motionSample;
        LocalMotionOptions motionOptions;
        motionOptions.registration = parameters.registration;
        if (!EvaluateLocalMotionField(
                alternate.motion.forward,
                { static_cast<double>(x), static_cast<double>(y) },
                motionOptions,
                motionSample,
                nullptr)) {
            sample.invalidReason = FusionRejectReason::CandidateUnavailable;
            return true;
        }
        const std::size_t referenceIndex = static_cast<std::size_t>(
            y * reference.prepared.activeExtent.width + x);
        const double referenceComparison =
            reference.normalized[referenceIndex] *
            reference.comparisonGain[referenceIndex];
        SameCfaScalarParameters sampleParameters;
        sampleParameters.exposureScale = alternate.alignment.exposureScale;
        sampleParameters.exposureScaleVariance =
            alternate.alignment.exposureScaleVariance;
        sampleParameters.referenceComparisonPilot = referenceComparison;
        sampleParameters.warpCovariance = motionSample.covarianceRaw;
        sampleParameters.numericalVarianceFloor =
            parameters.fusion.numericalVarianceFloor;
        const CfaSite site = reference.layout.SiteAt(
            static_cast<std::int64_t>(x),
            static_cast<std::int64_t>(y));
        SameCfaSampleResult sampled;
        if (!SampleMaterializedSameCfa(
                alternate.frame,
                motionSample.sourceRaw,
                site,
                sampleParameters,
                sampled,
                nullptr)) {
            sample.invalidReason = FusionRejectReason::CandidateUnavailable;
            return true;
        }
        const std::uint64_t cellX = std::min<std::uint64_t>(
            x / 2u, alternate.reliability.cellExtent.width - 1u);
        const std::uint64_t cellY = std::min<std::uint64_t>(
            y / 2u, alternate.reliability.cellExtent.height - 1u);
        const ReliabilityCell& cell = alternate.reliability.cells[
            static_cast<std::size_t>(
                cellY * alternate.reliability.cellExtent.width + cellX)];
        sample.valid = true;
        sample.hardValid = true;
        sample.noiseQuality = alternate.frame.noise.quality;
        sample.invalidReason = FusionRejectReason::None;
        sample.value = sampled.value;
        sample.gateVariance = sampled.gateVariance;
        sample.fusionVariance = sampled.fusionVariance;
        sample.darkVariance = sampled.darkVariance;
        sample.effectiveDnStep = sampled.effectiveDnStep;
        sample.reliability = std::clamp(
            cell.reliability * motionSample.alignmentConfidence, 0.0, 1.0);
        sample.referenceDefectGate = sample.reliability;
        return true;
    };
}

DecisionReason PreparationDecisionReason(bool referenceFrame) {
    return referenceFrame
        ? DecisionReason::ReferenceIncompatible
        : DecisionReason::AlternateIncompatible;
}

} // namespace

const char* MfdProcessingStatusName(MfdProcessingStatus status) {
    switch (status) {
        case MfdProcessingStatus::DenoisedCandidate:
            return "denoised-candidate";
        case MfdProcessingStatus::ReferenceOnly:
            return "reference-only";
        case MfdProcessingStatus::Canceled:
            return "canceled";
        case MfdProcessingStatus::Failed:
            return "failed";
    }
    return "failed";
}

const char* MfdAlignmentModeName(MfdAlignmentMode mode) {
    switch (mode) {
        case MfdAlignmentMode::Full:
            return "Full - global + local";
        case MfdAlignmentMode::TranslationOnly:
            return "Translation only";
        case MfdAlignmentMode::Identity:
            return "None - identity coordinates";
    }
    return "Unknown";
}

const char* MfdAlignmentModeId(MfdAlignmentMode mode) {
    switch (mode) {
        case MfdAlignmentMode::Full: return "full";
        case MfdAlignmentMode::TranslationOnly: return "translation-only";
        case MfdAlignmentMode::Identity: return "identity";
    }
    return "invalid";
}

bool ParseMfdAlignmentMode(
    const std::string& value,
    MfdAlignmentMode& mode) {
    if (value == "full") {
        mode = MfdAlignmentMode::Full;
        return true;
    }
    if (value == "translation-only") {
        mode = MfdAlignmentMode::TranslationOnly;
        return true;
    }
    if (value == "identity" || value == "none") {
        mode = MfdAlignmentMode::Identity;
        return true;
    }
    return false;
}

const char* MfdProcessingStageName(MfdProcessingStage stage) {
    switch (stage) {
        case MfdProcessingStage::Queued:
            return "Queued";
        case MfdProcessingStage::MaterializingSources:
            return "Materializing originals";
        case MfdProcessingStage::PreparingReference:
            return "Preparing reference";
        case MfdProcessingStage::BuildingReferencePyramid:
            return "Building reference pyramid";
        case MfdProcessingStage::PreparingAlternate:
            return "Preparing alternate";
        case MfdProcessingStage::RegisteringAlternate:
            return "Registering alternate";
        case MfdProcessingStage::FusingTiles:
            return "Fusing output tiles";
        case MfdProcessingStage::WritingInspection:
            return "Writing inspection output";
        case MfdProcessingStage::Finalizing:
            return "Finalizing";
    }
    return "Processing";
}

MfdProcessingServices MakeFilesystemMfdProcessingServices() {
    MfdProcessingServices services;
    services.loadRawFrame = DefaultLoadRawFrame;
    return services;
}

MfdProcessingResult ProcessMfdBurst(
    const MfdProcessingRequest& request,
    const MfdProcessingServices& services) {
    MfdProcessingResult result;
    result.diagnostics.alignmentMode = request.alignmentMode;
    std::string parameterError;
    if (request.frames.size() < 2u ||
        request.referenceFrameIndex >= request.frames.size() ||
        request.workingDirectory.empty() || request.memoryBudgetBytes == 0u ||
        request.workerCount == 0u || !services.loadRawFrame ||
        std::string(MfdAlignmentModeId(request.alignmentMode)) == "invalid" ||
        !ValidateParameters(request.parameters, &parameterError)) {
        result.message = parameterError.empty()
            ? "The MFD processor request is invalid."
            : parameterError;
        return result;
    }
    for (std::size_t inputIndex = 0u;
         inputIndex < request.frames.size();
         ++inputIndex) {
        const MfdProcessingFrameInput& input = request.frames[inputIndex];
        if (input.stableFrameId.empty() || input.sourcePath.empty() ||
            !LooksLikeSha256(input.expectedSourceSha256) ||
            input.expectedSourceByteLength == 0u ||
            input.expectedVisibleExtent.width == 0u ||
            input.expectedVisibleExtent.height == 0u) {
            result.message = "An MFD frame lacks a stable locked identity.";
            return result;
        }
        for (std::size_t prior = 0u; prior < inputIndex; ++prior) {
            if (request.frames[prior].stableFrameId == input.stableFrameId ||
                request.frames[prior].expectedSourceSha256 ==
                    input.expectedSourceSha256) {
                result.message =
                    "The MFD processor request contains a duplicate frame identity.";
                return result;
            }
        }
    }
    const PixelExtent preflightExtent = request.frames[
        static_cast<std::size_t>(request.referenceFrameIndex)]
        .expectedVisibleExtent;
    result.diagnostics.estimatedPeakResidentBytes = EstimatePeakBytes(
        preflightExtent,
        request.frames.size(),
        request.parameters,
        request.workerCount,
        request.alignmentMode);
    const std::uint64_t preflightPublicationBytes = SaturatingMultiply(
        SaturatingMultiply(preflightExtent.width, preflightExtent.height),
        sizeof(float) + sizeof(FusionPixelDiagnostics));
    if (preflightPublicationBytes > request.memoryBudgetBytes) {
        result.message =
            "The configured memory budget cannot hold even the atomic output; RAW decode was not started.";
        return result;
    }
    if (IsCanceled(request)) {
        result.status = MfdProcessingStatus::Canceled;
        result.message = "MFD processing was canceled before decode.";
        return result;
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(
        request.workingDirectory, filesystemError);
    if (filesystemError) {
        result.message = "The MFD working directory could not be created.";
        return result;
    }
    DirectoryNormalizedTileCache cache(
        request.workingDirectory / "normalized");
    result.diagnostics.frames.resize(request.frames.size());
    for (std::size_t index = 0u; index < request.frames.size(); ++index) {
        result.diagnostics.frames[index].stableFrameId =
            request.frames[index].stableFrameId;
        result.diagnostics.frames[index].referenceFrame =
            index == request.referenceFrameIndex;
    }

    MaterializedFrame reference;
    std::string error;
    const std::uint32_t referenceFrameOrdinal =
        static_cast<std::uint32_t>(request.referenceFrameIndex + 1u);
    MfdProcessingFrameDiagnostic& referenceDiagnostic =
        result.diagnostics.frames[
            static_cast<std::size_t>(request.referenceFrameIndex)];
    referenceDiagnostic.attempted = true;
    ReportProgress(
        request,
        MfdProcessingStage::PreparingReference,
        0.05,
        0.0,
        "Decoding and normalizing the reference frame.",
        0u,
        0u,
        referenceFrameOrdinal);
    if (!PrepareFrame(
            request.frames[static_cast<std::size_t>(request.referenceFrameIndex)],
            request.parameters,
            services,
            cache,
            request.shouldCancel,
            [&request, referenceFrameOrdinal](double fraction) {
                ReportProgress(
                    request,
                    MfdProcessingStage::PreparingReference,
                    0.05 + 0.12 * fraction,
                    0.7 * fraction,
                    "Decoding and normalizing the reference frame.",
                    0u,
                    0u,
                    referenceFrameOrdinal);
            },
            reference,
            error)) {
        result.status = IsCanceled(request)
            ? MfdProcessingStatus::Canceled
            : MfdProcessingStatus::Failed;
        result.message = error;
        referenceDiagnostic.decisionReason =
            IsCanceled(request) ? DecisionReason::Canceled
                                : PreparationDecisionReason(true);
        referenceDiagnostic.message = error;
        return result;
    }
    referenceDiagnostic.prepared = true;
    referenceDiagnostic.noiseQuality = reference.noise.quality;
    referenceDiagnostic.message =
        reference.noise.quality == NoiseModelQuality::GenericLowConfidence
            ? "Reference prepared with the conservative generic low-confidence noise fallback."
            : "Reference prepared successfully.";
    // Downstream RAW development must use the reference frame's color and
    // placement metadata. The fused mosaic itself deliberately remains
    // camera-native and does not bake white balance or a camera transform.
    result.referenceMetadata = reference.metadata;
    result.outputCfaPattern = reference.layout.ActiveAreaPattern();

    result.diagnostics.estimatedPeakResidentBytes = EstimatePeakBytes(
        reference.prepared.activeExtent,
        request.frames.size(),
        request.parameters,
        request.workerCount,
        request.alignmentMode);
    const std::uint64_t minimumPublicationBytes = SaturatingMultiply(
        SaturatingMultiply(
            reference.prepared.activeExtent.width,
            reference.prepared.activeExtent.height),
        sizeof(float) + sizeof(FusionPixelDiagnostics));
    if (minimumPublicationBytes > request.memoryBudgetBytes) {
        result.message =
            "The configured memory budget cannot hold even the atomic output; no result was published.";
        referenceDiagnostic.decisionReason = DecisionReason::OutOfMemory;
        return result;
    }

    if (!MaterializePreparedFrame(
            reference,
            cache,
            request.shouldCancel,
            [&request, referenceFrameOrdinal](double fraction) {
                ReportProgress(
                    request,
                    MfdProcessingStage::PreparingReference,
                    0.17 + 0.06 * fraction,
                    0.7 + 0.3 * fraction,
                    "Materializing normalized reference tiles.",
                    0u,
                    0u,
                    referenceFrameOrdinal);
            },
            error)) {
        result.status = IsCanceled(request)
            ? MfdProcessingStatus::Canceled
            : MfdProcessingStatus::Failed;
        result.message = error;
        referenceDiagnostic.decisionReason = IsCanceled(request)
            ? DecisionReason::Canceled
            : DecisionReason::ReferenceUnreadable;
        return result;
    }
    auto publishReference = [&](const std::string& message) {
        const std::vector<AcceptedAlternate> none;
        std::string publicationError;
        if (!PublishExactReference(
                reference,
                BuildResultCacheKey(
                    reference,
                    none,
                    request.parameters,
                    request.alignmentMode),
                result.published,
                publicationError)) {
            result.status = MfdProcessingStatus::Failed;
            result.message = publicationError;
            return;
        }
        result.status = MfdProcessingStatus::ReferenceOnly;
        result.message = message;
        result.referenceNormalizedMosaic = std::move(reference.normalized);
        result.diagnostics.exactReferencePixelCount =
            SaturatingMultiply(
                reference.prepared.activeExtent.width,
                reference.prepared.activeExtent.height);
        ReportProgress(
            request,
            MfdProcessingStage::Finalizing,
            0.95,
            1.0,
            "The reference-only result is complete; preparing inspection output.");
    };

    if (reference.noise.quality == NoiseModelQuality::Unavailable) {
        referenceDiagnostic.decisionReason = DecisionReason::NoiseModelUnavailable;
        publishReference(
            "No trustworthy or safely estimated reference noise model was available; exact reference published.");
        return result;
    }
    if (result.diagnostics.estimatedPeakResidentBytes >
        request.memoryBudgetBytes) {
        referenceDiagnostic.decisionReason = DecisionReason::OutOfMemory;
        publishReference(
            "The full burst exceeded the configured safe memory estimate; exact reference published.");
        return result;
    }
    if (request.alignmentMode != MfdAlignmentMode::Identity &&
        !BuildFramePyramid(
            reference,
            request.parameters,
            request.shouldCancel,
            [&request, referenceFrameOrdinal](double fraction) {
                ReportProgress(
                    request,
                    MfdProcessingStage::BuildingReferencePyramid,
                    0.23 + 0.07 * fraction,
                    fraction,
                    "Building the reference CFA pyramid.",
                    0u,
                    0u,
                    referenceFrameOrdinal);
            },
            error)) {
        if (IsCanceled(request)) {
            result.status = MfdProcessingStatus::Canceled;
            result.message = error;
            referenceDiagnostic.decisionReason = DecisionReason::Canceled;
            return result;
        }
        referenceDiagnostic.decisionReason = DecisionReason::OutOfMemory;
        publishReference(
            "The reference pyramid could not be built safely; exact reference published.");
        return result;
    }

    std::vector<AcceptedAlternate> accepted;
    try {
        accepted.reserve(request.frames.size() - 1u);
    } catch (const std::bad_alloc&) {
        publishReference(
            "Alternate bookkeeping exceeded available memory; exact reference published.");
        return result;
    }
    const std::size_t alternateCount = request.frames.size() - 1u;
    std::size_t alternatePosition = 0u;
    for (std::size_t index = 0u; index < request.frames.size(); ++index) {
        if (index == request.referenceFrameIndex) continue;
        const std::size_t currentAlternatePosition = alternatePosition++;
        const double alternateSpan = 0.48 /
            static_cast<double>(std::max<std::size_t>(1u, alternateCount));
        const double alternateBase = 0.30 + alternateSpan *
            static_cast<double>(currentAlternatePosition);
        const std::uint32_t frameOrdinal =
            static_cast<std::uint32_t>(index + 1u);
        MfdProcessingFrameDiagnostic& diagnostic =
            result.diagnostics.frames[index];
        if (IsCanceled(request)) {
            result.status = MfdProcessingStatus::Canceled;
            result.message = "MFD processing was canceled; no new result was published.";
            return result;
        }
        diagnostic.attempted = true;
        MaterializedFrame alternate;
        error.clear();
        ReportProgress(
            request,
            MfdProcessingStage::PreparingAlternate,
            alternateBase,
            0.0,
            "Decoding and normalizing alternate " +
                std::to_string(currentAlternatePosition + 1u) + " of " +
                std::to_string(alternateCount) + ".",
            currentAlternatePosition,
            alternateCount,
            frameOrdinal);
        if (!PrepareFrame(
                request.frames[index],
                request.parameters,
                services,
                cache,
                request.shouldCancel,
                [&request,
                 alternateBase,
                 alternateSpan,
                 currentAlternatePosition,
                 alternateCount,
                 frameOrdinal](double fraction) {
                    ReportProgress(
                        request,
                        MfdProcessingStage::PreparingAlternate,
                        alternateBase + alternateSpan * 0.30 * fraction,
                        0.6 * fraction,
                        "Decoding and normalizing alternate " +
                            std::to_string(currentAlternatePosition + 1u) +
                            " of " + std::to_string(alternateCount) + ".",
                        currentAlternatePosition,
                        alternateCount,
                        frameOrdinal);
                },
                alternate,
                error)) {
            if (IsCanceled(request)) {
                result.status = MfdProcessingStatus::Canceled;
                result.message = error;
                diagnostic.decisionReason = DecisionReason::Canceled;
                diagnostic.message = error;
                return result;
            }
            diagnostic.decisionReason = PreparationDecisionReason(false);
            diagnostic.message = error;
            continue;
        }
        diagnostic.prepared = true;
        diagnostic.noiseQuality = alternate.noise.quality;
        if (alternate.prepared.activeExtent.width !=
                reference.prepared.activeExtent.width ||
            alternate.prepared.activeExtent.height !=
                reference.prepared.activeExtent.height ||
            alternate.prepared.activeCfaPattern !=
                reference.prepared.activeCfaPattern ||
            alternate.metadata.orientation != reference.metadata.orientation) {
            diagnostic.decisionReason = DecisionReason::AlternateIncompatible;
            diagnostic.message =
                "The alternate active geometry, CFA, or orientation differs from the reference.";
            continue;
        }
        ++result.diagnostics.compatibleAlternateCount;
        if (alternate.noise.quality == NoiseModelQuality::Unavailable) {
            diagnostic.decisionReason = DecisionReason::NoiseModelUnavailable;
            diagnostic.message =
                "The alternate has no safe noise model and was skipped.";
            continue;
        }
        if (!MaterializePreparedFrame(
                alternate,
                cache,
                request.shouldCancel,
                [&request,
                 alternateBase,
                 alternateSpan,
                 currentAlternatePosition,
                 alternateCount,
                 frameOrdinal](double fraction) {
                    ReportProgress(
                        request,
                        MfdProcessingStage::PreparingAlternate,
                        alternateBase + alternateSpan *
                            (0.30 + 0.10 * fraction),
                        0.6 + 0.2 * fraction,
                        "Materializing alternate " +
                            std::to_string(currentAlternatePosition + 1u) +
                            " of " + std::to_string(alternateCount) + ".",
                        currentAlternatePosition,
                        alternateCount,
                        frameOrdinal);
                },
                error) ||
            (request.alignmentMode != MfdAlignmentMode::Identity &&
             !BuildFramePyramid(
                alternate,
                request.parameters,
                request.shouldCancel,
                [&request,
                 alternateBase,
                 alternateSpan,
                 currentAlternatePosition,
                 alternateCount,
                 frameOrdinal](double fraction) {
                    ReportProgress(
                        request,
                        MfdProcessingStage::PreparingAlternate,
                        alternateBase + alternateSpan *
                            (0.40 + 0.10 * fraction),
                        0.8 + 0.2 * fraction,
                        "Building the CFA pyramid for alternate " +
                            std::to_string(currentAlternatePosition + 1u) +
                            " of " + std::to_string(alternateCount) + ".",
                        currentAlternatePosition,
                        alternateCount,
                        frameOrdinal);
                },
                error))) {
            diagnostic.decisionReason = IsCanceled(request)
                ? DecisionReason::Canceled
                : DecisionReason::OutOfMemory;
            diagnostic.message = error;
            if (IsCanceled(request)) {
                result.status = MfdProcessingStatus::Canceled;
                result.message = error;
                return result;
            }
            continue;
        }

        AcceptedAlternate candidate;
        candidate.frame = std::move(alternate);
        AffineRefinementResult affine;
        error.clear();
        if (!RegisterAlternate(
                reference,
                candidate.frame,
                request.parameters,
                request.alignmentMode,
                request.shouldCancel,
                [&request,
                 alternateBase,
                 alternateSpan,
                 currentAlternatePosition,
                 alternateCount,
                 frameOrdinal,
                 alignmentMode = request.alignmentMode](double fraction) {
                    const char* action = alignmentMode ==
                            MfdAlignmentMode::Identity
                        ? "Checking identity-coordinate alternate "
                        : alignmentMode == MfdAlignmentMode::TranslationOnly
                            ? "Estimating translation for alternate "
                            : "Registering alternate ";
                    ReportProgress(
                        request,
                        MfdProcessingStage::RegisteringAlternate,
                        alternateBase + alternateSpan *
                            (0.50 + 0.50 * fraction),
                        fraction,
                        std::string(action) +
                            std::to_string(currentAlternatePosition + 1u) +
                            " of " + std::to_string(alternateCount) + ".",
                        currentAlternatePosition,
                        alternateCount,
                        frameOrdinal);
                },
                candidate.alignment,
                affine,
                candidate.motion,
                candidate.reliability,
                error)) {
            if (IsCanceled(request)) {
                result.status = MfdProcessingStatus::Canceled;
                result.message =
                    "MFD processing was canceled during alternate registration; no new result was published.";
                diagnostic.decisionReason = DecisionReason::Canceled;
                diagnostic.message = result.message;
                return result;
            }
            diagnostic.decisionReason =
                candidate.alignment.accepted
                    ? DecisionReason::LocalRegistrationFailed
                    : DecisionReason::GlobalRegistrationFailed;
            diagnostic.message = error;
            continue;
        }
        diagnostic.acceptedForFusion = true;
        diagnostic.globalAlignment = candidate.alignment.choice;
        diagnostic.globalRegistrationPerformed =
            request.alignmentMode != MfdAlignmentMode::Identity;
        diagnostic.localRegistrationPerformed =
            request.alignmentMode == MfdAlignmentMode::Full;
        diagnostic.globalTranslationRaw =
            candidate.alignment.model.translationRaw;
        diagnostic.exposureScale = candidate.alignment.exposureScale;
        diagnostic.structuredMotionNodes =
            candidate.motion.forward.structuredCount;
        diagnostic.flatSafeMotionNodes =
            candidate.motion.forward.flatSafeCount;
        diagnostic.rejectedMotionNodes =
            candidate.motion.forward.rejectedCount;
        diagnostic.usableReliabilityCells =
            candidate.reliability.usableCellCount;
        diagnostic.message = request.alignmentMode == MfdAlignmentMode::Identity
            ? "Alternate accepted for robust fusion with identity coordinates; geometric and exposure registration were skipped."
            : request.alignmentMode == MfdAlignmentMode::TranslationOnly
                ? "Alternate accepted for robust fusion after global translation; affine and local registration were skipped."
                : "Alternate accepted for robust fusion with full registration.";
        candidate.frame.pyramid = {};
        accepted.push_back(std::move(candidate));
    }
    reference.pyramid = {};
    result.diagnostics.acceptedAlternateCount = accepted.size();
    if (accepted.empty()) {
        publishReference(
            "Every alternate was rejected by compatibility, noise, registration, or reliability checks; exact reference published.");
        return result;
    }

    AtomicFusionResultPublisher publisher;
    StreamingFusionRequest streaming;
    streaming.rawExtent = reference.prepared.activeExtent;
    streaming.parameters = request.parameters;
    streaming.alternateCount = accepted.size();
    streaming.fusedResultCacheKey = BuildResultCacheKey(
        reference, accepted, request.parameters, request.alignmentMode);
    streaming.memoryBudgetBytes = FusionWorkingBudget(
        reference.prepared.activeExtent,
        accepted.size(),
        request.parameters,
        request.workerCount);
    streaming.workerCount = request.workerCount;
    streaming.referenceProvider = MakeReferenceProvider(
        reference, request.parameters);
    streaming.candidateProvider = MakeCandidateProvider(
        reference, accepted, request.parameters);
    streaming.publisher = &publisher;
    streaming.shouldCancel = request.shouldCancel;
    streaming.reportProgress = [&request](
        std::uint64_t completed,
        std::uint64_t total) {
        const double fraction = total == 0u
            ? 0.0
            : static_cast<double>(completed) /
                static_cast<double>(total);
        ReportProgress(
            request,
            MfdProcessingStage::FusingTiles,
            0.78 + 0.17 * fraction,
            fraction,
            "Fusing output tiles.",
            completed,
            total);
    };
    StreamingFusionResult fusion = ExecuteStreamingFusion(streaming);
    result.diagnostics.streaming = fusion.diagnostics;
    if (fusion.status == StreamingFusionStatus::Canceled) {
        result.status = MfdProcessingStatus::Canceled;
        result.message = fusion.message;
        return result;
    }
    if (fusion.status != StreamingFusionStatus::Success ||
        !fusion.published.result) {
        result.message = fusion.message;
        return result;
    }
    result.published = std::move(fusion.published);
    for (const FusionPixelDiagnostics& diagnostic :
         result.published.result->diagnostics) {
        if (diagnostic.contributingAlternateCount > 0u) {
            ++result.diagnostics.contributingPixelCount;
        }
        if (diagnostic.exactReferenceCopy) {
            ++result.diagnostics.exactReferencePixelCount;
        }
    }
    if (result.diagnostics.contributingPixelCount == 0u) {
        result.status = MfdProcessingStatus::ReferenceOnly;
        result.message =
            "All registered alternates were rejected per pixel; the published image is the exact reference.";
    } else {
        result.status = MfdProcessingStatus::DenoisedCandidate;
        result.message =
            "Experimental normalized Bayer denoise candidate published atomically.";
    }
    result.referenceNormalizedMosaic = std::move(reference.normalized);
    ReportProgress(
        request,
        MfdProcessingStage::Finalizing,
        0.95,
        1.0,
        "The Bayer result is complete; preparing inspection output.");
    return result;
}

nlohmann::json SerializeMfdProcessingResult(
    const MfdProcessingResult& result) {
    nlohmann::json frames = nlohmann::json::array();
    for (const MfdProcessingFrameDiagnostic& frame :
         result.diagnostics.frames) {
        frames.push_back({
            { "stableFrameId", frame.stableFrameId },
            { "referenceFrame", frame.referenceFrame },
            { "attempted", frame.attempted },
            { "prepared", frame.prepared },
            { "acceptedForFusion", frame.acceptedForFusion },
            { "decisionReason", DecisionReasonName(frame.decisionReason) },
            { "message", frame.message },
            { "noiseQuality", NoiseModelQualityName(frame.noiseQuality) },
            { "globalAlignment", frame.globalAlignment ==
                    GlobalFrameAlignmentChoice::Affine
                ? "affine"
                : frame.globalAlignment == GlobalFrameAlignmentChoice::Translation
                    ? "translation"
                    : frame.globalAlignment == GlobalFrameAlignmentChoice::Identity
                        ? "identity"
                    : "reject" },
            { "globalRegistrationPerformed",
                frame.globalRegistrationPerformed },
            { "localRegistrationPerformed",
                frame.localRegistrationPerformed },
            { "globalTranslationRaw", {
                frame.globalTranslationRaw.x,
                frame.globalTranslationRaw.y } },
            { "exposureScale", frame.exposureScale },
            { "structuredMotionNodes", frame.structuredMotionNodes },
            { "flatSafeMotionNodes", frame.flatSafeMotionNodes },
            { "rejectedMotionNodes", frame.rejectedMotionNodes },
            { "usableReliabilityCells", frame.usableReliabilityCells }
        });
    }
    nlohmann::json value = {
        { "contractId", result.contractId },
        { "contractVersion", result.contractVersion },
        { "experimental", true },
        { "productionApproved", false },
        { "status", MfdProcessingStatusName(result.status) },
        { "message", result.message },
        { "outputContractId", kOutputContractId },
        { "outputCfaPattern", CfaPatternName(result.outputCfaPattern) },
        { "alignmentMode", MfdAlignmentModeId(
            result.diagnostics.alignmentMode) },
        { "estimatedPeakResidentBytes",
            result.diagnostics.estimatedPeakResidentBytes },
        { "compatibleAlternateCount",
            result.diagnostics.compatibleAlternateCount },
        { "acceptedAlternateCount",
            result.diagnostics.acceptedAlternateCount },
        { "contributingPixelCount",
            result.diagnostics.contributingPixelCount },
        { "exactReferencePixelCount",
            result.diagnostics.exactReferencePixelCount },
        { "streaming", {
            { "plannedTileCount",
                result.diagnostics.streaming.plannedTileCount },
            { "computedTileCount",
                result.diagnostics.streaming.computedTileCount },
            { "referenceTileFallbackCount",
                result.diagnostics.streaming.referenceTileFallbackCount },
            { "peakWorkingBytes",
                result.diagnostics.streaming.peakWorkingBytes },
            { "effectiveWorkerCount",
                result.diagnostics.streaming.effectiveWorkerCount }
        } },
        { "frames", std::move(frames) }
    };
    if (result.published.result) {
        value["published"] = {
            { "generation", result.published.generation },
            { "width", result.published.result->extent.width },
            { "height", result.published.result->extent.height },
            { "fusedResultCacheKey",
                result.published.result->fusedResultCacheKey },
            { "contentHash", result.published.result->contentHash }
        };
    } else {
        value["published"] = nullptr;
    }
    return value;
}

} // namespace Raw::Mfd
