#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrame/LegacyAdapters.h"
#include "Raw/MultiFrame/PreparedTileEstimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

struct SyntheticPreparedSource {
    Raw::MultiFrame::PreparedMeasurementSource prepared;
    Raw::MultiFrame::NoiseModelReference noise;
    std::shared_ptr<std::vector<Raw::MultiFrame::PreparedMeasurementTile>>
        tiles;
    std::shared_ptr<Raw::MultiFrame::TileReadStatus> forcedReadStatus;
};

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Unified MultiFrame prepared-tile validation failed: "
                  << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double left, double right, double tolerance = 1.0e-7) {
    return std::abs(left - right) <= tolerance;
}

std::string Hash(char digit) {
    return std::string(64u, digit);
}

std::vector<Raw::MultiFrame::PreparedMeasurementTile> SplitTiles(
    std::uint64_t width,
    std::uint64_t height,
    std::uint32_t tileWidth,
    std::uint32_t tileHeight,
    const std::vector<float>& values,
    const std::vector<float>& gains,
    const std::vector<Raw::MultiFrame::SampleEvidence>& evidence) {
    std::vector<Raw::MultiFrame::PreparedMeasurementTile> tiles;
    for (std::uint64_t originY = 0; originY < height;
         originY += tileHeight) {
        for (std::uint64_t originX = 0; originX < width;
             originX += tileWidth) {
            Raw::MultiFrame::PreparedMeasurementTile tile;
            tile.tileX = static_cast<std::uint32_t>(originX / tileWidth);
            tile.tileY = static_cast<std::uint32_t>(originY / tileHeight);
            tile.originX = originX;
            tile.originY = originY;
            tile.extent.width = std::min<std::uint64_t>(
                tileWidth, width - originX);
            tile.extent.height = std::min<std::uint64_t>(
                tileHeight, height - originY);
            const std::size_t count = static_cast<std::size_t>(
                tile.extent.width * tile.extent.height);
            tile.normalizedMosaic.reserve(count);
            tile.comparisonGain.reserve(count);
            tile.sampleEvidence.reserve(count);
            for (std::uint64_t localY = 0;
                 localY < tile.extent.height;
                 ++localY) {
                for (std::uint64_t localX = 0;
                     localX < tile.extent.width;
                     ++localX) {
                    const std::size_t source = static_cast<std::size_t>(
                        (originY + localY) * width + originX + localX);
                    tile.normalizedMosaic.push_back(values[source]);
                    tile.comparisonGain.push_back(gains[source]);
                    tile.sampleEvidence.push_back(evidence[source]);
                }
            }
            tiles.push_back(std::move(tile));
        }
    }
    return tiles;
}

