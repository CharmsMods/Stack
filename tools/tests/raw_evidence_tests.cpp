#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RawProcessingMath.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_Failures = 0;

void Check(bool condition, const std::string& fixture, const std::string& detail) {
    if (condition) return;
    ++g_Failures;
    std::cerr << fixture << " failed: " << detail << '\n';
}

bool Near(double actual, double expected, double tolerance = 1.0e-5) {
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

Raw::RawImageData MakeRaw(int width, int height, float black = 64.0f, float white = 1024.0f) {
    Raw::RawImageData raw;
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.mosaiced = true;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.blackLevel = black;
    raw.metadata.perChannelBlack = { black, black, black, black };
    raw.metadata.whiteLevel = white;
    raw.metadata.whiteLevelSource = "fixture";
    raw.metadata.blackLevelSource = "fixture";
    raw.metadata.dngWhiteLevelValues = { white };
    raw.metadata.orientation = 1;
    raw.rawBuffer.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        static_cast<std::uint16_t>(black));
    return raw;
}

Stack::RawEvidence::SourceIdentity FixtureIdentity(const std::string& text = "fixture") {
    return Stack::RawEvidence::ComputeSourceIdentity(
        std::vector<std::uint8_t>(text.begin(), text.end()));
}

Stack::RawEvidence::RawTechnicalEvidenceRecord Build(
    const Raw::RawImageData& raw,
    const std::string& identity = "fixture",
    Stack::RawEvidence::BuildOptions options = {}) {
    options.maxSamples = 1000000;
    options.measureDefectivePixels = false;
    return Stack::RawEvidence::BuildRawTechnicalEvidence(raw, FixtureIdentity(identity), options);
}

