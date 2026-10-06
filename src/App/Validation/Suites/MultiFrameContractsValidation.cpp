#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrame/LegacyAdapters.h"
#include "Raw/MultiFrameDenoise/Processor.h"
#include "Raw/MultiFrameHdr/Processor.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Unified MultiFrame contract validation failed: "
                  << message << std::endl;
    }
    return condition;
}

bool EqualFloatBits(
    const std::vector<float>& left,
    const std::vector<float>& right) {
    return left.size() == right.size() &&
        (left.empty() || std::memcmp(
            left.data(), right.data(), left.size() * sizeof(float)) == 0);
}

Raw::MultiFrame::CfaPhase AdaptSite(Raw::Mfd::CfaSite site) {
    switch (site) {
        case Raw::Mfd::CfaSite::Red: return Raw::MultiFrame::CfaPhase::R;
        case Raw::Mfd::CfaSite::Green0: return Raw::MultiFrame::CfaPhase::G0;
        case Raw::Mfd::CfaSite::Green1: return Raw::MultiFrame::CfaPhase::G1;
        case Raw::Mfd::CfaSite::Blue: return Raw::MultiFrame::CfaPhase::B;
        default: return Raw::MultiFrame::CfaPhase::R;
    }
}

bool ValidateFrozenLegacyIdentities() {
    using Raw::MultiFrame::CaptureLegacyProcessorIdentity;
    using Raw::MultiFrame::LegacyProcessorKind;

    const Raw::MultiFrame::LegacyProcessorIdentity mfd =
        CaptureLegacyProcessorIdentity(LegacyProcessorKind::MultiFrameDenoise);
    const Raw::MultiFrame::LegacyProcessorIdentity hdr =
        CaptureLegacyProcessorIdentity(LegacyProcessorKind::RawBurstHdr);

    bool ok = true;
    ok &= Check(mfd.algorithmId == "ra-cfa", "MFD algorithm ID drifted");
    ok &= Check(mfd.algorithmVersion == 1u, "MFD algorithm version drifted");
    ok &= Check(
        mfd.processorContractId == "ra-cfa-offline-processor-v1" &&
            mfd.processorContractVersion == 1u,
        "MFD processor contract drifted");
    ok &= Check(
        hdr.algorithmId == "tripod-cfa-hdr" && hdr.algorithmVersion == 4u,
        "HDR algorithm identity drifted");
    ok &= Check(
        hdr.processorContractId == "tripod-cfa-hdr-processor-v2" &&
            hdr.processorContractVersion == 2u,
        "HDR processor contract drifted");
    for (const Raw::MultiFrame::LegacyProcessorIdentity* identity : { &mfd, &hdr }) {
        ok &= Check(
            identity->preparationContractId == "ra-cfa-raw-preparation-v1" &&
                identity->preparationContractVersion == 1u,
            "legacy shared preparation identity drifted");
        ok &= Check(
            identity->noiseModelContractId ==
                    "ra-cfa-poisson-gaussian-noise-v1" &&
                identity->noiseModelContractVersion == 1u,
            "legacy shared noise identity drifted");
    }
    return ok;
}

bool ValidateNeutralCfaDomain() {
    const std::array<Raw::CfaPattern, 4> patterns {
        Raw::CfaPattern::RGGB,
        Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GBRG,
        Raw::CfaPattern::GRBG
    };
    bool ok = true;
    for (const Raw::CfaPattern pattern : patterns) {
        Raw::MultiFrame::CfaDomainIdentity domain;
        std::string error;
        ok &= Check(
            Raw::MultiFrame::TryCreateCfaDomainIdentity(
                pattern,
                { 7, 11, 13, 19 },
                { 8u, 6u },
                domain,
                &error),
            "neutral CFA domain creation failed: " + error);
        Raw::Mfd::CfaLayout legacy;
        ok &= Check(
            Raw::Mfd::CfaLayout::TryCreate(pattern, legacy),
            "legacy CFA layout creation failed");
        for (std::uint64_t y = 0; y < 6u; ++y) {
            for (std::uint64_t x = 0; x < 8u; ++x) {
                ok &= Check(
                    Raw::MultiFrame::PhaseAt(domain, x, y) == AdaptSite(
                        legacy.SiteAt(
                            static_cast<std::int64_t>(x),
                            static_cast<std::int64_t>(y))),
                    "neutral and legacy CFA phase identities disagree");
            }
        }
    }

    Raw::MultiFrame::CfaDomainIdentity invalid;
    std::string error;
    ok &= Check(
        !Raw::MultiFrame::TryCreateCfaDomainIdentity(
            Raw::CfaPattern::Unknown,
            { 0, 0, 2, 2 },
            { 2u, 2u },
            invalid,
            &error),
        "unknown CFA domain was accepted");
    return ok;
}

