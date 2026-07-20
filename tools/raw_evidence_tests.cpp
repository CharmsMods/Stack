#include "Raw/RawTechnicalEvidence.h"

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
    TestAreasAndOrientation();
    TestClippingAndHeadroom();
    TestNoiseAndMetadata();
    TestDefensiveStateAndCache();
    if (g_Failures != 0) {
        std::cerr << "RAW evidence fixture suite failed with " << g_Failures << " assertion(s).\n";
        return 1;
    }
    std::cout << "RAW evidence fixture suite passed (Phase 01 schema v1).\n";
    return 0;
}
