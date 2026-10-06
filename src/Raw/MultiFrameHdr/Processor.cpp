#include "Raw/MultiFrameHdr/Processor.h"
#include "Raw/MultiFrameHdr/GpuFusion.h"

#include "NodeMath/ContractTypes.h"
#include "Raw/MultiFrameDenoise/NoiseModel.h"
#include "Raw/MultiFrameDenoise/Preparation.h"
#include "Raw/MultiFrameDenoise/SameCfaSampler.h"
#include "Raw/RawLoader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string_view>
#include <thread>
#include <type_traits>

namespace Raw::Hdr {
namespace {

using Raw::Mfd::CfaLayout;
using Raw::Mfd::CfaSite;
using Raw::Mfd::DirectoryNormalizedTileCache;
using Raw::Mfd::HasSampleFlag;
using Raw::Mfd::NoiseModel;
using Raw::Mfd::NoiseModelQuality;
using Raw::Mfd::PixelExtent;
using Raw::Mfd::PreparedRawFrame;
using Raw::Mfd::PreparedRawTile;
using Raw::Mfd::PreparedSampleFlag;
using Raw::Mfd::PreparationOptions;
using Raw::Mfd::PreparationResult;
using Raw::Mfd::TileCacheReadStatus;

constexpr std::array<char, 8> kCacheMagic { 'S', 'T', 'K', 'H', 'D', 'R', '1', '\0' };

bool Canceled(const Request& request) {
    return request.shouldCancel && request.shouldCancel();
}

void Report(
    const Request& request,
    ProcessingStage stage,
    double overall,
    double stageFraction,
    std::uint32_t ordinal,
    const std::string& message) {
    if (!request.reportProgress) return;
    Progress progress;
    progress.stage = stage;
    progress.overallFraction = std::clamp(overall, 0.0, 1.0);
    progress.stageFraction = std::clamp(stageFraction, 0.0, 1.0);
    progress.frameOrdinal = ordinal;
    progress.frameCount = static_cast<std::uint32_t>(request.frames.size());
    progress.message = message;
    request.reportProgress(progress);
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::string Sha256Digest(const std::string& content) {
    std::string identity = Stack::NodeMath::Sha256ContentIdentity(content);
    constexpr std::string_view prefix = "sha256:";
    if (identity.rfind(prefix, 0u) == 0u) {
        identity.erase(0u, prefix.size());
    }
    return identity;
}

std::string Sha256Digest(
    const std::vector<std::string_view>& contentChunks) {
    std::string identity = Stack::NodeMath::Sha256ContentIdentity(
        contentChunks);
    constexpr std::string_view prefix = "sha256:";
    if (identity.rfind(prefix, 0u) == 0u)
        identity.erase(0u, prefix.size());
    return identity;
}

bool IsRejectedFlag(std::uint8_t flags) {
    return HasSampleFlag(flags, PreparedSampleFlag::Saturated) ||
        HasSampleFlag(flags, PreparedSampleFlag::Defective) ||
        HasSampleFlag(flags, PreparedSampleFlag::DecoderRepaired) ||
        HasSampleFlag(flags, PreparedSampleFlag::ExplicitDecoderClip);
}

double ExposureMetric(const RawMetadata& metadata) {
    const double shutter = metadata.hasExposureTime
        ? std::max(1.0e-9, static_cast<double>(metadata.exposureTimeSeconds))
        : 1.0;
    const double iso = metadata.hasIsoSpeed
        ? std::max(1.0, static_cast<double>(metadata.isoSpeed))
        : 100.0;
    const double aperture = metadata.hasApertureFNumber
        ? std::max(0.1, static_cast<double>(metadata.apertureFNumber))
        : 1.0;
    return shutter * iso / (aperture * aperture);
}

struct MaterializedFrame {
    FrameInput input;
    RawMetadata metadata;
    PreparedRawFrame prepared;
    NoiseModel noise;
    CfaLayout layout;
    std::vector<float> normalized;
    std::vector<float> gain;
    std::vector<std::uint8_t> flags;
    std::shared_ptr<const std::vector<float>> explicitVariance;
    std::shared_ptr<const std::vector<float>> effectiveSupport;
    std::vector<std::uint8_t> clippedNeighborCount;
    double metadataExposure = 1.0;
    double exposureRelativeToReference = 1.0;
    double exposureRelativeToAnchor = 1.0;
    double exposureUncertaintyEv = 0.0;
    bool exposureVerified = false;
    bool lowConfidence = false;
    double noiseVarianceInflation = 1.0;
    double translationX = 0.0;
    double translationY = 0.0;
    double texture = 0.0;
};

bool DefaultLoad(
    const std::filesystem::path& path,
    RawImageData& frame,
    const std::function<bool()>& shouldCancel,
    std::string& error) {
    if (!RawLoader::LoadFile(path.u8string(), frame, shouldCancel)) {
        error = frame.metadata.error.empty()
            ? "The RAW frame could not be decoded."
            : frame.metadata.error;
        return false;
    }
    error.clear();
    return true;
}

bool BuildClippedNeighborhoodMap(
    MaterializedFrame& frame,
    std::uint32_t radius,
    const std::function<void(double)>& reportProgress,
    std::string& error) {
    const std::uint64_t rawWidth = frame.prepared.activeExtent.width;
    const std::uint64_t rawHeight = frame.prepared.activeExtent.height;
    const std::size_t rawCount = static_cast<std::size_t>(rawWidth * rawHeight);
    try {
        frame.clippedNeighborCount.assign(rawCount, 0u);
        constexpr std::array<CfaSite, 4> sites {
            CfaSite::Red, CfaSite::Green0, CfaSite::Green1, CfaSite::Blue };
        if (reportProgress) reportProgress(0.0);
        for (std::size_t siteIndex = 0u; siteIndex < sites.size(); ++siteIndex) {
            const CfaSite site = sites[siteIndex];
            const PixelExtent extent = frame.layout.PlaneExtent(
                site, frame.prepared.activeExtent);
            const std::size_t planeWidth = static_cast<std::size_t>(extent.width);
            const std::size_t planeHeight = static_cast<std::size_t>(extent.height);
            const std::size_t integralStride = planeWidth + 1u;
            std::vector<std::uint32_t> integral(
                integralStride * (planeHeight + 1u), 0u);
            for (std::size_t y = 0; y < planeHeight; ++y) {
                std::uint32_t rowSum = 0u;
                for (std::size_t x = 0; x < planeWidth; ++x) {
                    const auto raw = frame.layout.PlanePixelToRaw({
                        static_cast<std::int64_t>(x),
                        static_cast<std::int64_t>(y),
                        site });
                    const std::size_t rawIndex = static_cast<std::size_t>(
                        static_cast<std::uint64_t>(std::llround(raw.y)) * rawWidth +
                        static_cast<std::uint64_t>(std::llround(raw.x)));
                    const std::uint8_t flags = frame.flags[rawIndex];
                    const bool clipped =
                        HasSampleFlag(flags, PreparedSampleFlag::Saturated) ||
                        HasSampleFlag(flags, PreparedSampleFlag::ExplicitDecoderClip) ||
                        frame.normalized[rawIndex] >= 0.985f;
                    rowSum += clipped ? 1u : 0u;
                    integral[(y + 1u) * integralStride + x + 1u] =
                        integral[y * integralStride + x + 1u] + rowSum;
                }
                if (reportProgress &&
                    ((y + 1u) % 64u == 0u || y + 1u == planeHeight)) {
                    const double passFraction = planeHeight == 0u
                        ? 1.0
                        : static_cast<double>(y + 1u) /
                            static_cast<double>(planeHeight);
                    reportProgress((static_cast<double>(siteIndex) * 2.0 +
                        passFraction) / 8.0);
                }
            }
            for (std::size_t y = 0; y < planeHeight; ++y) {
                const std::size_t y0 = y > radius ? y - radius : 0u;
                const std::size_t y1 = std::min(
                    planeHeight, y + static_cast<std::size_t>(radius) + 1u);
                for (std::size_t x = 0; x < planeWidth; ++x) {
                    const std::size_t x0 = x > radius ? x - radius : 0u;
                    const std::size_t x1 = std::min(
                        planeWidth, x + static_cast<std::size_t>(radius) + 1u);
                    std::uint32_t count =
                        integral[y1 * integralStride + x1] -
                        integral[y0 * integralStride + x1] -
                        integral[y1 * integralStride + x0] +
                        integral[y0 * integralStride + x0];
                    const auto raw = frame.layout.PlanePixelToRaw({
                        static_cast<std::int64_t>(x),
                        static_cast<std::int64_t>(y),
                        site });
                    const std::size_t rawIndex = static_cast<std::size_t>(
                        static_cast<std::uint64_t>(std::llround(raw.y)) * rawWidth +
                        static_cast<std::uint64_t>(std::llround(raw.x)));
                    const std::uint8_t flags = frame.flags[rawIndex];
                    const bool centerClipped =
                        HasSampleFlag(flags, PreparedSampleFlag::Saturated) ||
                        HasSampleFlag(flags, PreparedSampleFlag::ExplicitDecoderClip) ||
                        frame.normalized[rawIndex] >= 0.985f;
                    if (centerClipped && count > 0u) --count;
                    frame.clippedNeighborCount[rawIndex] =
                        static_cast<std::uint8_t>(std::min<std::uint32_t>(count, 255u));
                }
                if (reportProgress &&
                    ((y + 1u) % 64u == 0u || y + 1u == planeHeight)) {
                    const double passFraction = planeHeight == 0u
                        ? 1.0
                        : static_cast<double>(y + 1u) /
                            static_cast<double>(planeHeight);
                    reportProgress((static_cast<double>(siteIndex) * 2.0 +
                        1.0 + passFraction) / 8.0);
                }
            }
        }
    } catch (const std::bad_alloc&) {
        error = "The HDR highlight-support map could not fit the memory budget.";
        return false;
    }
    return true;
}

bool Materialize(
    MaterializedFrame& frame,
    Raw::Mfd::NormalizedTileCache& cache,
    const Request& request,
    const std::function<void(double)>& reportProgress,
    std::string& error) {
    const std::uint64_t width = frame.prepared.activeExtent.width;
    const std::uint64_t height = frame.prepared.activeExtent.height;
    if (width == 0u || height == 0u ||
        width > std::numeric_limits<std::size_t>::max() / height) {
        error = "The HDR frame dimensions overflow the processor.";
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(width * height);
    try {
        frame.normalized.assign(count, 0.0f);
        frame.gain.assign(count, 1.0f);
        frame.flags.assign(count, 0u);
    } catch (const std::bad_alloc&) {
        error = "The HDR frame could not fit the memory budget.";
        return false;
    }
    const std::uint64_t totalTiles =
        static_cast<std::uint64_t>(frame.prepared.tileRows) *
        static_cast<std::uint64_t>(frame.prepared.tileColumns);
    std::uint64_t completedTiles = 0u;
    if (reportProgress) reportProgress(0.0);
    for (std::uint32_t tileY = 0; tileY < frame.prepared.tileRows; ++tileY) {
        for (std::uint32_t tileX = 0; tileX < frame.prepared.tileColumns; ++tileX) {
            if (Canceled(request)) {
                error = "HDR processing was canceled.";
                return false;
            }
            PreparedRawTile tile;
            std::string tileError;
            if (Raw::Mfd::ReadPreparedTile(
                    frame.prepared, cache, tileX, tileY, tile, &tileError) !=
                TileCacheReadStatus::Hit) {
                error = tileError.empty() ? "A prepared HDR tile is unavailable." : tileError;
                return false;
            }
            for (std::uint64_t y = 0; y < tile.extent.height; ++y) {
                const std::size_t src = static_cast<std::size_t>(y * tile.extent.width);
                const std::size_t dst = static_cast<std::size_t>(
                    (tile.originY + y) * width + tile.originX);
                const std::size_t row = static_cast<std::size_t>(tile.extent.width);
                std::copy_n(tile.normalizedMosaic.begin() + src, row, frame.normalized.begin() + dst);
                std::copy_n(tile.comparisonGain.begin() + src, row, frame.gain.begin() + dst);
                std::copy_n(tile.sampleFlags.begin() + src, row, frame.flags.begin() + dst);
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

bool Prepare(
    const FrameInput& input,
    const Request& request,
    const Services& services,
    Raw::Mfd::NormalizedTileCache& cache,
    MaterializedFrame& frame,
    const std::function<void(double, const char*)>& reportProgress,
    std::string& error) {
    if (reportProgress) reportProgress(0.0, "Decoding RAW source");
    RawImageData raw;
    if (!services.loadRawFrame || !services.loadRawFrame(
            input.sourcePath, raw, request.shouldCancel, error)) return false;
    if (reportProgress) reportProgress(0.10, "Decoded RAW source");
    if ((!input.expectedSourceSha256.empty() &&
         (!LooksLikeSha256(input.expectedSourceSha256) ||
          raw.metadata.sourceContentSha256 != input.expectedSourceSha256)) ||
        (input.expectedSourceByteLength != 0u &&
         raw.metadata.sourceByteSize != input.expectedSourceByteLength)) {
        error = "Decoded RAW identity does not match the embedded HDR source.";
        return false;
    }
    const auto explicitVariance = raw.multiFrameMeasurementSidecars
        ? raw.multiFrameMeasurementSidecars->variance
        : std::shared_ptr<const std::vector<float>> {};
    Raw::Mfd::Parameters preparationParameters;
    preparationParameters.fusion.outputTileRawPixels = request.parameters.tileRawPixels;
    PreparationOptions options = Raw::Mfd::MakePreparationOptions(preparationParameters);
    options.tileRawPixels = request.parameters.tileRawPixels;
    options.shouldCancel = request.shouldCancel;
    options.reportProgress = [reportProgress](
        std::uint64_t completed,
        std::uint64_t total) {
        if (!reportProgress) return;
        const double fraction = total == 0u
            ? 1.0
            : static_cast<double>(completed) / static_cast<double>(total);
        reportProgress(
            0.10 + 0.40 * std::clamp(fraction, 0.0, 1.0),
            "Canonical RAW preparation");
    };
    PreparationResult prepared = Raw::Mfd::PrepareRawFrame(raw, options, cache);
    if (!prepared.success) {
        error = prepared.message;
        return false;
    }
    Raw::Mfd::NoiseResolutionOptions noiseOptions;
    noiseOptions.enableBurstEstimate = true;
    noiseOptions.enableGenericLowConfidence = true;
    for (auto& site : noiseOptions.genericLowConfidenceSites) {
        site.shotScale = 1.0 / 4096.0;
        site.offsetVariance = 4.0 / (4096.0 * 4096.0);
        site.quantizationVariance = 1.0 / (12.0 * 4096.0 * 4096.0);
        site.residualModelTau0 = site.offsetVariance;
        site.residualModelTau1 = site.shotScale;
        site.quantizationIncluded = true;
    }
    Raw::Mfd::NoiseResolutionResult noise = Raw::Mfd::ResolveNoiseModel(
        raw.metadata, prepared.frame, &cache, noiseOptions);
    if (reportProgress) reportProgress(0.55, "Resolved RAW noise model");
    frame = {};
    frame.input = input;
    frame.metadata = raw.metadata;
    frame.prepared = std::move(prepared.frame);
    frame.noise = std::move(noise.model);
    frame.explicitVariance = explicitVariance;
    frame.effectiveSupport = raw.multiFrameMeasurementSidecars ? raw.multiFrameMeasurementSidecars->effectiveSupport : nullptr;
    frame.metadataExposure = ExposureMetric(frame.metadata);
    if (!CfaLayout::TryCreate(frame.prepared.activeCfaPattern, frame.layout)) {
        error = "The prepared HDR CFA layout is unsupported.";
        return false;
    }
    if (!Materialize(
            frame,
            cache,
            request,
            [reportProgress](double fraction) {
                if (reportProgress) reportProgress(
                    0.55 + 0.25 * std::clamp(fraction, 0.0, 1.0),
                    "Materializing normalized RAW tiles");
            },
            error)) return false;
    return BuildClippedNeighborhoodMap(
        frame,
        request.parameters.highlightNeighborhoodRadiusCfa,
        [reportProgress](double fraction) {
            if (reportProgress) reportProgress(
                0.80 + 0.20 * std::clamp(fraction, 0.0, 1.0),
                "Building highlight reliability map");
        },
        error);
}

double ComparisonAt(const MaterializedFrame& frame, std::size_t index) {
    return static_cast<double>(frame.normalized[index]) *
        static_cast<double>(frame.gain[index]);
}

double TextureScore(const MaterializedFrame& frame) {
    const std::uint64_t width = frame.prepared.activeExtent.width;
    const std::uint64_t height = frame.prepared.activeExtent.height;
    double sum = 0.0;
    double square = 0.0;
    std::uint64_t count = 0;
    for (std::uint64_t y = 4; y + 4 < height; y += 16) {
        for (std::uint64_t x = 4; x + 4 < width; x += 16) {
            const std::size_t p = static_cast<std::size_t>(y * width + x);
            const std::size_t q = static_cast<std::size_t>(y * width + x + 2);
            if (IsRejectedFlag(frame.flags[p]) || IsRejectedFlag(frame.flags[q])) continue;
            const double d = ComparisonAt(frame, q) - ComparisonAt(frame, p);
            sum += d;
            square += d * d;
            ++count;
        }
    }
    if (count < 16u) return 0.0;
    const double mean = sum / static_cast<double>(count);
    return std::max(0.0, square / static_cast<double>(count) - mean * mean);
}

struct Sample {
    double comparison = 0.0;
    double normalized = 0.0;
    double gain = 1.0;
    double shotCoefficient = 0.0;
    double offsetVariance = 0.0;
    double variance = 0.0;
    double explicitVariance = -1.0;
    std::uint8_t flags = 0;
    bool valid = false;
};

bool SampleSameCfa(
    const MaterializedFrame& frame,
    CfaSite site,
    double rawX,
    double rawY,
    Sample& result) {
    result = {};
    const auto plane = frame.layout.RawToPlane({ rawX, rawY }, site);
    const auto extent = frame.layout.PlaneExtent(site, frame.prepared.activeExtent);
    const std::int64_t x0 = static_cast<std::int64_t>(std::floor(plane.x));
    const std::int64_t y0 = static_cast<std::int64_t>(std::floor(plane.y));
    if (x0 < 0 || y0 < 0 || x0 >= static_cast<std::int64_t>(extent.width) ||
        y0 >= static_cast<std::int64_t>(extent.height)) return false;
    const double fx = plane.x - static_cast<double>(x0);
    const double fy = plane.y - static_cast<double>(y0);
    const std::array<double, 4> weights {
        (1.0 - fx) * (1.0 - fy), fx * (1.0 - fy),
        (1.0 - fx) * fy, fx * fy
    };
    const std::array<std::pair<std::int64_t, std::int64_t>, 4> taps {{
        { x0, y0 }, { x0 + 1, y0 }, { x0, y0 + 1 }, { x0 + 1, y0 + 1 }
    }};
    const std::uint64_t width = frame.prepared.activeExtent.width;
    result.gain = 0.0;
    for (std::size_t i = 0; i < taps.size(); ++i) {
        if (weights[i] <= 0) continue;
        if (taps[i].first >= static_cast<std::int64_t>(extent.width) ||
            taps[i].second >= static_cast<std::int64_t>(extent.height)) return false;
        const auto raw = frame.layout.PlanePixelToRaw({ taps[i].first, taps[i].second, site });
        const auto ix = static_cast<std::uint64_t>(std::llround(raw.x));
        const auto iy = static_cast<std::uint64_t>(std::llround(raw.y));
        const std::size_t index = static_cast<std::size_t>(iy * width + ix);
        if (index >= frame.normalized.size() || IsRejectedFlag(frame.flags[index])) return false;
        const double gain = frame.gain[index];
        if (!std::isfinite(gain) || gain <= 0.0) return false;
        const double comparison = ComparisonAt(frame, index);
        if (!std::isfinite(comparison)) return false;
        result.comparison += weights[i] * comparison;
        result.normalized += weights[i] * frame.normalized[index];
        result.gain += weights[i] * gain;
        result.flags |= frame.flags[index];
        const auto& profile = frame.noise.sites[static_cast<std::size_t>(site)];
        const double squaredWeight = weights[i] * weights[i];
        // The noise model is expressed before the known comparison gain.
        // In comparison space, shot variance scales by gain while offset and
        // quantization variance scale by gain squared. Interpolation combines
        // independent tap variances with squared interpolation weights.
        result.shotCoefficient += squaredWeight * gain * profile.shotScale;
        result.offsetVariance += squaredWeight * gain * gain *
            (profile.offsetVariance + profile.quantizationVariance);
        if (frame.explicitVariance &&
            frame.explicitVariance->size() == frame.normalized.size()) {
            if (result.explicitVariance < 0.0) result.explicitVariance = 0.0;
            const double variance = (*frame.explicitVariance)[index];
            if (!std::isfinite(variance) || variance < 0.0) return false;
            result.explicitVariance +=
                squaredWeight * gain * gain * variance;
        }
    }
    result.valid = std::isfinite(result.comparison) &&
        std::isfinite(result.shotCoefficient) &&
        std::isfinite(result.offsetVariance) && result.gain > 0.0;
    return result.valid;
}

bool ExactSample(
    const MaterializedFrame& frame,
    CfaSite site,
    std::size_t index,
    Sample& result) {
    result = {};
    if (index >= frame.normalized.size() || IsRejectedFlag(frame.flags[index]))
        return false;
    const double gain = frame.gain[index];
    if (!std::isfinite(gain) || gain <= 0.0) return false;
    const auto& profile = frame.noise.sites[static_cast<std::size_t>(site)];
    result.comparison = ComparisonAt(frame, index);
    result.normalized = frame.normalized[index];
    result.gain = gain;
    result.shotCoefficient = gain * profile.shotScale;
    result.offsetVariance = gain * gain *
        (profile.offsetVariance + profile.quantizationVariance);
    if (frame.explicitVariance &&
        frame.explicitVariance->size() == frame.normalized.size()) {
        const double variance = (*frame.explicitVariance)[index];
        if (!std::isfinite(variance) || variance < 0.0) return false;
        result.explicitVariance = gain * gain * variance;
    }
    result.flags = frame.flags[index];
    result.valid = std::isfinite(result.comparison) &&
        std::isfinite(result.shotCoefficient) &&
        std::isfinite(result.offsetVariance);
    return result.valid;
}

// Publication must remain renderable even when every frame marks a sensor
// location as clipped, defective, or decoder-repaired. This is deliberately
// separate from ExactSample: flagged evidence never participates in the HDR
// estimate, but the geometric reference remains the final exact-data owner
// when no admissible equality sample exists. The categorical fallback flags
// preserve that distinction for diagnostics and downstream policy.
bool ExactPolicyFallbackSample(
    const MaterializedFrame& frame,
    CfaSite site,
    std::size_t index,
    Sample& result) {
    result = {};
    if (index >= frame.normalized.size() || index >= frame.gain.size())
        return false;
    const double normalized = frame.normalized[index];
    const double gain = frame.gain[index];
    if (!std::isfinite(normalized) || !std::isfinite(gain) || gain <= 0.0)
        return false;
    const auto& profile = frame.noise.sites[static_cast<std::size_t>(site)];
    result.comparison = normalized * gain;
    result.normalized = normalized;
    result.gain = gain;
    result.shotCoefficient = gain * profile.shotScale;
    result.offsetVariance = gain * gain *
        (profile.offsetVariance + profile.quantizationVariance);
    if (frame.explicitVariance &&
        frame.explicitVariance->size() == frame.normalized.size()) {
        const double variance = (*frame.explicitVariance)[index];
        if (std::isfinite(variance) && variance >= 0.0)
            result.explicitVariance = gain * gain * variance;
    }
    result.flags = frame.flags[index];
    result.valid = std::isfinite(result.comparison) &&
        std::isfinite(result.shotCoefficient) &&
        std::isfinite(result.offsetVariance);
    return result.valid;
}

double PredictedSampleVariance(
    const MaterializedFrame& frame,
    const Sample& sample,
    double predictedComparison,
    double numericalFloor) {
    const double modeled = frame.noiseVarianceInflation *
        (sample.explicitVariance >= 0.0
            ? sample.explicitVariance
            : sample.shotCoefficient * std::max(0.0, predictedComparison) +
                sample.offsetVariance);
    return std::max(numericalFloor,
        std::isfinite(modeled) ? modeled : std::max(1.0e-6, numericalFloor));
}

std::uint32_t ClippedSameCfaNeighborCountAt(
    const MaterializedFrame& frame,
    CfaSite site,
    double rawX,
    double rawY) {
    const auto center = frame.layout.RawToPlane({ rawX, rawY }, site);
    const auto extent = frame.layout.PlaneExtent(site, frame.prepared.activeExtent);
    const std::int64_t centerX = static_cast<std::int64_t>(std::llround(center.x));
    const std::int64_t centerY = static_cast<std::int64_t>(std::llround(center.y));
    if (centerX < 0 || centerY < 0 ||
        centerX >= static_cast<std::int64_t>(extent.width) ||
        centerY >= static_cast<std::int64_t>(extent.height)) return 0u;
    const auto raw = frame.layout.PlanePixelToRaw({ centerX, centerY, site });
    const std::size_t rawIndex = static_cast<std::size_t>(
        static_cast<std::uint64_t>(std::llround(raw.y)) *
            frame.prepared.activeExtent.width +
        static_cast<std::uint64_t>(std::llround(raw.x)));
    return rawIndex < frame.clippedNeighborCount.size()
        ? frame.clippedNeighborCount[rawIndex]
        : 0u;
}

bool LocalSameCfaMedian(
    const Result& result,
    const CfaLayout& layout,
    CfaSite site,
    std::uint64_t rawX,
    std::uint64_t rawY,
    std::uint32_t radius,
    double& median,
    std::size_t& sampleCount) {
    constexpr std::size_t kMaximumSamples = 48u;
    std::array<double, kMaximumSamples> samples {};
    sampleCount = 0u;
    const auto center = layout.RawToPlane({
        static_cast<double>(rawX), static_cast<double>(rawY) }, site);
    const auto extent = layout.PlaneExtent(site, { result.width, result.height });
    const std::int64_t centerX = static_cast<std::int64_t>(std::llround(center.x));
    const std::int64_t centerY = static_cast<std::int64_t>(std::llround(center.y));
    for (std::int64_t dy = -static_cast<std::int64_t>(radius);
         dy <= static_cast<std::int64_t>(radius); ++dy) {
        for (std::int64_t dx = -static_cast<std::int64_t>(radius);
             dx <= static_cast<std::int64_t>(radius); ++dx) {
            if (dx == 0 && dy == 0) continue;
            const std::int64_t x = centerX + dx;
            const std::int64_t y = centerY + dy;
            if (x < 0 || y < 0 ||
                x >= static_cast<std::int64_t>(extent.width) ||
                y >= static_cast<std::int64_t>(extent.height)) continue;
            const auto raw = layout.PlanePixelToRaw({ x, y, site });
            const std::uint64_t sampleX = static_cast<std::uint64_t>(std::llround(raw.x));
            const std::uint64_t sampleY = static_cast<std::uint64_t>(std::llround(raw.y));
            const std::size_t index = static_cast<std::size_t>(
                sampleY * result.width + sampleX);
            if (index >= result.virtualAnchorMosaic.size() ||
                result.validityMask[index] == 0u) continue;
            const double value = result.virtualAnchorMosaic[index];
            if (!std::isfinite(value)) continue;
            samples[sampleCount++] = value;
        }
    }
    if (sampleCount < 4u) return false;
    const std::size_t middle = sampleCount / 2u;
    std::nth_element(samples.begin(), samples.begin() + middle,
        samples.begin() + sampleCount);
    median = samples[middle];
    if ((sampleCount & 1u) == 0u) {
        const auto lower = std::max_element(samples.begin(), samples.begin() + middle);
        median = 0.5 * (median + *lower);
    }
    return std::isfinite(median);
}

double SmoothStep(double edge0, double edge1, double value) {
    if (!(edge1 > edge0)) return value >= edge1 ? 1.0 : 0.0;
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double AlignmentScore(
    const MaterializedFrame& reference,
    const MaterializedFrame& alternate,
    double dx,
    double dy,
    std::uint32_t stride = 24u) {
    const std::uint64_t width = reference.prepared.activeExtent.width;
    const std::uint64_t height = reference.prepared.activeExtent.height;
    double sumR = 0.0, sumA = 0.0, sumRR = 0.0, sumAA = 0.0, sumRA = 0.0;
    std::uint64_t count = 0;
    for (std::uint64_t y = 8; y + 8 < height; y += stride) {
        for (std::uint64_t x = 8; x + 8 < width; x += stride) {
            const CfaSite site = reference.layout.SiteAt(
                static_cast<std::int64_t>(x), static_cast<std::int64_t>(y));
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            if (IsRejectedFlag(reference.flags[index])) continue;
            Sample sample;
            if (!SampleSameCfa(alternate, site, x + dx, y + dy, sample)) continue;
            const double r = std::asinh(ComparisonAt(reference, index) /
                std::max(1.0e-12, reference.metadataExposure));
            const double a = std::asinh(sample.comparison /
                std::max(1.0e-12, alternate.metadataExposure));
            if (!std::isfinite(r) || !std::isfinite(a)) continue;
            sumR += r; sumA += a; sumRR += r * r; sumAA += a * a; sumRA += r * a;
            ++count;
        }
    }
    if (count < 64u) return -std::numeric_limits<double>::infinity();
    const double n = static_cast<double>(count);
    const double covariance = sumRA - sumR * sumA / n;
    const double varR = sumRR - sumR * sumR / n;
    const double varA = sumAA - sumA * sumA / n;
    if (varR <= 1.0e-15 || varA <= 1.0e-15) return -std::numeric_limits<double>::infinity();
    return covariance / std::sqrt(varR * varA);
}

bool EstimateTranslation(
    const MaterializedFrame& reference,
    const MaterializedFrame& alternate,
    double& dx,
    double& dy,
    bool preserveCfa = false) {
    double best = AlignmentScore(reference, alternate, 0.0, 0.0);
    dx = 0.0;
    dy = 0.0;
    for (int y = -16; y <= 16; y += 2) {
        for (int x = -16; x <= 16; x += 2) {
            const double score = AlignmentScore(reference, alternate, x, y, 32u);
            if (score > best) {
                best = score;
                dx = static_cast<double>(x);
                dy = static_cast<double>(y);
            }
        }
    }
    if (preserveCfa) return std::isfinite(best) && best > 0.25;
    for (double step : { 1.0, 0.5, 0.25, 0.125, 0.0625 }) {
        double localX = dx;
        double localY = dy;
        double localBest = AlignmentScore(reference, alternate, dx, dy, 20u);
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                const double candidateX = dx + ox * step;
                const double candidateY = dy + oy * step;
                const double score = AlignmentScore(
                    reference, alternate, candidateX, candidateY, 20u);
                if (score > localBest) {
                    localBest = score;
                    localX = candidateX;
                    localY = candidateY;
                }
            }
        }
        dx = localX;
        dy = localY;
        best = localBest;
    }
    return std::isfinite(best) && best > 0.25;
}

template <typename RandomAccessIterator>
double MedianRange(RandomAccessIterator begin, RandomAccessIterator end) {
    const std::size_t count = static_cast<std::size_t>(end - begin);
    if (count == 0u) return std::numeric_limits<double>::quiet_NaN();
    const std::size_t middle = count / 2u;
    std::nth_element(begin, begin + middle, end);
    double result = *(begin + middle);
    if ((count & 1u) == 0u) {
        const auto lower = std::max_element(begin, begin + middle);
        result = 0.5 * (result + *lower);
    }
    return result;
}

double Median(std::vector<double>& values) {
    return MedianRange(values.begin(), values.end());
}

struct PairObservation {
    double from = 0.0;
    double to = 0.0;
    Sample fromSample;
    Sample toSample;
};

struct ExposureFitEdge {
    std::size_t from = 0u;
    std::size_t to = 0u;
    ExposureFitEdgeDiagnostic diagnostic;
};

bool SampleAligned(
    const std::vector<MaterializedFrame>& frames,
    std::size_t frameIndex,
    std::size_t geometricReference,
    CfaSite site,
    std::uint64_t x,
    std::uint64_t y,
    Sample& sample) {
    if (frameIndex == geometricReference) {
        const std::size_t index = static_cast<std::size_t>(
            y * frames[geometricReference].prepared.activeExtent.width + x);
        return ExactSample(frames[frameIndex], site, index, sample);
    }
    return SampleSameCfa(
        frames[frameIndex],
        site,
        static_cast<double>(x) + frames[frameIndex].translationX,
        static_cast<double>(y) + frames[frameIndex].translationY,
        sample);
}

ExposureFitEdge FitExposurePair(
    const std::vector<MaterializedFrame>& frames,
    std::size_t from,
    std::size_t to,
    std::size_t geometricReference,
    const Request& request,
    bool& wasCanceled) {
    ExposureFitEdge edge;
    edge.from = from;
    edge.to = to;
    edge.diagnostic.fromFrameId = frames[from].input.stableFrameId;
    edge.diagnostic.toFrameId = frames[to].input.stableFrameId;
    const std::uint64_t width =
        frames[geometricReference].prepared.activeExtent.width;
    const std::uint64_t height =
        frames[geometricReference].prepared.activeExtent.height;
    std::array<std::vector<PairObservation>, 4> observations;

    // An odd stride walks all four CFA phases. The previous even stride
    // repeatedly landed on one phase and could certify an exposure fit from a
    // single green plane.
    for (std::uint64_t y = 8; y + 8 < height; y += 11) {
        if (Canceled(request)) {
            wasCanceled = true;
            return edge;
        }
        for (std::uint64_t x = 8; x + 8 < width; x += 11) {
            const CfaSite site = frames[geometricReference].layout.SiteAt(
                static_cast<std::int64_t>(x), static_cast<std::int64_t>(y));
            Sample a;
            Sample b;
            if (!SampleAligned(frames, from, geometricReference, site, x, y, a) ||
                !SampleAligned(frames, to, geometricReference, site, x, y, b) ||
                a.comparison < 0.01 || a.comparison > 0.82 ||
                b.comparison < 0.005 || b.comparison > 0.82) {
                continue;
            }
            const double ratio = b.comparison / a.comparison;
            if (!std::isfinite(ratio) || ratio <= 1.0 / 1024.0 || ratio >= 1024.0)
                continue;
            observations[static_cast<std::size_t>(site)].push_back(
                { a.comparison, b.comparison, a, b });
        }
    }

    std::vector<double> siteScales;
    std::vector<double> siteOffsets;
    std::vector<double> siteUncertainties;
    const std::uint64_t minimumPerSite = std::max<std::uint64_t>(
        4u, request.parameters.minimumExposureFitSamples / 4u);
    for (std::size_t site = 0; site < observations.size(); ++site) {
        auto& values = observations[site];
        edge.diagnostic.siteSampleCounts[site] = values.size();
        edge.diagnostic.sampleCount += values.size();
        if (values.size() < minimumPerSite) continue;

        std::vector<double> ratios;
        ratios.reserve(values.size());
        for (const PairObservation& value : values)
            ratios.push_back(value.to / value.from);
        double scale = Median(ratios);
        if (!std::isfinite(scale) || scale <= 0.0) continue;

        std::vector<double> offsets;
        offsets.reserve(values.size());
        for (const PairObservation& value : values)
            offsets.push_back(value.to - scale * value.from);
        double offset = std::clamp(Median(offsets), -0.002, 0.002);

        ratios.clear();
        for (const PairObservation& value : values) {
            const double adjusted = value.to - offset;
            if (adjusted > 0.0) ratios.push_back(adjusted / value.from);
        }
        if (ratios.size() < minimumPerSite) continue;
        scale = Median(ratios);
        if (!std::isfinite(scale) || scale <= 0.0) continue;

        std::vector<double> residualsEv;
        residualsEv.reserve(values.size());
        for (const PairObservation& value : values) {
            const double predicted = scale * value.from + offset;
            if (predicted > 0.0 && value.to > 0.0) {
                residualsEv.push_back(std::abs(std::log2(value.to / predicted)));
            }
        }
        if (residualsEv.size() < minimumPerSite) continue;
        siteScales.push_back(scale);
        siteOffsets.push_back(offset);
        siteUncertainties.push_back(std::max(0.005, 1.4826 * Median(residualsEv)));
    }

    // Require independent evidence from every Bayer phase. A rejected pair is
    // still retained in diagnostics, but it cannot connect the scale graph.
    if (edge.diagnostic.sampleCount < request.parameters.minimumExposureFitSamples ||
        siteScales.size() != 4u) {
        edge.diagnostic.uncertaintyEv = 1.0;
        return edge;
    }

    edge.diagnostic.fittedScale = Median(siteScales);
    edge.diagnostic.fittedOffset = Median(siteOffsets);
    std::vector<double> siteDisagreement;
    siteDisagreement.reserve(siteScales.size());
    for (double scale : siteScales) {
        siteDisagreement.push_back(std::abs(std::log2(
            scale / edge.diagnostic.fittedScale)));
    }
    const double betweenSiteUncertainty = 1.4826 * Median(siteDisagreement);
    const double withinSiteUncertainty = Median(siteUncertainties);
    edge.diagnostic.uncertaintyEv = std::max(
        0.005, std::hypot(betweenSiteUncertainty, withinSiteUncertainty));
    edge.diagnostic.verified = std::isfinite(edge.diagnostic.fittedScale) &&
        edge.diagnostic.fittedScale > 0.0 &&
        std::isfinite(edge.diagnostic.uncertaintyEv);
    if (!edge.diagnostic.verified) return edge;

    // Missing-profile models are checked against aligned pair residuals. A
    // single burst may only inflate uncertainty; it never makes a model more
    // optimistic than its metadata/estimate/generic source.
    std::vector<double> normalizedResiduals;
    for (const auto& siteValues : observations) {
        for (const PairObservation& value : siteValues) {
            const double fromPilot = std::max(0.0, 0.5 * (
                value.from +
                (value.to - edge.diagnostic.fittedOffset) /
                    edge.diagnostic.fittedScale));
            const double toPilot = std::max(0.0,
                edge.diagnostic.fittedScale * fromPilot +
                    edge.diagnostic.fittedOffset);
            const double fromVariance =
                value.fromSample.shotCoefficient * fromPilot +
                value.fromSample.offsetVariance;
            const double toVariance =
                value.toSample.shotCoefficient * toPilot +
                value.toSample.offsetVariance;
            const double residualVariance = toVariance +
                edge.diagnostic.fittedScale * edge.diagnostic.fittedScale *
                    fromVariance;
            const double residual = value.to -
                (edge.diagnostic.fittedScale * value.from +
                 edge.diagnostic.fittedOffset);
            if (std::isfinite(residualVariance) && residualVariance > 1.0e-16 &&
                std::isfinite(residual)) {
                normalizedResiduals.push_back(
                    residual * residual / residualVariance);
            }
        }
    }
    if (!normalizedResiduals.empty()) {
        // Median of chi-square(1) is approximately 0.4549364.
        edge.diagnostic.residualNoiseInflation = std::clamp(
            Median(normalizedResiduals) / 0.4549364231, 1.0, 16.0);
    }
    return edge;
}

bool SolveLinearSystem(
    std::vector<std::vector<double>>& matrix,
    std::vector<double>& values) {
    const std::size_t count = values.size();
    for (std::size_t column = 0; column < count; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1u; row < count; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column]))
                pivot = row;
        }
        if (std::abs(matrix[pivot][column]) < 1.0e-12) return false;
        if (pivot != column) {
            std::swap(matrix[pivot], matrix[column]);
            std::swap(values[pivot], values[column]);
        }
        const double divisor = matrix[column][column];
        for (std::size_t item = column; item < count; ++item)
            matrix[column][item] /= divisor;
        values[column] /= divisor;
        for (std::size_t row = 0; row < count; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            if (factor == 0.0) continue;
            for (std::size_t item = column; item < count; ++item)
                matrix[row][item] -= factor * matrix[column][item];
            values[row] -= factor * values[column];
        }
    }
    return true;
}

bool CalibrateExposureGraph(
    std::vector<MaterializedFrame>& frames,
    std::size_t geometricReference,
    std::size_t anchor,
    const Request& request,
    Diagnostics& diagnostics,
    std::vector<std::vector<std::string>>& paths) {
    std::vector<ExposureFitEdge> edges;
    bool wasCanceled = false;
    for (std::size_t from = 0; from < frames.size(); ++from) {
        for (std::size_t to = from + 1u; to < frames.size(); ++to) {
            ExposureFitEdge edge = FitExposurePair(
                frames, from, to, geometricReference, request, wasCanceled);
            diagnostics.exposureFitEdges.push_back(edge.diagnostic);
            edges.push_back(std::move(edge));
            if (wasCanceled) return false;
        }
    }

    const std::size_t count = frames.size();
    std::vector<int> variable(count, -1);
    int variableCount = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != anchor) variable[i] = variableCount++;
    }
    std::vector<std::vector<double>> normal(
        static_cast<std::size_t>(variableCount),
        std::vector<double>(static_cast<std::size_t>(variableCount), 0.0));
    std::vector<double> rhs(static_cast<std::size_t>(variableCount), 0.0);
    for (const ExposureFitEdge& edge : edges) {
        if (!edge.diagnostic.verified) continue;
        const double equation = std::log2(edge.diagnostic.fittedScale);
        const double sigma = std::max(0.005, edge.diagnostic.uncertaintyEv);
        const double weight = 1.0 / (sigma * sigma);
        const int a = variable[edge.from];
        const int b = variable[edge.to];
        if (a >= 0) {
            normal[a][a] += weight;
            rhs[a] -= weight * equation;
        }
        if (b >= 0) {
            normal[b][b] += weight;
            rhs[b] += weight * equation;
        }
        if (a >= 0 && b >= 0) {
            normal[a][b] -= weight;
            normal[b][a] -= weight;
        }
    }

