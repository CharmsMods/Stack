#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Preparation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 1 validation failed: " << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double a, double b, double tolerance = 1.0e-6) {
    return std::abs(a - b) <= tolerance;
}

Raw::RawImageData MakeRaw(
    int width,
    int height,
    const std::vector<std::uint16_t>& samples,
    char identityCharacter = 'a') {
    Raw::RawImageData raw;
    raw.metadata.sourceContentSha256 = std::string(64u, identityCharacter);
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.mosaiced = true;
    raw.metadata.blackLevel = 0.0f;
    raw.metadata.whiteLevel = 100.0f;
    raw.rawBuffer = samples;
    return raw;
}

bool CollectPreparedMosaic(
    const Raw::Mfd::PreparedRawFrame& frame,
    Raw::Mfd::NormalizedTileCache& cache,
    std::vector<float>& normalized,
    std::vector<float>& gain,
    std::vector<std::uint8_t>& flags) {
    const std::size_t pixelCount = static_cast<std::size_t>(
        frame.activeExtent.width * frame.activeExtent.height);
    normalized.assign(pixelCount, 0.0f);
    gain.assign(pixelCount, 0.0f);
    flags.assign(pixelCount, 0u);
    for (std::uint32_t tileY = 0; tileY < frame.tileRows; ++tileY) {
        for (std::uint32_t tileX = 0; tileX < frame.tileColumns; ++tileX) {
            Raw::Mfd::PreparedRawTile tile;
            std::string error;
            if (Raw::Mfd::ReadPreparedTile(
                    frame, cache, tileX, tileY, tile, &error) !=
                Raw::Mfd::TileCacheReadStatus::Hit) {
                std::cerr << "MFD Phase 1 tile read failed: " << error << std::endl;
                return false;
            }
            for (std::uint64_t y = 0; y < tile.extent.height; ++y) {
                for (std::uint64_t x = 0; x < tile.extent.width; ++x) {
                    const std::size_t source = static_cast<std::size_t>(
                        y * tile.extent.width + x);
                    const std::size_t destination = static_cast<std::size_t>(
                        (tile.originY + y) * frame.activeExtent.width +
                        tile.originX + x);
                    normalized[destination] = tile.normalizedMosaic[source];
                    gain[destination] = tile.comparisonGain[source];
                    flags[destination] = tile.sampleFlags[source];
                }
            }
        }
    }
    return true;
}