bool ValidateEvidenceSemantics() {
    using Raw::Mfd::PreparedSampleFlag;
    using Raw::MultiFrame::AdaptLegacySampleEvidence;
    using Raw::MultiFrame::HasSampleEvidenceCause;
    using Raw::MultiFrame::IsNumericMeasurement;
    using Raw::MultiFrame::SampleEvidenceCause;
    using Raw::MultiFrame::SampleEvidenceState;

    bool ok = true;
    const Raw::MultiFrame::SampleEvidence exact = AdaptLegacySampleEvidence(0u);
    ok &= Check(
        exact.state == SampleEvidenceState::ExactMeasurement &&
            IsNumericMeasurement(exact),
        "unflagged legacy sample did not remain an exact measurement");

    const Raw::MultiFrame::SampleEvidence repaired = AdaptLegacySampleEvidence(
        Raw::Mfd::SampleFlagMask(PreparedSampleFlag::DecoderRepaired));
    ok &= Check(
        repaired.state == SampleEvidenceState::RepairedMeasurement &&
            IsNumericMeasurement(repaired) &&
            HasSampleEvidenceCause(repaired.causes, SampleEvidenceCause::DecoderRepair),
        "decoder repair provenance was lost");

    const std::uint8_t clippedFlags = static_cast<std::uint8_t>(
        Raw::Mfd::SampleFlagMask(PreparedSampleFlag::Saturated) |
        Raw::Mfd::SampleFlagMask(PreparedSampleFlag::ExplicitDecoderClip));
    const Raw::MultiFrame::SampleEvidence clipped =
        AdaptLegacySampleEvidence(clippedFlags);
    ok &= Check(
        clipped.state == SampleEvidenceState::UpperCensored &&
            !IsNumericMeasurement(clipped) &&
            HasSampleEvidenceCause(clipped.causes, SampleEvidenceCause::SensorSaturation) &&
            HasSampleEvidenceCause(clipped.causes, SampleEvidenceCause::ExplicitDecoderClip),
        "clipped legacy sample became a numeric measurement");

    const std::uint8_t defectiveFlags = static_cast<std::uint8_t>(
        clippedFlags | Raw::Mfd::SampleFlagMask(PreparedSampleFlag::Defective));
    const Raw::MultiFrame::SampleEvidence defective =
        AdaptLegacySampleEvidence(defectiveFlags);
    ok &= Check(
        defective.state == SampleEvidenceState::Defective &&
            !IsNumericMeasurement(defective) &&
            HasSampleEvidenceCause(defective.causes, SampleEvidenceCause::KnownDefect),
        "known defect did not retain categorical precedence");

    Raw::MultiFrame::RadiometricBounds invalidBounds;
    invalidBounds.lowerInclusive = 0.8;
    invalidBounds.upperInclusive = 0.2;
    std::string error;
    ok &= Check(
        !Raw::MultiFrame::ValidateRadiometricBounds(invalidBounds, &error),
        "reversed radiometric bounds were accepted");

    Raw::MultiFrame::SampleEvidence invalidZero;
    invalidZero.state = SampleEvidenceState::DecoderInvalid;
    ok &= Check(
        !IsNumericMeasurement(invalidZero),
        "invalid sample could be interpreted as a numeric zero");
    return ok;
}