void TestIdentity() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "stack-raw-evidence-id-fixture";
    std::error_code ec;
    fs::create_directories(root, ec);
    const fs::path first = root / "first.raw";
    const fs::path second = root / "second.raw";
    const std::vector<unsigned char> bytes { 'a', 'b', 'c' };
    {
        std::ofstream file(first, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    {
        std::ofstream file(second, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto a = Stack::RawEvidence::ComputeSourceIdentity(first);
    const auto b = Stack::RawEvidence::ComputeSourceIdentity(second);
    Check(a.valid && b.valid && a.sha256 == b.sha256 && a.byteSize == 3,
        "P01-ID-01", "same bytes at different paths must share content identity");
    Check(a.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "P01-ID-01", "SHA-256 implementation must match the standard abc vector");
    {
        std::ofstream file(second, std::ios::binary | std::ios::app);
        const char changed = '!';
        file.write(&changed, 1);
    }
    const auto changed = Stack::RawEvidence::ComputeSourceIdentity(second);
    Check(changed.valid && changed.sha256 != a.sha256,
        "P01-ID-02", "one byte must invalidate source identity");
    fs::resize_file(second, 512 * 1024);
    int cancelChecks = 0;
    const auto canceled = Stack::RawEvidence::ComputeSourceIdentity(second,
        [&] { return ++cancelChecks == 4; });
    Check(!canceled.valid && canceled.sha256.empty() && canceled.reason == "source-hash-canceled" &&
        canceled.byteSize > 0 && canceled.byteSize < fs::file_size(second),
        "P01-ID-03", "cancellation interrupts streaming hash without publishing a partial identity");
    fs::remove_all(root, ec);
}

void TestDecodeIdentity() {
    Raw::RawImageData raw = MakeRaw(4, 4);
    Stack::RawEvidence::DecodeIdentityOptions base;
    const auto original = Stack::RawEvidence::BuildDecodeIdentity(raw.metadata, base);
    auto changed = [&](Stack::RawEvidence::DecodeIdentityOptions value, const std::string& label) {
        const auto identity = Stack::RawEvidence::BuildDecodeIdentity(raw.metadata, value);
        Check(identity.sha256 != original.sha256, "P01-DECODE-01", label + " must change decode identity");
    };
    auto value = base; value.decoderBackend = "other"; changed(value, "decoder backend");
    value = base; value.decoderVersion = "2"; changed(value, "decoder version");
    value = base; value.normalizationVersion = "other"; changed(value, "normalization version");
    value = base; value.applyLinearizationTable = false; changed(value, "linearization policy");
    value = base; value.useActiveArea = false; changed(value, "active-area policy");
    value = base; value.useMaskedAreas = false; changed(value, "masked-area policy");
    value = base; value.overrideBlackLevel = true; changed(value, "black override policy");
    value = base; value.overrideWhiteLevel = true; changed(value, "white override policy");
    value = base; value.opcodeList1Applied = true; changed(value, "opcode-list-1 policy");
    value = base; value.opcodeList2GainMapsApplied = true; changed(value, "gain-map policy");
    value = base; value.profileGainTableMapApplied = true; changed(value, "profile-gain policy");
    const auto repeat = Stack::RawEvidence::BuildDecodeIdentity(raw.metadata, base);
    Check(original.sha256 == repeat.sha256 && original.canonicalFields == repeat.canonicalFields,
        "P01-DECODE-01", "decode identity must be deterministic");
}

void TestNormalization() {
    Raw::RawImageData raw = MakeRaw(4, 4, 64.0f, 1024.0f);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 544);
    auto record = Build(raw, "norm-1");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 0.5), "P01-NORM-01", plane.planeName + " midpoint normalization");
    }
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 64);
    record = Build(raw, "norm-black");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 0.0), "P01-NORM-01", plane.planeName + " black maps to zero");
    }
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 1024);
    record = Build(raw, "norm-white");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 1.0) && Near(plane.clippedFraction.value, 1.0),
            "P01-NORM-01", plane.planeName + " white maps to one and clips");
    }

    raw.metadata.dngBlackLevelRepeatDim = { 2, 2 };
    raw.metadata.dngBlackLevelValues = { 64.0f, 32.0f, 32.0f, 16.0f };
    raw.metadata.dngBlackLevelPattern = { 64.0f, 32.0f, 32.0f, 16.0f };
    raw.metadata.dngCfaRepeatPatternDim = { 2, 2 };
    raw.metadata.dngCfaPattern = { 0, 1, 1, 2 };
    raw.metadata.dngCfaPlaneColor = { 0, 1, 2 };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float black = raw.metadata.dngBlackLevelValues[static_cast<std::size_t>((y % 2) * 2 + (x % 2))];
            raw.rawBuffer[static_cast<std::size_t>(y * 4 + x)] =
                static_cast<std::uint16_t>(std::lround(black + 0.5f * (1024.0f - black)));
        }
    }
    record = Build(raw, "norm-2");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 0.5, 0.001), "P01-NORM-02", plane.planeName + " repeating black pattern");
    }

    raw.metadata.dngBlackLevelDeltaH = { 0.0f, 10.0f, 0.0f, 10.0f };
    raw.metadata.dngBlackLevelDeltaV = { 0.0f, 20.0f, 0.0f, 20.0f };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float baseBlack = raw.metadata.dngBlackLevelValues[static_cast<std::size_t>((y % 2) * 2 + (x % 2))];
            const float black = baseBlack + raw.metadata.dngBlackLevelDeltaH[static_cast<std::size_t>(x)] +
                raw.metadata.dngBlackLevelDeltaV[static_cast<std::size_t>(y)];
            const int color = std::array<int, 4>{ 0, 1, 1, 2 }[static_cast<std::size_t>((y % 2) * 2 + (x % 2))];
            const float maxBlack = std::array<float, 3>{ 64.0f, 52.0f, 46.0f }[static_cast<std::size_t>(color)];
            raw.rawBuffer[static_cast<std::size_t>(y * 4 + x)] =
                static_cast<std::uint16_t>(std::lround(black + 0.5f * (1024.0f - maxBlack)));
        }
    }
    record = Build(raw, "norm-3");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 0.5, 0.001), "P01-NORM-03", plane.planeName + " H/V black deltas");
    }

    raw.metadata.dngBlackLevelDeltaH = { -10.0f, 0.0f, -10.0f, 0.0f };
    raw.metadata.dngBlackLevelDeltaV = { -20.0f, 0.0f, -20.0f, 0.0f };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float baseBlack = raw.metadata.dngBlackLevelValues[static_cast<std::size_t>((y % 2) * 2 + (x % 2))];
            const float black = baseBlack + raw.metadata.dngBlackLevelDeltaH[static_cast<std::size_t>(x)] +
                raw.metadata.dngBlackLevelDeltaV[static_cast<std::size_t>(y)];
            const int color = std::array<int, 4>{ 0, 1, 1, 2 }[static_cast<std::size_t>((y % 2) * 2 + (x % 2))];
            const float maxBlack = std::array<float, 3>{ 34.0f, 22.0f, 16.0f }[static_cast<std::size_t>(color)];
            raw.rawBuffer[static_cast<std::size_t>(y * 4 + x)] =
                static_cast<std::uint16_t>(std::lround(black + 0.5f * (1024.0f - maxBlack)));
        }
    }
    record = Build(raw, "norm-3-negative");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 0.5, 0.001), "P01-NORM-03", plane.planeName + " negative H/V black deltas");
    }

    raw = MakeRaw(4, 4, 0.0f, 1024.0f);
    raw.metadata.dngLinearizationTable.resize(1024);
    for (std::size_t i = 0; i < raw.metadata.dngLinearizationTable.size(); ++i) {
        raw.metadata.dngLinearizationTable[i] = static_cast<std::uint16_t>(std::min<std::size_t>(1023, i * 2));
    }
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 256);
    record = Build(raw, "norm-4");
    for (const auto& plane : record.planes) {
        Check(Near(plane.p999.value, 0.5, 0.001), "P01-NORM-04", plane.planeName + " linearization before black subtraction");
    }
}