    const double anchorMetadata = std::max(1.0e-12, frames[anchor].metadataExposure);
    constexpr double kWeakMetadataWeight = 0.25; // 2 EV standard deviation.
    for (std::size_t i = 0; i < count; ++i) {
        if (i == anchor) continue;
        const double metadataLog = std::log2(
            std::max(1.0e-12, frames[i].metadataExposure) / anchorMetadata);
        if (!std::isfinite(metadataLog)) return false;
        const int v = variable[i];
        normal[v][v] += kWeakMetadataWeight;
        rhs[v] += kWeakMetadataWeight * metadataLog;
    }
    if (!SolveLinearSystem(normal, rhs)) return false;

    std::vector<double> logs(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (i != anchor) logs[i] = rhs[variable[i]];
        frames[i].exposureRelativeToAnchor = std::exp2(logs[i]);
        if (!std::isfinite(frames[i].exposureRelativeToAnchor) ||
            frames[i].exposureRelativeToAnchor <= 0.0) return false;
    }
    const double referenceLog = logs[geometricReference];
    for (std::size_t i = 0; i < count; ++i)
        frames[i].exposureRelativeToReference = std::exp2(logs[i] - referenceLog);

    // Record the most certain image-evidence path from the anchor. This path
    // also determines which residual checks can conservatively inflate each
    // missing-profile noise model.
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> distance(count, infinity);
    std::vector<bool> visited(count, false);
    std::vector<std::size_t> parentFrame(count, count);
    std::vector<std::size_t> parentEdge(count, edges.size());
    distance[anchor] = 0.0;
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        std::size_t current = count;
        for (std::size_t i = 0; i < count; ++i) {
            if (!visited[i] && (current == count || distance[i] < distance[current]))
                current = i;
        }
        if (current == count || !std::isfinite(distance[current])) break;
        visited[current] = true;
        for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
            const ExposureFitEdge& edge = edges[edgeIndex];
            if (!edge.diagnostic.verified) continue;
            std::size_t next = count;
            if (edge.from == current) next = edge.to;
            else if (edge.to == current) next = edge.from;
            if (next == count || visited[next]) continue;
            const double sigma = std::max(0.005, edge.diagnostic.uncertaintyEv);
            const double candidate = distance[current] + sigma * sigma;
            if (candidate < distance[next]) {
                distance[next] = candidate;
                parentFrame[next] = current;
                parentEdge[next] = edgeIndex;
            }
        }
    }

    paths.assign(count, {});
    for (std::size_t i = 0; i < count; ++i) {
        const bool connected = std::isfinite(distance[i]);
        frames[i].exposureVerified = connected;
        frames[i].exposureUncertaintyEv = connected
            ? std::sqrt(std::max(0.0, distance[i]))
            : 1.0;
        frames[i].lowConfidence = frames[i].lowConfidence || !connected ||
            frames[i].exposureUncertaintyEv > request.parameters.mixedIsoWarningEv;
        if (!connected) continue;
        std::vector<std::size_t> reversed;
        std::size_t current = i;
        reversed.push_back(current);
        while (current != anchor && parentFrame[current] < count) {
            const std::size_t edgeIndex = parentEdge[current];
            if (edgeIndex < edges.size()) {
                const ExposureFitEdge& edge = edges[edgeIndex];
                for (std::size_t endpoint : { edge.from, edge.to }) {
                    const NoiseModelQuality quality = frames[endpoint].noise.quality;
                    if (quality == NoiseModelQuality::EstimatedBurst ||
                        quality == NoiseModelQuality::GenericLowConfidence) {
                        frames[endpoint].noiseVarianceInflation = std::max(
                            frames[endpoint].noiseVarianceInflation,
                            edge.diagnostic.residualNoiseInflation);
                    }
                }
            }
            current = parentFrame[current];
            reversed.push_back(current);
        }
        std::reverse(reversed.begin(), reversed.end());
        for (std::size_t frameIndex : reversed)
            paths[i].push_back(frames[frameIndex].input.stableFrameId);
    }
    return true;
}