Raw::Mfd::PreparedRawFrame MakePreparedFrame() {
    Raw::Mfd::PreparedRawFrame frame;
    frame.cacheKey = std::string(64u, 'a');
    frame.sourceContentSha256 = std::string(64u, 'b');
    frame.sourceByteSize = 4096u;
    frame.sensorActiveArea = { 7, 11, 9, 15 };
    frame.activeExtent = { 4u, 2u };
    frame.activeCfaPattern = Raw::CfaPattern::RGGB;
    frame.tileRawPixels = 2u;
    frame.tileColumns = 2u;
    frame.tileRows = 1u;
    return frame;
}

Raw::Mfd::PreparedRawTile MakePreparedTile() {
    Raw::Mfd::PreparedRawTile tile;
    tile.tileX = 0u;
    tile.tileY = 0u;
    tile.originX = 0u;
    tile.originY = 0u;
    tile.extent = { 2u, 2u };
    tile.normalizedMosaic = { -0.25f, 1.25f, 0.0f, 0.5f };
    tile.comparisonGain = { 1.0f, 2.0f, 3.0f, 4.0f };
    tile.sampleFlags = {
        0u,
        static_cast<std::uint8_t>(
            Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::Saturated) |
            Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::ExplicitDecoderClip)),
        Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::Defective),
        Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::DecoderRepaired)
    };
    return tile;
}

bool ValidatePreparedSourceAdapters() {
    const Raw::Mfd::PreparedRawFrame frame = MakePreparedFrame();
    const Raw::Mfd::PreparedRawTile legacyTile = MakePreparedTile();
    auto cache = std::make_shared<Raw::Mfd::MemoryNormalizedTileCache>();
    std::string error;
    bool ok = Check(
        cache->Write(frame.cacheKey, legacyTile, &error),
        "legacy fixture tile could not be cached: " + error);

    Raw::MultiFrame::PreparedMeasurementSource mfdSource;
    Raw::MultiFrame::PreparedMeasurementSource hdrSource;
    ok &= Check(
        Raw::MultiFrame::AdaptLegacyPreparedFrame(
            Raw::MultiFrame::LegacyProcessorKind::MultiFrameDenoise,
            "frame-mfd",
            frame,
            cache,
            mfdSource,
            &error),
        "MFD prepared source adapter failed: " + error);
    ok &= Check(
        Raw::MultiFrame::AdaptLegacyPreparedFrame(
            Raw::MultiFrame::LegacyProcessorKind::RawBurstHdr,
            "frame-hdr",
            frame,
            cache,
            hdrSource,
            &error),
        "HDR prepared source adapter failed: " + error);
    ok &= Check(
        mfdSource.provenance.producerAlgorithmId == "ra-cfa" &&
            hdrSource.provenance.producerAlgorithmId == "tripod-cfa-hdr" &&
            !mfdSource.provenance.numericalValuesReinterpreted &&
            !hdrSource.provenance.numericalValuesReinterpreted,
        "prepared-source producer provenance is wrong");

    for (const Raw::MultiFrame::PreparedMeasurementSource* source : {
             &mfdSource, &hdrSource }) {
        Raw::MultiFrame::PreparedMeasurementTile adaptedTile;
        error.clear();
        ok &= Check(
            source->tiles.Read(0u, 0u, adaptedTile, &error) ==
                Raw::MultiFrame::TileReadStatus::Hit,
            "adapted prepared tile could not be reloaded: " + error);
        ok &= Check(
            EqualFloatBits(
                adaptedTile.normalizedMosaic,
                legacyTile.normalizedMosaic) &&
                EqualFloatBits(adaptedTile.comparisonGain, legacyTile.comparisonGain),
            "legacy adapter changed normalized samples or comparison gains");
        ok &= Check(
            adaptedTile.normalizedMosaic.front() < 0.0f &&
                adaptedTile.normalizedMosaic[1] > 1.0f,
            "legacy adapter lost signed or positive-overrange samples");
        ok &= Check(
            adaptedTile.sampleEvidence[0].state ==
                    Raw::MultiFrame::SampleEvidenceState::ExactMeasurement &&
                adaptedTile.sampleEvidence[1].state ==
                    Raw::MultiFrame::SampleEvidenceState::UpperCensored &&
                adaptedTile.sampleEvidence[2].state ==
                    Raw::MultiFrame::SampleEvidenceState::Defective &&
                adaptedTile.sampleEvidence[3].state ==
                    Raw::MultiFrame::SampleEvidenceState::RepairedMeasurement,
            "legacy sample flags did not map to categorical evidence");

        Raw::MultiFrame::PreparedMeasurementTile missing;
        error.clear();
        ok &= Check(
            source->tiles.Read(1u, 0u, missing, &error) ==
                Raw::MultiFrame::TileReadStatus::Missing,
            "reloadable source handle did not preserve a cache miss");
    }
    return ok;
}