bool EqualFloatBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
        (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

bool ValidateLinearizationNormalizationAndMasks() {
    Raw::RawImageData raw = MakeRaw(
        4,
        2,
        { 9, 10, 15, 20, 5, 11, 14, 16 });
    raw.metadata.dngLinearizationTable.resize(32u);
    for (std::size_t i = 0; i < raw.metadata.dngLinearizationTable.size(); ++i) {
        raw.metadata.dngLinearizationTable[i] =
            static_cast<std::uint16_t>(i * 10u);
    }
    raw.metadata.blackLevel = 100.0f;
    raw.metadata.whiteLevel = 200.0f;

    Raw::Mfd::Parameters parameters;
    parameters.fusion.outputTileRawPixels = 3;
    parameters.radiometric.saturationDnMargin = 4.0;
    Raw::Mfd::PreparationOptions options =
        Raw::Mfd::MakePreparationOptions(parameters);
    options.decoderSaturationMask.assign(raw.rawBuffer.size(), 0u);
    options.decoderDefectMask.assign(raw.rawBuffer.size(), 0u);
    options.decoderRepairedMask.assign(raw.rawBuffer.size(), 0u);
    options.decoderSaturationMask[1] = 1u;
    options.decoderDefectMask[4] = 1u;
    options.decoderRepairedMask[6] = 1u;

    Raw::Mfd::MemoryNormalizedTileCache cache;
    const Raw::Mfd::PreparationResult result =
        Raw::Mfd::PrepareRawFrame(raw, options, cache);
    bool ok = true;
    ok &= Check(result.success, "linearization fixture did not prepare: " + result.message);
    if (!result.success) return false;
    ok &= Check(result.frame.generatedTileCount == 2u,
        "odd-width fixture did not produce the expected two tiles");
    ok &= Check(result.frame.defectMaskProvenance ==
            Raw::Mfd::DefectMaskProvenance::DecoderKnownDefectsAndRepairMap,
        "defect and repair provenance was not retained");

    std::vector<float> normalized;
    std::vector<float> gain;
    std::vector<std::uint8_t> flags;
    ok &= Check(CollectPreparedMosaic(
        result.frame, cache, normalized, gain, flags), "prepared fixture could not be collected");
    ok &= Check(NearlyEqual(normalized[0], -0.1),
        "negative black-subtracted sample was clipped or changed");
    ok &= Check(NearlyEqual(normalized[1], 0.0),
        "linearization/black subtraction did not map black to zero");
    ok &= Check(NearlyEqual(normalized[2], 0.5),
        "linearization/white normalization produced the wrong value");
    ok &= Check(NearlyEqual(normalized[3], 1.0),
        "linearization did not preserve a white-level sample");
    ok &= Check(std::all_of(gain.begin(), gain.end(), [](float value) {
            return value == 1.0f;
        }), "identity calibration did not remain a separate unit gain");
    ok &= Check(Raw::Mfd::HasSampleFlag(
            flags[1], Raw::Mfd::PreparedSampleFlag::ExplicitDecoderClip) &&
        Raw::Mfd::HasSampleFlag(
            flags[1], Raw::Mfd::PreparedSampleFlag::Saturated),
        "explicit decoder clipping was not retained in the saturation mask");
    ok &= Check(Raw::Mfd::HasSampleFlag(
            flags[4], Raw::Mfd::PreparedSampleFlag::Defective),
        "known defect was not retained");
    ok &= Check(Raw::Mfd::HasSampleFlag(
            flags[6], Raw::Mfd::PreparedSampleFlag::DecoderRepaired),
        "decoder-repaired sample was not retained");
    return ok;
}

bool ValidateSpatialBlackAndMultipleWhiteLevels() {
    Raw::RawImageData raw = MakeRaw(4, 4, std::vector<std::uint16_t>(16u, 0u), 'b');
    raw.metadata.dngBlackLevelRepeatDim = { 2, 2 };
    raw.metadata.dngBlackLevelValues = { 10.0f, 20.0f, 30.0f, 40.0f };
    raw.metadata.dngBlackLevelDeltaH = { 0.0f, 1.0f, 2.0f, 3.0f };
    raw.metadata.dngBlackLevelDeltaV = { 0.0f, 4.0f, 8.0f, 12.0f };
    // Four-value metadata follows the decoder's R, G0, B, G1 indexing.
    raw.metadata.dngWhiteLevelValues = { 100.0f, 200.0f, 300.0f, 400.0f };

    Raw::Mfd::PreparationOptions options;
    options.tileRawPixels = 4;
    Raw::Mfd::MemoryNormalizedTileCache cache;
    const Raw::Mfd::PreparationResult result =
        Raw::Mfd::PrepareRawFrame(raw, options, cache);
    bool ok = true;
    ok &= Check(result.success, "spatial black fixture did not prepare: " + result.message);
    if (!result.success) return false;

    const std::array<double, 4> expectedMaximumBlack { 20.0, 31.0, 44.0, 55.0 };
    const std::array<double, 4> expectedWhite { 100.0, 200.0, 400.0, 300.0 };
    for (std::size_t i = 0; i < expectedMaximumBlack.size(); ++i) {
        ok &= Check(NearlyEqual(
                result.frame.calibration.maximumBlackByCfaSite[i],
                expectedMaximumBlack[i]),
            "per-row/per-CFA maximum black denominator is wrong");
        ok &= Check(NearlyEqual(
                result.frame.calibration.whiteLevelByCfaSite[i],
                expectedWhite[i]),
            "multiple white levels were not mapped to the correct CFA site");
        ok &= Check(NearlyEqual(
                result.frame.calibration.usableSpanByCfaSite[i],
                expectedWhite[i] - expectedMaximumBlack[i]),
            "white-minus-maximum-black usable span is wrong");
    }

    std::vector<float> normalized;
    std::vector<float> gain;
    std::vector<std::uint8_t> flags;
    ok &= Check(CollectPreparedMosaic(
        result.frame, cache, normalized, gain, flags), "spatial black tile could not be read");
    ok &= Check(NearlyEqual(normalized[0], -10.0 / 80.0),
        "R-site local black subtraction used the wrong denominator");
    ok &= Check(NearlyEqual(normalized[1], -21.0 / 169.0),
        "G0-site horizontal black delta was not applied");
    ok &= Check(NearlyEqual(normalized[4], -34.0 / 356.0),
        "G1-site vertical black delta was not applied");
    ok &= Check(NearlyEqual(normalized[5], -45.0 / 245.0),
        "B-site spatial black level was not applied");
    return ok;
}

bool ValidateSaturationMargins() {
    Raw::RawImageData raw = MakeRaw(2, 2, { 79, 80, 95, 96 }, 'c');
    Raw::Mfd::PreparationOptions options;
    options.saturationDnMargin = 4.0;
    options.saturationNoiseSigmaMargin = 2.0;
    options.saturationStdDevAtWhite.fill(0.10);
    Raw::Mfd::MemoryNormalizedTileCache cache;
    const Raw::Mfd::PreparationResult result =
        Raw::Mfd::PrepareRawFrame(raw, options, cache);
    bool ok = true;
    ok &= Check(result.success, "saturation fixture did not prepare: " + result.message);
    if (!result.success) return false;
    std::vector<float> normalized;
    std::vector<float> gain;
    std::vector<std::uint8_t> flags;
    ok &= Check(CollectPreparedMosaic(
        result.frame, cache, normalized, gain, flags), "saturation tile could not be read");
    ok &= Check(!Raw::Mfd::HasSampleFlag(
            flags[0], Raw::Mfd::PreparedSampleFlag::Saturated),
        "sample below the noise-derived saturation guard was rejected");
    ok &= Check(Raw::Mfd::HasSampleFlag(
            flags[1], Raw::Mfd::PreparedSampleFlag::Saturated),
        "sample at the noise-derived saturation guard was not rejected");
    ok &= Check(result.frame.saturatedSampleCount == 3u,
        "saturation mask count does not match the noise/DN guard");
    return ok;
}

Raw::DngGainMapOpcode MakeGainMap() {
    Raw::DngGainMapOpcode map;
    map.top = 0;
    map.left = 0;
    map.bottom = 4;
    map.right = 4;
    map.plane = 0;
    map.planes = 1;
    map.rowPitch = 1;
    map.colPitch = 1;
    map.mapPointsV = 2;
    map.mapPointsH = 2;
    map.mapPlanes = 1;
    map.mapSpacingV = 1.0;
    map.mapSpacingH = 1.0;
    map.mapOriginV = 0.0;
    map.mapOriginH = 0.0;
    map.gains = { 1.0f, 2.0f, 3.0f, 4.0f };
    return map;
}

bool ValidateGainSeparationAndRoundTrip() {
    Raw::RawImageData raw = MakeRaw(4, 4, std::vector<std::uint16_t>(16u, 25u), 'd');
    raw.metadata.dngGainMaps.push_back(MakeGainMap());
    raw.metadata.dngGainMapCount = 1;
    raw.metadata.dngOpcodeCount[1] = 1;
    raw.metadata.dngAppliedOpcodeCountByList[1] = 1;

    Raw::Mfd::MemoryNormalizedTileCache cache;
    const Raw::Mfd::PreparationResult result =
        Raw::Mfd::PrepareRawFrame(raw, {}, cache);
    bool ok = true;
    ok &= Check(result.success, "GainMap fixture did not prepare: " + result.message);
    if (!result.success) return false;
    std::vector<float> normalized;
    std::vector<float> gains;
    std::vector<std::uint8_t> flags;
    ok &= Check(CollectPreparedMosaic(
        result.frame, cache, normalized, gains, flags), "GainMap tile could not be read");
    ok &= Check(std::all_of(normalized.begin(), normalized.end(), [](float value) {
            return NearlyEqual(value, 0.25);
        }), "GainMap was incorrectly baked into the normalized mosaic");
    ok &= Check(std::any_of(gains.begin(), gains.end(), [](float value) {
            return value > 1.0f;
        }), "pointwise GainMap did not produce a separate calibration field");
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        const float comparison = normalized[i] * gains[i];
        float restored = 0.0f;
        ok &= Check(Raw::Mfd::ReturnToPreGainDomain(
                comparison, gains[i], restored) &&
                NearlyEqual(restored, normalized[i], 2.0e-7),
            "gain round trip q -> gq -> /g did not restore the pre-gain sample");
    }
    return ok;
}

