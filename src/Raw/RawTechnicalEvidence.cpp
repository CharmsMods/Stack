#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

namespace Stack::RawEvidence {
namespace {

constexpr double kEpsilon = 1.0e-12;

class Sha256 {
public:
    void Update(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        m_TotalBytes += size;
        while (size > 0) {
            const std::size_t count = std::min(size, m_Block.size() - m_BlockBytes);
            std::memcpy(m_Block.data() + m_BlockBytes, bytes, count);
            m_BlockBytes += count;
            bytes += count;
            size -= count;
            if (m_BlockBytes == m_Block.size()) {
                Transform(m_Block.data());
                m_BlockBytes = 0;
            }
        }
    }

    std::string Finish() {
        const std::uint64_t bitCount = static_cast<std::uint64_t>(m_TotalBytes) * 8u;
        const std::uint8_t marker = 0x80;
        Update(&marker, 1);
        const std::uint8_t zero = 0;
        while (m_BlockBytes != 56) {
            Update(&zero, 1);
        }
        std::array<std::uint8_t, 8> length {};
        for (int i = 0; i < 8; ++i) {
            length[static_cast<std::size_t>(7 - i)] =
                static_cast<std::uint8_t>((bitCount >> (i * 8)) & 0xffu);
        }
        Update(length.data(), length.size());

        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (std::uint32_t word : m_State) {
            stream << std::setw(8) << word;
        }
        return stream.str();
    }

private:
    static std::uint32_t RotateRight(std::uint32_t value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void Transform(const std::uint8_t* block) {
        static constexpr std::array<std::uint32_t, 64> k {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
        std::array<std::uint32_t, 64> w {};
        for (int i = 0; i < 16; ++i) {
            const std::size_t p = static_cast<std::size_t>(i) * 4;
            w[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(block[p]) << 24) |
                (static_cast<std::uint32_t>(block[p + 1]) << 16) |
                (static_cast<std::uint32_t>(block[p + 2]) << 8) |
                static_cast<std::uint32_t>(block[p + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = RotateRight(w[static_cast<std::size_t>(i - 15)], 7) ^
                RotateRight(w[static_cast<std::size_t>(i - 15)], 18) ^
                (w[static_cast<std::size_t>(i - 15)] >> 3);
            const std::uint32_t s1 = RotateRight(w[static_cast<std::size_t>(i - 2)], 17) ^
                RotateRight(w[static_cast<std::size_t>(i - 2)], 19) ^
                (w[static_cast<std::size_t>(i - 2)] >> 10);
            w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i - 16)] + s0 +
                w[static_cast<std::size_t>(i - 7)] + s1;
        }

        std::uint32_t a = m_State[0];
        std::uint32_t b = m_State[1];
        std::uint32_t c = m_State[2];
        std::uint32_t d = m_State[3];
        std::uint32_t e = m_State[4];
        std::uint32_t f = m_State[5];
        std::uint32_t g = m_State[6];
        std::uint32_t h = m_State[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = h + s1 + choice + k[static_cast<std::size_t>(i)] + w[static_cast<std::size_t>(i)];
            const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        m_State[0] += a;
        m_State[1] += b;
        m_State[2] += c;
        m_State[3] += d;
        m_State[4] += e;
        m_State[5] += f;
        m_State[6] += g;
        m_State[7] += h;
    }

    std::array<std::uint32_t, 8> m_State {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    std::array<std::uint8_t, 64> m_Block {};
    std::size_t m_BlockBytes = 0;
    std::size_t m_TotalBytes = 0;
};

std::string Sha256String(const std::string& text) {
    Sha256 hash;
    hash.Update(text.data(), text.size());
    return hash.Finish();
}

EvidenceMeasurement Measurement(
    double value,
    std::string units,
    EvidenceProvenance provenance,
    double uncertainty01,
    std::string reason = {}) {
    EvidenceMeasurement measurement;
    measurement.valid = std::isfinite(value);
    measurement.value = measurement.valid ? value : 0.0;
    measurement.units = std::move(units);
    measurement.provenance = measurement.valid ? provenance : EvidenceProvenance::Unavailable;
    measurement.uncertainty01 = measurement.valid
        ? std::clamp(uncertainty01, 0.0, 1.0)
        : 1.0;
    measurement.reason = measurement.valid ? std::move(reason) : "non-finite-value";
    return measurement;
}

EvidenceMeasurement Unavailable(std::string units, std::string reason) {
    EvidenceMeasurement measurement;
    measurement.units = std::move(units);
    measurement.reason = std::move(reason);
    return measurement;
}

bool RectValid(const Raw::RawSensorRect& rect, int width, int height) {
    return rect.top >= 0 && rect.left >= 0 && rect.bottom > rect.top &&
        rect.right > rect.left && rect.bottom <= height && rect.right <= width;
}

bool InRect(int x, int y, const Raw::RawSensorRect& rect) {
    return y >= rect.top && y < rect.bottom && x >= rect.left && x < rect.right;
}

bool InAnyRect(int x, int y, const std::vector<Raw::RawSensorRect>& rects) {
    return std::any_of(rects.begin(), rects.end(), [x, y](const Raw::RawSensorRect& rect) {
        return InRect(x, y, rect);
    });
}

int PositiveModulo(int value, int modulus) {
    if (modulus <= 0) return 0;
    const int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int PatternColor(Raw::CfaPattern pattern, int x, int y) {
    const int index = PositiveModulo(y, 2) * 2 + PositiveModulo(x, 2);
    switch (pattern) {
        case Raw::CfaPattern::RGGB: return std::array<int, 4>{ 0, 1, 1, 2 }[static_cast<std::size_t>(index)];
        case Raw::CfaPattern::BGGR: return std::array<int, 4>{ 2, 1, 1, 0 }[static_cast<std::size_t>(index)];
        case Raw::CfaPattern::GBRG: return std::array<int, 4>{ 1, 2, 0, 1 }[static_cast<std::size_t>(index)];
        case Raw::CfaPattern::GRBG: return std::array<int, 4>{ 1, 0, 2, 1 }[static_cast<std::size_t>(index)];
        default: return -1;
    }
}

int ColorAt(const Raw::RawMetadata& metadata, const Raw::RawSensorRect& active, int x, int y) {
    if (metadata.dngCfaRepeatPatternDim[0] == 2 && metadata.dngCfaRepeatPatternDim[1] == 2) {
        const int patternIndex = PositiveModulo(y - active.top, 2) * 2 +
            PositiveModulo(x - active.left, 2);
        const int plane = metadata.dngCfaPattern[static_cast<std::size_t>(patternIndex)];
        if (plane >= 0 && plane < static_cast<int>(metadata.dngCfaPlaneColor.size())) {
            const int color = metadata.dngCfaPlaneColor[static_cast<std::size_t>(plane)];
            if (color >= 0 && color < 3) return color;
        }
    }
    return PatternColor(metadata.cfaPattern, x - active.left, y - active.top);
}

double BlackAt(
    const Raw::RawMetadata& metadata,
    const Raw::RawSensorRect& active,
    int x,
    int y,
    int color,
    const DecodeIdentityOptions& options) {
    if (options.overrideBlackLevel) return options.blackLevelOverride;
    double black = color >= 0 && color < 3
        ? metadata.perChannelBlack[static_cast<std::size_t>(color)]
        : metadata.blackLevel;
    const int repeatRows = metadata.dngBlackLevelRepeatDim[0];
    const int repeatCols = metadata.dngBlackLevelRepeatDim[1];
    if (repeatRows > 0 && repeatCols > 0 && !metadata.dngBlackLevelValues.empty()) {
        const std::size_t index = static_cast<std::size_t>(PositiveModulo(y - active.top, repeatRows)) *
            static_cast<std::size_t>(repeatCols) +
            static_cast<std::size_t>(PositiveModulo(x - active.left, repeatCols));
        if (index < metadata.dngBlackLevelValues.size()) {
            black = metadata.dngBlackLevelValues[index];
        }
    }
    const int localX = x - active.left;
    const int localY = y - active.top;
    if (localX >= 0 && localX < static_cast<int>(metadata.dngBlackLevelDeltaH.size())) {
        black += metadata.dngBlackLevelDeltaH[static_cast<std::size_t>(localX)];
    }
    if (localY >= 0 && localY < static_cast<int>(metadata.dngBlackLevelDeltaV.size())) {
        black += metadata.dngBlackLevelDeltaV[static_cast<std::size_t>(localY)];
    }
    return black;
}

double WhiteForColor(
    const Raw::RawMetadata& metadata,
    int color,
    const DecodeIdentityOptions& options) {
    if (options.overrideWhiteLevel) return options.whiteLevelOverride;
    if (metadata.dngWhiteLevelValues.size() == 1) return metadata.dngWhiteLevelValues.front();
    if (color >= 0 && color < static_cast<int>(metadata.dngWhiteLevelValues.size())) {
        return metadata.dngWhiteLevelValues[static_cast<std::size_t>(color)];
    }
    return metadata.whiteLevel;
}

double LinearizedValue(
    const Raw::RawMetadata& metadata,
    std::uint16_t stored,
    const DecodeIdentityOptions& options,
    bool& valid) {
    if (!options.applyLinearizationTable || metadata.dngLinearizationTable.empty()) {
        return stored;
    }
    if (stored >= metadata.dngLinearizationTable.size()) {
        valid = false;
        return 0.0;
    }
    return metadata.dngLinearizationTable[stored];
}

double Quantile(std::vector<double>& values, double q) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const double position = q * static_cast<double>(values.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = static_cast<std::size_t>(std::ceil(position));
    const double t = position - static_cast<double>(low);
    return values[low] * (1.0 - t) + values[high] * t;
}

double Median(std::vector<double> values) {
    return Quantile(values, 0.5);
}

std::string OrientationTransform(int orientation) {
    switch (orientation) {
        case 1: return "identity";
        case 2: return "mirror-horizontal";
        case 3: return "rotate-180";
        case 4: return "mirror-vertical";
        case 5: return "transpose";
        case 6: return "rotate-90-cw";
        case 7: return "transverse";
        case 8: return "rotate-270-cw";
        default: return "decoder-orientation-unspecified";
    }
}

std::string PlaneName(int plane) {
    return plane == 0 ? "R" : (plane == 1 ? "G" : (plane == 2 ? "B" : "unknown"));
}

nlohmann::json SerializeMeasurement(const EvidenceMeasurement& measurement) {
    return {
        { "valid", measurement.valid },
        { "value", measurement.valid ? nlohmann::json(measurement.value) : nlohmann::json(nullptr) },
        { "units", measurement.units },
        { "stage", measurement.stage },
        { "provenance", EvidenceProvenanceName(measurement.provenance) },
        { "uncertainty01", measurement.uncertainty01 },
        { "reason", measurement.reason }
    };
}

nlohmann::json SerializeRect(const Raw::RawSensorRect& rect) {
    return {
        { "top", rect.top }, { "left", rect.left },
        { "bottom", rect.bottom }, { "right", rect.right }
    };
}

} // namespace

SourceIdentity ComputeSourceIdentity(const std::filesystem::path& path) {
    SourceIdentity identity;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        identity.reason = "source-open-failed";
        return identity;
    }
    Sha256 hash;
    std::array<char, 64 * 1024> buffer {};
    while (file.good()) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0) {
            hash.Update(buffer.data(), static_cast<std::size_t>(count));
            identity.byteSize += static_cast<std::uint64_t>(count);
        }
    }
    if (!file.eof()) {
        identity.reason = "source-read-failed";
        return identity;
    }
    identity.sha256 = hash.Finish();
    identity.valid = true;
    identity.reason = "content-sha256";
    return identity;
}

SourceIdentity ComputeSourceIdentity(const std::vector<std::uint8_t>& bytes) {
    SourceIdentity identity;
    Sha256 hash;
    hash.Update(bytes.data(), bytes.size());
    identity.valid = true;
    identity.sha256 = hash.Finish();
    identity.byteSize = static_cast<std::uint64_t>(bytes.size());
    identity.reason = "content-sha256";
    return identity;
}

DecodeIdentity BuildDecodeIdentity(
    const Raw::RawMetadata& metadata,
    const DecodeIdentityOptions& options) {
    DecodeIdentity identity;
    std::ostringstream fields;
    fields << "schema=1"
        << "|backend=" << options.decoderBackend
        << "|backendVersion=" << options.decoderVersion
        << "|normalization=" << options.normalizationVersion
        << "|linearization=" << options.applyLinearizationTable
        << "|activeArea=" << options.useActiveArea
        << "|maskedAreas=" << options.useMaskedAreas
        << "|blackOverride=" << options.overrideBlackLevel
        << "|blackValue=" << std::setprecision(17) << options.blackLevelOverride
        << "|whiteOverride=" << options.overrideWhiteLevel
        << "|whiteValue=" << std::setprecision(17) << options.whiteLevelOverride
        << "|opcode1Applied=" << options.opcodeList1Applied
        << "|opcode2GainApplied=" << options.opcodeList2GainMapsApplied
        << "|profileGainApplied=" << options.profileGainTableMapApplied
        << "|layout=" << Raw::RawPixelLayoutName(metadata.pixelLayout)
        << "|sampleFormat=" << (metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer
            ? "UInt16"
            : Raw::RawSampleFormatName(metadata.linearSampleFormat))
        << "|rawSize=" << metadata.rawWidth << 'x' << metadata.rawHeight
        << "|cfa=" << Raw::CfaPatternName(metadata.cfaPattern);
    identity.canonicalFields = fields.str();
    identity.sha256 = Sha256String(identity.canonicalFields);
    identity.valid = !options.decoderBackend.empty() && !options.normalizationVersion.empty();
    identity.reason = identity.valid ? "canonical-decode-fields" : "missing-decode-identity-field";
    return identity;
}

RawTechnicalEvidenceRecord BuildRawTechnicalEvidence(
    const Raw::RawImageData& raw,
    const SourceIdentity& sourceIdentity,
    const BuildOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    RawTechnicalEvidenceRecord record;
    record.sourceIdentity = sourceIdentity;
    record.decodeIdentity = BuildDecodeIdentity(raw.metadata, options.decode);
    record.evidenceIdentitySha256 = Sha256String(
        record.sourceIdentity.sha256 + "|" + record.decodeIdentity.sha256 + "|" + record.featureVersion);
    record.pixelLayout = Raw::RawPixelLayoutName(raw.metadata.pixelLayout);
    record.sampleFormat = raw.metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer
        ? "UInt16"
        : Raw::RawSampleFormatName(raw.metadata.linearSampleFormat);
    record.cfaPattern = Raw::CfaPatternName(raw.metadata.cfaPattern);
    record.geometry.orientation = raw.metadata.orientation;
    record.geometry.photographicTransform = OrientationTransform(raw.metadata.orientation);

    const int width = raw.metadata.rawWidth;
    const int height = raw.metadata.rawHeight;
    if (!record.sourceIdentity.valid || !record.decodeIdentity.valid) {
        record.statusMessage = "Source or decode identity is unavailable.";
        return record;
    }
    if (raw.metadata.pixelLayout != Raw::RawPixelLayout::MosaicBayer ||
        width <= 0 || height <= 0 ||
        raw.rawBuffer.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        record.statusMessage = "Normalized mosaic evidence is unavailable for this decoded layout.";
        record.warnings.push_back("raw-mosaic-buffer-unavailable");
        return record;
    }

    Raw::RawSensorRect active;
    if (options.decode.useActiveArea &&
        raw.metadata.hasDngActiveArea &&
        RectValid(raw.metadata.dngActiveArea, width, height)) {
        active = raw.metadata.dngActiveArea;
        record.geometry.activeAreaFromMetadata = true;
    } else {
        active.top = std::max(0, raw.metadata.topMargin);
        active.left = std::max(0, raw.metadata.leftMargin);
        active.bottom = std::min(height, active.top + std::max(0, raw.metadata.visibleHeight));
        active.right = std::min(width, active.left + std::max(0, raw.metadata.visibleWidth));
        if (!RectValid(active, width, height)) active = { 0, 0, height, width };
        record.warnings.push_back("active-area-fallback-visible-rectangle");
    }
    record.geometry.activeArea = active;
    if (options.decode.useMaskedAreas) {
        for (const Raw::RawSensorRect& rect : raw.metadata.dngMaskedAreas) {
            if (RectValid(rect, width, height)) record.geometry.maskedAreas.push_back(rect);
        }
    }
    record.geometry.maskedAreasFromMetadata = !record.geometry.maskedAreas.empty();

    const std::uint64_t activePixels = static_cast<std::uint64_t>(active.bottom - active.top) *
        static_cast<std::uint64_t>(active.right - active.left);
    std::uint64_t maskedInsideActive = 0;
    for (const Raw::RawSensorRect& rect : record.geometry.maskedAreas) {
        const int top = std::max(active.top, rect.top);
        const int left = std::max(active.left, rect.left);
        const int bottom = std::min(active.bottom, rect.bottom);
        const int right = std::min(active.right, rect.right);
        if (bottom > top && right > left) {
            maskedInsideActive += static_cast<std::uint64_t>(bottom - top) *
                static_cast<std::uint64_t>(right - left);
        }
    }
    record.geometry.activeValidFraction = activePixels > 0
        ? static_cast<double>(activePixels - maskedInsideActive) / static_cast<double>(activePixels)
        : 0.0;

    record.metadataCoverage.hasLinearizationTable = !raw.metadata.dngLinearizationTable.empty();
    record.metadataCoverage.hasSpatialBlackPattern = !raw.metadata.dngBlackLevelValues.empty();
    record.metadataCoverage.hasBlackDeltaH = !raw.metadata.dngBlackLevelDeltaH.empty();
    record.metadataCoverage.hasBlackDeltaV = !raw.metadata.dngBlackLevelDeltaV.empty();
    record.metadataCoverage.hasActiveArea = raw.metadata.hasDngActiveArea;
    record.metadataCoverage.hasMaskedAreas = !raw.metadata.dngMaskedAreas.empty();
    record.metadataCoverage.hasLinearResponseLimit = raw.metadata.hasDngLinearResponseLimit;
    record.metadataCoverage.hasAsShotNeutral = raw.metadata.hasDngAsShotNeutral;
    record.metadataCoverage.hasBaselineExposure = raw.metadata.hasDngBaselineExposure;
    record.metadataCoverage.hasNoiseProfile = raw.metadata.hasDngNoiseProfile;
    record.metadataCoverage.opcodeCount = raw.metadata.dngOpcodeCount;
    record.metadataCoverage.appliedOpcodeCount = {
        options.decode.opcodeList1Applied ? raw.metadata.dngOpcodeCount[0] : 0,
        options.decode.opcodeList2GainMapsApplied ? raw.metadata.dngGainMapCount : 0,
        0
    };
    record.metadataCoverage.unsupportedOpcodeCount = raw.metadata.dngUnsupportedOpcodeCountByList;
    record.metadataCoverage.hasOpcodeList1 = raw.metadata.dngOpcodeCount[0] > 0;
    record.metadataCoverage.hasOpcodeList2 = raw.metadata.dngOpcodeCount[1] > 0;
    record.metadataCoverage.hasOpcodeList3 = raw.metadata.dngOpcodeCount[2] > 0;
    record.metadataCoverage.hasOpcodeList2GainMap = raw.metadata.dngGainMapCount > 0;
    record.metadataCoverage.hasProfileGainTableMap = raw.metadata.hasDngProfileGainTableMap;
    record.metadataCoverage.hasProfileGainTableMap2 = raw.metadata.hasDngProfileGainTableMap2;
    if (!raw.metadata.hasDngLinearResponseLimit) record.metadataCoverage.omissions.push_back("LinearResponseLimit");
    if (!raw.metadata.hasDngNoiseProfile) record.metadataCoverage.omissions.push_back("NoiseProfile");
    if (!raw.metadata.hasDngActiveArea) record.metadataCoverage.omissions.push_back("ActiveArea");
    if (raw.metadata.dngMaskedAreas.empty()) record.metadataCoverage.omissions.push_back("MaskedAreas");
    if (raw.metadata.dngOpcodeCount[0] > record.metadataCoverage.appliedOpcodeCount[0]) {
        record.metadataCoverage.omissions.push_back("OpcodeList1-not-applied-to-evidence");
    }
    if (raw.metadata.dngOpcodeCount[1] > record.metadataCoverage.appliedOpcodeCount[1]) {
        record.metadataCoverage.omissions.push_back("OpcodeList2-not-applied-to-evidence");
    }
    if (raw.metadata.dngOpcodeCount[2] > record.metadataCoverage.appliedOpcodeCount[2]) {
        record.metadataCoverage.omissions.push_back("OpcodeList3-not-applied-to-evidence");
    }
    if (raw.metadata.hasDngProfileGainTableMap || raw.metadata.hasDngProfileGainTableMap2) {
        record.metadataCoverage.omissions.push_back("ProfileGainTableMap-reported-not-applied");
    }

    std::array<double, 3> maxBlack {
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    const int repeatRows = std::max(1, raw.metadata.dngBlackLevelRepeatDim[0]);
    const int repeatCols = std::max(1, raw.metadata.dngBlackLevelRepeatDim[1]);
    const double missingDelta = -std::numeric_limits<double>::infinity();
    std::vector<double> maxDeltaHByClass(
        static_cast<std::size_t>(repeatCols),
        raw.metadata.dngBlackLevelDeltaH.empty() ? 0.0 : missingDelta);
    for (std::size_t x = 0; x < raw.metadata.dngBlackLevelDeltaH.size(); ++x) {
        const float delta = raw.metadata.dngBlackLevelDeltaH[x];
        if (std::isfinite(delta)) {
            const std::size_t columnClass = x % static_cast<std::size_t>(repeatCols);
            maxDeltaHByClass[columnClass] = std::max(maxDeltaHByClass[columnClass], static_cast<double>(delta));
        }
    }
    for (double& value : maxDeltaHByClass) if (!std::isfinite(value)) value = 0.0;
    std::vector<double> maxDeltaVByClass(
        static_cast<std::size_t>(repeatRows),
        raw.metadata.dngBlackLevelDeltaV.empty() ? 0.0 : missingDelta);
    for (std::size_t y = 0; y < raw.metadata.dngBlackLevelDeltaV.size(); ++y) {
        const float delta = raw.metadata.dngBlackLevelDeltaV[y];
        if (std::isfinite(delta)) {
            const std::size_t rowClass = y % static_cast<std::size_t>(repeatRows);
            maxDeltaVByClass[rowClass] = std::max(maxDeltaVByClass[rowClass], static_cast<double>(delta));
        }
    }
    for (double& value : maxDeltaVByClass) if (!std::isfinite(value)) value = 0.0;
    for (int y = 0; y < repeatRows; ++y) {
        for (int x = 0; x < repeatCols; ++x) {
            const int color = ColorAt(raw.metadata, active, active.left + x, active.top + y);
            if (color >= 0 && color < 3) {
                double baseBlack = options.decode.overrideBlackLevel
                    ? options.decode.blackLevelOverride
                    : raw.metadata.perChannelBlack[static_cast<std::size_t>(color)];
                if (!options.decode.overrideBlackLevel &&
                    !raw.metadata.dngBlackLevelValues.empty()) {
                    const std::size_t patternIndex = static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(repeatCols) + static_cast<std::size_t>(x);
                    if (patternIndex < raw.metadata.dngBlackLevelValues.size()) {
                        baseBlack = raw.metadata.dngBlackLevelValues[patternIndex];
                    }
                }
                if (!options.decode.overrideBlackLevel) {
                    baseBlack += maxDeltaHByClass[static_cast<std::size_t>(x)] +
                        maxDeltaVByClass[static_cast<std::size_t>(y)];
                }
                maxBlack[static_cast<std::size_t>(color)] = std::max(
                    maxBlack[static_cast<std::size_t>(color)],
                    baseBlack);
            }
        }
    }
    for (int color = 0; color < 3; ++color) {
        if (!std::isfinite(maxBlack[static_cast<std::size_t>(color)])) {
            maxBlack[static_cast<std::size_t>(color)] = options.decode.overrideBlackLevel
                ? options.decode.blackLevelOverride
                : raw.metadata.perChannelBlack[static_cast<std::size_t>(color)];
        }
    }
    auto normalizeAt = [&](int x, int y, int color, bool& valid) {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x);
        const double linear = LinearizedValue(raw.metadata, raw.rawBuffer[index], options.decode, valid);
        const double white = WhiteForColor(raw.metadata, color, options.decode);
        const double denominator = white - maxBlack[static_cast<std::size_t>(color)];
        if (!std::isfinite(linear) || !std::isfinite(denominator) || denominator <= kEpsilon) {
            valid = false;
            return 0.0;
        }
        const double black = BlackAt(raw.metadata, active, x, y, color, options.decode);
        const double value = (linear - black) / denominator;
        if (!std::isfinite(value)) valid = false;
        return valid ? value : 0.0;
    };

    std::array<std::vector<double>, 3> values;
    std::array<std::uint64_t, 3> nearNonlinear {};
    std::array<std::uint64_t, 3> nearWhite {};
    std::array<std::uint64_t, 3> clipped {};
    const std::size_t maxSamples = std::max<std::size_t>(1, options.maxSamples);
    std::uint64_t stride = std::max<std::uint64_t>(1, activePixels / maxSamples);
    if ((stride & 1u) == 0u) ++stride;
    std::uint64_t linearIndex = 0;
    std::uint64_t invalidSamples = 0;
    const double responseLimit = raw.metadata.hasDngLinearResponseLimit &&
        std::isfinite(raw.metadata.dngLinearResponseLimit) &&
        raw.metadata.dngLinearResponseLimit > 0.0f && raw.metadata.dngLinearResponseLimit <= 1.0f
        ? raw.metadata.dngLinearResponseLimit
        : 1.0;
    for (int y = active.top; y < active.bottom; ++y) {
        if (options.shouldCancel && options.shouldCancel()) {
            record.statusMessage = "Raw technical evidence canceled.";
            record.warnings.push_back("canceled");
            return record;
        }
        for (int x = active.left; x < active.right; ++x, ++linearIndex) {
            if ((linearIndex % stride) != 0 || InAnyRect(x, y, record.geometry.maskedAreas)) continue;
            const int color = ColorAt(raw.metadata, active, x, y);
            if (color < 0 || color >= 3) {
                ++invalidSamples;
                continue;
            }
            bool valid = true;
            const double normalized = normalizeAt(x, y, color, valid);
            if (!valid) {
                ++invalidSamples;
                continue;
            }
            values[static_cast<std::size_t>(color)].push_back(normalized);
            if (raw.metadata.hasDngLinearResponseLimit &&
                normalized >= responseLimit - options.nearNonlinearMargin) {
                ++nearNonlinear[static_cast<std::size_t>(color)];
            }
            if (normalized >= 1.0 - options.nearWhiteMargin) ++nearWhite[static_cast<std::size_t>(color)];
            if (normalized >= 1.0) ++clipped[static_cast<std::size_t>(color)];
        }
    }

    std::array<double, 3> wb { 1.0, 1.0, 1.0 };
    bool wbValid = !raw.metadata.whiteBalanceSource.empty();
    for (int color = 0; color < 3; ++color) {
        const double value = raw.metadata.cameraWhiteBalance[static_cast<std::size_t>(color)];
        wb[static_cast<std::size_t>(color)] = value;
        wbValid = wbValid && std::isfinite(value) && value > kEpsilon;
    }
    if (wbValid) {
        const double geometricMean = std::cbrt(wb[0] * wb[1] * wb[2]);
        for (double& value : wb) value /= geometricMean;
    }

    double minimumWbHeadroom = std::numeric_limits<double>::infinity();
    for (int color = 0; color < 3; ++color) {
        PlaneEvidence plane;
        plane.plane = color;
        plane.planeName = PlaneName(color);
        plane.sampleCount = values[static_cast<std::size_t>(color)].size();
        plane.blackMaximum = Measurement(maxBlack[static_cast<std::size_t>(color)], "stored-code-value", EvidenceProvenance::Derived, 0.1);
        const double white = WhiteForColor(raw.metadata, color, options.decode);
        plane.whiteLevel = std::isfinite(white) && white > maxBlack[static_cast<std::size_t>(color)]
            ? Measurement(white, "stored-code-value", EvidenceProvenance::Metadata, 0.05, raw.metadata.whiteLevelSource)
            : Unavailable("stored-code-value", "invalid-white-level");
        plane.linearResponseLimit = raw.metadata.hasDngLinearResponseLimit
            ? Measurement(responseLimit, "normalized-linear-reference", EvidenceProvenance::Metadata, 0.05, "DNG LinearResponseLimit")
            : Unavailable("normalized-linear-reference", "LinearResponseLimit-missing; white-level-is-outer-limit");
        if (!values[static_cast<std::size_t>(color)].empty()) {
            const double p999 = Quantile(values[static_cast<std::size_t>(color)], 0.999);
            const double count = static_cast<double>(values[static_cast<std::size_t>(color)].size());
            plane.p999 = Measurement(p999, "normalized-linear-reference", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / count));
            plane.nearNonlinearFraction = raw.metadata.hasDngLinearResponseLimit
                ? Measurement(static_cast<double>(nearNonlinear[static_cast<std::size_t>(color)]) / count, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / count))
                : Unavailable("fraction", "LinearResponseLimit-missing");
            plane.nearWhiteFraction = Measurement(static_cast<double>(nearWhite[static_cast<std::size_t>(color)]) / count, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / count));
            plane.clippedFraction = Measurement(static_cast<double>(clipped[static_cast<std::size_t>(color)]) / count, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / count));
            const double headroom = std::log2(responseLimit / std::max(kEpsilon, p999));
            plane.headroomEv = Measurement(headroom, "EV", EvidenceProvenance::Derived, plane.p999.uncertainty01);
            if (wbValid) {
                plane.wbMultiplier = Measurement(
                    wb[static_cast<std::size_t>(color)],
                    "relative-gain-geometric-mean-1",
                    raw.metadata.hasDngAsShotNeutral ? EvidenceProvenance::Metadata : EvidenceProvenance::Fallback,
                    raw.metadata.hasDngAsShotNeutral ? 0.1 : 0.3,
                    raw.metadata.whiteBalanceSource);
                const double wbHeadroom = std::log2(responseLimit /
                    std::max(kEpsilon, p999 * wb[static_cast<std::size_t>(color)]));
                plane.wbScaledHeadroomEv = Measurement(wbHeadroom, "EV", EvidenceProvenance::Derived, plane.p999.uncertainty01 + 0.05);
                if (wbHeadroom < minimumWbHeadroom) {
                    minimumWbHeadroom = wbHeadroom;
                    record.limitingPlane = plane.planeName;
                }
            } else {
                plane.wbMultiplier = Unavailable("relative-gain-geometric-mean-1", "technical-as-shot-WB-unavailable");
                plane.wbScaledHeadroomEv = Unavailable("EV", "technical-as-shot-WB-unavailable");
            }
        } else {
            plane.p999 = Unavailable("normalized-linear-reference", "no-valid-plane-samples");
            plane.nearNonlinearFraction = Unavailable("fraction", "no-valid-plane-samples");
            plane.nearWhiteFraction = Unavailable("fraction", "no-valid-plane-samples");
            plane.clippedFraction = Unavailable("fraction", "no-valid-plane-samples");
            plane.headroomEv = Unavailable("EV", "no-valid-plane-samples");
            plane.wbMultiplier = Unavailable("relative-gain-geometric-mean-1", "no-valid-plane-samples");
            plane.wbScaledHeadroomEv = Unavailable("EV", "no-valid-plane-samples");
        }
        record.sampledRawValues += plane.sampleCount;
        record.planes.push_back(std::move(plane));
    }
    if (invalidSamples > 0) record.warnings.push_back("invalid-normalization-samples=" + std::to_string(invalidSamples));

    std::uint64_t cells = 0;
    std::array<std::uint64_t, 4> clipClass {};
    const std::uint64_t possibleCells = static_cast<std::uint64_t>((active.bottom - active.top) / 2) *
        static_cast<std::uint64_t>((active.right - active.left) / 2);
    std::uint64_t cellStride = std::max<std::uint64_t>(1, possibleCells / std::max<std::size_t>(1, maxSamples / 4));
    std::uint64_t cellIndex = 0;
    for (int y = active.top; y + 1 < active.bottom; y += 2) {
        for (int x = active.left; x + 1 < active.right; x += 2, ++cellIndex) {
            if ((cellIndex % cellStride) != 0) continue;
            std::array<double, 3> maxima { -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
            bool cellValid = true;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    if (InAnyRect(x + dx, y + dy, record.geometry.maskedAreas)) {
                        cellValid = false;
                        continue;
                    }
                    const int color = ColorAt(raw.metadata, active, x + dx, y + dy);
                    if (color < 0 || color >= 3) {
                        cellValid = false;
                        continue;
                    }
                    bool valid = true;
                    const double normalized = normalizeAt(x + dx, y + dy, color, valid);
                    cellValid = cellValid && valid;
                    maxima[static_cast<std::size_t>(color)] = std::max(maxima[static_cast<std::size_t>(color)], normalized);
                }
            }
            if (!cellValid || !std::all_of(maxima.begin(), maxima.end(), [](double v) { return std::isfinite(v); })) continue;
            const int clippedPlanes = static_cast<int>(maxima[0] >= 1.0) +
                static_cast<int>(maxima[1] >= 1.0) + static_cast<int>(maxima[2] >= 1.0);
            ++clipClass[static_cast<std::size_t>(clippedPlanes)];
            ++cells;
        }
    }
    record.clipping.superpixelCount = cells;
    record.clipping.valid = cells > 0;
    record.clipping.reason = cells > 0 ? "CFA-aware-2x2-RGB-superpixels" : "no-valid-CFA-superpixels";
    if (cells > 0) {
        const double denominator = static_cast<double>(cells);
        record.clipping.noChannelClippedFraction = Measurement(clipClass[0] / denominator, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / denominator));
        record.clipping.singleChannelClippedFraction = Measurement(clipClass[1] / denominator, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / denominator));
        record.clipping.multiChannelClippedFraction = Measurement(clipClass[2] / denominator, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / denominator));
        record.clipping.allChannelClippedFraction = Measurement(clipClass[3] / denominator, "fraction", EvidenceProvenance::Measured, std::min(0.5, 1000.0 / denominator));
    }

    std::vector<double> maskedValues;
    for (const Raw::RawSensorRect& rect : record.geometry.maskedAreas) {
        const std::uint64_t rectPixels = static_cast<std::uint64_t>(rect.bottom - rect.top) *
            static_cast<std::uint64_t>(rect.right - rect.left);
        const std::uint64_t maskedStride = std::max<std::uint64_t>(1, rectPixels / maxSamples);
        std::uint64_t i = 0;
        for (int y = rect.top; y < rect.bottom; ++y) {
            for (int x = rect.left; x < rect.right; ++x, ++i) {
                if ((i % maskedStride) == 0) {
                    maskedValues.push_back(raw.rawBuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]);
                }
            }
        }
    }
    if (!maskedValues.empty()) {
        const double mean = std::accumulate(maskedValues.begin(), maskedValues.end(), 0.0) /
            static_cast<double>(maskedValues.size());
        double sumSquares = 0.0;
        for (double value : maskedValues) sumSquares += (value - mean) * (value - mean);
        const double stddev = std::sqrt(sumSquares / static_cast<double>(maskedValues.size()));
        record.maskedBlackMean = Measurement(mean, "stored-code-value", EvidenceProvenance::Measured, std::min(0.5, 100.0 / static_cast<double>(maskedValues.size())));
        record.maskedBlackStd = Measurement(stddev, "stored-code-value-standard-deviation", EvidenceProvenance::Measured, record.maskedBlackMean.uncertainty01);
        record.maskedBlackDeltaFromMetadata = Measurement(mean - raw.metadata.blackLevel, "stored-code-value", EvidenceProvenance::Derived, record.maskedBlackMean.uncertainty01);
    } else {
        record.maskedBlackMean = Unavailable("stored-code-value", "MaskedAreas-unavailable");
        record.maskedBlackStd = Unavailable("stored-code-value-standard-deviation", "MaskedAreas-unavailable");
        record.maskedBlackDeltaFromMetadata = Unavailable("stored-code-value", "MaskedAreas-unavailable");
    }

    if (options.measureDefectivePixels) {
        std::uint64_t tested = 0;
        std::uint64_t hot = 0;
        std::uint64_t dead = 0;
        std::uint64_t defectStride = std::max<std::uint64_t>(1, activePixels / std::min<std::size_t>(maxSamples, 100000));
        if ((defectStride & 1u) == 0u) ++defectStride;
        std::uint64_t i = 0;
        for (int y = active.top + 2; y + 2 < active.bottom; ++y) {
            for (int x = active.left + 2; x + 2 < active.right; ++x, ++i) {
                if ((i % defectStride) != 0 || InAnyRect(x, y, record.geometry.maskedAreas)) continue;
                const int color = ColorAt(raw.metadata, active, x, y);
                bool centerValid = true;
                const double center = normalizeAt(x, y, color, centerValid);
                if (!centerValid) continue;
                std::vector<double> neighbors;
                for (int dy = -2; dy <= 2; dy += 2) {
                    for (int dx = -2; dx <= 2; dx += 2) {
                        if ((dx == 0 && dy == 0) || ColorAt(raw.metadata, active, x + dx, y + dy) != color) continue;
                        bool valid = true;
                        const double value = normalizeAt(x + dx, y + dy, color, valid);
                        if (valid) neighbors.push_back(value);
                    }
                }
                if (neighbors.size() < 4) continue;
                const double median = Median(neighbors);
                std::vector<double> deviations;
                deviations.reserve(neighbors.size());
                for (double value : neighbors) deviations.push_back(std::abs(value - median));
                const double sigma = 1.4826 * Median(deviations);
                const double threshold = std::max(0.02, 8.0 * sigma);
                if (center > median + threshold) ++hot;
                if (center < median - threshold) ++dead;
                ++tested;
            }
        }
        if (tested > 0) {
            record.hotPixelFraction = Measurement(static_cast<double>(hot) / tested, "fraction", EvidenceProvenance::Measured, 0.7, "same-CFA robust outlier diagnostic; not a correction mask");
            record.deadPixelFraction = Measurement(static_cast<double>(dead) / tested, "fraction", EvidenceProvenance::Measured, 0.7, "same-CFA robust outlier diagnostic; not a correction mask");
        } else {
            record.hotPixelFraction = Unavailable("fraction", "insufficient-same-CFA-neighborhoods");
            record.deadPixelFraction = Unavailable("fraction", "insufficient-same-CFA-neighborhoods");
        }
    } else {
        record.hotPixelFraction = Unavailable("fraction", "defective-pixel-measurement-disabled");
        record.deadPixelFraction = Unavailable("fraction", "defective-pixel-measurement-disabled");
    }

    if (raw.metadata.hasDngNoiseProfile &&
        (raw.metadata.dngNoiseProfile.size() == 1 || raw.metadata.dngNoiseProfile.size() >= 3)) {
        static constexpr std::array<double, 4> signals { 1.0 / 1024.0, 1.0 / 256.0, 1.0 / 64.0, 0.18 };
        for (int color = 0; color < 3; ++color) {
            std::size_t coefficientIndex = 0;
            if (raw.metadata.dngNoiseProfile.size() > 1) {
                for (std::size_t plane = 0;
                     plane < raw.metadata.dngCfaPlaneColor.size() && plane < raw.metadata.dngNoiseProfile.size();
                     ++plane) {
                    if (raw.metadata.dngCfaPlaneColor[plane] == color) {
                        coefficientIndex = plane;
                        break;
                    }
                }
            }
            const Raw::DngNoiseProfilePlane& coefficients =
                raw.metadata.dngNoiseProfile[coefficientIndex];
            NoisePlaneEvidence plane;
            plane.plane = color;
            plane.planeName = PlaneName(color);
            const bool coefficientsValid = std::isfinite(coefficients.shotScale) &&
                std::isfinite(coefficients.readNoiseVariance) &&
                coefficients.shotScale > 0.0 && coefficients.readNoiseVariance >= 0.0;
            if (coefficientsValid) {
                plane.shotScale = Measurement(coefficients.shotScale, "normalized-variance-per-normalized-signal", EvidenceProvenance::Metadata, 0.05, "DNG NoiseProfile S");
                plane.readNoiseVariance = Measurement(coefficients.readNoiseVariance, "normalized-variance", EvidenceProvenance::Metadata, 0.05, "DNG NoiseProfile O");
                for (double signal : signals) {
                    const double sigma = std::sqrt(coefficients.shotScale * signal + coefficients.readNoiseVariance);
                    plane.snrBySignal.push_back(Measurement(signal / std::max(kEpsilon, sigma), "linear-SNR", EvidenceProvenance::Derived, 0.1, "signal=" + std::to_string(signal)));
                }
            } else {
                plane.shotScale = Unavailable("normalized-variance-per-normalized-signal", "invalid-NoiseProfile-coefficients");
                plane.readNoiseVariance = Unavailable("normalized-variance", "invalid-NoiseProfile-coefficients");
            }
            record.noise.push_back(std::move(plane));
        }
    } else {
        for (int color = 0; color < 3; ++color) {
            NoisePlaneEvidence plane;
            plane.plane = color;
            plane.planeName = PlaneName(color);
            const std::string reason = raw.metadata.hasDngBaselineNoise
                ? "NoiseProfile-missing; BaselineNoise recorded but not substituted as camera-specific truth"
                : "NoiseProfile-unavailable";
            plane.shotScale = Unavailable("normalized-variance-per-normalized-signal", reason);
            plane.readNoiseVariance = Unavailable("normalized-variance", reason);
            record.noise.push_back(std::move(plane));
        }
    }

    record.baselineExposureEv = raw.metadata.hasDngBaselineExposure
        ? Measurement(raw.metadata.dngBaselineExposure, "EV", EvidenceProvenance::Metadata, 0.05, "DNG BaselineExposure; not current RAW Exposure")
        : Measurement(0.0, "EV", EvidenceProvenance::Fallback, 0.5, "DNG default when BaselineExposure tag is absent");
    double profileConfidence = 0.0;
    std::string profileReason;
    if (raw.metadata.hasDngForwardMatrix1 || raw.metadata.hasDngForwardMatrix2) {
        profileConfidence = 1.0;
        profileReason = "DNG ForwardMatrix available";
    } else if (raw.metadata.hasDngColorMatrix1 || raw.metadata.hasDngColorMatrix2) {
        profileConfidence = 0.7;
        profileReason = "DNG ColorMatrix available";
    } else if (raw.metadata.hasCameraMatrix) {
        profileConfidence = 0.45;
        profileReason = "LibRaw camera matrix fallback";
    } else {
        profileReason = "camera profile unavailable";
    }
    record.cameraProfileConfidence = profileConfidence > 0.0
        ? Measurement(profileConfidence, "confidence-0-to-1", EvidenceProvenance::Derived, 0.3, profileReason)
        : Unavailable("confidence-0-to-1", profileReason);

    record.valid = record.sampledRawValues > 0 && record.planes.size() == 3 &&
        std::all_of(record.planes.begin(), record.planes.end(), [](const PlaneEvidence& plane) {
            return plane.p999.valid && plane.whiteLevel.valid;
        });
    record.statusMessage = record.valid
        ? "Raw technical evidence complete; diagnostic only and not consumed by Pass 94."
        : "Raw technical evidence incomplete; missing values remain explicit.";
    const auto finished = std::chrono::steady_clock::now();
    record.runtimeMs = std::chrono::duration<double, std::milli>(finished - started).count();
    return record;
}