bool ValidateNoiseAdapter() {
    Raw::Mfd::NoiseModel legacy;
    legacy.preparedFrameCacheKey = std::string(64u, 'c');
    legacy.identitySha256 = std::string(64u, 'd');
    legacy.quality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    legacy.diagnostics.quality = legacy.quality;
    legacy.diagnostics.resolved = true;
    legacy.diagnostics.referenceOnly = false;
    legacy.diagnostics.source = "validation-profile";
    legacy.diagnostics.sourceRecordId = "record-1";
    for (std::size_t index = 0; index < legacy.sites.size(); ++index) {
        Raw::Mfd::SiteNoiseProfile& site = legacy.sites[index];
        site.shotScale = 0.001 * static_cast<double>(index + 1u);
        site.offsetVariance = 0.00001 * static_cast<double>(index + 1u);
        site.quantizationVariance = 0.000001;
        site.residualModelTau0 = 0.000002;
        site.residualModelTau1 = 0.000003;
        site.quantizationIncluded = true;
    }

    Raw::MultiFrame::NoiseModelReference adapted;
    std::string error;
    bool ok = Check(
        Raw::MultiFrame::AdaptLegacyNoiseModel(legacy, adapted, &error),
        "legacy noise adapter failed: " + error);
    ok &= Check(
        adapted.sourceContractId == Raw::Mfd::kNoiseModelContractId &&
            adapted.sourceContractVersion == Raw::Mfd::kNoiseModelContractVersion &&
            adapted.quality == Raw::MultiFrame::NoiseModelQuality::TrustedMetadata &&
            !adapted.numericalValuesReinterpreted,
        "legacy noise provenance changed");
    for (std::size_t index = 0; index < legacy.sites.size(); ++index) {
        ok &= Check(
            adapted.sites[index].shotScale == legacy.sites[index].shotScale &&
                adapted.sites[index].offsetVariance ==
                    legacy.sites[index].offsetVariance &&
                adapted.sites[index].quantizationVariance ==
                    legacy.sites[index].quantizationVariance &&
                adapted.sites[index].residualModelTau0 ==
                    legacy.sites[index].residualModelTau0 &&
                adapted.sites[index].residualModelTau1 ==
                    legacy.sites[index].residualModelTau1,
            "legacy noise coefficients changed during adaptation");
    }
    return ok;
}

Raw::RawImageData MakeLegacyProcessorFixtureRaw(
    Raw::CfaPattern pattern,
    char identity) {
    constexpr int width = 6;
    constexpr int height = 4;
    Raw::RawImageData raw;
    raw.metadata.sourceContentSha256 = std::string(64u, identity);
    raw.metadata.sourceByteSize =
        static_cast<std::uint64_t>(width * height * sizeof(std::uint16_t));
    raw.metadata.sourcePath = std::string("legacy-") + identity + ".dng";
    raw.metadata.cameraMake = "Stack Validation";
    raw.metadata.cameraModel = "Legacy Adapter Fixture";
    raw.metadata.dngUniqueCameraModel = "Stack Legacy Adapter Fixture";
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.bitDepth = 16;
    raw.metadata.cfaPattern = pattern;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.mosaiced = true;
    raw.metadata.isDng = true;
    raw.metadata.blackLevel = 64.0f;
    raw.metadata.perChannelBlack.fill(64.0f);
    raw.metadata.whiteLevel = 4095.0f;
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = {
        { 2.0e-4, 1.0e-6 },
        { 2.1e-4, 1.1e-6 },
        { 2.2e-4, 1.2e-6 }
    };
    raw.rawBuffer = {
        0u, 5000u, 128u, 1024u, 2048u, 4095u,
        96u, 512u, 1536u, 2560u, 3584u, 4200u,
        80u, 640u, 1664u, 2688u, 3712u, 4300u,
        72u, 768u, 1792u, 2816u, 3840u, 4400u
    };
    return raw;
}