bool ValidateOperationClassificationAndMalformedInputs() {
    bool ok = true;
    Raw::Mfd::MemoryNormalizedTileCache cache;

    Raw::RawImageData list1 = MakeRaw(2, 2, { 1, 2, 3, 4 }, 'e');
    list1.metadata.dngOpcodeCount[0] = 1;
    Raw::Mfd::PreparationResult result = Raw::Mfd::PrepareRawFrame(list1, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::UnsupportedRequiredOperation,
        "unapplied OpcodeList1 operation was not rejected");

    Raw::RawImageData list2 = MakeRaw(2, 2, { 1, 2, 3, 4 }, 'f');
    list2.metadata.dngOpcodeCount[1] = 1;
    result = Raw::Mfd::PrepareRawFrame(list2, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::UnsupportedRequiredOperation,
        "unknown OpcodeList2 operation was not rejected");

    Raw::RawImageData list3 = MakeRaw(2, 2, { 1, 2, 3, 4 }, '1');
    list3.metadata.dngOpcodeCount[2] = 2;
    result = Raw::Mfd::PrepareRawFrame(list3, {}, cache);
    ok &= Check(result.success,
        "post-demosaic OpcodeList3 operations should be deferred, not rejected");
    if (result.success) {
        ok &= Check(std::any_of(
                result.frame.calibration.operations.records.begin(),
                result.frame.calibration.operations.records.end(),
                [](const Raw::Mfd::RawOperationRecord& record) {
                    return record.stage == Raw::Mfd::RawOperationStage::PostDemosaic &&
                        record.disposition == Raw::Mfd::RawOperationDisposition::DeferredDownstream;
                }), "OpcodeList3 deferral was not recorded");
    }

    Raw::RawImageData profileGain = MakeRaw(2, 2, { 1, 2, 3, 4 }, '2');
    profileGain.metadata.hasDngProfileGainTableMap = true;
    result = Raw::Mfd::PrepareRawFrame(profileGain, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::UnsupportedRequiredOperation,
        "unevaluated ProfileGainTableMap was not rejected conservatively");

    Raw::RawImageData badBlack = MakeRaw(2, 2, { 1, 2, 3, 4 }, '3');
    badBlack.metadata.dngBlackLevelRepeatDim = { 2, 2 };
    badBlack.metadata.dngBlackLevelValues = { 1.0f, 2.0f, 3.0f };
    result = Raw::Mfd::PrepareRawFrame(badBlack, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::MalformedMetadata,
        "truncated spatial black map was not rejected");

    Raw::RawImageData badWhite = MakeRaw(2, 2, { 1, 2, 3, 4 }, '4');
    badWhite.metadata.blackLevel = 50.0f;
    badWhite.metadata.whiteLevel = 50.0f;
    result = Raw::Mfd::PrepareRawFrame(badWhite, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::MalformedMetadata,
        "white level at black was not rejected");

    Raw::RawImageData badLinearization = MakeRaw(2, 2, { 0, 1, 2, 3 }, '5');
    badLinearization.metadata.dngLinearizationTable = { 0, 10, 9, 30 };
    result = Raw::Mfd::PrepareRawFrame(badLinearization, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::MalformedMetadata,
        "non-monotonic linearization table was not rejected");

    Raw::RawImageData badGain = MakeRaw(4, 4, std::vector<std::uint16_t>(16u, 25u), '6');
    Raw::DngGainMapOpcode map = MakeGainMap();
    map.gains[2] = 0.0f;
    badGain.metadata.dngGainMaps.push_back(map);
    badGain.metadata.dngGainMapCount = 1;
    badGain.metadata.dngOpcodeCount[1] = 1;
    result = Raw::Mfd::PrepareRawFrame(badGain, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::InvalidGainMap,
        "nonpositive GainMap sample was not rejected");

    Raw::RawImageData linearDng = MakeRaw(2, 2, { 1, 2, 3, 4 }, '7');
    linearDng.metadata.pixelLayout = Raw::RawPixelLayout::LinearRgb;
    linearDng.metadata.mosaiced = false;
    result = Raw::Mfd::PrepareRawFrame(linearDng, {}, cache);
    ok &= Check(!result.success &&
            result.failure == Raw::Mfd::PreparationFailure::UnsupportedPixelLayout,
        "already-demosaiced RAW was not rejected by the mosaic-only boundary");
    return ok;
}