void TestTruthfulProcessingMath() {
    Raw::RawImageData raw = MakeRaw(4, 4, 64.0f, 1024.0f);
    raw.metadata.isDng = true;
    raw.metadata.dngBlackLevelRepeatDim = { 2, 2 };
    raw.metadata.dngBlackLevelValues = { 64.0f, 32.0f, 32.0f, 16.0f };
    raw.metadata.dngBlackLevelDeltaH = { 0.0f, 8.0f, 0.0f, 8.0f };
    raw.metadata.dngBlackLevelDeltaV = { 0.0f, 16.0f, 0.0f, 16.0f };
    raw.metadata.dngLinearizationTable = { 0, 128, 544 };
    raw.metadata.dngWhiteLevelValues = { 1024.0f };
    Raw::RawDevelopSettings settings;
    settings.processingVersion = Raw::RawProcessingVersion::TruthfulV2;

    const Raw::RawSensorRect active = Raw::Processing::ResolveActiveArea(raw.metadata);
    const float lastEntry = Raw::Processing::NormalizeStoredSample(
        raw.metadata, settings, active, 0, 0, 400);
    Check(Near(lastEntry, 0.5), "RAW-TRUTH-01",
        "linearization-table indices above the table must use the final table entry before black subtraction");

    raw.metadata.dngLinearizationTable.clear();
    const float negative = Raw::Processing::NormalizeStoredSample(
        raw.metadata, settings, active, 1, 1, 20);
    Check(negative < 0.0f, "RAW-TRUTH-02",
        "truthful normalization must preserve below-black negative samples");
    const float clippedHigh = Raw::Processing::NormalizeStoredSample(
        raw.metadata, settings, active, 0, 0, 2048);
    Check(Near(clippedHigh, 1.0), "RAW-TRUTH-03",
        "truthful normalization must identify values above sensor white without carrying impossible sensor code values");

    raw.metadata.dngBlackLevelDeltaH.clear();
    raw.metadata.dngBlackLevelDeltaV.clear();
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 544);
    std::vector<float> normalized;
    std::string error;
    Check(Raw::Processing::BuildTruthfulNormalizedMosaic(raw, settings, normalized, &error) &&
            normalized.size() == raw.rawBuffer.size(),
        "RAW-TRUTH-04", "truthful normalized mosaic must cover every sensor sample");
    std::fill(normalized.begin(), normalized.end(), 1.0f);
    Check(
        Raw::Processing::ApplyWhiteBalanceToCfaMosaic(
            raw.metadata,
            { 2.0f, 1.0f, 3.0f },
            normalized,
            &error) &&
        Near(normalized[0], 2.0) &&
        Near(normalized[1], 1.0) &&
        Near(normalized[4], 1.0) &&
        Near(normalized[5], 3.0),
        "RAW-WB-01",
        "Truthful V1 white balance must scale the declared CFA planes before demosaic");

    Raw::RawImageData gainRaw = MakeRaw(4, 4, 0.0f, 1000.0f);
    std::fill(gainRaw.rawBuffer.begin(), gainRaw.rawBuffer.end(), 250);
    Raw::DngGainMapOpcode gainMap;
    gainMap.top = 0;
    gainMap.left = 0;
    gainMap.bottom = 4;
    gainMap.right = 4;
    gainMap.mapPointsV = 2;
    gainMap.mapPointsH = 2;
    gainMap.mapPlanes = 1;
    gainMap.mapSpacingV = 0.75;
    gainMap.mapSpacingH = 0.75;
    gainMap.mapOriginV = 0.125;
    gainMap.mapOriginH = 0.125;
    gainMap.gains = { 1.0f, 2.0f, 3.0f, 4.0f };
    gainRaw.metadata.dngGainMaps = { gainMap };
    std::vector<float> gainCorrected;
    Check(
        Raw::Processing::BuildTruthfulNormalizedMosaic(
            gainRaw,
            settings,
            gainCorrected,
            &error) &&
        Near(gainCorrected[0], 0.25) &&
        Near(gainCorrected[3], 0.5) &&
        Near(gainCorrected[12], 0.75) &&
        Near(gainCorrected[15], 1.0),
        "RAW-GAINMAP-01",
        "OpcodeList2 gain-map interpolation must use pixel-center normalized image coordinates after sensor normalization");
    std::fill(gainRaw.rawBuffer.begin(), gainRaw.rawBuffer.end(), 750);
    Check(
        Raw::Processing::BuildTruthfulNormalizedMosaic(
            gainRaw,
            settings,
            gainCorrected,
            &error) &&
        Near(gainCorrected[15], 1.0),
        "RAW-GAINMAP-02",
        "OpcodeList2 processing must clip to the legal normalized image range after each opcode");

    Raw::RawImageData croppedGainRaw = MakeRaw(6, 6, 0.0f, 1000.0f);
    std::fill(croppedGainRaw.rawBuffer.begin(), croppedGainRaw.rawBuffer.end(), 250);
    croppedGainRaw.metadata.hasDngActiveArea = true;
    croppedGainRaw.metadata.dngActiveArea = { 1, 1, 5, 5 };
    croppedGainRaw.metadata.dngGainMaps = { gainMap };
    Check(
        Raw::Processing::BuildTruthfulNormalizedMosaic(
            croppedGainRaw,
            settings,
            gainCorrected,
            &error) &&
        Near(gainCorrected[0], 0.25) &&
        Near(gainCorrected[7], 0.25) &&
        Near(gainCorrected[10], 0.5) &&
        Near(gainCorrected[25], 0.75) &&
        Near(gainCorrected[28], 1.0),
        "RAW-GAINMAP-03",
        "OpcodeList2 bounds and normalized coordinates must be relative to the linearized DNG ActiveArea");

    const std::vector<float> constantMosaic(64, 0.25f);
    for (Raw::CfaPattern pattern : {
            Raw::CfaPattern::RGGB,
            Raw::CfaPattern::BGGR,
            Raw::CfaPattern::GBRG,
            Raw::CfaPattern::GRBG }) {
        const std::array<float, 3> rgb =
            Raw::Processing::DemosaicMalvarHeCutlerAt(constantMosaic, 8, 8, pattern, 3, 3);
        Check(Near(rgb[0], 0.25) && Near(rgb[1], 0.25) && Near(rgb[2], 0.25),
            "RAW-MHC-01", std::string("MHC must preserve constant fields for ") + Raw::CfaPatternName(pattern));
    }

    const std::array<float, 3> sensorPatch { 0.20f, 0.35f, 0.25f };
    const std::array<float, 3> patchWhiteBalance { 2.0f, 1.0f, 1.6f };
    const std::array<float, 3> expectedPatch {
        sensorPatch[0] * patchWhiteBalance[0],
        sensorPatch[1] * patchWhiteBalance[1],
        sensorPatch[2] * patchWhiteBalance[2]
    };
    for (Raw::CfaPattern pattern : {
            Raw::CfaPattern::RGGB,
            Raw::CfaPattern::BGGR,
            Raw::CfaPattern::GBRG,
            Raw::CfaPattern::GRBG }) {
        Raw::RawMetadata patchMetadata;
        patchMetadata.rawWidth = 8;
        patchMetadata.rawHeight = 8;
        patchMetadata.visibleWidth = 8;
        patchMetadata.visibleHeight = 8;
        patchMetadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
        patchMetadata.cfaPattern = pattern;
        const Raw::RawSensorRect patchArea =
            Raw::Processing::ResolveActiveArea(patchMetadata);
        std::vector<float> whiteBalancedPatch(64, 0.0f);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int color =
                    Raw::Processing::CfaColorAt(patchMetadata, patchArea, x, y);
                whiteBalancedPatch[static_cast<std::size_t>(y * 8 + x)] =
                    sensorPatch[static_cast<std::size_t>(color)];
            }
        }
        Check(
            Raw::Processing::ApplyWhiteBalanceToCfaMosaic(
                patchMetadata,
                patchWhiteBalance,
                whiteBalancedPatch,
                &error),
            "RAW-MHC-WB-01",
            std::string("white-balance fixture setup failed for ") +
                Raw::CfaPatternName(pattern));
        bool phaseStable = true;
        for (int y = 2; y < 6; ++y) {
            for (int x = 2; x < 6; ++x) {
                const std::array<float, 3> rgb =
                    Raw::Processing::DemosaicMalvarHeCutlerAt(
                        whiteBalancedPatch,
                        8,
                        8,
                        pattern,
                        x,
                        y);
                phaseStable =
                    phaseStable &&
                    Near(rgb[0], expectedPatch[0]) &&
                    Near(rgb[1], expectedPatch[1]) &&
                    Near(rgb[2], expectedPatch[2]);
            }
        }
        Check(
            phaseStable,
            "RAW-MHC-WB-01",
            std::string("MHC must preserve a non-neutral white-balanced patch across every CFA phase for ") +
                Raw::CfaPatternName(pattern));
    }

    Check(Near(Raw::Processing::EncodeSrgb(0.0f), 0.0) &&
            Near(Raw::Processing::EncodeSrgb(0.0031308f), 0.040449936, 1.0e-6) &&
            Near(Raw::Processing::EncodeSrgb(1.0f), 1.0),
        "RAW-OUTPUT-01", "sRGB output transfer must match the IEC piecewise curve");
}