const RawTechnicalEvidenceRecord& RawTechnicalEvidenceCache::GetOrBuild(
    const Raw::RawImageData& raw,
    const SourceIdentity& sourceIdentity,
    const BuildOptions& options) {
    const DecodeIdentity decode = BuildDecodeIdentity(raw.metadata, options.decode);
    const std::string key = Sha256String(sourceIdentity.sha256 + "|" + decode.sha256 + "|" + kRawTechnicalEvidenceVersion);
    const auto found = m_Records.find(key);
    if (found != m_Records.end()) return found->second;
    RawTechnicalEvidenceRecord record = BuildRawTechnicalEvidence(raw, sourceIdentity, options);
    return m_Records.emplace(key, std::move(record)).first->second;
}

void RawTechnicalEvidenceCache::Clear() {
    m_Records.clear();
}

std::size_t RawTechnicalEvidenceCache::Size() const {
    return m_Records.size();
}

const char* EvidenceProvenanceName(EvidenceProvenance provenance) {
    switch (provenance) {
        case EvidenceProvenance::Measured: return "measured";
        case EvidenceProvenance::Metadata: return "metadata";
        case EvidenceProvenance::Derived: return "derived";
        case EvidenceProvenance::Fallback: return "fallback";
        case EvidenceProvenance::Unavailable: return "unavailable";
    }
    return "unavailable";
}

