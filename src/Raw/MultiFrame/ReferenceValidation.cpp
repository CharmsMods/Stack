#include "Raw/MultiFrame/ReferenceValidation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <utility>

namespace Raw::MultiFrame {
namespace {

bool SetError(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

bool CheckedPixelCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return false;
    }
    const std::uint64_t product = extent.width * extent.height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

ReferenceRgbChannel ChannelForPhase(CfaPhase phase) {
    switch (phase) {
        case CfaPhase::R: return ReferenceRgbChannel::Red;
        case CfaPhase::G0:
        case CfaPhase::G1: return ReferenceRgbChannel::Green;
        case CfaPhase::B: return ReferenceRgbChannel::Blue;
        default: return ReferenceRgbChannel::Green;
    }
}

std::size_t ChannelIndex(ReferenceRgbChannel channel) {
    return static_cast<std::size_t>(channel);
}

std::size_t PixelIndex(
    std::uint64_t x,
    std::uint64_t y,
    std::uint64_t width) {
    return static_cast<std::size_t>(y * width + x);
}

std::size_t RgbIndex(std::size_t pixel, std::size_t channel) {
    return pixel * 3u + channel;
}

std::uint8_t ChannelMask(std::size_t channel) {
    return static_cast<std::uint8_t>(1u << channel);
}

struct ErrorAccumulator {
    std::uint64_t count = 0;
    long double absoluteSum = 0.0;
    long double squaredSum = 0.0;
    double maximumAbsolute = 0.0;

    void Add(double candidate, double reference) {
        const double difference = candidate - reference;
        const double absolute = std::abs(difference);
        ++count;
        absoluteSum += absolute;
        squaredSum += static_cast<long double>(difference) * difference;
        maximumAbsolute = std::max(maximumAbsolute, absolute);
    }