void TestAreasAndOrientation() {
    Raw::RawImageData raw = MakeRaw(6, 6);
    raw.metadata.dngActiveArea = { 1, 1, 5, 5 };
    raw.metadata.hasDngActiveArea = true;
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 544);
    double p999 = -1.0;
    for (int orientation : { 1, 3, 6, 8 }) {
        raw.metadata.orientation = orientation;
        const auto record = Build(raw, "orientation");
        if (p999 < 0.0) p999 = record.planes.front().p999.value;
        Check(Near(record.planes.front().p999.value, p999), "P01-AREA-01", "sensor evidence must be orientation invariant");
        Check(record.geometry.photographicTransform != "decoder-orientation-unspecified", "P01-AREA-01", "orientation transform must be explicit");
    }

    raw = MakeRaw(6, 6);
    raw.metadata.dngActiveArea = { 0, 0, 6, 6 };
    raw.metadata.hasDngActiveArea = true;
    raw.metadata.dngMaskedAreas = { Raw::RawSensorRect { 0, 0, 1, 6 } };
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 544);
    for (int x = 0; x < 6; ++x) raw.rawBuffer[static_cast<std::size_t>(x)] = 64;
    const auto masked = Build(raw, "masked");
    Check(Near(masked.geometry.activeValidFraction, 5.0 / 6.0), "P01-AREA-02", "masked area must not count as active scene samples");
    Check(masked.maskedBlackMean.valid && Near(masked.maskedBlackMean.value, 64.0), "P01-AREA-02", "masked black evidence must remain separate");
}