nlohmann::json SerializeRawTechnicalEvidence(const RawTechnicalEvidenceRecord& record) {
    nlohmann::json planes = nlohmann::json::array();
    for (const PlaneEvidence& plane : record.planes) {
        planes.push_back({
            { "plane", plane.plane }, { "planeName", plane.planeName },
            { "sampleCount", plane.sampleCount },
            { "blackMaximum", SerializeMeasurement(plane.blackMaximum) },
            { "whiteLevel", SerializeMeasurement(plane.whiteLevel) },
            { "linearResponseLimit", SerializeMeasurement(plane.linearResponseLimit) },
            { "p999", SerializeMeasurement(plane.p999) },
            { "nearNonlinearFraction", SerializeMeasurement(plane.nearNonlinearFraction) },
            { "nearWhiteFraction", SerializeMeasurement(plane.nearWhiteFraction) },
            { "clippedFraction", SerializeMeasurement(plane.clippedFraction) },
            { "headroomEv", SerializeMeasurement(plane.headroomEv) },
            { "wbMultiplier", SerializeMeasurement(plane.wbMultiplier) },
            { "wbScaledHeadroomEv", SerializeMeasurement(plane.wbScaledHeadroomEv) }
        });
    }
    nlohmann::json noise = nlohmann::json::array();
    for (const NoisePlaneEvidence& plane : record.noise) {
        nlohmann::json snr = nlohmann::json::array();
        for (const EvidenceMeasurement& value : plane.snrBySignal) snr.push_back(SerializeMeasurement(value));
        noise.push_back({
            { "plane", plane.plane }, { "planeName", plane.planeName },
            { "shotScale", SerializeMeasurement(plane.shotScale) },
            { "readNoiseVariance", SerializeMeasurement(plane.readNoiseVariance) },
            { "snrBySignal", std::move(snr) }
        });
    }
    nlohmann::json masked = nlohmann::json::array();
    for (const Raw::RawSensorRect& rect : record.geometry.maskedAreas) masked.push_back(SerializeRect(rect));
    return {
        { "schemaVersion", record.schemaVersion },
        { "featureVersion", record.featureVersion },
        { "valid", record.valid },
        { "stage", record.stage },
        { "units", record.units },
        { "normalizationVersion", record.normalizationVersion },
        { "sourceIdentity", {
            { "valid", record.sourceIdentity.valid }, { "sha256", record.sourceIdentity.sha256 },
            { "byteSize", record.sourceIdentity.byteSize }, { "reason", record.sourceIdentity.reason }
        } },
        { "decodeIdentity", {
            { "valid", record.decodeIdentity.valid }, { "sha256", record.decodeIdentity.sha256 },
            { "canonicalFields", record.decodeIdentity.canonicalFields }, { "reason", record.decodeIdentity.reason }
        } },
        { "evidenceIdentitySha256", record.evidenceIdentitySha256 },
        { "geometry", {
            { "activeArea", SerializeRect(record.geometry.activeArea) },
            { "maskedAreas", std::move(masked) },
            { "activeAreaFromMetadata", record.geometry.activeAreaFromMetadata },
            { "maskedAreasFromMetadata", record.geometry.maskedAreasFromMetadata },
            { "orientation", record.geometry.orientation },
            { "sensorCoordinates", record.geometry.sensorCoordinates },
            { "photographicTransform", record.geometry.photographicTransform },
            { "activeValidFraction", record.geometry.activeValidFraction }
        } },
        { "pixelLayout", record.pixelLayout },
        { "sampleFormat", record.sampleFormat },
        { "cfaPattern", record.cfaPattern },
        { "planeOrder", record.planeOrder },
        { "planes", std::move(planes) },
        { "clipping", {
            { "valid", record.clipping.valid },
            { "superpixelCount", record.clipping.superpixelCount },
            { "noChannelClippedFraction", SerializeMeasurement(record.clipping.noChannelClippedFraction) },
            { "singleChannelClippedFraction", SerializeMeasurement(record.clipping.singleChannelClippedFraction) },
            { "multiChannelClippedFraction", SerializeMeasurement(record.clipping.multiChannelClippedFraction) },
            { "allChannelClippedFraction", SerializeMeasurement(record.clipping.allChannelClippedFraction) },
            { "reason", record.clipping.reason }
        } },
        { "noise", std::move(noise) },
        { "maskedBlackMean", SerializeMeasurement(record.maskedBlackMean) },
        { "maskedBlackStd", SerializeMeasurement(record.maskedBlackStd) },
        { "maskedBlackDeltaFromMetadata", SerializeMeasurement(record.maskedBlackDeltaFromMetadata) },
        { "hotPixelFraction", SerializeMeasurement(record.hotPixelFraction) },
        { "deadPixelFraction", SerializeMeasurement(record.deadPixelFraction) },
        { "baselineExposureEv", SerializeMeasurement(record.baselineExposureEv) },
        { "cameraProfileConfidence", SerializeMeasurement(record.cameraProfileConfidence) },
        { "metadataCoverage", {
            { "linearizationTable", record.metadataCoverage.hasLinearizationTable },
            { "spatialBlackPattern", record.metadataCoverage.hasSpatialBlackPattern },
            { "blackDeltaH", record.metadataCoverage.hasBlackDeltaH },
            { "blackDeltaV", record.metadataCoverage.hasBlackDeltaV },
            { "activeArea", record.metadataCoverage.hasActiveArea },
            { "maskedAreas", record.metadataCoverage.hasMaskedAreas },
            { "linearResponseLimit", record.metadataCoverage.hasLinearResponseLimit },
            { "asShotNeutral", record.metadataCoverage.hasAsShotNeutral },
            { "baselineExposure", record.metadataCoverage.hasBaselineExposure },
            { "noiseProfile", record.metadataCoverage.hasNoiseProfile },
            { "opcodeList1", record.metadataCoverage.hasOpcodeList1 },
            { "opcodeList2", record.metadataCoverage.hasOpcodeList2 },
            { "opcodeList3", record.metadataCoverage.hasOpcodeList3 },
            { "opcodeList2GainMap", record.metadataCoverage.hasOpcodeList2GainMap },
            { "profileGainTableMap", record.metadataCoverage.hasProfileGainTableMap },
            { "profileGainTableMap2", record.metadataCoverage.hasProfileGainTableMap2 },
            { "opcodeCount", record.metadataCoverage.opcodeCount },
            { "appliedOpcodeCount", record.metadataCoverage.appliedOpcodeCount },
            { "unsupportedOpcodeCount", record.metadataCoverage.unsupportedOpcodeCount },
            { "omissions", record.metadataCoverage.omissions }
        } },
        { "limitingPlane", record.limitingPlane },
        { "statusMessage", record.statusMessage },
        { "warnings", record.warnings },
        { "runtimeMs", record.runtimeMs },
        { "sampledRawValues", record.sampledRawValues }
    };
}

std::string SummarizeRawTechnicalEvidence(const RawTechnicalEvidenceRecord& record) {
    std::ostringstream summary;
    summary << (record.valid ? "RAW evidence v1 complete" : "RAW evidence v1 incomplete")
        << "; source=" << (record.sourceIdentity.sha256.empty() ? "unavailable" : record.sourceIdentity.sha256.substr(0, 12))
        << "; CFA=" << record.cfaPattern
        << "; samples=" << record.sampledRawValues;
    if (!record.limitingPlane.empty()) summary << "; WB limiting plane=" << record.limitingPlane;
    if (record.metadataCoverage.hasNoiseProfile) summary << "; NoiseProfile=yes";
    else summary << "; NoiseProfile=unavailable";
    if (record.metadataCoverage.hasLinearResponseLimit) summary << "; LRL=yes";
    else summary << "; LRL=white-level fallback";
    return summary.str();
}

} // namespace Stack::RawEvidence