SyntheticPreparedSource MakeSource(
    Raw::MultiFrame::LegacyProcessorKind producer,
    std::size_t sourceIndex,
    const Raw::MultiFrame::CfaDomainIdentity& domain,
    std::uint32_t tileWidth,
    std::uint32_t tileHeight,
    std::vector<Raw::MultiFrame::PreparedMeasurementTile> tiles,
    double shotScale = 0.0,
    double offsetVariance = 0.01) {
    static constexpr std::array<char, 16> digits {
        '1', '2', '3', '4', '5', '6', '7', '8',
        '9', 'a', 'b', 'c', 'd', 'e', 'f', '0'
    };
    const char identityDigit = digits[sourceIndex % digits.size()];
    const char cacheDigit = digits[(sourceIndex + 5u) % digits.size()];
    const char noiseDigit = digits[(sourceIndex + 10u) % digits.size()];
    const auto legacy =
        Raw::MultiFrame::CaptureLegacyProcessorIdentity(producer);

    SyntheticPreparedSource result;
    result.tiles = std::make_shared<
        std::vector<Raw::MultiFrame::PreparedMeasurementTile>>(
            std::move(tiles));
    result.forcedReadStatus =
        std::make_shared<Raw::MultiFrame::TileReadStatus>(
            Raw::MultiFrame::TileReadStatus::Hit);
    result.prepared.source.stableFrameId =
        "prepared-tile-frame-" + std::to_string(sourceIndex);
    result.prepared.source.contentSha256 = Hash(identityDigit);
    result.prepared.source.byteLength = 4096u + sourceIndex;
    result.prepared.domain = domain;
    result.prepared.tileRawPixels = std::max(tileWidth, tileHeight);
    result.prepared.tileColumns = static_cast<std::uint32_t>(
        (domain.activeExtent.width + tileWidth - 1u) / tileWidth);
    result.prepared.tileRows = static_cast<std::uint32_t>(
        (domain.activeExtent.height + tileHeight - 1u) / tileHeight);
    result.prepared.provenance.producerAlgorithmId = legacy.algorithmId;
    result.prepared.provenance.producerAlgorithmVersion =
        legacy.algorithmVersion;
    result.prepared.provenance.producerProcessorContractId =
        legacy.processorContractId;
    result.prepared.provenance.producerProcessorContractVersion =
        legacy.processorContractVersion;
    result.prepared.provenance.preparationContractId =
        legacy.preparationContractId;
    result.prepared.provenance.preparationContractVersion =
        legacy.preparationContractVersion;
    result.prepared.provenance.preparedCacheKey = Hash(cacheDigit);
    result.prepared.tiles.identity =
        "synthetic-prepared-tiles-" + std::to_string(sourceIndex);
    const auto storage = result.tiles;
    const auto forcedStatus = result.forcedReadStatus;
    result.prepared.tiles.readTile =
        [storage, forcedStatus](
            std::uint32_t tileX,
            std::uint32_t tileY,
            Raw::MultiFrame::PreparedMeasurementTile& tile,
            std::string* error) {
            if (*forcedStatus != Raw::MultiFrame::TileReadStatus::Hit) {
                if (error) *error = "Synthetic prepared-tile read fault.";
                return *forcedStatus;
            }
            for (const auto& candidate : *storage) {
                if (candidate.tileX == tileX && candidate.tileY == tileY) {
                    tile = candidate;
                    if (error) error->clear();
                    return Raw::MultiFrame::TileReadStatus::Hit;
                }
            }
            if (error) *error = "Synthetic prepared tile is missing.";
            return Raw::MultiFrame::TileReadStatus::Missing;
        };

    result.noise.sourceContractId = legacy.noiseModelContractId;
    result.noise.sourceContractVersion = legacy.noiseModelContractVersion;
    result.noise.preparedFrameCacheKey = Hash(cacheDigit);
    result.noise.identitySha256 = Hash(noiseDigit);
    result.noise.quality = Raw::MultiFrame::NoiseModelQuality::TrustedMetadata;
    result.noise.sourceDescription = "synthetic prepared-tile noise";
    result.noise.sourceRecordId =
        "synthetic-noise-" + std::to_string(sourceIndex);
    for (auto& site : result.noise.sites) {
        site.shotScale = shotScale;
        site.offsetVariance = offsetVariance;
        site.quantizationVariance = offsetVariance * 0.1;
        site.residualModelTau0 = 0.0;
        site.residualModelTau1 = 0.0;
        site.quantizationIncluded = true;
    }
    return result;
}

Raw::MultiFrame::PreparedTileEstimatorConfiguration Configuration(
    Raw::MultiFrame::PreparedTileEstimatorProfile profile) {
    Raw::MultiFrame::PreparedTileEstimatorConfiguration configuration;
    configuration.profile = profile;
    configuration.modelIdentity = "synthetic-prepared-tile-noise-v1";
    configuration.geometryIdentity = "synthetic-identity-geometry-v1";
    configuration.pilotIdentity = "synthetic-shared-pilot-v1";
    configuration.huberThreshold = 1.5;
    return configuration;
}