    ScalarErrorMetrics Finish() const {
        ScalarErrorMetrics result;
        result.sampleCount = count;
        if (count == 0u) return result;
        result.meanAbsoluteError = static_cast<double>(absoluteSum / count);
        result.rootMeanSquareError = std::sqrt(
            static_cast<double>(squaredSum / count));
        result.maximumAbsoluteError = maximumAbsolute;
        return result;
    }
};

bool ValidateDemosaicInput(
    const std::vector<float>& packedMosaic,
    const std::vector<SampleEvidence>& evidence,
    const CfaDomainIdentity& domain,
    std::size_t& pixelCount,
    std::string* error) {
    if (!ValidateCfaDomainIdentity(domain, error) ||
        !CheckedPixelCount(domain.activeExtent, pixelCount) ||
        packedMosaic.size() != pixelCount || evidence.size() != pixelCount) {
        return SetError(error, "Reference demosaic input shape is invalid.");
    }
    for (std::size_t index = 0; index < pixelCount; ++index) {
        if (!ValidateSampleEvidence(evidence[index], error)) return false;
        if (IsNumericMeasurement(evidence[index]) &&
            !std::isfinite(packedMosaic[index])) {
            return SetError(error, "Reference demosaic numeric input is not finite.");
        }
    }
    return true;
}

void ComputeDarkArtifacts(
    const StageComparisonRequest& request,
    const ReferenceRgbImage& reference,
    const ReferenceRgbImage& candidate,
    DarkArtifactMetrics& metrics) {
    const std::uint64_t width = request.domain.activeExtent.width;
    const std::uint64_t height = request.domain.activeExtent.height;
    const std::size_t count = static_cast<std::size_t>(width * height);
    std::vector<std::uint8_t> risk(count, 0u);
    for (std::uint64_t y = 0; y < height; ++y) {
        for (std::uint64_t x = 0; x < width; ++x) {
            bool nearInvalid = false;
            const std::uint64_t radius = request.clippedProximityRadius;
            const std::uint64_t minY = y > radius ? y - radius : 0u;
            const std::uint64_t maxY = std::min(height - 1u, y + radius);
            const std::uint64_t minX = x > radius ? x - radius : 0u;
            const std::uint64_t maxX = std::min(width - 1u, x + radius);
            for (std::uint64_t sourceY = minY;
                 sourceY <= maxY && !nearInvalid;
                 ++sourceY) {
                for (std::uint64_t sourceX = minX; sourceX <= maxX; ++sourceX) {
                    if (!IsNumericMeasurement(request.expectedEvidence[
                            PixelIndex(sourceX, sourceY, width)])) {
                        nearInvalid = true;
                        break;
                    }
                }
            }
            if (!nearInvalid) continue;
            const std::size_t pixel = PixelIndex(x, y, width);
            if (reference.validChannelMask[pixel] != 0x7u ||
                candidate.validChannelMask[pixel] != 0x7u) {
                continue;
            }
            double referenceIntensity = 0.0;
            double candidateIntensity = 0.0;
            for (std::size_t channel = 0; channel < 3u; ++channel) {
                referenceIntensity += reference.linearRgb[RgbIndex(pixel, channel)];
                candidateIntensity += candidate.linearRgb[RgbIndex(pixel, channel)];
            }
            referenceIntensity /= 3.0;
            candidateIntensity /= 3.0;
            if (!std::isfinite(referenceIntensity) ||
                !std::isfinite(candidateIntensity) ||
                referenceIntensity <= request.minimumReferenceIntensity) {
                continue;
            }
            const double ratio = candidateIntensity / referenceIntensity;
            if (ratio < request.darkArtifactRatio) risk[pixel] = 1u;
        }
    }

    std::vector<std::uint8_t> visited(count, 0u);
    metrics.minimumCandidateToReferenceRatio = 1.0;
    const std::array<std::pair<int, int>, 4> neighbors {
        std::pair<int, int>{ -1, 0 },
        std::pair<int, int>{ 1, 0 },
        std::pair<int, int>{ 0, -1 },
        std::pair<int, int>{ 0, 1 }
    };
    for (std::uint64_t startY = 0; startY < height; ++startY) {
        for (std::uint64_t startX = 0; startX < width; ++startX) {
            const std::size_t start = PixelIndex(startX, startY, width);
            if (!risk[start] || visited[start]) continue;
            ++metrics.connectedRegionCount;
            std::queue<std::pair<std::uint64_t, std::uint64_t>> pending;
            pending.push({ startX, startY });
            visited[start] = 1u;
            while (!pending.empty()) {
                const auto [x, y] = pending.front();
                pending.pop();
                const std::size_t pixel = PixelIndex(x, y, width);
                ++metrics.affectedPixelCount;
                const CfaPhase phase = PhaseAt(request.domain, x, y);
                ++metrics.affectedPhaseCounts[static_cast<std::size_t>(phase)];

                double referenceIntensity = 0.0;
                double candidateIntensity = 0.0;
                for (std::size_t channel = 0; channel < 3u; ++channel) {
                    referenceIntensity += reference.linearRgb[RgbIndex(pixel, channel)];
                    candidateIntensity += candidate.linearRgb[RgbIndex(pixel, channel)];
                }
                const double ratio = candidateIntensity / referenceIntensity;
                metrics.minimumCandidateToReferenceRatio = std::min(
                    metrics.minimumCandidateToReferenceRatio,
                    ratio);

                for (const auto [dx, dy] : neighbors) {
                    const std::int64_t nextX = static_cast<std::int64_t>(x) + dx;
                    const std::int64_t nextY = static_cast<std::int64_t>(y) + dy;
                    if (nextX < 0 || nextY < 0 ||
                        nextX >= static_cast<std::int64_t>(width) ||
                        nextY >= static_cast<std::int64_t>(height)) {
                        continue;
                    }
                    const std::size_t next = PixelIndex(
                        static_cast<std::uint64_t>(nextX),
                        static_cast<std::uint64_t>(nextY),
                        width);
                    if (!risk[next] || visited[next]) continue;
                    visited[next] = 1u;
                    pending.push({
                        static_cast<std::uint64_t>(nextX),
                        static_cast<std::uint64_t>(nextY)
                    });
                }
            }
        }
    }
}

} // namespace

bool FixedReferenceDemosaic(
    const std::vector<float>& packedMosaic,
    const std::vector<SampleEvidence>& evidence,
    const CfaDomainIdentity& domain,
    ReferenceRgbImage& result,
    std::string* error) {
    std::size_t pixelCount = 0;
    if (!ValidateDemosaicInput(
            packedMosaic, evidence, domain, pixelCount, error)) {
        return false;
    }

    ReferenceRgbImage image;
    image.extent = domain.activeExtent;
    image.linearRgb.assign(
        pixelCount * 3u,
        std::numeric_limits<float>::quiet_NaN());
    image.validChannelMask.assign(pixelCount, 0u);
    image.supportCounts.resize(pixelCount);
    const std::uint64_t width = domain.activeExtent.width;
    const std::uint64_t height = domain.activeExtent.height;

    for (std::uint64_t y = 0; y < height; ++y) {
        for (std::uint64_t x = 0; x < width; ++x) {
            const std::size_t pixel = PixelIndex(x, y, width);
            const CfaPhase centerPhase = PhaseAt(domain, x, y);
            const ReferenceRgbChannel centerChannel = ChannelForPhase(centerPhase);
            for (std::size_t channel = 0; channel < 3u; ++channel) {
                long double weightedSum = 0.0;
                long double weightSum = 0.0;
                std::uint16_t support = 0u;
                if (ChannelIndex(centerChannel) == channel &&
                    IsNumericMeasurement(evidence[pixel])) {
                    weightedSum = packedMosaic[pixel];
                    weightSum = 1.0;
                    support = 1u;
                } else {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const std::int64_t sampleX =
                                static_cast<std::int64_t>(x) + dx;
                            const std::int64_t sampleY =
                                static_cast<std::int64_t>(y) + dy;
                            if (sampleX < 0 || sampleY < 0 ||
                                sampleX >= static_cast<std::int64_t>(width) ||
                                sampleY >= static_cast<std::int64_t>(height)) {
                                continue;
                            }
                            const std::uint64_t sourceX =
                                static_cast<std::uint64_t>(sampleX);
                            const std::uint64_t sourceY =
                                static_cast<std::uint64_t>(sampleY);
                            const CfaPhase sourcePhase =
                                PhaseAt(domain, sourceX, sourceY);
                            if (ChannelIndex(ChannelForPhase(sourcePhase)) != channel) {
                                continue;
                            }
                            const std::size_t source =
                                PixelIndex(sourceX, sourceY, width);
                            if (!IsNumericMeasurement(evidence[source])) continue;
                            const double weight = 1.0 /
                                static_cast<double>(1 + dx * dx + dy * dy);
                            weightedSum += weight * packedMosaic[source];
                            weightSum += weight;
                            ++support;
                        }
                    }
                }
                if (support == 0u || weightSum <= 0.0) continue;
                image.linearRgb[RgbIndex(pixel, channel)] = static_cast<float>(
                    weightedSum / weightSum);
                image.validChannelMask[pixel] |= ChannelMask(channel);
                image.supportCounts[pixel][channel] = support;
            }
        }
    }
    result = std::move(image);
    return true;
}