std::string BuildCacheKey(
    const Request& request,
    std::size_t reference,
    std::size_t anchor,
    const std::string& executionBackend,
    const std::string& gpuDeviceIdentity) {
    nlohmann::json identity = {
        { "contract", kProcessorContractId },
        { "algorithm", kAlgorithmId },
        { "version", kAlgorithmVersion },
        { "parameters", SerializeParameters(request.parameters) },
        { "reference", reference },
        { "anchor", anchor },
        { "inputRevision", executionBackend == "preparation-v1" ? 0u : request.inputRevision },
        { "fusion", executionBackend == "preparation-v1" ? nlohmann::json::object() : SerializeFusionControls(request.fusion) },
        { "interactiveFusion", request.interactiveFusion },
        { "numericProfile", {
            { "backend", executionBackend },
            { "gpuDevice", gpuDeviceIdentity }
        } },
        { "frames", nlohmann::json::array() }
    };
    for (const FrameInput& frame : request.frames) {
        identity["frames"].push_back({
            { "id", frame.stableFrameId },
            { "sha256", frame.expectedSourceSha256 },
            { "bytes", frame.expectedSourceByteLength }
        });
    }
    return Sha256Digest(identity.dump());
}

template <typename T>
bool WriteVector(std::ofstream& stream, const std::vector<T>& values) {
    const std::uint64_t count = values.size();
    if (!stream.write(
            reinterpret_cast<const char*>(&count), sizeof(count))) {
        return false;
    }
    return values.empty() || static_cast<bool>(stream.write(
        reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T))));
}

template <typename T>
bool ReadScalar(const std::vector<char>& bytes, std::size_t& cursor, T& value) {
    if (cursor > bytes.size() || bytes.size() - cursor < sizeof(T)) return false;
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

template <typename T>
bool ReadVector(const std::vector<char>& bytes, std::size_t& cursor, std::vector<T>& values) {
    std::uint64_t count = 0;
    if (!ReadScalar(bytes, cursor, count) ||
        count > std::numeric_limits<std::size_t>::max() / sizeof(T) ||
        cursor > bytes.size() || bytes.size() - cursor < count * sizeof(T)) return false;
    values.resize(static_cast<std::size_t>(count));
    if (count != 0u) std::memcpy(values.data(), bytes.data() + cursor, count * sizeof(T));
    cursor += static_cast<std::size_t>(count * sizeof(T));
    return true;
}

bool ValidatePublishedPayload(
    const Result& value,
    std::size_t frameCount,
    std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (value.width == 0u || value.height == 0u ||
        value.width > std::numeric_limits<std::uint64_t>::max() /
            value.height) {
        return fail("The HDR output extent is invalid.");
    }
    const std::uint64_t count64 = value.width * value.height;
    if (count64 > std::numeric_limits<std::size_t>::max())
        return fail("The HDR output extent exceeds the addressable payload.");
    const std::size_t count = static_cast<std::size_t>(count64);
    if (value.virtualAnchorMosaic.size() != count ||
        value.varianceProxy.size() != count ||
        value.mergeConfidence.size() != count ||
        value.effectiveSampleCount.size() != count ||
        value.recoveredHeadroomStops.size() != count ||
        value.validityMask.size() != count ||
        value.ownerFrame.size() != count ||
        value.flags.size() != count) {
        return fail("The HDR output planes do not match the published extent.");
    }
    if (frameCount == 0u || frameCount > kMaximumFrameCount)
        return fail("The HDR output has an invalid source-frame count.");
    constexpr std::uint8_t supportedFlags =
        ResultFlagReferenceFallback | ResultFlagLowConfidenceOwner |
        ResultFlagRecoveredHighlight | ResultFlagSingleExposure |
        ResultFlagHighlightSafeHandoff | ResultFlagColorCoherentRepair | ResultFlagNoValidMeasurement;
    for (std::size_t index = 0u; index < count; ++index) {
        const std::string prefix = "HDR Virtual Bayer sample " +
            std::to_string(index) + " ";
        if (!std::isfinite(value.virtualAnchorMosaic[index]))
            return fail(prefix + "is non-finite.");
        if (!std::isfinite(value.varianceProxy[index]) ||
            value.varianceProxy[index] < 0.0f)
            return fail(prefix + "has invalid variance.");
        if (!std::isfinite(value.mergeConfidence[index]) ||
            value.mergeConfidence[index] < 0.0f ||
            value.mergeConfidence[index] > 1.0f)
            return fail(prefix + "has invalid merge confidence.");
        if (!std::isfinite(value.effectiveSampleCount[index]) ||
            value.effectiveSampleCount[index] < 1.0f)
            return fail(prefix + "has invalid effective support.");
        if (!std::isfinite(value.recoveredHeadroomStops[index]) ||
            value.recoveredHeadroomStops[index] < 0.0f)
            return fail(prefix + "has invalid recovered headroom.");
        if (value.validityMask[index] == 0u)
            return fail(prefix + "has no resolved owner.");
        if (value.ownerFrame[index] >= frameCount)
            return fail(prefix + "references an unavailable owner frame.");
        if ((value.flags[index] & ~supportedFlags) != 0u)
            return fail(prefix + "contains unsupported categorical flags.");
    }
    if (value.diagnostics.finitePixelCount != count64)
        return fail("HDR fusion did not resolve every Virtual Bayer sample.");
    if (error) error->clear();
    return true;
}

} // namespace