Raw::MultiFrame::PreparedTileEstimatorRequest RequestFor(
    Raw::MultiFrame::PreparedTileEstimatorProfile profile,
    const std::vector<SyntheticPreparedSource>& sources,
    const std::vector<double>& exposures,
    std::uint32_t tileX,
    std::uint32_t tileY,
    std::vector<double> pilot) {
    Raw::MultiFrame::PreparedTileEstimatorRequest request;
    request.configuration = Configuration(profile);
    request.outputReferenceSourceIndex = 0u;
    request.tileX = tileX;
    request.tileY = tileY;
    request.sharedPilotVirtualBayer = std::move(pilot);
    request.sources.reserve(sources.size());
    for (std::size_t index = 0; index < sources.size(); ++index) {
        Raw::MultiFrame::PreparedTileEstimatorSource source;
        source.prepared = &sources[index].prepared;
        source.noise = &sources[index].noise;
        source.exposureScale = exposures[index];
        source.sourceOrdinal = index;
        request.sources.push_back(source);
    }
    return request;
}

std::vector<double> ExtractPilot(
    const std::vector<double>& full,
    std::uint64_t fullWidth,
    const Raw::MultiFrame::PreparedMeasurementTile& tile) {
    std::vector<double> result;
    result.reserve(static_cast<std::size_t>(
        tile.extent.width * tile.extent.height));
    for (std::uint64_t localY = 0; localY < tile.extent.height; ++localY) {
        for (std::uint64_t localX = 0; localX < tile.extent.width; ++localX) {
            result.push_back(full[static_cast<std::size_t>(
                (tile.originY + localY) * fullWidth +
                tile.originX + localX)]);
        }
    }
    return result;
}

void Stitch(
    const Raw::MultiFrame::PreparedTileEstimatorResult& tile,
    std::uint64_t fullWidth,
    std::vector<double>& mosaic,
    std::vector<std::uint64_t>* owner = nullptr,
    std::vector<double>* ownerFraction = nullptr) {
    for (std::uint64_t localY = 0; localY < tile.extent.height; ++localY) {
        for (std::uint64_t localX = 0; localX < tile.extent.width; ++localX) {
            const std::size_t source = static_cast<std::size_t>(
                localY * tile.extent.width + localX);
            const std::size_t destination = static_cast<std::size_t>(
                (tile.originY + localY) * fullWidth +
                tile.originX + localX);
            mosaic[destination] = tile.virtualBayer[source];
            if (owner) (*owner)[destination] =
                tile.ownerSourceOrdinal[source];
            if (ownerFraction) (*ownerFraction)[destination] =
                tile.ownerWeightFraction[source];
        }
    }
}

bool MakeDomain(
    Raw::CfaPattern pattern,
    std::uint64_t width,
    std::uint64_t height,
    Raw::MultiFrame::CfaDomainIdentity& domain) {
    return Raw::MultiFrame::TryCreateCfaDomainIdentity(
        pattern,
        { 0, 0, static_cast<int>(height), static_cast<int>(width) },
        { width, height },
        domain,
        nullptr);
}