bool CompareReferenceStages(
    const StageComparisonRequest& request,
    StageComparisonMetrics& result,
    std::string* error) {
    std::size_t pixelCount = 0;
    if (!ValidateCfaDomainIdentity(request.domain, error) ||
        !CheckedPixelCount(request.domain.activeExtent, pixelCount) ||
        request.latentMosaic.size() != pixelCount ||
        request.expectedEvidence.size() != pixelCount ||
        request.candidateMosaic.size() != pixelCount ||
        request.candidateEvidence.size() != pixelCount ||
        !std::isfinite(request.zeroTolerance) || request.zeroTolerance < 0.0 ||
        !std::isfinite(request.darkArtifactRatio) ||
        request.darkArtifactRatio <= 0.0 || request.darkArtifactRatio >= 1.0 ||
        !std::isfinite(request.minimumReferenceIntensity) ||
        request.minimumReferenceIntensity < 0.0) {
        return SetError(error, "MultiFrame stage-comparison request is invalid.");
    }

    std::vector<SampleEvidence> latentEvidence(
        pixelCount,
        SampleEvidence {});
    std::array<ErrorAccumulator, 4> mosaicByPhase;
    ErrorAccumulator mosaicAll;
    StageComparisonMetrics metrics;
    const std::uint64_t width = request.domain.activeExtent.width;
    for (std::size_t index = 0; index < pixelCount; ++index) {
        if (!std::isfinite(request.latentMosaic[index]) ||
            !ValidateSampleEvidence(request.expectedEvidence[index], error) ||
            !ValidateSampleEvidence(request.candidateEvidence[index], error)) {
            return SetError(error, "MultiFrame stage-comparison evidence is invalid.");
        }
        const SampleEvidence& expected = request.expectedEvidence[index];
        const SampleEvidence& candidate = request.candidateEvidence[index];
        if (expected.state != candidate.state ||
            expected.causes != candidate.causes) {
            ++metrics.categoricalEvidenceMismatchCount;
        }
        if (!IsNumericMeasurement(expected) && IsNumericMeasurement(candidate)) {
            ++metrics.falseEqualitySupportCount;
            if (std::isfinite(request.candidateMosaic[index]) &&
                std::abs(request.candidateMosaic[index]) <= request.zeroTolerance) {
                ++metrics.invalidOrClippedZeroCount;
            }
        }
        if (!IsNumericMeasurement(candidate)) {
            ++metrics.unresolvedCandidateCount;
            continue;
        }
        if (!std::isfinite(request.candidateMosaic[index])) {
            return SetError(error, "MultiFrame candidate numeric sample is not finite.");
        }
        const std::uint64_t y = static_cast<std::uint64_t>(index) / width;
        const std::uint64_t x = static_cast<std::uint64_t>(index) % width;
        const std::size_t phase = static_cast<std::size_t>(
            PhaseAt(request.domain, x, y));
        mosaicByPhase[phase].Add(
            request.candidateMosaic[index],
            request.latentMosaic[index]);
        mosaicAll.Add(
            request.candidateMosaic[index],
            request.latentMosaic[index]);
    }

    ReferenceRgbImage referenceRgb;
    ReferenceRgbImage candidateRgb;
    if (!FixedReferenceDemosaic(
            request.latentMosaic,
            latentEvidence,
            request.domain,
            referenceRgb,
            error) ||
        !FixedReferenceDemosaic(
            request.candidateMosaic,
            request.candidateEvidence,
            request.domain,
            candidateRgb,
            error)) {
        return false;
    }
    std::array<ErrorAccumulator, 3> rgbByChannel;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (std::size_t channel = 0; channel < 3u; ++channel) {
            const std::uint8_t mask = ChannelMask(channel);
            if ((referenceRgb.validChannelMask[pixel] & mask) == 0u ||
                (candidateRgb.validChannelMask[pixel] & mask) == 0u) {
                continue;
            }
            rgbByChannel[channel].Add(
                candidateRgb.linearRgb[RgbIndex(pixel, channel)],
                referenceRgb.linearRgb[RgbIndex(pixel, channel)]);
        }
    }

    for (std::size_t phase = 0; phase < 4u; ++phase) {
        metrics.mosaicByPhase[phase] = mosaicByPhase[phase].Finish();
    }
    metrics.mosaicAll = mosaicAll.Finish();
    for (std::size_t channel = 0; channel < 3u; ++channel) {
        metrics.referenceRgbByChannel[channel] = rgbByChannel[channel].Finish();
    }
    ComputeDarkArtifacts(request, referenceRgb, candidateRgb, metrics.darkArtifacts);
    result = std::move(metrics);
    return true;
}

} // namespace Raw::MultiFrame