void SetCell(Raw::RawImageData& raw, int cell, bool r, bool g, bool b) {
    const int x = cell * 2;
    const int width = raw.metadata.rawWidth;
    const std::uint16_t safe = 900;
    const std::uint16_t clip = 1024;
    raw.rawBuffer[static_cast<std::size_t>(x)] = r ? clip : safe;
    raw.rawBuffer[static_cast<std::size_t>(x + 1)] = g ? clip : safe;
    raw.rawBuffer[static_cast<std::size_t>(width + x)] = g ? clip : safe;
    raw.rawBuffer[static_cast<std::size_t>(width + x + 1)] = b ? clip : safe;
}

void TestClippingAndHeadroom() {
    Raw::RawImageData raw = MakeRaw(8, 2, 0.0f, 1024.0f);
    SetCell(raw, 0, false, false, false);
    SetCell(raw, 1, true, false, false);
    SetCell(raw, 2, true, true, false);
    SetCell(raw, 3, true, true, true);
    auto record = Build(raw, "clip");
    Check(record.clipping.valid && record.clipping.superpixelCount == 4, "P01-CLIP-02", "four CFA superpixels expected");
    Check(Near(record.clipping.noChannelClippedFraction.value, 0.25), "P01-CLIP-02", "no-channel fraction");
    Check(Near(record.clipping.singleChannelClippedFraction.value, 0.25), "P01-CLIP-02", "single-channel fraction");
    Check(Near(record.clipping.multiChannelClippedFraction.value, 0.25), "P01-CLIP-02", "multi-channel fraction");
    Check(Near(record.clipping.allChannelClippedFraction.value, 0.25), "P01-CLIP-02", "all-channel fraction");
    Check(record.planes[0].clippedFraction.value > record.planes[2].clippedFraction.value,
        "P01-CLIP-01", "per-plane clip ordering must survive");

    raw = MakeRaw(4, 4, 0.0f, 1024.0f);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 1020);
    record = Build(raw, "near-white");
    for (const auto& plane : record.planes) {
        Check(plane.nearWhiteFraction.value > 0.99 && plane.clippedFraction.value == 0.0,
            "P01-CLIP-01", plane.planeName + " near-white must remain distinct from clipped");
    }

    raw = MakeRaw(4, 4, 0.0f, 1024.0f);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 870);
    raw.metadata.dngLinearResponseLimit = 0.8f;
    raw.metadata.hasDngLinearResponseLimit = true;
    record = Build(raw, "lrl");
    Check(record.planes[0].nearNonlinearFraction.valid && record.planes[0].nearNonlinearFraction.value > 0.99,
        "P01-LRL-01", "LRL pressure must be distinct from white clipping");
    Check(record.planes[0].clippedFraction.value == 0.0 && record.planes[0].headroomEv.value < 0.0,
        "P01-LRL-01", "nonlinear pressure may exist without white-level clipping");
    raw.metadata.hasDngLinearResponseLimit = false;
    record = Build(raw, "lrl-missing");
    Check(!record.planes[0].nearNonlinearFraction.valid && record.planes[0].headroomEv.value > 0.0,
        "P01-LRL-01", "missing LRL must use explicit white outer-limit fallback");

    raw = MakeRaw(4, 4, 0.0f, 1024.0f);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 256);
    raw.metadata.hasDngAsShotNeutral = true;
    raw.metadata.whiteBalanceSource = "DNG AsShotNeutral fixture";
    raw.metadata.cameraWhiteBalance = { 2.0f, 1.0f, 1.0f, 1.0f };
    record = Build(raw, "wb");
    Check(record.limitingPlane == "R", "P01-WB-01", "WB-amplified red plane must be limiting");
    Check(record.planes[0].wbScaledHeadroomEv.value < record.planes[1].wbScaledHeadroomEv.value,
        "P01-WB-01", "WB headroom must be per plane");
}