bool ValidateEqualExposureMosaicAndTileParity() {
    constexpr std::uint64_t width = 16u;
    constexpr std::uint64_t height = 12u;
    constexpr std::size_t frameCount = 4u;
    const std::array<Raw::CfaPattern, 4> patterns {
        Raw::CfaPattern::RGGB,
        Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GBRG,
        Raw::CfaPattern::GRBG
    };
    const std::array<double, frameCount> frameError {
        -0.015625, 0.015625, -0.0078125, 0.0078125
    };
    const std::vector<double> exposures(frameCount, 1.0);
    bool ok = true;

    for (const Raw::CfaPattern pattern : patterns) {
        Raw::MultiFrame::CfaDomainIdentity domain;
        ok &= Check(MakeDomain(pattern, width, height, domain),
            "equal-exposure CFA domain could not be created");
        std::vector<double> truth(width * height);
        for (std::uint64_t y = 0; y < height; ++y) {
            for (std::uint64_t x = 0; x < width; ++x) {
                const auto phase = Raw::MultiFrame::PhaseAt(domain, x, y);
                static constexpr std::array<double, 4> phaseSignal {
                    -0.125, 0.25, 0.75, 1.25
                };
                truth[static_cast<std::size_t>(y * width + x)] =
                    phaseSignal[static_cast<std::size_t>(phase)] +
                    static_cast<double>((x + 3u * y) % 7u) / 1024.0;
            }
        }

        std::vector<SyntheticPreparedSource> fullSources;
        std::vector<SyntheticPreparedSource> tiledSources;
        fullSources.reserve(frameCount);
        tiledSources.reserve(frameCount);
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            std::vector<float> values(width * height);
            std::vector<float> gains(width * height, 1.0f);
            std::vector<Raw::MultiFrame::SampleEvidence> evidence(
                width * height);
            for (std::size_t pixel = 0; pixel < values.size(); ++pixel) {
                values[pixel] = static_cast<float>(
                    truth[pixel] + frameError[frame]);
            }
            fullSources.push_back(MakeSource(
                Raw::MultiFrame::LegacyProcessorKind::MultiFrameDenoise,
                frame,
                domain,
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                SplitTiles(width, height,
                    static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height),
                    values, gains, evidence)));
            tiledSources.push_back(MakeSource(
                Raw::MultiFrame::LegacyProcessorKind::MultiFrameDenoise,
                frame,
                domain,
                8u,
                6u,
                SplitTiles(width, height, 8u, 6u,
                    values, gains, evidence)));
        }

        for (const auto profile : {
                 Raw::MultiFrame::PreparedTileEstimatorProfile::FixedGaussian,
                 Raw::MultiFrame::PreparedTileEstimatorProfile::SharedPilotHuber }) {
            Raw::MultiFrame::PreparedTileEstimatorResult full;
            std::string error;
            ok &= Check(
                Raw::MultiFrame::EstimatePreparedMeasurementTile(
                    RequestFor(profile, fullSources, exposures,
                        0u, 0u, truth),
                    full,
                    &error),
                "full equal-exposure prepared mosaic failed: " + error);
            std::vector<double> stitched(width * height,
                std::numeric_limits<double>::quiet_NaN());
            for (std::uint32_t tileY = 0; tileY < 2u; ++tileY) {
                for (std::uint32_t tileX = 0; tileX < 2u; ++tileX) {
                    const auto& sourceTile =
                        (*tiledSources[0].tiles)[tileY * 2u + tileX];
                    Raw::MultiFrame::PreparedTileEstimatorResult tile;
                    ok &= Check(
                        Raw::MultiFrame::EstimatePreparedMeasurementTile(
                            RequestFor(profile, tiledSources, exposures,
                                tileX, tileY,
                                ExtractPilot(truth, width, sourceTile)),
                            tile,
                            &error),
                        "tiled equal-exposure prepared mosaic failed: " + error);
                    Stitch(tile, width, stitched);
                }
            }
            for (std::size_t pixel = 0; pixel < truth.size(); ++pixel) {
                ok &= Check(
                    NearlyEqual(full.virtualBayer[pixel], truth[pixel]) &&
                        full.virtualBayer[pixel] == stitched[pixel] &&
                        std::isfinite(full.variance[pixel]) &&
                        full.effectiveSupport[pixel] > 3.99,
                    "equal-exposure full/tiled parity, signed signal, or support failed");
            }
            ok &= Check(
                full.estimatedPixelCount == width * height &&
                    full.unresolvedPixelCount == 0u &&
                    full.boundedOnlyPixelCount == 0u &&
                    (profile != Raw::MultiFrame::PreparedTileEstimatorProfile::SharedPilotHuber ||
                     (full.allHuberPixelsConverged &&
                      full.robustAttenuatedSampleCount == 0u)),
                "equal-exposure prepared mosaic diagnostics are inconsistent");
        }
    }
    return ok;
}