struct TemporaryDirectory {
    std::filesystem::path path;
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool ValidateTiledDirectoryCache() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path() /
            ("stack-mfd-phase1-" + std::to_string(nonce))
    };
    Raw::Mfd::DirectoryNormalizedTileCache cache(temporary.path);
    Raw::RawImageData raw = MakeRaw(
        5,
        3,
        { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
        '8');
    Raw::Mfd::PreparationOptions options;
    options.tileRawPixels = 2;

    const Raw::Mfd::PreparationResult first =
        Raw::Mfd::PrepareRawFrame(raw, options, cache);
    bool ok = true;
    ok &= Check(first.success, "directory cache fixture did not prepare: " + first.message);
    if (!first.success) return false;
    ok &= Check(first.frame.generatedTileCount == 6u &&
            first.frame.cacheHitTileCount == 0u,
        "first directory-cache preparation did not generate every odd-border tile");
    std::vector<float> firstNormalized;
    std::vector<float> firstGain;
    std::vector<std::uint8_t> firstFlags;
    ok &= Check(CollectPreparedMosaic(
        first.frame, cache, firstNormalized, firstGain, firstFlags),
        "directory-cache tiles could not be collected");

    const Raw::Mfd::PreparationResult second =
        Raw::Mfd::PrepareRawFrame(raw, options, cache);
    ok &= Check(second.success && second.frame.generatedTileCount == 0u &&
            second.frame.cacheHitTileCount == 6u,
        "second preparation did not reuse every normalized tile");
    if (second.success) {
        std::vector<float> secondNormalized;
        std::vector<float> secondGain;
        std::vector<std::uint8_t> secondFlags;
        ok &= Check(CollectPreparedMosaic(
            second.frame, cache, secondNormalized, secondGain, secondFlags),
            "reused directory-cache tiles could not be collected");
        ok &= Check(EqualFloatBits(firstNormalized, secondNormalized) &&
                EqualFloatBits(firstGain, secondGain) && firstFlags == secondFlags,
            "lossless normalized tile cache changed prepared values or masks");
    }

    const std::filesystem::path corruptTile = temporary.path / first.frame.cacheKey /
        "tile-0-0.mfdn";
    {
        std::fstream stream(corruptTile, std::ios::binary | std::ios::in | std::ios::out);
        stream.seekp(-1, std::ios::end);
        const char corrupt = static_cast<char>(0x5a);
        stream.write(&corrupt, 1);
    }
    const Raw::Mfd::PreparationResult repaired =
        Raw::Mfd::PrepareRawFrame(raw, options, cache);
    ok &= Check(repaired.success && repaired.frame.generatedTileCount == 1u &&
            repaired.frame.cacheHitTileCount == 5u,
        "corrupt disposable tile was not detected and regenerated in isolation");

    Raw::Mfd::PreparationOptions changed = options;
    changed.decoderDefectMask.assign(raw.rawBuffer.size(), 0u);
    changed.decoderDefectMask[0] = 1u;
    const Raw::Mfd::PreparationResult invalidated =
        Raw::Mfd::PrepareRawFrame(raw, changed, cache);
    ok &= Check(invalidated.success &&
            invalidated.frame.cacheKey != first.frame.cacheKey &&
            invalidated.frame.generatedTileCount == 6u,
        "defect-policy input did not invalidate normalized cache identity");
    return ok;
}

} // namespace

bool ValidateMfdPhase1Preparation() {
    bool ok = true;
    ok &= ValidateLinearizationNormalizationAndMasks();
    ok &= ValidateSpatialBlackAndMultipleWhiteLevels();
    ok &= ValidateSaturationMargins();
    ok &= ValidateGainSeparationAndRoundTrip();
    ok &= ValidateOperationClassificationAndMalformedInputs();
    ok &= ValidateTiledDirectoryCache();
    if (ok) {
        std::cout << "MFD Phase 1 RAW preparation validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