void TestNoiseAndMetadata() {
    Raw::RawImageData raw = MakeRaw(4, 4, 0.0f, 1024.0f);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 256);
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = { Raw::DngNoiseProfilePlane { 0.01, 0.0001 } };
    raw.metadata.hasDngAsShotNeutral = true;
    raw.metadata.dngAsShotNeutral = { 0.5f, 1.0f, 1.0f };
    raw.metadata.hasDngBaselineExposure = true;
    raw.metadata.dngBaselineExposure = 0.5f;
    raw.metadata.dngOpcodeCount = { 2, 1, 3 };
    raw.metadata.dngUnsupportedOpcodeCountByList = { 2, 0, 3 };
    raw.metadata.dngAppliedOpcodeCountByList = { 0, 1, 0 };
    raw.metadata.dngGainMapCount = 1;
    raw.metadata.hasDngProfileGainTableMap = true;
    auto record = Build(raw, "noise");
    const double expected = 0.18 / std::sqrt(0.01 * 0.18 + 0.0001);
    Check(record.noise.size() == 3 && record.noise[0].snrBySignal.size() == 4,
        "P01-NOISE-01", "one DNG profile pair must apply to all planes");
    Check(Near(record.noise[0].snrBySignal.back().value, expected),
        "P01-NOISE-01", "SNR formula must match DNG model");
    const double gain = 3.0;
    const double signal = 0.18;
    const double variance = 0.01 * signal + 0.0001;
    Check(Near((gain * signal) / std::sqrt(gain * gain * variance), signal / std::sqrt(variance)),
        "P01-NOISE-02", "pure WB/exposure gain must preserve sensor SNR");
    Check(record.metadataCoverage.hasNoiseProfile && record.metadataCoverage.hasAsShotNeutral &&
        record.metadataCoverage.hasBaselineExposure && record.metadataCoverage.hasOpcodeList1 &&
        record.metadataCoverage.hasOpcodeList2 && record.metadataCoverage.hasOpcodeList3 &&
        record.metadataCoverage.hasProfileGainTableMap,
        "P01-META-01", "metadata presence and unsupported coverage must serialize explicitly");

    raw.metadata.hasDngNoiseProfile = false;
    raw.metadata.dngNoiseProfile.clear();
    record = Build(raw, "noise-missing");
    Check(!record.noise[0].shotScale.valid && record.noise[0].shotScale.reason.find("unavailable") != std::string::npos,
        "P01-NOISE-01", "missing profile must not become a zero coefficient");
}