bool ValidateBracketOwnershipAndClippedBlackPayload() {
    constexpr std::uint64_t width = 30u;
    constexpr std::uint64_t height = 16u;
    const std::vector<double> exposures { 0.25, 1.0, 4.0 };
    Raw::MultiFrame::CfaDomainIdentity domain;
    bool ok = Check(MakeDomain(Raw::CfaPattern::RGGB, width, height, domain),
        "bracket CFA domain could not be created");
    std::vector<double> truth(width * height);
    for (std::uint64_t y = 0; y < height; ++y) {
        for (std::uint64_t x = 0; x < width; ++x) {
            truth[static_cast<std::size_t>(y * width + x)] =
                x < 10u ? 0.02 : (x < 20u ? 0.30 : 1.50);
        }
    }

    std::vector<SyntheticPreparedSource> sources;
    sources.reserve(exposures.size());
    for (std::size_t frame = 0; frame < exposures.size(); ++frame) {
        std::vector<float> values(width * height);
        std::vector<float> gains(width * height, 1.0f);
        std::vector<Raw::MultiFrame::SampleEvidence> evidence(width * height);
        for (std::size_t pixel = 0; pixel < values.size(); ++pixel) {
            const double prepared = exposures[frame] * truth[pixel];
            if (prepared >= 0.95) {
                // Deliberately black payload: its categorical state must keep
                // it out of equality fusion and prevent a black-spot result.
                values[pixel] = 0.0f;
                evidence[pixel].state =
                    Raw::MultiFrame::SampleEvidenceState::UpperCensored;
                evidence[pixel].causes =
                    Raw::MultiFrame::SampleEvidenceCauseMask(
                        Raw::MultiFrame::SampleEvidenceCause::SensorSaturation);
                evidence[pixel].bounds.lowerInclusive = 0.95;
            } else {
                values[pixel] = static_cast<float>(prepared);
            }
        }
        sources.push_back(MakeSource(
            Raw::MultiFrame::LegacyProcessorKind::RawBurstHdr,
            frame,
            domain,
            10u,
            8u,
            SplitTiles(width, height, 10u, 8u,
                values, gains, evidence),
            0.0,
            0.001));
    }

    for (const auto profile : {
             Raw::MultiFrame::PreparedTileEstimatorProfile::FixedGaussian,
             Raw::MultiFrame::PreparedTileEstimatorProfile::SharedPilotHuber }) {
        std::vector<double> mosaic(width * height,
            std::numeric_limits<double>::quiet_NaN());
        std::vector<std::uint64_t> owner(width * height,
            std::numeric_limits<std::uint64_t>::max());
        std::vector<double> ownerFraction(width * height, 0.0);
        std::string error;
        for (std::uint32_t tileY = 0; tileY < 2u; ++tileY) {
            for (std::uint32_t tileX = 0; tileX < 3u; ++tileX) {
                const auto& sourceTile =
                    (*sources[0].tiles)[tileY * 3u + tileX];
                Raw::MultiFrame::PreparedTileEstimatorResult tile;
                ok &= Check(
                    Raw::MultiFrame::EstimatePreparedMeasurementTile(
                        RequestFor(profile, sources, exposures,
                            tileX, tileY,
                            ExtractPilot(truth, width, sourceTile)),
                        tile,
                        &error),
                    "prepared bracket tile failed: " + error);
                ok &= Check(
                    tile.unresolvedPixelCount == 0u &&
                        tile.boundedOnlyPixelCount == 0u,
                    "prepared bracket produced unsupported pixels");
                Stitch(tile, width, mosaic, &owner, &ownerFraction);
            }
        }

        for (std::uint64_t y = 0; y < height; ++y) {
            const std::size_t shadow = static_cast<std::size_t>(y * width + 5u);
            const std::size_t midtone = static_cast<std::size_t>(y * width + 15u);
            const std::size_t highlight = static_cast<std::size_t>(y * width + 25u);
            ok &= Check(
                NearlyEqual(mosaic[shadow], truth[shadow]) &&
                    owner[shadow] == 2u && ownerFraction[shadow] > 0.90,
                "long exposure did not naturally own the static shadow");
            ok &= Check(
                NearlyEqual(mosaic[midtone], truth[midtone]) &&
                    owner[midtone] == 1u && ownerFraction[midtone] > 0.90,
                "middle exposure did not own the post-clipping midtone");
            ok &= Check(
                NearlyEqual(mosaic[highlight], truth[highlight]) &&
                    mosaic[highlight] > 1.0 &&
                    owner[highlight] == 0u &&
                    NearlyEqual(ownerFraction[highlight], 1.0),
                "black clipped payload contaminated highlight handoff");
        }
    }
    return ok;
}