const char* ProcessingStatusName(ProcessingStatus status) {
    switch (status) {
        case ProcessingStatus::Published: return "published";
        case ProcessingStatus::Canceled: return "canceled";
        case ProcessingStatus::Failed: return "failed";
    }
    return "failed";
}

const char* ProcessingStageName(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::Queued: return "Queued";
        case ProcessingStage::Decoding: return "Decoding RAW frames";
        case ProcessingStage::Preparing: return "Canonical RAW preparation";
        case ProcessingStage::Registering: return "Translation registration";
        case ProcessingStage::Calibrating: return "Exposure calibration";
        case ProcessingStage::Fusing: return "Noise-aware HDR fusion";
        case ProcessingStage::Caching: return "Writing HDR cache";
        case ProcessingStage::Finalizing: return "Publishing result";
    }
    return "HDR processing";
}

class PreparedFusion {
public:
    std::string identity;
    std::vector<MaterializedFrame> frames;
    std::size_t reference = 0, anchor = 0;
    Result metadata;
    std::vector<float> analysisEv;
};

Services MakeFilesystemServices() {
    Services services;
    services.loadRawFrame = DefaultLoad;
    return services;
}

Result ProcessBurst(const Request& request, const Services& services) {
    if (!request.interactiveFusion && !IsNeutralFusion(request.fusion)) {
        Request interactive = request;
        interactive.interactiveFusion = true;
        return ProcessBurst(interactive, services);
    }
    Result result;
    result.diagnostics.inputRevision = request.inputRevision;
    const auto fail = [&](ProcessingStatus status, const std::string& message) {
        result.status = status;
        result.message = message;
        // Failed/canceled calls are never publishable results. Drop any
        // partially filled GPU or CPU payload so callers cannot accidentally
        // inspect or retain an incomplete Virtual Bayer measurement.
        if (status != ProcessingStatus::Published) {
            result.virtualAnchorMosaic.clear();
            result.varianceProxy.clear();
            result.mergeConfidence.clear();
            result.effectiveSampleCount.clear();
            result.recoveredHeadroomStops.clear();
            result.validityMask.clear();
            result.ownerFrame.clear();
            result.flags.clear();
        }
        return result;
    };
    std::string parameterError;
    if (!ValidateFusionControls(request.fusion, &parameterError))
        return fail(ProcessingStatus::Failed, parameterError);
    if (!ValidateParameters(request.parameters, &parameterError))
        return fail(ProcessingStatus::Failed, parameterError);
    if (request.frames.size() < kMinimumFrameCount ||
        request.frames.size() > kMaximumFrameCount)
        return fail(ProcessingStatus::Failed, "HDR requires between two and twenty RAW frames.");
    if (!services.loadRawFrame)
        return fail(ProcessingStatus::Failed, "No RAW frame loader is available.");
    if (request.workingDirectory.empty())
        return fail(ProcessingStatus::Failed, "HDR requires a working directory for its cache.");
    if (request.workerCount == 0u)
        return fail(ProcessingStatus::Failed, "HDR requires at least one processing worker.");

    const std::uint64_t estimatedBytesPerPixel =
        static_cast<std::uint64_t>(request.frames.size()) * 10u + 30u;
    const std::string preparationIdentity = BuildCacheKey(request,
        static_cast<std::size_t>(request.geometricReferenceFrameIndex),
        static_cast<std::size_t>(request.radiometricAnchorFrameIndex), "preparation-v1", "");
    const bool reuse = request.preparedFusion &&
        request.preparedFusion->identity == preparationIdentity;
    if (request.interactiveFusion && !IsNeutralFusion(request.fusion) &&
        (!reuse || request.preparedFusion->analysisEv.empty())) {
        Request neutral = request;
        neutral.fusion = {};
        Result baseline = ProcessBurst(neutral, services);
        if (baseline.status != ProcessingStatus::Published) return baseline;
        Request active = request;
        active.preparedFusion = baseline.preparedFusion;
        Result activeResult = ProcessBurst(active, services);
        activeResult.diagnostics.reusedPreparation = reuse;
        return activeResult;
    }
    auto prepared = reuse ? std::const_pointer_cast<PreparedFusion>(request.preparedFusion)
                          : std::make_shared<PreparedFusion>();
    auto& frames = prepared->frames;
    PixelExtent extent;
    CfaPattern cfa = CfaPattern::Unknown;
    std::size_t reference = 0, anchor = 0;
    if (reuse) {
        result = prepared->metadata;
        result.diagnostics.inputRevision = request.inputRevision;
        result.diagnostics.reusedPreparation = true;
        extent = frames.front().prepared.activeExtent;
        cfa = frames.front().prepared.activeCfaPattern;
        reference = prepared->reference; anchor = prepared->anchor;
        Report(request, ProcessingStage::Fusing, 0.45, 0, 0,
            "Reusing decoded measurements and registration");
    } else {
    frames.resize(request.frames.size());
    DirectoryNormalizedTileCache preparationCache(request.workingDirectory / "prepared");
    Report(request, ProcessingStage::Decoding, 0.01, 0.0, 0, "Reading HDR sources");
    for (std::size_t i = 0; i < request.frames.size(); ++i) {
        if (Canceled(request)) return fail(ProcessingStatus::Canceled, "HDR processing was canceled.");
        std::string error;
        const double frameBase = 0.05 + 0.20 *
            static_cast<double>(i) / static_cast<double>(request.frames.size());
        const double frameSpan = 0.20 /
            static_cast<double>(request.frames.size());
        if (!Prepare(
                request.frames[i],
                request,
                services,
                preparationCache,
                frames[i],
                [&request, i, frameBase, frameSpan](
                    double fraction,
                    const char* activity) {
                    const double clamped = std::clamp(fraction, 0.0, 1.0);
                    const ProcessingStage stage = clamped <= 0.10
                        ? ProcessingStage::Decoding
                        : ProcessingStage::Preparing;
                    Report(
                        request,
                        stage,
                        frameBase + frameSpan * clamped,
                        clamped,
                        static_cast<std::uint32_t>(i + 1u),
                        std::string(activity ? activity : "Preparing RAW frame") +
                            " " + std::to_string(i + 1u) + " of " +
                            std::to_string(request.frames.size()));
                },
                error)) {
            return fail(Canceled(request) ? ProcessingStatus::Canceled : ProcessingStatus::Failed,
                error.empty() ? "An HDR frame could not be prepared." : error);
        }
        frames[i].texture = TextureScore(frames[i]);
        const std::uint64_t pixels = frames[i].prepared.activeExtent.width *
            frames[i].prepared.activeExtent.height;
        if (request.enforceMemoryBudget && pixels > 0u &&
            estimatedBytesPerPixel > request.memoryBudgetBytes / pixels) {
            return fail(ProcessingStatus::Failed,
                "The HDR working set exceeds the selected memory budget.");
        }
        Report(request, ProcessingStage::Preparing,
            0.05 + 0.20 * static_cast<double>(i + 1u) / request.frames.size(),
            1.0, static_cast<std::uint32_t>(i + 1u), "Prepared RAW frame");
    }

    extent = frames.front().prepared.activeExtent;
    cfa = frames.front().prepared.activeCfaPattern;
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (frames[i].prepared.activeExtent.width != extent.width ||
            frames[i].prepared.activeExtent.height != extent.height ||
            frames[i].prepared.activeCfaPattern != cfa) {
            return fail(ProcessingStatus::Failed,
                "HDR frames have incompatible active areas or CFA phases.");
        }
        if (request.interactiveFusion) {
            const auto& a = frames.front().metadata;
            const auto& b = frames[i].metadata;
            if (a.cameraMake != b.cameraMake || a.cameraModel != b.cameraModel ||
                (!a.lensModel.empty() && !b.lensModel.empty() && a.lensModel != b.lensModel) ||
                (a.isoSpeed > 0 && b.isoSpeed > 0 && std::abs(std::log2(a.isoSpeed / b.isoSpeed)) > 0.05f))
                return fail(ProcessingStatus::Failed, "Interactive Fusion requires the same camera, lens, and ISO.");
        }
    }

    reference = 0u;
    if (request.geometricReferenceFrameIndex >= 0 &&
        static_cast<std::size_t>(request.geometricReferenceFrameIndex) < frames.size()) {
        reference = static_cast<std::size_t>(request.geometricReferenceFrameIndex);
    } else {
        const double maximumTexture = std::max_element(frames.begin(), frames.end(),
            [](const MaterializedFrame& a, const MaterializedFrame& b) {
                return a.texture < b.texture;
            })->texture;
        double shortest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const bool textured = maximumTexture <= 1.0e-12 ||
                frames[i].texture >= maximumTexture * 0.10;
            if (textured && frames[i].metadataExposure < shortest) {
                shortest = frames[i].metadataExposure;
                reference = i;
            }
        }
    }
    anchor = 0u;
    if (request.radiometricAnchorFrameIndex >= 0 &&
        static_cast<std::size_t>(request.radiometricAnchorFrameIndex) < frames.size()) {
        anchor = static_cast<std::size_t>(request.radiometricAnchorFrameIndex);
    } else {
        std::vector<std::pair<double, std::size_t>> ordered;
        for (std::size_t i = 0; i < frames.size(); ++i)
            ordered.emplace_back(frames[i].metadataExposure, i);
        std::sort(ordered.begin(), ordered.end());
        anchor = ordered[ordered.size() / 2u].second;
    }

    result.referenceMetadata = frames[reference].metadata;
    result.radiometricAnchorMetadata = frames[anchor].metadata;
    result.outputCfaPattern = cfa;
    result.width = extent.width;
    result.height = extent.height;
    result.geometricReferenceFrameId = frames[reference].input.stableFrameId;
    result.radiometricAnchorFrameId = frames[anchor].input.stableFrameId;
    frames[reference].translationX = 0.0;
    frames[reference].translationY = 0.0;
    Report(request, ProcessingStage::Registering, 0.27, 0.0, 0,
        "Estimating global translations");
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (i == reference) continue;
        if (Canceled(request)) return fail(ProcessingStatus::Canceled, "HDR processing was canceled.");
        if (request.parameters.alignmentMode != AlignmentMode::Identity &&
            !EstimateTranslation(frames[reference], frames[i],
                frames[i].translationX, frames[i].translationY,
                request.interactiveFusion && request.parameters.alignmentMode != AlignmentMode::VerifyOnly)) {
            result.diagnostics.warnings.push_back(
                frames[i].input.stableFrameId +
                ": translation was ambiguous; identity alignment was used.");
            frames[i].translationX = 0.0;
            frames[i].translationY = 0.0;
            frames[i].lowConfidence = true;
        }
        if (request.parameters.alignmentMode == AlignmentMode::VerifyOnly) {
            result.diagnostics.warnings.push_back(frames[i].input.stableFrameId +
                ": measured movement " + std::to_string(frames[i].translationX) + ", " +
                std::to_string(frames[i].translationY) + " RAW pixels; transforms remain identity.");
            frames[i].translationX = frames[i].translationY = 0;
        }
        Report(request, ProcessingStage::Registering,
            0.27 + 0.13 * static_cast<double>(i + 1u) / frames.size(),
            static_cast<double>(i + 1u) / frames.size(),
            static_cast<std::uint32_t>(i + 1u), "Registered HDR frame");
    }

    std::vector<std::vector<std::string>> exposureFitPaths;
    if (!CalibrateExposureGraph(
            frames, reference, anchor, request, result.diagnostics,
            exposureFitPaths)) {
        return fail(Canceled(request)
                ? ProcessingStatus::Canceled
                : ProcessingStatus::Failed,
            Canceled(request)
                ? "HDR processing was canceled."
                : "No finite exposure relationship could be established for the HDR bracket.");
    }
    double minimumExposure = std::numeric_limits<double>::infinity();
    double maximumExposure = 0.0;
    for (MaterializedFrame& frame : frames) {
        minimumExposure = std::min(minimumExposure, frame.exposureRelativeToAnchor);
        maximumExposure = std::max(maximumExposure, frame.exposureRelativeToAnchor);
    }
    result.diagnostics.exposureSpanEv = std::log2(maximumExposure / minimumExposure);
    if (!std::isfinite(result.diagnostics.exposureSpanEv) ||
        result.diagnostics.exposureSpanEv < request.parameters.minimumExposureSpanEv) {
        return fail(ProcessingStatus::Failed,
            "The selected frames span less than 0.5 EV; use Multi-Frame Denoise instead.");
    }
    Report(request, ProcessingStage::Calibrating, 0.45, 1.0, 0,
        "Verified Bayer-plane exposure scales");

    result.diagnostics.frames.resize(frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        auto& diagnostic = result.diagnostics.frames[i];
        diagnostic.stableFrameId = frames[i].input.stableFrameId;
        diagnostic.geometricReference = i == reference;
        diagnostic.radiometricAnchor = i == anchor;
        diagnostic.structurallyUsable = true;
        diagnostic.exposureVerified = frames[i].exposureVerified;
        diagnostic.lowConfidence = frames[i].lowConfidence;
        diagnostic.metadataExposureRelativeToAnchor =
            frames[i].metadataExposure / std::max(1.0e-12, frames[anchor].metadataExposure);
        diagnostic.fittedExposureRelativeToAnchor = frames[i].exposureRelativeToAnchor;
        diagnostic.fittedExposureUncertaintyEv = frames[i].exposureUncertaintyEv;
        diagnostic.metadataDisagreementEv = std::abs(std::log2(
            std::max(1.0e-12, diagnostic.fittedExposureRelativeToAnchor) /
            std::max(1.0e-12, diagnostic.metadataExposureRelativeToAnchor)));
        diagnostic.translationRawX = frames[i].translationX;
        diagnostic.translationRawY = frames[i].translationY;
        diagnostic.exposureFitPath = exposureFitPaths[i];
        diagnostic.noiseModelSource = frames[i].noise.diagnostics.source;
        diagnostic.noiseModelQuality = Raw::Mfd::NoiseModelQualityName(
            frames[i].noise.quality);
        diagnostic.noiseVarianceInflation = frames[i].noiseVarianceInflation;
        if (diagnostic.metadataDisagreementEv > request.parameters.mixedIsoWarningEv) {
            result.diagnostics.warnings.push_back(
                diagnostic.stableFrameId + ": fitted exposure differs from metadata by " +
                std::to_string(diagnostic.metadataDisagreementEv) + " EV.");
        }
        if (diagnostic.metadataDisagreementEv >
            request.parameters.mixedIsoStrongUncertaintyEv) {
            frames[i].lowConfidence = true;
            diagnostic.lowConfidence = true;
        }
    }

    prepared->identity = preparationIdentity;
    prepared->reference = reference; prepared->anchor = anchor;
    prepared->metadata = result;
    }
    result.preparedFusion = request.interactiveFusion ? prepared : nullptr;
    const std::size_t pixelCount = static_cast<std::size_t>(extent.width * extent.height);
    std::shared_ptr<FusionPreview> preview;
    if (request.interactiveFusion) {
        preview = std::make_shared<FusionPreview>();
        preview->rawWidth = extent.width; preview->rawHeight = extent.height;
        preview->stride = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(
            (std::max(extent.width, extent.height) + 639) / 640));
        preview->width = static_cast<std::uint32_t>((extent.width + 2 * preview->stride - 1) / (2 * preview->stride));
        preview->height = static_cast<std::uint32_t>((extent.height + 2 * preview->stride - 1) / (2 * preview->stride));
        preview->anchor = reference;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto site = frames[reference].layout.SiteAt(i % 2, i / 2);
            preview->channelByParity[i] = site == CfaSite::Red ? 0 : (site == CfaSite::Blue ? 2 : 1);
        }
        const auto& wb = frames[reference].metadata.cameraWhiteBalance;
        const float green = std::max(1.0e-6f, wb[1]);
        preview->whiteBalance = {wb[0] / green, 1, wb[2] / green};
        for (const auto& f : frames) preview->sources.push_back({ f.input.stableFrameId,
            static_cast<float>(std::log2(f.exposureRelativeToAnchor)), f.lowConfidence ? 0.35f : 1.0f });
        preview->observations.resize(static_cast<std::size_t>(preview->width) * preview->height * 4 * frames.size());
        preview->analysisEv.resize(static_cast<std::size_t>(preview->width) * preview->height);
        result.fusionPreview = preview;
    }
    const auto allocateCpuOutput = [&]() {
        try {
            result.virtualAnchorMosaic.resize(pixelCount);
            result.varianceProxy.resize(pixelCount);
            result.mergeConfidence.resize(pixelCount);
            result.effectiveSampleCount.resize(pixelCount);
            result.recoveredHeadroomStops.resize(pixelCount);
            result.validityMask.assign(pixelCount, 0u);
            result.ownerFrame.assign(pixelCount, 0u);
            result.flags.assign(pixelCount, ResultFlagNone);
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        }
    };

    std::vector<double> contribution(frames.size(), 0.0);
    std::vector<std::array<double, 3>> contributionByRange(frames.size());
    std::array<double, 3> rangePixelCount {};
    double neffSum = 0.0;
    bool gpuSucceeded = false;
    if (!request.interactiveFusion && IsNeutralFusion(request.fusion) && request.preferGpuFusion && services.executeOpenGlTask &&
        extent.width <= std::numeric_limits<std::uint32_t>::max() &&
        extent.height <= std::numeric_limits<std::uint32_t>::max()) {
        GpuFusionRequest gpuRequest;
        gpuRequest.width = static_cast<std::uint32_t>(extent.width);
        gpuRequest.height = static_cast<std::uint32_t>(extent.height);
        gpuRequest.cfaPattern = cfa;
        gpuRequest.referenceFrame = static_cast<std::uint32_t>(reference);
        gpuRequest.tileRawPixels = request.parameters.tileRawPixels;
        gpuRequest.parameters = request.parameters;
        gpuRequest.shouldCancel = request.shouldCancel;
        gpuRequest.reportProgress = [&](double fraction) {
            Report(request, ProcessingStage::Fusing,
                0.48 + 0.42 * fraction, fraction, 0,
                "Fusing scene-linear Bayer samples on the GPU");
        };
        gpuRequest.frames.reserve(frames.size());
        for (const MaterializedFrame& frame : frames) {
            GpuFusionFrameView view;
            view.normalized = &frame.normalized;
            view.gain = &frame.gain;
            view.flags = &frame.flags;
            view.clippedNeighborCount = &frame.clippedNeighborCount;
            view.explicitVariance = frame.explicitVariance &&
                frame.explicitVariance->size() == pixelCount
                ? frame.explicitVariance.get() : nullptr;
            view.noise = frame.noise.sites;
            view.exposureRelativeToAnchor = static_cast<float>(
                frame.exposureRelativeToAnchor);
            view.exposureUncertaintyEv = static_cast<float>(
                frame.exposureUncertaintyEv);
            view.noiseVarianceInflation = static_cast<float>(
                frame.noiseVarianceInflation);
            view.translationX = static_cast<float>(frame.translationX);
            view.translationY = static_cast<float>(frame.translationY);
            view.exposureVerified = frame.exposureVerified;
            view.lowConfidence = frame.lowConfidence;
            gpuRequest.frames.push_back(std::move(view));
        }
        GpuFusionOutput gpuOutput;
        std::string gpuError;
        const bool taskSucceeded = services.executeOpenGlTask(
            [&](std::string& taskError) {
                return FuseOpenGl(gpuRequest, gpuOutput, taskError);
            }, gpuError);
        if (taskSucceeded &&
            gpuOutput.virtualAnchorMosaic.size() == pixelCount &&
            gpuOutput.varianceProxy.size() == pixelCount &&
            gpuOutput.mergeConfidence.size() == pixelCount &&
            gpuOutput.effectiveSampleCount.size() == pixelCount &&
            gpuOutput.recoveredHeadroomStops.size() == pixelCount &&
            gpuOutput.validityMask.size() == pixelCount &&
            gpuOutput.ownerFrame.size() == pixelCount &&
            gpuOutput.flags.size() == pixelCount) {
            result.virtualAnchorMosaic = std::move(gpuOutput.virtualAnchorMosaic);
            result.varianceProxy = std::move(gpuOutput.varianceProxy);
            result.mergeConfidence = std::move(gpuOutput.mergeConfidence);
            result.effectiveSampleCount = std::move(gpuOutput.effectiveSampleCount);
            result.recoveredHeadroomStops = std::move(
                gpuOutput.recoveredHeadroomStops);
            result.validityMask = std::move(gpuOutput.validityMask);
            result.ownerFrame = std::move(gpuOutput.ownerFrame);
            result.flags = std::move(gpuOutput.flags);
            contribution = std::move(gpuOutput.diagnostics.contribution);
            contributionByRange = std::move(
                gpuOutput.diagnostics.contributionByRange);
            rangePixelCount = gpuOutput.diagnostics.rangePixelCount;
            neffSum = gpuOutput.diagnostics.effectiveSampleSum;
            result.diagnostics.finitePixelCount =
                gpuOutput.diagnostics.finitePixelCount;
            result.diagnostics.referenceFallbackPixelCount =
                gpuOutput.diagnostics.referenceFallbackPixelCount;
            result.diagnostics.highlightSafeHandoffPixelCount =
                gpuOutput.diagnostics.highlightSafeHandoffPixelCount;
            result.diagnostics.recoveredHighlightPixelCount =
                gpuOutput.diagnostics.recoveredHighlightPixelCount;
            result.diagnostics.gpuDispatchedTileCount =
                gpuOutput.diagnostics.dispatchedTileCount;
            result.diagnostics.gpuDeviceIdentity =
                std::move(gpuOutput.diagnostics.deviceIdentity);
            result.diagnostics.executionBackend =
                "opengl-4.3-compute-fp32+cpu-color-repair";
            gpuSucceeded = true;
        } else {
            result.diagnostics.gpuFallbackReason = gpuError.empty()
                ? "The GPU result did not satisfy the HDR output contract."
                : gpuError;
        }
    } else if (request.preferGpuFusion) {
        result.diagnostics.gpuFallbackReason = request.interactiveFusion || !IsNeutralFusion(request.fusion)
            ? "Interactive Fusion uses the CPU measurement estimator."
            : services.executeOpenGlTask
            ? "The frame dimensions exceed the OpenGL compute contract."
            : "No OpenGL compute executor is available.";
    }

    if (!gpuSucceeded && !allocateCpuOutput()) {
        return fail(ProcessingStatus::Failed, "The HDR output exceeds the memory budget.");
    }

    if (!gpuSucceeded) {

    struct Candidate {
        std::size_t frame = 0;
        Sample sample;
        double scene = 0.0;
        double variance = 0.0;
        double exposure = 1.0;
        double normalized = 0.0;
        double gain = 1.0;
        double baseWeight = 0.0;
        double weight = 0.0;
        double fusionWeight = 0.0;
        bool lowConfidence = false;
        bool highlightHole = false;
    };
    struct FusionRowDiagnostics {
        std::array<double, kMaximumFrameCount> contribution {};
        std::array<std::array<double, 3>, kMaximumFrameCount>
            contributionByRange {};
        std::array<double, 3> rangePixelCount {};
        double neffSum = 0.0;
        std::uint64_t finitePixelCount = 0u;
        std::uint64_t referenceFallbackPixelCount = 0u;
        std::uint64_t highlightSafeHandoffPixelCount = 0u;
        std::uint64_t recoveredHighlightPixelCount = 0u;
    };
    std::vector<FusionRowDiagnostics> fusionRows;
    try {
        fusionRows.resize(static_cast<std::size_t>(extent.height));
    } catch (const std::bad_alloc&) {
        return fail(ProcessingStatus::Failed,
            "The HDR fusion diagnostics could not be allocated.");
    }
    std::atomic<std::uint64_t> nextFusionRow { 0u };
    std::atomic<std::uint64_t> completedFusionRows { 0u };
    std::atomic<bool> fusionCanceled { false };
    std::atomic<bool> fusionFailed { false };
    std::mutex fusionProgressMutex;
    std::mutex fusionFailureMutex;
    std::uint64_t reportedFusionRows = 0u;
    std::string fusionFailureMessage;

    const auto processFusionRows = [&]() {
      try {
        while (!fusionCanceled.load(std::memory_order_relaxed) &&
               !fusionFailed.load(std::memory_order_relaxed)) {
          const std::uint64_t y = nextFusionRow.fetch_add(
              1u, std::memory_order_relaxed);
          if (y >= extent.height) break;
          if (Canceled(request)) {
              fusionCanceled.store(true, std::memory_order_relaxed);
              break;
          }
          FusionRowDiagnostics& rowDiagnostics = fusionRows[
              static_cast<std::size_t>(y)];
          for (std::uint64_t x = 0; x < extent.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * extent.width + x);
            const CfaSite site = frames[reference].layout.SiteAt(
                static_cast<std::int64_t>(x), static_cast<std::int64_t>(y));
            const double referenceGain = std::max(1.0e-8,
                static_cast<double>(frames[reference].gain[index]));
            std::array<Candidate, kMaximumFrameCount> candidates {};
            std::size_t candidateCount = 0;
            bool referenceAvailable = false;
            double referenceScene = 0.0;
            double referenceVariance = request.parameters.numericalVarianceFloor;
            for (std::size_t i = 0; i < frames.size(); ++i) {
                Sample sample;
                if (i == reference) {
                    if (!ExactSample(frames[i], site, index, sample)) continue;
                } else if (!SampleSameCfa(frames[i], site,
                        x + frames[i].translationX, y + frames[i].translationY, sample)) {
                    continue;
                }
                if (!sample.valid) continue;
                const double exposure = frames[i].exposureRelativeToAnchor;
                const double scene = sample.comparison / exposure;
                if (!std::isfinite(scene)) continue;
                Candidate candidate;
                candidate.frame = i;
                candidate.sample = sample;
                candidate.scene = scene;
                candidate.exposure = exposure;
                candidate.normalized = sample.normalized;
                candidate.gain = std::max(1.0e-8, sample.gain);
                candidate.lowConfidence = frames[i].lowConfidence || !frames[i].exposureVerified;
                candidate.highlightHole =
                    sample.normalized < request.parameters.highlightHoleNormalizedCeiling &&
                    ClippedSameCfaNeighborCountAt(
                        frames[i],
                        site,
                        x + frames[i].translationX,
                        y + frames[i].translationY) >=
                            request.parameters.highlightHoleMinimumClippedNeighbors;
                candidates[candidateCount++] = candidate;
                if (i == reference) {
                    referenceAvailable = true;
                    referenceScene = scene;
                }
            }
            if (candidateCount == 0u) {
                Sample fallbackSample;
                const double fallbackExposure =
                    frames[reference].exposureRelativeToAnchor;
                if (!ExactPolicyFallbackSample(
                        frames[reference], site, index, fallbackSample) ||
                    !std::isfinite(fallbackExposure) ||
                    fallbackExposure <= 0.0) {
                    result.virtualAnchorMosaic[index] =
                        std::numeric_limits<float>::quiet_NaN();
                    result.varianceProxy[index] =
                        std::numeric_limits<float>::infinity();
                    continue;
                }
                const double fallbackScene =
                    fallbackSample.comparison / fallbackExposure;
                const double fallbackVariance = PredictedSampleVariance(
                    frames[reference],
                    fallbackSample,
                    fallbackSample.comparison,
                    request.parameters.numericalVarianceFloor) /
                    (fallbackExposure * fallbackExposure *
                     fallbackSample.gain * fallbackSample.gain);
                const double fallbackHeadroom = std::max(
                    0.0, std::log2(std::max(1.0, fallbackScene)));
                float fallbackGain = 1;
                if (preview) {
                    const auto ai = static_cast<std::size_t>((y / 2) * ((extent.width + 1) / 2) + x / 2);
                    const float luma = prepared->analysisEv.empty() ? -12.0f : prepared->analysisEv[ai];
                    fallbackGain = std::exp2(EvaluateFusionField(request.fusion, luma,
                        static_cast<float>((x + 0.5) / extent.width), static_cast<float>((y + 0.5) / extent.height)).targetEv);
                    if ((x / 2) % preview->stride == 0 && (y / 2) % preview->stride == 0) {
                        const auto pi = (y / (2 * preview->stride)) * preview->width + x / (2 * preview->stride);
                        const auto parity = (y % 2) * 2 + x % 2;
                        if (parity == 0) preview->analysisEv[pi] = luma;
                        auto& o = preview->observations[(pi * 4 + parity) * frames.size() + reference];
                        o.scene = static_cast<float>(fallbackSample.normalized / fallbackExposure);
                        o.variance = static_cast<float>(fallbackVariance);
                    }
                }
                result.virtualAnchorMosaic[index] = static_cast<float>(
                    fallbackSample.normalized / fallbackExposure * fallbackGain);
                result.varianceProxy[index] = static_cast<float>(
                    fallbackVariance * fallbackGain * fallbackGain);
                result.mergeConfidence[index] = 0.0f;
                result.effectiveSampleCount[index] = 1.0f;
                result.recoveredHeadroomStops[index] = static_cast<float>(
                    fallbackHeadroom);
                result.validityMask[index] = 1u;
                result.ownerFrame[index] = static_cast<std::uint8_t>(reference);
                result.flags[index] = static_cast<std::uint8_t>(
                    ResultFlagReferenceFallback |
                    (preview ? ResultFlagNoValidMeasurement : ResultFlagNone) |
                    ResultFlagSingleExposure |
                    (frames[reference].lowConfidence
                        ? ResultFlagLowConfidenceOwner : ResultFlagNone) |
                    (fallbackHeadroom > 0.001
                        ? ResultFlagRecoveredHighlight : ResultFlagNone));
                const std::size_t range = fallbackScene < 0.10
                    ? 0u : (fallbackScene < 0.75 ? 1u : 2u);
                rowDiagnostics.contribution[reference] += 1.0;
                rowDiagnostics.contributionByRange[reference][range] += 1.0;
                rowDiagnostics.rangePixelCount[range] += 1.0;
                rowDiagnostics.neffSum += 1.0;
                ++rowDiagnostics.finitePixelCount;
                ++rowDiagnostics.referenceFallbackPixelCount;
                if (fallbackHeadroom > 0.001)
                    ++rowDiagnostics.recoveredHighlightPixelCount;
                continue;
            }

            bool haveHighlightHole = false;
            bool haveNonHighlightHole = false;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                haveHighlightHole = haveHighlightHole || candidates[i].highlightHole;
                haveNonHighlightHole = haveNonHighlightHole || !candidates[i].highlightHole;
            }
            const bool highlightSafeHandoff = haveHighlightHole && haveNonHighlightHole;
            const auto eligible = [&](std::size_t i) {
                return !highlightSafeHandoff || !candidates[i].highlightHole;
            };

            std::array<double, kMaximumFrameCount> roughWeights {};
            std::size_t eligibleCount = 0u;
            std::size_t referenceCandidate = kMaximumFrameCount;
            std::array<double, kMaximumFrameCount> initialScenes {};
            std::size_t initialSceneCount = 0u;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                if (!eligible(i)) continue;
                initialScenes[initialSceneCount++] = candidates[i].scene;
                ++eligibleCount;
                if (candidates[i].frame == reference) referenceCandidate = i;
            }
            double estimate = MedianRange(
                initialScenes.begin(), initialScenes.begin() + initialSceneCount);
            if (!std::isfinite(estimate)) estimate = candidates[0].scene;

            const auto updateVarianceAndWeight = [&](Candidate& candidate) {
                const double predictedComparison = estimate * candidate.exposure;
                const double sampleVariance = PredictedSampleVariance(
                    frames[candidate.frame],
                    candidate.sample,
                    predictedComparison,
                    request.parameters.numericalVarianceFloor);
                const double scale = std::max(1.0e-12, candidate.exposure);
                const double sensorSceneVariance = sampleVariance / (scale * scale);
                const double sceneNumericalFloor =
                    request.parameters.numericalVarianceFloor / (scale * scale);
                const double exposureSigma = std::log(2.0) *
                    frames[candidate.frame].exposureUncertaintyEv;
                // Exposure-fit uncertainty is a frame-wide correlated scale
                // error, not independent pixel noise. Let it temper a sample,
                // but never let it erase the sensor-noise advantage of a long
                // exposure in every shadow pixel.
                const double scaleUncertaintyVariance = std::min(
                    sensorSceneVariance,
                    estimate * estimate * exposureSigma * exposureSigma);
                candidate.variance = std::max(
                    sceneNumericalFloor,
                    sensorSceneVariance + scaleUncertaintyVariance);
                candidate.baseWeight = 1.0 / candidate.variance;
                if (candidate.lowConfidence) candidate.baseWeight *= 0.35;
            };
            for (std::size_t i = 0; i < candidateCount; ++i) {
                if (!eligible(i)) continue;
                updateVarianceAndWeight(candidates[i]);
                roughWeights[i] = candidates[i].baseWeight;
            }

            double roughSum = 0.0;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                if (!eligible(i)) continue;
                roughSum += roughWeights[i];
            }

            std::array<std::pair<double, double>, kMaximumFrameCount> ordered {};
            std::size_t orderedCount = 0u;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                if (!eligible(i)) continue;
                ordered[orderedCount++] = { candidates[i].scene, roughWeights[i] };
            }
            std::sort(ordered.begin(), ordered.begin() + orderedCount,
                [](const auto& a, const auto& b) { return a.first < b.first; });
            double cumulative = 0.0;
            const double target = 0.5 * std::max(1.0e-30, roughSum);
            estimate = ordered[orderedCount - 1u].first;
            for (std::size_t i = 0; i < orderedCount; ++i) {
                cumulative += ordered[i].second;
                if (cumulative >= target) {
                    estimate = ordered[i].first;
                    break;
                }
            }

            // Re-evaluate every variance from the shared scene prediction.
            // This avoids one-sided weighting from each frame's noisy sample.
            roughSum = 0.0;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                if (!eligible(i)) continue;
                updateVarianceAndWeight(candidates[i]);
                roughWeights[i] = candidates[i].baseWeight;
                roughSum += roughWeights[i];
                if (i == referenceCandidate) referenceVariance = candidates[i].variance;
            }

            // Predict clipping from the shared scene estimate. Using each
            // observed sample as its own saturation gate preferentially keeps
            // negative noise excursions and creates dark exposure-handoff
            // artifacts around clipped lights.
            double sumWeight = 0.0;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                Candidate& candidate = candidates[i];
                if (!eligible(i)) continue;
                const double predictedNormalized =
                    estimate * candidate.exposure / candidate.gain;
                const double sigmaNormalized = std::max(
                    1.0e-8,
                    std::sqrt(candidate.variance) * candidate.exposure / candidate.gain);
                const double headroomSigma =
                    (0.995 - predictedNormalized) / sigmaNormalized;
                double saturationConfidence = SmoothStep(
                    request.parameters.saturationHeadroomSigma,
                    request.parameters.saturationHeadroomSigma + 4.0,
                    headroomSigma);
                if (request.interactiveFusion)
                    saturationConfidence *= 1.0 - SmoothStep(0.80, 0.995, predictedNormalized);
                candidate.weight = candidate.baseWeight * saturationConfidence;
                sumWeight += candidate.weight;
            }

            double verifiedWeight = 0.0;
            for (std::size_t i = 0; i < candidateCount; ++i)
                if (eligible(i) && !candidates[i].lowConfidence)
                    verifiedWeight += candidates[i].weight;
            if (verifiedWeight > 0.0) {
                const double cap = verifiedWeight * request.parameters.unverifiedContributionCap /
                    (1.0 - request.parameters.unverifiedContributionCap);
                for (std::size_t i = 0; i < candidateCount; ++i) {
                    if (eligible(i) && candidates[i].lowConfidence) {
                        sumWeight += std::min(candidates[i].weight, cap) -
                            candidates[i].weight;
                        candidates[i].weight = std::min(candidates[i].weight, cap);
                    }
                }
            }
            if (sumWeight <= 1.0e-30) {
                std::size_t shortest = kMaximumFrameCount;
                for (std::size_t i = 0; i < candidateCount; ++i) {
                    if (eligible(i) && (shortest == kMaximumFrameCount ||
                        candidates[i].exposure < candidates[shortest].exposure)) {
                        shortest = i;
                    }
                }
                candidates[shortest].weight = candidates[shortest].baseWeight;
                sumWeight = candidates[shortest].weight;
                estimate = candidates[shortest].scene;
            }
            for (std::size_t i = 0; i < candidateCount; ++i)
                candidates[i].fusionWeight = candidates[i].weight;

            bool strongDisagreement = false;
            for (int iteration = 0; iteration < 2; ++iteration) {
                double robustSum = 0.0;
                double robustValue = 0.0;
                std::array<double, kMaximumFrameCount> nextWeights {};
                for (std::size_t i = 0; i < candidateCount; ++i) {
                    if (candidates[i].weight <= 0.0) continue;
                    const double z = std::abs(candidates[i].scene - estimate) /
                        std::sqrt(candidates[i].variance + 1.0 / std::max(1.0e-30, sumWeight));
                    strongDisagreement = strongDisagreement ||
                        z > request.parameters.disagreementFallbackSigma;
                    const double u = z / request.parameters.robustCutoffSigma;
                    const double robust = u < 1.0 ? std::pow(1.0 - u * u, 2.0) : 0.0;
                    const double weight = candidates[i].weight * robust;
                    nextWeights[i] = weight;
                    robustSum += weight;
                    robustValue += weight * candidates[i].scene;
                }
                if (robustSum <= 1.0e-30) break;
                for (std::size_t i = 0; i < candidateCount; ++i)
                    candidates[i].fusionWeight = nextWeights[i];
                sumWeight = robustSum;
                estimate = robustValue / robustSum;
            }

            std::size_t contributorCount = 0u;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                if (candidates[i].fusionWeight >
                    std::max(1.0e-30, candidates[i].weight * 0.01)) {
                    ++contributorCount;
                }
            }
            const bool fallback = strongDisagreement && referenceAvailable &&
                !highlightSafeHandoff &&
                (eligibleCount <= 2u || contributorCount < 2u);
            if (fallback) {
                estimate = referenceScene;
                result.flags[index] |= ResultFlagReferenceFallback;
                ++rowDiagnostics.referenceFallbackPixelCount;
            }
            if (highlightSafeHandoff) {
                result.flags[index] |= ResultFlagHighlightSafeHandoff;
                ++rowDiagnostics.highlightSafeHandoffPixelCount;
            }
            FusionPixel manualPixel;
            if (preview) {
                std::array<FusionObservation, kFusionMaxSources> observations {};
                Sample anchorSample;
                if (ExactPolicyFallbackSample(frames[reference], site, index, anchorSample)) {
                    observations[reference].scene = static_cast<float>(anchorSample.normalized /
                        frames[reference].exposureRelativeToAnchor);
                    observations[reference].variance = static_cast<float>(referenceVariance /
                        (referenceGain * referenceGain));
                }
                for (std::size_t i = 0; i < candidateCount; ++i) {
                    const auto& c = candidates[i];
                    auto& o = observations[c.frame];
                    o.scene = static_cast<float>(c.scene / referenceGain);
                    o.variance = static_cast<float>(c.variance / (referenceGain * referenceGain));
                    o.automaticWeight = static_cast<float>(fallback
                        ? (c.frame == reference ? 1.0 : 0.0)
                        : c.fusionWeight / std::max(1.0e-30, sumWeight));
                    o.headroom = static_cast<float>(c.baseWeight > 0 ? c.weight / c.baseWeight : 0);
                    o.motionConfidence = static_cast<float>(c.weight > 0 ? std::clamp(c.fusionWeight / c.weight, 0.0, 1.0) : 0);
                    o.anchorFallback = fallback;
                    const auto& support = frames[c.frame].effectiveSupport;
                    if (support && support->size() == pixelCount) {
                        const auto sx = std::clamp<std::int64_t>(static_cast<std::int64_t>(
                            std::llround(x + frames[c.frame].translationX)), 0, extent.width - 1);
                        const auto sy = std::clamp<std::int64_t>(static_cast<std::int64_t>(
                            std::llround(y + frames[c.frame].translationY)), 0, extent.height - 1);
                        o.effectiveSamples = std::max(1.0f, (*support)[sy * extent.width + sx]);
                    }
                }
                const std::size_t analysisIndex = static_cast<std::size_t>((y / 2) * ((extent.width + 1) / 2) + x / 2);
                const float luma = prepared->analysisEv.empty() ?
                    static_cast<float>(std::log2(std::max(1.0e-6, estimate / referenceGain) / 0.18)) : prepared->analysisEv[analysisIndex];
                manualPixel = EvaluateFusionPixel(request.fusion, preview->sources.data(),
                    observations.data(), frames.size(), reference, luma,
                    static_cast<float>((x + 0.5) / extent.width), static_cast<float>((y + 0.5) / extent.height));
                if ((x / 2) % preview->stride == 0 && (y / 2) % preview->stride == 0) {
                    const std::size_t pi = (y / (2 * preview->stride)) * preview->width + x / (2 * preview->stride);
                    const std::size_t parity = (y % 2) * 2 + x % 2;
                    std::copy_n(observations.begin(), frames.size(),
                        preview->observations.begin() + (pi * 4 + parity) * frames.size());
                    if (parity == 0) preview->analysisEv[pi] = luma;
                }
                if (manualPixel.fallback) {
                    result.flags[index] |= ResultFlagReferenceFallback;
                    if (!fallback) ++rowDiagnostics.referenceFallbackPixelCount;
                }
                if (manualPixel.noValidMeasurement) result.flags[index] |= ResultFlagNoValidMeasurement;
            }
            double sumWeightSquared = 0.0;
            std::size_t owner = reference;
            double ownerWeight = -1.0;
            const std::size_t contributionRange = estimate < 0.10
                ? 0u
                : (estimate < 0.75 ? 1u : 2u);
            rowDiagnostics.rangePixelCount[contributionRange] += 1.0;
            for (std::size_t i = 0; i < candidateCount; ++i) {
                const double normalizedWeight = preview ? manualPixel.contribution[candidates[i].frame] : fallback
                    ? (candidates[i].frame == reference ? 1.0 : 0.0)
                    : candidates[i].fusionWeight / std::max(1.0e-30, sumWeight);
                rowDiagnostics.contribution[candidates[i].frame] +=
                    normalizedWeight;
                rowDiagnostics.contributionByRange[
                    candidates[i].frame][contributionRange] += normalizedWeight;
                sumWeightSquared += normalizedWeight * normalizedWeight;
                if (normalizedWeight > ownerWeight) {
                    ownerWeight = normalizedWeight;
                    owner = candidates[i].frame;
                }
            }
            if (preview && manualPixel.contribution[reference] > ownerWeight) {
                owner = reference;
                rowDiagnostics.contribution[reference] += manualPixel.contribution[reference];
                rowDiagnostics.contributionByRange[reference][contributionRange] += manualPixel.contribution[reference];
            }
            const double neff = preview ? manualPixel.effectiveSamples : 1.0 / std::max(1.0e-30, sumWeightSquared);
            const double output = preview ? manualPixel.scene : estimate / referenceGain;
            result.virtualAnchorMosaic[index] = static_cast<float>(output);
            result.varianceProxy[index] = static_cast<float>(
                preview ? manualPixel.variance :
                (fallback ? referenceVariance : 1.0 / std::max(1.0e-30, sumWeight)) /
                (referenceGain * referenceGain));
            result.effectiveSampleCount[index] = static_cast<float>(neff);
            result.mergeConfidence[index] = static_cast<float>((fallback || (preview && manualPixel.fallback))
                ? 0.0
                : std::clamp(neff - 1.0, 0.0, 1.0));
            const double headroom = std::max(0.0, std::log2(std::max(1.0, estimate)));
            result.recoveredHeadroomStops[index] = static_cast<float>(headroom);
            result.validityMask[index] = 1u;
            result.ownerFrame[index] = static_cast<std::uint8_t>(owner);
            if (frames[owner].lowConfidence) result.flags[index] |= ResultFlagLowConfidenceOwner;
            if (headroom > 0.001) {
                result.flags[index] |= ResultFlagRecoveredHighlight;
                ++rowDiagnostics.recoveredHighlightPixelCount;
            }
            if (contributorCount <= 1u) result.flags[index] |= ResultFlagSingleExposure;
            ++rowDiagnostics.finitePixelCount;
            rowDiagnostics.neffSum += neff;
          }
          const std::uint64_t completed = completedFusionRows.fetch_add(
              1u, std::memory_order_relaxed) + 1u;
          if ((completed & 31u) == 0u || completed == extent.height) {
              std::lock_guard<std::mutex> lock(fusionProgressMutex);
              const std::uint64_t current = completedFusionRows.load(
                  std::memory_order_relaxed);
              if (current > reportedFusionRows) {
                  reportedFusionRows = current;
                  const double fraction = static_cast<double>(current) /
                      std::max<std::uint64_t>(1u, extent.height);
                  Report(request, ProcessingStage::Fusing,
                      0.48 + 0.42 * fraction, fraction, 0,
                      "Fusing scene-linear Bayer samples");
              }
          }
        }
      } catch (const std::bad_alloc&) {
          std::lock_guard<std::mutex> lock(fusionFailureMutex);
          fusionFailureMessage =
              "The HDR fusion worker could not satisfy a memory allocation.";
          fusionFailed.store(true, std::memory_order_relaxed);
      } catch (const std::exception& exception) {
          std::lock_guard<std::mutex> lock(fusionFailureMutex);
          fusionFailureMessage = std::string("HDR fusion failed: ") +
              exception.what();
          fusionFailed.store(true, std::memory_order_relaxed);
      } catch (...) {
          std::lock_guard<std::mutex> lock(fusionFailureMutex);
          fusionFailureMessage = "HDR fusion failed unexpectedly.";
          fusionFailed.store(true, std::memory_order_relaxed);
      }
    };

    const std::uint32_t requestedFusionWorkers = std::max(
        1u, std::min<std::uint32_t>(request.workerCount,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                extent.height,
                std::numeric_limits<std::uint32_t>::max()))));
    std::vector<std::thread> fusionWorkers;
    std::uint32_t fusionWorkerCount = requestedFusionWorkers;
    try {
        fusionWorkers.reserve(fusionWorkerCount > 0u
            ? fusionWorkerCount - 1u : 0u);
    } catch (const std::bad_alloc&) {
        fusionWorkerCount = 1u;
    }
    for (std::uint32_t worker = 1u; worker < fusionWorkerCount; ++worker) {
        try {
            fusionWorkers.emplace_back(processFusionRows);
        } catch (...) {
            break;
        }
    }
    processFusionRows();
    for (std::thread& worker : fusionWorkers) {
        if (worker.joinable()) worker.join();
    }
    if (fusionCanceled.load(std::memory_order_relaxed)) {
        return fail(ProcessingStatus::Canceled, "HDR processing was canceled.");
    }
    if (fusionFailed.load(std::memory_order_relaxed)) {
        return fail(ProcessingStatus::Failed,
            fusionFailureMessage.empty()
                ? "HDR fusion failed unexpectedly."
                : fusionFailureMessage);
    }

    for (const FusionRowDiagnostics& row : fusionRows) {
        result.diagnostics.finitePixelCount += row.finitePixelCount;
        result.diagnostics.referenceFallbackPixelCount +=
            row.referenceFallbackPixelCount;
        result.diagnostics.highlightSafeHandoffPixelCount +=
            row.highlightSafeHandoffPixelCount;
        result.diagnostics.recoveredHighlightPixelCount +=
            row.recoveredHighlightPixelCount;
        neffSum += row.neffSum;
        for (std::size_t range = 0u; range < rangePixelCount.size(); ++range)
            rangePixelCount[range] += row.rangePixelCount[range];
        for (std::size_t frameIndex = 0u;
             frameIndex < frames.size(); ++frameIndex) {
            contribution[frameIndex] += row.contribution[frameIndex];
            for (std::size_t range = 0u; range < rangePixelCount.size(); ++range) {
                contributionByRange[frameIndex][range] +=
                    row.contributionByRange[frameIndex][range];
            }
        }
    }
    }

    if (!request.interactiveFusion) {
    double maximumExposure = 0;
    for (const auto& frame : frames) maximumExposure = std::max(maximumExposure, frame.exposureRelativeToAnchor);
    // A clipped exposure can occasionally leave one CFA color substantially
    // darker than the other colors in the same 2x2 cell even after the local
    // highlight handoff. Detect that cross-color inconsistency with independent
    // same-CFA spatial medians, then replace it only with a real, unclipped
    // sample from one of the source frames. Neighbor values are never copied
    // into the result, so this pass cannot blur radiance or invent a color.
    struct ColorRepair {
        std::size_t index = 0u;
        std::size_t owner = 0u;
        float output = 0.0f;
        float variance = 0.0f;
        float headroom = 0.0f;
    };
    const std::size_t maximumColorRepairCount = std::max<std::size_t>(
        1024u, pixelCount / 1000u);
    const std::uint64_t colorCellRowCount = extent.height / 2u;
    std::vector<std::vector<ColorRepair>> colorRepairsByRow;
    try {
        colorRepairsByRow.resize(static_cast<std::size_t>(colorCellRowCount));
    } catch (const std::bad_alloc&) {
        return fail(ProcessingStatus::Failed,
            "The HDR color-repair index could not be allocated.");
    }
    std::atomic<std::uint64_t> nextColorCellRow { 0u };
    std::atomic<std::size_t> colorRepairCount { 0u };
    std::atomic<bool> colorRepairLimitExceeded { false };
    std::atomic<bool> colorRepairCanceled { false };
    std::atomic<bool> colorRepairFailed { false };
    std::mutex colorRepairFailureMutex;
    std::string colorRepairFailureMessage;
    const auto processColorRepairRows = [&]() {
      try {
        while (!colorRepairLimitExceeded.load(std::memory_order_relaxed) &&
               !colorRepairCanceled.load(std::memory_order_relaxed) &&
               !colorRepairFailed.load(std::memory_order_relaxed)) {
          const std::uint64_t cellRow = nextColorCellRow.fetch_add(
              1u, std::memory_order_relaxed);
          if (cellRow >= colorCellRowCount) break;
          if (Canceled(request)) {
              colorRepairCanceled.store(true, std::memory_order_relaxed);
              break;
          }
          const std::uint64_t cellY = cellRow * 2u;
          std::vector<ColorRepair>& rowRepairs = colorRepairsByRow[
              static_cast<std::size_t>(cellRow)];
          for (std::uint64_t cellX = 0; cellX + 1u < extent.width;
               cellX += 2u) {
            if (colorRepairLimitExceeded.load(std::memory_order_relaxed)) break;
            const std::array<std::uint64_t, 4> rawX {
                cellX, cellX + 1u, cellX, cellX + 1u };
            const std::array<std::uint64_t, 4> rawY {
                cellY, cellY, cellY + 1u, cellY + 1u };
            std::array<std::size_t, 4> indices {};
            std::array<CfaSite, 4> sites {};
            std::array<double, 4> expected {};
            std::array<double, 4> ratios {};
            std::array<bool, 4> haveExpected {};
            bool highlightContext = false;
            for (std::size_t channel = 0; channel < 4u; ++channel) {
                indices[channel] = static_cast<std::size_t>(
                    rawY[channel] * extent.width + rawX[channel]);
                sites[channel] = frames[reference].layout.SiteAt(
                    static_cast<std::int64_t>(rawX[channel]),
                    static_cast<std::int64_t>(rawY[channel]));
                std::size_t localSamples = 0u;
                haveExpected[channel] = LocalSameCfaMedian(
                    result,
                    frames[reference].layout,
                    sites[channel],
                    rawX[channel],
                    rawY[channel],
                    request.parameters.colorCoherenceRadiusCfa,
                    expected[channel],
                    localSamples) && expected[channel] > 1.0e-8;
                if (haveExpected[channel]) {
                    ratios[channel] = static_cast<double>(
                        result.virtualAnchorMosaic[indices[channel]]) /
                        expected[channel];
                    const double referenceGain = std::max(1.0e-8,
                        static_cast<double>(frames[reference].gain[indices[channel]]));
                    highlightContext = highlightContext ||
                        expected[channel] * referenceGain * maximumExposure > 0.90;
                }
                const std::uint8_t cellFlags = result.flags[indices[channel]];
                highlightContext = highlightContext ||
                    (cellFlags & (ResultFlagRecoveredHighlight |
                                  ResultFlagHighlightSafeHandoff)) != 0u;
            }

            for (std::size_t suspect = 0; suspect < 4u; ++suspect) {
                if (!haveExpected[suspect] ||
                    result.validityMask[indices[suspect]] == 0u ||
                    !std::isfinite(result.virtualAnchorMosaic[indices[suspect]]) ||
                    ratios[suspect] >= request.parameters.colorCoherenceDarkRatio) {
                    continue;
                }
                const double standardDeviation = std::sqrt(std::max(
                    request.parameters.numericalVarianceFloor,
                    static_cast<double>(result.varianceProxy[indices[suspect]])));
                if (expected[suspect] <=
                    static_cast<double>(result.virtualAnchorMosaic[indices[suspect]]) +
                        8.0 * standardDeviation) {
                    continue;
                }
                std::uint32_t supportingChannels = 0u;
                for (std::size_t channel = 0; channel < 4u; ++channel) {
                    if (channel != suspect && haveExpected[channel] &&
                        ratios[channel] >=
                            request.parameters.colorCoherenceNeighborRatio) {
                        ++supportingChannels;
                    }
                }
                if (supportingChannels <
                    request.parameters.colorCoherenceMinimumSupportingChannels) {
                    continue;
                }

                if (!highlightContext) {
                    for (std::size_t frameIndex = 0;
                         frameIndex < frames.size() && !highlightContext;
                         ++frameIndex) {
                        for (std::size_t channel = 0; channel < 4u; ++channel) {
                            if (ClippedSameCfaNeighborCountAt(
                                    frames[frameIndex],
                                    sites[channel],
                                    rawX[channel] + frames[frameIndex].translationX,
                                    rawY[channel] + frames[frameIndex].translationY) > 0u) {
                                highlightContext = true;
                                break;
                            }
                        }
                    }
                }
                if (!highlightContext) continue;

                const double currentError = std::abs(std::log2(
                    std::max(1.0e-8, ratios[suspect])));
                double bestScore = std::numeric_limits<double>::infinity();
                double bestCenterError = std::numeric_limits<double>::infinity();
                double bestOutput = 0.0;
                double bestVariance = 0.0;
                double bestScene = 0.0;
                std::size_t bestFrame = frames.size();
                for (std::size_t frameIndex = 0; frameIndex < frames.size();
                     ++frameIndex) {
                    std::array<Sample, 4> sourceSamples {};
                    std::array<double, 4> sourceOutputs {};
                    double score = 0.0;
                    double centerError = std::numeric_limits<double>::infinity();
                    std::uint32_t usableChannels = 0u;
                    bool centerUsable = false;
                    for (std::size_t channel = 0; channel < 4u; ++channel) {
                        if (!haveExpected[channel]) continue;
                        Sample& sample = sourceSamples[channel];
                        if (frameIndex == reference) {
                            const std::size_t index = indices[channel];
                            if (!ExactSample(
                                    frames[frameIndex], sites[channel], index, sample)) {
                                continue;
                            }
                        } else if (!SampleSameCfa(
                                frames[frameIndex],
                                sites[channel],
                                rawX[channel] + frames[frameIndex].translationX,
                                rawY[channel] + frames[frameIndex].translationY,
                                sample)) {
                            continue;
                        }
                        if (!sample.valid || sample.gain <= 0.0) continue;
                        const double referenceGain = std::max(1.0e-8,
                            static_cast<double>(frames[reference].gain[indices[channel]]));
                        const double expectedScene = expected[channel] * referenceGain;
                        const double predictedComparison = expectedScene *
                            frames[frameIndex].exposureRelativeToAnchor;
                        sample.variance = PredictedSampleVariance(
                            frames[frameIndex],
                            sample,
                            predictedComparison,
                            request.parameters.numericalVarianceFloor);
                        const double predictedNormalized =
                            predictedComparison / sample.gain;
                        const double sigmaNormalized = std::max(
                            1.0e-8, std::sqrt(sample.variance) / sample.gain);
                        const double headroomSigma =
                            (0.995 - predictedNormalized) / sigmaNormalized;
                        if (SmoothStep(
                                request.parameters.saturationHeadroomSigma,
                                request.parameters.saturationHeadroomSigma + 4.0,
                                headroomSigma) < 0.5) {
                            continue;
                        }
                        const double output = sample.comparison /
                            frames[frameIndex].exposureRelativeToAnchor /
                            referenceGain;
                        if (!std::isfinite(output) || output <= 0.0) continue;
                        sourceOutputs[channel] = output;
                        const double error = std::abs(std::log2(
                            output / expected[channel]));
                        score += (channel == suspect ? 2.0 : 1.0) * error;
                        ++usableChannels;
                        if (channel == suspect) {
                            centerUsable = true;
                            centerError = error;
                        }
                    }
                    if (!centerUsable || usableChannels <
                            request.parameters.colorCoherenceMinimumSupportingChannels + 1u ||
                        centerError >
                            request.parameters.colorCoherenceMaximumRepairErrorEv) {
                        continue;
                    }
                    score /= static_cast<double>(usableChannels + 1u);
                    if (score < bestScore) {
                        bestScore = score;
                        bestCenterError = centerError;
                        bestOutput = sourceOutputs[suspect];
                        bestFrame = frameIndex;
                        bestScene = bestOutput * std::max(1.0e-8,
                            static_cast<double>(frames[reference].gain[indices[suspect]]));
                        bestVariance = sourceSamples[suspect].variance /
                            (frames[frameIndex].exposureRelativeToAnchor *
                             frames[frameIndex].exposureRelativeToAnchor) /
                            std::pow(std::max(1.0e-8,
                                static_cast<double>(frames[reference].gain[
                                    indices[suspect]])), 2.0);
                    }
                }
                if (bestFrame >= frames.size() ||
                    currentError - bestCenterError <
                        request.parameters.colorCoherenceMinimumImprovementEv) {
                    continue;
                }
                const std::size_t repairOrdinal = colorRepairCount.fetch_add(
                    1u, std::memory_order_relaxed) + 1u;
                if (repairOrdinal > maximumColorRepairCount) {
                    colorRepairLimitExceeded.store(
                        true, std::memory_order_relaxed);
                    break;
                }
                rowRepairs.push_back({
                    indices[suspect],
                    bestFrame,
                    static_cast<float>(bestOutput),
                    static_cast<float>(std::max(
                        request.parameters.numericalVarianceFloor, bestVariance)),
                    static_cast<float>(std::max(
                        0.0, std::log2(std::max(1.0, bestScene)))) });
            }
          }
        }
      } catch (const std::bad_alloc&) {
          std::lock_guard<std::mutex> lock(colorRepairFailureMutex);
          colorRepairFailureMessage =
              "The HDR color-repair worker could not satisfy a memory allocation.";
          colorRepairFailed.store(true, std::memory_order_relaxed);
      } catch (const std::exception& exception) {
          std::lock_guard<std::mutex> lock(colorRepairFailureMutex);
          colorRepairFailureMessage = std::string(
              "HDR color-repair analysis failed: ") + exception.what();
          colorRepairFailed.store(true, std::memory_order_relaxed);
      } catch (...) {
          std::lock_guard<std::mutex> lock(colorRepairFailureMutex);
          colorRepairFailureMessage =
              "HDR color-repair analysis failed unexpectedly.";
          colorRepairFailed.store(true, std::memory_order_relaxed);
      }
    };

    const std::uint32_t requestedColorWorkers = std::max(
        1u, std::min<std::uint32_t>(request.workerCount,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                colorCellRowCount,
                std::numeric_limits<std::uint32_t>::max()))));
    std::vector<std::thread> colorWorkers;
    std::uint32_t colorWorkerCount = requestedColorWorkers;
    try {
        colorWorkers.reserve(colorWorkerCount > 0u
            ? colorWorkerCount - 1u : 0u);
    } catch (const std::bad_alloc&) {
        colorWorkerCount = 1u;
    }
    for (std::uint32_t worker = 1u; worker < colorWorkerCount; ++worker) {
        try {
            colorWorkers.emplace_back(processColorRepairRows);
        } catch (...) {
            break;
        }
    }
    processColorRepairRows();
    for (std::thread& worker : colorWorkers) {
        if (worker.joinable()) worker.join();
    }
    if (colorRepairCanceled.load(std::memory_order_relaxed)) {
        return fail(ProcessingStatus::Canceled, "HDR processing was canceled.");
    }
    if (colorRepairFailed.load(std::memory_order_relaxed)) {
        return fail(ProcessingStatus::Failed,
            colorRepairFailureMessage.empty()
                ? "HDR color-repair analysis failed unexpectedly."
                : colorRepairFailureMessage);
    }
    if (colorRepairLimitExceeded.load(std::memory_order_relaxed)) {
        colorRepairsByRow.clear();
        result.diagnostics.warnings.push_back(
            "Color-coherent highlight repair was skipped because the inconsistency was not sparse.");
    } else {
      for (const std::vector<ColorRepair>& rowRepairs : colorRepairsByRow) {
        for (const ColorRepair& repair : rowRepairs) {
            const std::uint8_t oldFlags = result.flags[repair.index];
            const std::size_t oldOwner = result.ownerFrame[repair.index];
            neffSum += 1.0 - static_cast<double>(
                result.effectiveSampleCount[repair.index]);
            if (oldOwner != repair.owner && oldOwner < contribution.size()) {
                contribution[oldOwner] = std::max(0.0, contribution[oldOwner] - 1.0);
                contribution[repair.owner] += 1.0;
            }
            if ((oldFlags & ResultFlagReferenceFallback) != 0u &&
                result.diagnostics.referenceFallbackPixelCount > 0u) {
                --result.diagnostics.referenceFallbackPixelCount;
            }
            const bool oldRecovered =
                (oldFlags & ResultFlagRecoveredHighlight) != 0u;
            const bool newRecovered = repair.headroom > 0.001f;
            if (oldRecovered && !newRecovered &&
                result.diagnostics.recoveredHighlightPixelCount > 0u) {
                --result.diagnostics.recoveredHighlightPixelCount;
            } else if (!oldRecovered && newRecovered) {
                ++result.diagnostics.recoveredHighlightPixelCount;
            }
            result.virtualAnchorMosaic[repair.index] = repair.output;
            result.varianceProxy[repair.index] = repair.variance;
            result.mergeConfidence[repair.index] = 0.0f;
            result.effectiveSampleCount[repair.index] = 1.0f;
            result.recoveredHeadroomStops[repair.index] = repair.headroom;
            result.ownerFrame[repair.index] = static_cast<std::uint8_t>(repair.owner);
            result.flags[repair.index] = static_cast<std::uint8_t>(
                (oldFlags & ~(ResultFlagReferenceFallback |
                              ResultFlagLowConfidenceOwner |
                              ResultFlagRecoveredHighlight)) |
                ResultFlagSingleExposure |
                ResultFlagColorCoherentRepair |
                (newRecovered ? ResultFlagRecoveredHighlight : ResultFlagNone) |
                (frames[repair.owner].lowConfidence
                    ? ResultFlagLowConfidenceOwner : ResultFlagNone));
            ++result.diagnostics.colorCoherentRepairPixelCount;
        }
      }
    }

    }
    if (preview && prepared->analysisEv.empty()) {
        const auto cw = (extent.width + 1) / 2, ch = (extent.height + 1) / 2;
        prepared->analysisEv.resize(static_cast<std::size_t>(cw * ch));
        for (std::uint64_t cy = 0; cy < ch; ++cy) for (std::uint64_t cx = 0; cx < cw; ++cx) {
            float luma = 0; unsigned int n = 0;
            for (unsigned int dy = 0; dy < 2; ++dy) for (unsigned int dx = 0; dx < 2; ++dx) {
                if (cx * 2 + dx >= extent.width || cy * 2 + dy >= extent.height) continue;
                luma += result.virtualAnchorMosaic[(cy * 2 + dy) * extent.width + cx * 2 + dx]; ++n;
            }
            const float ev = std::log2(std::max(1.0e-6f, luma / std::max(1u, n)) / 0.18f);
            prepared->analysisEv[cy * cw + cx] = ev;
            if (cx % preview->stride == 0 && cy % preview->stride == 0)
                preview->analysisEv[(cy / preview->stride) * preview->width + cx / preview->stride] = ev;
        }
    }
    if (result.diagnostics.finitePixelCount == 0u)
        return fail(ProcessingStatus::Failed, "HDR fusion produced no finite pixels.");
    std::string payloadError;
    if (!ValidatePublishedPayload(result, frames.size(), &payloadError)) {
        return fail(
            ProcessingStatus::Failed,
            payloadError.empty()
                ? "HDR fusion did not produce a complete Virtual Bayer payload."
                : payloadError);
    }
    result.diagnostics.meanEffectiveSamples = neffSum /
        static_cast<double>(result.diagnostics.finitePixelCount);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        result.diagnostics.frames[i].aggregateContribution = contribution[i] /
            static_cast<double>(result.diagnostics.finitePixelCount);
        result.diagnostics.frames[i].shadowContribution =
            contributionByRange[i][0] / std::max(1.0, rangePixelCount[0]);
        result.diagnostics.frames[i].midtoneContribution =
            contributionByRange[i][1] / std::max(1.0, rangePixelCount[1]);
        result.diagnostics.frames[i].highlightContribution =
            contributionByRange[i][2] / std::max(1.0, rangePixelCount[2]);
    }

    Report(request, ProcessingStage::Caching, 0.92, 0.0, 0, "Caching HDR result");
    result.diagnostics.cacheKey = BuildCacheKey(
        request,
        reference,
        anchor,
        result.diagnostics.executionBackend,
        result.diagnostics.gpuDeviceIdentity);
    if (preview) preview->contentIdentity = result.diagnostics.cacheKey;
    result.status = ProcessingStatus::Published;
    result.message = "Scene-linear Bayer HDR result published.";
    std::string cacheError;
    if (!WriteResultCache(request.workingDirectory / "results", result, &cacheError))
        return fail(ProcessingStatus::Failed, cacheError.empty()
            ? "The HDR result cache could not be written." : cacheError);
    Report(request, ProcessingStage::Finalizing, 1.0, 1.0, 0, result.message);
    return result;
}