void TestNoiseAwareFastDenoiseMath() {
    Raw::RawMetadata metadata;
    metadata.hasDngNoiseProfile = true;
    metadata.dngCfaPlaneColor = { 2, 0, 1 };
    metadata.dngNoiseProfile = {
        Raw::DngNoiseProfilePlane { 0.030, 0.0030 },
        Raw::DngNoiseProfilePlane { 0.010, 0.0010 },
        Raw::DngNoiseProfilePlane { 0.020, 0.0020 }
    };
    std::array<Raw::DngNoiseProfilePlane, 3> profiles {};
    Check(
        Raw::Processing::ResolveDngNoiseProfile(metadata, profiles) &&
            Near(profiles[0].shotScale, 0.010) &&
            Near(profiles[1].shotScale, 0.020) &&
            Near(profiles[2].shotScale, 0.030),
        "DENOISE-PROFILE-01",
        "DNG profile coefficients must follow CFAPlaneColor rather than assuming RGB storage order");

    metadata.dngNoiseProfile = {
        Raw::DngNoiseProfilePlane { 0.012, 0.0002 }
    };
    Check(
        Raw::Processing::ResolveDngNoiseProfile(metadata, profiles) &&
            Near(profiles[0].shotScale, 0.012) &&
            Near(profiles[1].shotScale, 0.012) &&
            Near(profiles[2].shotScale, 0.012),
        "DENOISE-PROFILE-02",
        "a single DNG profile pair must apply to every sensor color plane");
    Check(
        Near(
            Raw::Processing::DngNoiseVariance(profiles[0], 0.25f),
            0.012 * 0.25 + 0.0002),
        "DENOISE-PROFILE-03",
        "noise variance must implement the DNG S*x+O model in normalized sensor space");

    const float lowNoiseWeight = Raw::Processing::NoiseAwareRangeWeight(
        0.20f,
        0.22f,
        1.0e-6f,
        1.0e-6f,
        0.5f);
    const float highNoiseWeight = Raw::Processing::NoiseAwareRangeWeight(
        0.20f,
        0.22f,
        1.0e-3f,
        1.0e-3f,
        0.5f);
    const float protectedWeight = Raw::Processing::NoiseAwareRangeWeight(
        0.20f,
        0.22f,
        1.0e-4f,
        1.0e-4f,
        1.0f);
    const float permissiveWeight = Raw::Processing::NoiseAwareRangeWeight(
        0.20f,
        0.22f,
        1.0e-4f,
        1.0e-4f,
        0.0f);
    Check(
        highNoiseWeight > lowNoiseWeight &&
            protectedWeight < permissiveWeight &&
            Near(
                Raw::Processing::NoiseAwareRangeWeight(
                    0.20f,
                    0.20f,
                    0.0f,
                    0.0f,
                    1.0f),
                1.0),
        "DENOISE-WEIGHT-01",
        "range weighting must accept expected noise, reject real edges more strongly, and preserve identical samples");

    Raw::RawImageData raw = MakeRaw(4, 4, 0.0f, 1024.0f);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 256);
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = {
        Raw::DngNoiseProfilePlane { 0.01, 0.0001 }
    };
    Raw::DngGainMapOpcode gainMap;
    gainMap.top = 0;
    gainMap.left = 0;
    gainMap.bottom = 4;
    gainMap.right = 4;
    gainMap.plane = 0;
    gainMap.planes = 1;
    gainMap.rowPitch = 1;
    gainMap.colPitch = 1;
    gainMap.mapPointsV = 1;
    gainMap.mapPointsH = 1;
    gainMap.mapPlanes = 1;
    gainMap.mapSpacingV = 1.0;
    gainMap.mapSpacingH = 1.0;
    gainMap.mapOriginV = 0.0;
    gainMap.mapOriginH = 0.0;
    gainMap.gains = { 2.0f };
    raw.metadata.dngGainMaps = { gainMap };
    raw.metadata.dngGainMapCount = 1;

    Raw::RawDevelopSettings settings;
    settings.processingVersion = Raw::RawProcessingVersion::TruthfulV2;
    std::vector<float> variance;
    std::string error;
    const double expectedGainMappedVariance =
        (0.01 * 0.25 + 0.0001) * 4.0;
    Check(
        Raw::Processing::BuildTruthfulNoiseVarianceMosaic(
            raw,
            settings,
            variance,
            &error) &&
            variance.size() == raw.rawBuffer.size() &&
            Near(variance.front(), expectedGainMappedVariance),
        "DENOISE-GAIN-01",
        "OpcodeList2 gain must scale DNG noise variance by gain squared");

    raw.metadata.hasDngNoiseProfile = false;
    raw.metadata.dngNoiseProfile.clear();
    variance.assign(1, 1.0f);
    Check(
        !Raw::Processing::BuildTruthfulNoiseVarianceMosaic(
            raw,
            settings,
            variance,
            &error) &&
            variance.empty(),
        "DENOISE-FALLBACK-01",
        "missing DNG noise metadata must produce an explicit fallback instead of zero-noise coefficients");
}