bool ValidateReadFailurePreservesVerifiedResult() {
    constexpr std::uint64_t width = 4u;
    constexpr std::uint64_t height = 4u;
    Raw::MultiFrame::CfaDomainIdentity domain;
    bool ok = Check(MakeDomain(Raw::CfaPattern::BGGR, width, height, domain),
        "fault fixture CFA domain could not be created");
    std::vector<double> truth(width * height, 0.25);
    std::vector<SyntheticPreparedSource> sources;
    sources.reserve(2u);
    for (std::size_t frame = 0; frame < 2u; ++frame) {
        std::vector<float> values(width * height,
            static_cast<float>(truth[0]));
        std::vector<float> gains(width * height, 1.0f);
        std::vector<Raw::MultiFrame::SampleEvidence> evidence(width * height);
        sources.push_back(MakeSource(
            Raw::MultiFrame::LegacyProcessorKind::MultiFrameDenoise,
            frame,
            domain,
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            SplitTiles(width, height,
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                values, gains, evidence)));
    }
    const std::vector<double> exposures { 1.0, 1.0 };
    auto request = RequestFor(
        Raw::MultiFrame::PreparedTileEstimatorProfile::FixedGaussian,
        sources, exposures, 0u, 0u, truth);
    Raw::MultiFrame::PreparedTileEstimatorResult result;
    std::string error;
    ok &= Check(
        Raw::MultiFrame::EstimatePreparedMeasurementTile(
            request, result, &error),
        "verified prepared-tile baseline failed: " + error);
    const auto verifiedMosaic = result.virtualBayer;
    const auto verifiedStates = result.state;
    *sources[1].forcedReadStatus = Raw::MultiFrame::TileReadStatus::Corrupt;
    ok &= Check(
        !Raw::MultiFrame::EstimatePreparedMeasurementTile(
            request, result, &error) &&
            result.virtualBayer == verifiedMosaic &&
            result.state == verifiedStates,
        "corrupt prepared tile partially replaced a verified result");
    return ok;
}

} // namespace

bool ValidateUnifiedMultiFramePreparedTiles() {
    bool ok = true;
    ok &= Check(
        Raw::MultiFrame::kPreparedTileEstimatorVersion == 1u &&
            std::string(Raw::MultiFrame::kPreparedTileEstimatorId) ==
                "stack-multiframe-prepared-tile-estimator-v1",
        "prepared-tile estimator identity changed without review");
    ok &= ValidateEqualExposureMosaicAndTileParity();
    ok &= ValidateBracketOwnershipAndClippedBlackPayload();
    ok &= ValidateReadFailurePreservesVerifiedResult();
    if (ok) {
        std::cout
            << "Unified MultiFrame prepared-tile and complete-mosaic "
               "validation passed."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