nlohmann::json SerializeDiagnostics(const Result& result) {
    nlohmann::json frames = nlohmann::json::array();
    for (const FrameDiagnostic& frame : result.diagnostics.frames) {
        frames.push_back({
            { "stableFrameId", frame.stableFrameId },
            { "geometricReference", frame.geometricReference },
            { "radiometricAnchor", frame.radiometricAnchor },
            { "structurallyUsable", frame.structurallyUsable },
            { "exposureVerified", frame.exposureVerified },
            { "lowConfidence", frame.lowConfidence },
            { "metadataExposureRelativeToAnchor", frame.metadataExposureRelativeToAnchor },
            { "fittedExposureRelativeToAnchor", frame.fittedExposureRelativeToAnchor },
            { "fittedExposureUncertaintyEv", frame.fittedExposureUncertaintyEv },
            { "metadataDisagreementEv", frame.metadataDisagreementEv },
            { "translationRaw", { frame.translationRawX, frame.translationRawY } },
            { "aggregateContribution", frame.aggregateContribution },
            { "shadowContribution", frame.shadowContribution },
            { "midtoneContribution", frame.midtoneContribution },
            { "highlightContribution", frame.highlightContribution },
            { "exposureFitPath", frame.exposureFitPath },
            { "noiseModelSource", frame.noiseModelSource },
            { "noiseModelQuality", frame.noiseModelQuality },
            { "noiseVarianceInflation", frame.noiseVarianceInflation },
            { "message", frame.message }
        });
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const ExposureFitEdgeDiagnostic& edge :
            result.diagnostics.exposureFitEdges) {
        edges.push_back({
            { "fromFrameId", edge.fromFrameId },
            { "toFrameId", edge.toFrameId },
            { "verified", edge.verified },
            { "fittedScale", edge.fittedScale },
            { "fittedOffset", edge.fittedOffset },
            { "uncertaintyEv", edge.uncertaintyEv },
            { "residualNoiseInflation", edge.residualNoiseInflation },
            { "sampleCount", edge.sampleCount },
            { "siteSampleCounts", edge.siteSampleCounts }
        });
    }
    return {
        { "contractVersion", result.contractVersion },
        { "contractId", result.contractId },
        { "status", ProcessingStatusName(result.status) },
        { "message", result.message },
        { "cacheKey", result.diagnostics.cacheKey },
        { "executionBackend", result.diagnostics.executionBackend },
        { "gpuDeviceIdentity", result.diagnostics.gpuDeviceIdentity },
        { "gpuFallbackReason", result.diagnostics.gpuFallbackReason },
        { "gpuDispatchedTileCount", result.diagnostics.gpuDispatchedTileCount },
        { "inputRevision", result.diagnostics.inputRevision },
        { "geometricReferenceFrameId", result.geometricReferenceFrameId },
        { "radiometricAnchorFrameId", result.radiometricAnchorFrameId },
        { "width", result.width },
        { "height", result.height },
        { "cfaPattern", CfaPatternName(result.outputCfaPattern) },
        { "exposureSpanEv", result.diagnostics.exposureSpanEv },
        { "meanEffectiveSamples", result.diagnostics.meanEffectiveSamples },
        { "finitePixelCount", result.diagnostics.finitePixelCount },
        { "referenceFallbackPixelCount", result.diagnostics.referenceFallbackPixelCount },
        { "highlightSafeHandoffPixelCount",
            result.diagnostics.highlightSafeHandoffPixelCount },
        { "colorCoherentRepairPixelCount",
            result.diagnostics.colorCoherentRepairPixelCount },
        { "recoveredHighlightPixelCount", result.diagnostics.recoveredHighlightPixelCount },
        { "warnings", result.diagnostics.warnings },
        { "frames", std::move(frames) },
        { "exposureFitEdges", std::move(edges) }
    };
}