void TestDefensiveStateAndCache() {
    Raw::RawImageData raw = MakeRaw(6, 6);
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), 544);
    const std::vector<std::uint16_t> beforePixels = raw.rawBuffer;
    const float beforeBlack = raw.metadata.blackLevel;
    auto record = Build(raw, "state");
    Check(raw.rawBuffer == beforePixels && raw.metadata.blackLevel == beforeBlack,
        "P01-STATE-01", "evidence build must not mutate decoded source state");
    const nlohmann::json serialized = Stack::RawEvidence::SerializeRawTechnicalEvidence(record);
    Check(serialized.value("schemaVersion", 0) == 1 &&
        serialized["planes"][0]["p999"].contains("units") &&
        serialized["planes"][0]["p999"].contains("stage") &&
        serialized["planes"][0]["p999"].contains("provenance") &&
        serialized["planes"][0]["p999"].contains("uncertainty01"),
        "P01-META-01", "serialized measurements require units, stage, provenance, and uncertainty");

    Stack::RawEvidence::BuildOptions invalid;
    invalid.decode.overrideWhiteLevel = true;
    invalid.decode.whiteLevelOverride = std::numeric_limits<double>::quiet_NaN();
    invalid.measureDefectivePixels = false;
    record = Stack::RawEvidence::BuildRawTechnicalEvidence(raw, FixtureIdentity("nan"), invalid);
    Check(!record.valid && !record.planes[0].whiteLevel.valid,
        "P01-DEFENSIVE-01", "NaN metadata must fail explicitly");

    Stack::RawEvidence::BuildOptions canceled;
    canceled.shouldCancel = [] { return true; };
    canceled.measureDefectivePixels = false;
    record = Stack::RawEvidence::BuildRawTechnicalEvidence(raw, FixtureIdentity("cancel"), canceled);
    Check(!record.valid && std::find(record.warnings.begin(), record.warnings.end(), "canceled") != record.warnings.end(),
        "P01-STATE-01", "cancellation must return no stale successful evidence");

    Stack::RawEvidence::RawTechnicalEvidenceCache cache;
    Stack::RawEvidence::BuildOptions options;
    options.measureDefectivePixels = false;
    const auto firstIdentity = FixtureIdentity("cache-a");
    const auto& first = cache.GetOrBuild(raw, firstIdentity, options);
    const std::string firstKey = first.evidenceIdentitySha256;
    const auto& repeat = cache.GetOrBuild(raw, firstIdentity, options);
    Check(cache.Size() == 1 && repeat.evidenceIdentitySha256 == firstKey,
        "P01-STATE-01", "same source/decode must reuse deterministic evidence");
    const auto& changed = cache.GetOrBuild(raw, FixtureIdentity("cache-b"), options);
    Check(cache.Size() == 2 && changed.evidenceIdentitySha256 != firstKey,
        "P01-STATE-01", "source identity change must invalidate cache key");
    options.decode.overrideBlackLevel = true;
    options.decode.blackLevelOverride = 32.0;
    cache.GetOrBuild(raw, firstIdentity, options);
    Check(cache.Size() == 3, "P01-DECODE-01", "decode identity change must invalidate cache key");
}

} // namespace

int main() {
    TestIdentity();
    TestDecodeIdentity();
    TestNormalization();
    TestTruthfulProcessingMath();
    TestAreasAndOrientation();
    TestClippingAndHeadroom();
    TestNoiseAndMetadata();
    TestNoiseAwareFastDenoiseMath();
    TestDefensiveStateAndCache();
    if (g_Failures != 0) {
        std::cerr << "RAW evidence fixture suite failed with " << g_Failures << " assertion(s).\n";
        return 1;
    }
    std::cout << "RAW evidence fixture suite passed (Phase 01 schema v1).\n";
    return 0;
}