struct TemporaryDirectory {
    std::filesystem::path path;
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool ValidateFullLegacyPreparedOutputs() {
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path() /
            ("stack-multiframe-legacy-adapters-" + std::to_string(nonce))
    };
    const std::array<Raw::MultiFrame::LegacyProcessorKind, 2> producers {
        Raw::MultiFrame::LegacyProcessorKind::MultiFrameDenoise,
        Raw::MultiFrame::LegacyProcessorKind::RawBurstHdr
    };
    const std::array<Raw::CfaPattern, 2> patterns {
        Raw::CfaPattern::RGGB,
        Raw::CfaPattern::GBRG
    };

    bool ok = true;
    for (std::size_t producerIndex = 0;
         producerIndex < producers.size();
         ++producerIndex) {
        const Raw::MultiFrame::LegacyProcessorKind producer =
            producers[producerIndex];
        const char identity = producerIndex == 0u ? 'a' : 'b';
        const Raw::RawImageData raw = MakeLegacyProcessorFixtureRaw(
            patterns[producerIndex], identity);
        auto cache = std::make_shared<Raw::Mfd::DirectoryNormalizedTileCache>(
            temporary.path /
            Raw::MultiFrame::LegacyProcessorKindName(producer));

        Raw::Mfd::Parameters parameters;
        parameters.fusion.outputTileRawPixels = 2u;
        Raw::Mfd::PreparationOptions options =
            Raw::Mfd::MakePreparationOptions(parameters);
        options.tileRawPixels = 2u;
        Raw::Mfd::PreparationResult prepared =
            Raw::Mfd::PrepareRawFrame(raw, options, *cache);
        ok &= Check(prepared.success,
            std::string(Raw::MultiFrame::LegacyProcessorKindName(producer)) +
                " full preparation failed: " + prepared.message);
        if (!prepared.success) continue;

        Raw::Mfd::NoiseResolutionOptions noiseOptions;
        noiseOptions.enableBurstEstimate = true;
        noiseOptions.enableGenericLowConfidence = true;
        Raw::Mfd::NoiseResolutionResult noise = Raw::Mfd::ResolveNoiseModel(
            raw.metadata, prepared.frame, cache.get(), noiseOptions);
        ok &= Check(noise.resolved,
            std::string(Raw::MultiFrame::LegacyProcessorKindName(producer)) +
                " full noise resolution failed: " + noise.message);
        if (!noise.resolved) continue;

        // The current MFD processor performs its second preparation after
        // resolving noise-aware saturation margins. HDR v4 uses the first
        // shared preparation directly.
        if (producer ==
            Raw::MultiFrame::LegacyProcessorKind::MultiFrameDenoise) {
            Raw::Mfd::ApplyNoiseModelToPreparationOptions(noise.model, options);
            prepared = Raw::Mfd::PrepareRawFrame(raw, options, *cache);
            ok &= Check(prepared.success,
                "MFD noise-aware re-preparation failed: " + prepared.message);
            if (!prepared.success) continue;
            noise = Raw::Mfd::ResolveNoiseModel(
                raw.metadata, prepared.frame, cache.get(), noiseOptions);
            ok &= Check(noise.resolved,
                "MFD final noise resolution failed: " + noise.message);
            if (!noise.resolved) continue;
        }

        Raw::MultiFrame::PreparedMeasurementSource adapted;
        std::string error;
        ok &= Check(
            Raw::MultiFrame::AdaptLegacyPreparedFrame(
                producer,
                std::string("full-") + identity,
                prepared.frame,
                cache,
                adapted,
                &error),
            std::string(Raw::MultiFrame::LegacyProcessorKindName(producer)) +
                " full prepared-output adapter failed: " + error);

        Raw::Mfd::PreparedRawTile directTile;
        error.clear();
        ok &= Check(
            Raw::Mfd::ReadPreparedTile(
                prepared.frame, *cache, 0u, 0u, directTile, &error) ==
                Raw::Mfd::TileCacheReadStatus::Hit,
            "direct full prepared tile could not be read: " + error);
        Raw::MultiFrame::PreparedMeasurementTile adaptedTile;
        error.clear();
        ok &= Check(
            adapted.tiles.Read(0u, 0u, adaptedTile, &error) ==
                Raw::MultiFrame::TileReadStatus::Hit,
            "adapted full prepared tile could not be read: " + error);
        ok &= Check(
            EqualFloatBits(
                directTile.normalizedMosaic,
                adaptedTile.normalizedMosaic) &&
                EqualFloatBits(
                    directTile.comparisonGain,
                    adaptedTile.comparisonGain) &&
                directTile.sampleFlags.size() ==
                    adaptedTile.sampleEvidence.size(),
            "full prepared output changed across the neutral adapter boundary");
        ok &= Check(
            adaptedTile.normalizedMosaic.front() < 0.0f &&
                adaptedTile.normalizedMosaic[1] > 1.0f,
            "full prepared output lost signed or overrange samples");

        Raw::MultiFrame::NoiseModelReference adaptedNoise;
        error.clear();
        ok &= Check(
            Raw::MultiFrame::AdaptLegacyNoiseModel(
                noise.model, adaptedNoise, &error),
            "full resolved noise model could not be adapted: " + error);
        ok &= Check(
            adaptedNoise.preparedFrameCacheKey == prepared.frame.cacheKey &&
                adaptedNoise.identitySha256 == noise.model.identitySha256 &&
                !adaptedNoise.numericalValuesReinterpreted,
            "full resolved noise provenance changed across the adapter");

        const std::filesystem::path corruptTile = cache->Root() /
            prepared.frame.cacheKey / "tile-0-0.mfdn";
        {
            std::fstream stream(
                corruptTile,
                std::ios::binary | std::ios::in | std::ios::out);
            ok &= Check(static_cast<bool>(stream),
                "full prepared cache tile could not be opened for fault injection");
            if (stream) {
                stream.seekp(-1, std::ios::end);
                const char corrupt = static_cast<char>(0x5a);
                stream.write(&corrupt, 1);
            }
        }
        Raw::MultiFrame::PreparedMeasurementTile unchanged = adaptedTile;
        error.clear();
        ok &= Check(
            adapted.tiles.Read(0u, 0u, unchanged, &error) ==
                Raw::MultiFrame::TileReadStatus::Corrupt,
            "corrupt legacy prepared cache did not remain categorical");
        ok &= Check(
            unchanged.tileX == adaptedTile.tileX &&
                unchanged.tileY == adaptedTile.tileY &&
                EqualFloatBits(
                    unchanged.normalizedMosaic,
                    adaptedTile.normalizedMosaic) &&
                EqualFloatBits(
                    unchanged.comparisonGain,
                    adaptedTile.comparisonGain),
            "corrupt adapter read partially replaced its verified output");
    }
    return ok;
}

} // namespace

bool ValidateUnifiedMultiFrameContracts() {
    bool ok = true;
    ok &= ValidateFrozenLegacyIdentities();
    ok &= ValidateNeutralCfaDomain();
    ok &= ValidateEvidenceSemantics();
    ok &= ValidatePreparedSourceAdapters();
    ok &= ValidateNoiseAdapter();
    ok &= ValidateFullLegacyPreparedOutputs();
    if (ok) {
        std::cout
            << "Unified MultiFrame contracts and legacy adapters validation passed."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