bool WriteResultCache(
    const std::filesystem::path& directory,
    const Result& result,
    std::string* error) {
    if (!LooksLikeSha256(result.diagnostics.cacheKey)) {
        if (error) *error = "HDR cache key is invalid.";
        return false;
    }
    nlohmann::json header = SerializeDiagnostics(result);
    const std::string headerText = header.dump();
    const std::array<std::uint64_t, 8> vectorCounts {
        result.virtualAnchorMosaic.size(),
        result.varianceProxy.size(),
        result.mergeConfidence.size(),
        result.effectiveSampleCount.size(),
        result.recoveredHeadroomStops.size(),
        result.validityMask.size(),
        result.ownerFrame.size(),
        result.flags.size()
    };
    std::vector<std::string_view> hashChunks;
    try {
        hashChunks.reserve(1u + vectorCounts.size() * 2u);
        hashChunks.emplace_back(headerText);
        const auto addVectorChunks = [&](std::size_t index,
                                         const auto& values) {
            hashChunks.emplace_back(
                reinterpret_cast<const char*>(&vectorCounts[index]),
                sizeof(vectorCounts[index]));
            hashChunks.emplace_back(
                reinterpret_cast<const char*>(values.data()),
                values.size() * sizeof(typename std::decay_t<
                    decltype(values)>::value_type));
        };
        addVectorChunks(0u, result.virtualAnchorMosaic);
        addVectorChunks(1u, result.varianceProxy);
        addVectorChunks(2u, result.mergeConfidence);
        addVectorChunks(3u, result.effectiveSampleCount);
        addVectorChunks(4u, result.recoveredHeadroomStops);
        addVectorChunks(5u, result.validityMask);
        addVectorChunks(6u, result.ownerFrame);
        addVectorChunks(7u, result.flags);
    } catch (const std::bad_alloc&) {
        if (error) *error = "HDR cache hashing could not be initialized.";
        return false;
    }
    const std::string payloadHash = Sha256Digest(hashChunks);
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        if (error) *error = "The HDR cache directory could not be created.";
        return false;
    }
    const std::filesystem::path finalPath = directory /
        (result.diagnostics.cacheKey + ".hdr-cache");
    const std::filesystem::path temporaryPath = finalPath.string() + ".tmp";
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        const std::uint64_t headerSize = headerText.size();
        if (!stream ||
            !stream.write(kCacheMagic.data(), kCacheMagic.size()) ||
            !stream.write(reinterpret_cast<const char*>(&headerSize), sizeof(headerSize)) ||
            !stream.write(headerText.data(), static_cast<std::streamsize>(headerText.size())) ||
            !stream.write(payloadHash.data(), static_cast<std::streamsize>(payloadHash.size())) ||
            !WriteVector(stream, result.virtualAnchorMosaic) ||
            !WriteVector(stream, result.varianceProxy) ||
            !WriteVector(stream, result.mergeConfidence) ||
            !WriteVector(stream, result.effectiveSampleCount) ||
            !WriteVector(stream, result.recoveredHeadroomStops) ||
            !WriteVector(stream, result.validityMask) ||
            !WriteVector(stream, result.ownerFrame) ||
            !WriteVector(stream, result.flags)) {
            stream.close();
            std::filesystem::remove(temporaryPath, filesystemError);
            if (error) *error = "The HDR cache could not be written.";
            return false;
        }
    }
    std::filesystem::remove(finalPath, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporaryPath, finalPath, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(temporaryPath, filesystemError);
        if (error) *error = "The HDR cache could not be published atomically.";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ReadResultCache(
    const std::filesystem::path& directory,
    const std::string& cacheKey,
    Result& result,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey)) {
        if (error) *error = "HDR cache key is invalid.";
        return false;
    }
    std::ifstream stream(directory / (cacheKey + ".hdr-cache"), std::ios::binary);
    if (!stream) {
        if (error) *error = "The HDR cache entry is missing.";
        return false;
    }
    std::array<char, 8> magic {};
    std::uint64_t headerSize = 0;
    stream.read(magic.data(), magic.size());
    stream.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize));
    if (!stream || magic != kCacheMagic || headerSize > (16u << 20u)) {
        if (error) *error = "The HDR cache header is corrupt.";
        return false;
    }
    std::string headerText(static_cast<std::size_t>(headerSize), '\0');
    std::string expectedHash(64u, '\0');
    stream.read(headerText.data(), static_cast<std::streamsize>(headerText.size()));
    stream.read(expectedHash.data(), static_cast<std::streamsize>(expectedHash.size()));
    std::vector<char> payload {
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    std::string verifiedBytes = headerText;
    verifiedBytes.append(payload.data(), payload.size());
    // istreambuf_iterator may consume the complete stream without setting
    // eofbit. A hard I/O failure is meaningful here; eofbit itself is not.
    if (stream.bad() || Sha256Digest(verifiedBytes) != expectedHash) {
        if (error) *error = "The HDR cache payload failed content verification.";
        return false;
    }
    nlohmann::json header;
    try { header = nlohmann::json::parse(headerText); }
    catch (...) {
        if (error) *error = "The HDR cache diagnostics are corrupt.";
        return false;
    }
    if (header.value("cacheKey", std::string()) != cacheKey ||
        header.value("contractId", std::string()) != kProcessorContractId) {
        if (error) *error = "The HDR cache identity does not match the request.";
        return false;
    }
    Result decoded;
    decoded.status = ProcessingStatus::Published;
    decoded.message = header.value("message", std::string());
    decoded.width = header.value("width", std::uint64_t { 0 });
    decoded.height = header.value("height", std::uint64_t { 0 });
    decoded.geometricReferenceFrameId = header.value(
        "geometricReferenceFrameId", std::string());
    decoded.radiometricAnchorFrameId = header.value(
        "radiometricAnchorFrameId", std::string());
    decoded.diagnostics.cacheKey = cacheKey;
    decoded.diagnostics.executionBackend = header.value(
        "executionBackend", std::string("cpu-reference"));
    decoded.diagnostics.gpuDeviceIdentity = header.value(
        "gpuDeviceIdentity", std::string());
    decoded.diagnostics.gpuFallbackReason = header.value(
        "gpuFallbackReason", std::string());
    decoded.diagnostics.gpuDispatchedTileCount = header.value(
        "gpuDispatchedTileCount", std::uint32_t { 0 });
    decoded.diagnostics.inputRevision = header.value("inputRevision", std::uint64_t { 0 });
    decoded.diagnostics.exposureSpanEv = header.value("exposureSpanEv", 0.0);
    decoded.diagnostics.meanEffectiveSamples = header.value("meanEffectiveSamples", 0.0);
    decoded.diagnostics.finitePixelCount = header.value(
        "finitePixelCount", std::uint64_t { 0 });
    decoded.diagnostics.referenceFallbackPixelCount = header.value(
        "referenceFallbackPixelCount", std::uint64_t { 0 });
    decoded.diagnostics.highlightSafeHandoffPixelCount = header.value(
        "highlightSafeHandoffPixelCount", std::uint64_t { 0 });
    decoded.diagnostics.colorCoherentRepairPixelCount = header.value(
        "colorCoherentRepairPixelCount", std::uint64_t { 0 });
    decoded.diagnostics.recoveredHighlightPixelCount = header.value(
        "recoveredHighlightPixelCount", std::uint64_t { 0 });
    const nlohmann::json warnings = header.value("warnings", nlohmann::json::array());
    if (warnings.is_array()) {
        for (const nlohmann::json& warning : warnings)
            if (warning.is_string()) decoded.diagnostics.warnings.push_back(
                warning.get<std::string>());
    }
    const nlohmann::json frames = header.value("frames", nlohmann::json::array());
    if (frames.is_array()) {
        for (const nlohmann::json& item : frames) {
            if (!item.is_object()) continue;
            FrameDiagnostic frame;
            frame.stableFrameId = item.value("stableFrameId", std::string());
            frame.geometricReference = item.value("geometricReference", false);
            frame.radiometricAnchor = item.value("radiometricAnchor", false);
            frame.structurallyUsable = item.value("structurallyUsable", false);
            frame.exposureVerified = item.value("exposureVerified", false);
            frame.lowConfidence = item.value("lowConfidence", false);
            frame.metadataExposureRelativeToAnchor = item.value(
                "metadataExposureRelativeToAnchor", 1.0);
            frame.fittedExposureRelativeToAnchor = item.value(
                "fittedExposureRelativeToAnchor", 1.0);
            frame.fittedExposureUncertaintyEv = item.value(
                "fittedExposureUncertaintyEv", 0.0);
            frame.metadataDisagreementEv = item.value(
                "metadataDisagreementEv", 0.0);
            const nlohmann::json translation = item.value(
                "translationRaw", nlohmann::json::array());
            if (translation.is_array() && translation.size() >= 2u) {
                frame.translationRawX = translation[0].get<double>();
                frame.translationRawY = translation[1].get<double>();
            }
            frame.aggregateContribution = item.value("aggregateContribution", 0.0);
            frame.shadowContribution = item.value("shadowContribution", 0.0);
            frame.midtoneContribution = item.value("midtoneContribution", 0.0);
            frame.highlightContribution = item.value("highlightContribution", 0.0);
            const nlohmann::json path = item.value(
                "exposureFitPath", nlohmann::json::array());
            if (path.is_array()) {
                for (const nlohmann::json& pathItem : path) {
                    if (pathItem.is_string())
                        frame.exposureFitPath.push_back(pathItem.get<std::string>());
                }
            }
            frame.noiseModelSource = item.value("noiseModelSource", std::string());
            frame.noiseModelQuality = item.value("noiseModelQuality", std::string());
            frame.noiseVarianceInflation = item.value("noiseVarianceInflation", 1.0);
            frame.message = item.value("message", std::string());
            decoded.diagnostics.frames.push_back(std::move(frame));
        }
    }
    const nlohmann::json edges = header.value(
        "exposureFitEdges", nlohmann::json::array());
    if (edges.is_array()) {
        for (const nlohmann::json& item : edges) {
            if (!item.is_object()) continue;
            ExposureFitEdgeDiagnostic edge;
            edge.fromFrameId = item.value("fromFrameId", std::string());
            edge.toFrameId = item.value("toFrameId", std::string());
            edge.verified = item.value("verified", false);
            edge.fittedScale = item.value("fittedScale", 1.0);
            edge.fittedOffset = item.value("fittedOffset", 0.0);
            edge.uncertaintyEv = item.value("uncertaintyEv", 0.0);
            edge.residualNoiseInflation = item.value(
                "residualNoiseInflation", 1.0);
            edge.sampleCount = item.value("sampleCount", std::uint64_t { 0 });
            const nlohmann::json counts = item.value(
                "siteSampleCounts", nlohmann::json::array());
            if (counts.is_array()) {
                for (std::size_t site = 0;
                     site < edge.siteSampleCounts.size() && site < counts.size();
                     ++site) {
                    edge.siteSampleCounts[site] = counts[site].get<std::uint64_t>();
                }
            }
            decoded.diagnostics.exposureFitEdges.push_back(std::move(edge));
        }
    }
    const std::string cfa = header.value("cfaPattern", std::string());
    if (cfa == "RGGB") decoded.outputCfaPattern = CfaPattern::RGGB;
    else if (cfa == "BGGR") decoded.outputCfaPattern = CfaPattern::BGGR;
    else if (cfa == "GRBG") decoded.outputCfaPattern = CfaPattern::GRBG;
    else if (cfa == "GBRG") decoded.outputCfaPattern = CfaPattern::GBRG;
    std::size_t cursor = 0;
    if (!ReadVector(payload, cursor, decoded.virtualAnchorMosaic) ||
        !ReadVector(payload, cursor, decoded.varianceProxy) ||
        !ReadVector(payload, cursor, decoded.mergeConfidence) ||
        !ReadVector(payload, cursor, decoded.effectiveSampleCount) ||
        !ReadVector(payload, cursor, decoded.recoveredHeadroomStops) ||
        !ReadVector(payload, cursor, decoded.validityMask) ||
        !ReadVector(payload, cursor, decoded.ownerFrame) ||
        !ReadVector(payload, cursor, decoded.flags) || cursor != payload.size()) {
        if (error) *error = "The HDR cache vector layout is corrupt.";
        return false;
    }
    const std::size_t expectedPixels = static_cast<std::size_t>(decoded.width * decoded.height);
    if (decoded.virtualAnchorMosaic.size() != expectedPixels ||
        decoded.varianceProxy.size() != expectedPixels ||
        decoded.mergeConfidence.size() != expectedPixels ||
        decoded.effectiveSampleCount.size() != expectedPixels ||
        decoded.recoveredHeadroomStops.size() != expectedPixels ||
        decoded.validityMask.size() != expectedPixels ||
        decoded.ownerFrame.size() != expectedPixels ||
        decoded.flags.size() != expectedPixels) {
        if (error) *error = "The HDR cache dimensions do not match its payload.";
        return false;
    }
    std::string payloadError;
    if (!ValidatePublishedPayload(
            decoded, decoded.diagnostics.frames.size(), &payloadError)) {
        if (error) *error = payloadError.empty()
            ? "The HDR cache contains an incomplete Virtual Bayer payload."
            : payloadError;
        return false;
    }
    result = std::move(decoded);
    if (error) error->clear();
    return true;
}

} // namespace Raw::Hdr
