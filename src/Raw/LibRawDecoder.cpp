#include "LibRawDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#ifdef STACK_ENABLE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace Raw {
namespace {

int EstimateBitDepth(float whiteLevel) {
    if (whiteLevel <= 0.0f) {
        return 0;
    }
    return static_cast<int>(std::ceil(std::log2(whiteLevel + 1.0f)));
}

float SafePositive(float value, float fallback) {
    return value > 0.0f ? value : fallback;
}

#ifdef STACK_ENABLE_LIBRAW
bool HasMatrix3x3(const std::array<float, 9>& matrix) {
    for (float value : matrix) {
        if (std::abs(value) > 0.000001f) {
            return true;
        }
    }
    return false;
}

char ColorChar(const libraw_data_t& image, int colorIndex) {
    if (colorIndex < 0 || colorIndex >= 4) {
        return '?';
    }
    const char value = image.idata.cdesc[colorIndex];
    return value ? value : '?';
}

CfaPattern PatternFromString(const std::string& pattern) {
    if (pattern == "RGGB") return CfaPattern::RGGB;
    if (pattern == "BGGR") return CfaPattern::BGGR;
    if (pattern == "GBRG") return CfaPattern::GBRG;
    if (pattern == "GRBG") return CfaPattern::GRBG;
    return CfaPattern::Unknown;
}

class DngTiffReader {
public:
    explicit DngTiffReader(std::vector<std::uint8_t> bytes)
        : m_Bytes(std::move(bytes)) {
        if (m_Bytes.size() >= 8) {
            if (m_Bytes[0] == 'I' && m_Bytes[1] == 'I') {
                m_LittleEndian = true;
                m_Valid = ReadU16(2) == 42;
                m_FirstIfd = ReadU32(4);
            } else if (m_Bytes[0] == 'M' && m_Bytes[1] == 'M') {
                m_LittleEndian = false;
                m_Valid = ReadU16(2) == 42;
                m_FirstIfd = ReadU32(4);
            }
        }
    }

    struct Entry {
        std::uint16_t tag = 0;
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        std::uint32_t valueOffset = 0;
    };

    bool Valid() const { return m_Valid; }
    std::uint32_t FirstIfd() const { return m_FirstIfd; }

    std::uint16_t ReadU16(std::size_t offset) const {
        if (offset + 2 > m_Bytes.size()) return 0;
        if (m_LittleEndian) {
            return static_cast<std::uint16_t>(m_Bytes[offset] | (m_Bytes[offset + 1] << 8));
        }
        return static_cast<std::uint16_t>((m_Bytes[offset] << 8) | m_Bytes[offset + 1]);
    }

    std::uint32_t ReadU32(std::size_t offset) const {
        if (offset + 4 > m_Bytes.size()) return 0;
        if (m_LittleEndian) {
            return static_cast<std::uint32_t>(m_Bytes[offset]) |
                (static_cast<std::uint32_t>(m_Bytes[offset + 1]) << 8) |
                (static_cast<std::uint32_t>(m_Bytes[offset + 2]) << 16) |
                (static_cast<std::uint32_t>(m_Bytes[offset + 3]) << 24);
        }
        return (static_cast<std::uint32_t>(m_Bytes[offset]) << 24) |
            (static_cast<std::uint32_t>(m_Bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(m_Bytes[offset + 2]) << 8) |
            static_cast<std::uint32_t>(m_Bytes[offset + 3]);
    }

    std::int32_t ReadI32(std::size_t offset) const {
        return static_cast<std::int32_t>(ReadU32(offset));
    }

    float ReadFloat(std::size_t offset) const {
        const std::uint32_t bits = ReadU32(offset);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(float));
        return value;
    }

    double ReadDouble(std::size_t offset) const {
        if (offset + 8 > m_Bytes.size()) return 0.0;
        std::uint64_t bits = 0;
        if (m_LittleEndian) {
            for (int i = 7; i >= 0; --i) bits = (bits << 8) | m_Bytes[offset + static_cast<std::size_t>(i)];
        } else {
            for (int i = 0; i < 8; ++i) bits = (bits << 8) | m_Bytes[offset + static_cast<std::size_t>(i)];
        }
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(double));
        return value;
    }

    std::vector<Entry> ReadEntries(std::uint32_t ifdOffset) const {
        std::vector<Entry> entries;
        if (!m_Valid || ifdOffset == 0 || static_cast<std::size_t>(ifdOffset) + 2 > m_Bytes.size()) {
            return entries;
        }
        const std::uint16_t count = ReadU16(ifdOffset);
        std::size_t p = static_cast<std::size_t>(ifdOffset) + 2;
        for (std::uint16_t i = 0; i < count && p + 12 <= m_Bytes.size(); ++i, p += 12) {
            Entry entry;
            entry.tag = ReadU16(p);
            entry.type = ReadU16(p + 2);
            entry.count = ReadU32(p + 4);
            entry.valueOffset = ReadU32(p + 8);
            entries.push_back(entry);
        }
        return entries;
    }

    std::vector<std::uint8_t> RawBytes(const Entry& entry) const {
        const std::size_t bytes = TypeSize(entry.type) * static_cast<std::size_t>(entry.count);
        const std::size_t offset = bytes <= 4 ? ValueInlineOffset(entry) : static_cast<std::size_t>(entry.valueOffset);
        if (offset + bytes > m_Bytes.size()) return {};
        return std::vector<std::uint8_t>(m_Bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            m_Bytes.begin() + static_cast<std::ptrdiff_t>(offset + bytes));
    }

    std::string StringValue(const Entry& entry) const {
        std::vector<std::uint8_t> bytes = RawBytes(entry);
        while (!bytes.empty() && bytes.back() == 0) bytes.pop_back();
        return std::string(bytes.begin(), bytes.end());
    }

    std::vector<double> NumberValues(const Entry& entry) const {
        std::vector<double> values;
        const std::size_t typeSize = TypeSize(entry.type);
        if (typeSize == 0 || entry.count == 0) return values;
        const std::size_t bytes = typeSize * static_cast<std::size_t>(entry.count);
        const std::size_t base = bytes <= 4 ? ValueInlineOffset(entry) : static_cast<std::size_t>(entry.valueOffset);
        if (base + bytes > m_Bytes.size()) return values;
        values.reserve(entry.count);
        for (std::uint32_t i = 0; i < entry.count; ++i) {
            const std::size_t p = base + static_cast<std::size_t>(i) * typeSize;
            switch (entry.type) {
                case 1:
                case 7: values.push_back(m_Bytes[p]); break;
                case 3: values.push_back(ReadU16(p)); break;
                case 4: values.push_back(ReadU32(p)); break;
                case 5: {
                    const double num = ReadU32(p);
                    const double den = std::max(1.0, static_cast<double>(ReadU32(p + 4)));
                    values.push_back(num / den);
                    break;
                }
                case 9: values.push_back(ReadI32(p)); break;
                case 10: {
                    const double num = ReadI32(p);
                    const double den = static_cast<double>(ReadI32(p + 4));
                    values.push_back(den == 0.0 ? 0.0 : num / den);
                    break;
                }
                case 11: values.push_back(ReadFloat(p)); break;
                case 12: values.push_back(ReadDouble(p)); break;
                default: break;
            }
        }
        return values;
    }

    static std::size_t TypeSize(std::uint16_t type) {
        switch (type) {
            case 1:
            case 2:
            case 6:
            case 7:
                return 1;
            case 3:
            case 8:
                return 2;
            case 4:
            case 9:
            case 11:
                return 4;
            case 5:
            case 10:
            case 12:
                return 8;
            default:
                return 0;
        }
    }

private:
    std::size_t ValueInlineOffset(const Entry& entry) const {
        const std::size_t valueField = EntryOffset(entry) + 8;
        return valueField;
    }

    std::size_t EntryOffset(const Entry& target) const {
        const std::uint16_t count = ReadU16(m_FirstIfd);
        std::size_t p = static_cast<std::size_t>(m_FirstIfd) + 2;
        for (std::uint16_t i = 0; i < count && p + 12 <= m_Bytes.size(); ++i, p += 12) {
            if (ReadU16(p) == target.tag && ReadU16(p + 2) == target.type &&
                ReadU32(p + 4) == target.count && ReadU32(p + 8) == target.valueOffset) {
                return p;
            }
        }
        return 0;
    }

    std::vector<std::uint8_t> m_Bytes;
    bool m_LittleEndian = true;
    bool m_Valid = false;
    std::uint32_t m_FirstIfd = 0;
};

CfaPattern PatternFromDngCfa(const std::array<int, 4>& pattern, const std::array<int, 3>& planeColors) {
    std::string text;
    text.reserve(4);
    for (int plane : pattern) {
        if (plane < 0 || plane >= static_cast<int>(planeColors.size())) {
            return CfaPattern::Unknown;
        }
        const int color = planeColors[static_cast<std::size_t>(plane)];
        if (color == 0) text.push_back('R');
        else if (color == 1) text.push_back('G');
        else if (color == 2) text.push_back('B');
        else return CfaPattern::Unknown;
    }
    return PatternFromString(text);
}

double ReadBeDouble(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 8 > bytes.size()) return 0.0;
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) bits = (bits << 8) | bytes[offset + static_cast<std::size_t>(i)];
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

std::uint32_t ReadBeU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) return 0;
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::int32_t ReadBeI32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int32_t>(ReadBeU32(bytes, offset));
}

float ReadBeFloat(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const std::uint32_t bits = ReadBeU32(bytes, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

void ParseDngOpcodeList2(const std::vector<std::uint8_t>& bytes, RawMetadata& metadata) {
    if (bytes.size() < 4) return;
    const std::uint32_t count = ReadBeU32(bytes, 0);
    std::size_t p = 4;
    for (std::uint32_t i = 0; i < count && p + 16 <= bytes.size(); ++i) {
        const std::uint32_t opcodeId = ReadBeU32(bytes, p);
        const std::uint32_t byteCount = ReadBeU32(bytes, p + 12);
        p += 16;
        if (p + byteCount > bytes.size()) {
            metadata.warnings.push_back("DNG OpcodeList2 is truncated.");
            return;
        }
        if (opcodeId != 9) {
            ++metadata.dngUnsupportedOpcodeCount;
            p += byteCount;
            continue;
        }

        if (byteCount < 76) {
            ++metadata.dngUnsupportedOpcodeCount;
            p += byteCount;
            continue;
        }

        DngGainMapOpcode map;
        map.top = ReadBeI32(bytes, p + 0);
        map.left = ReadBeI32(bytes, p + 4);
        map.bottom = ReadBeI32(bytes, p + 8);
        map.right = ReadBeI32(bytes, p + 12);
        map.plane = ReadBeI32(bytes, p + 16);
        map.planes = std::max(1, ReadBeI32(bytes, p + 20));
        map.rowPitch = std::max(1, ReadBeI32(bytes, p + 24));
        map.colPitch = std::max(1, ReadBeI32(bytes, p + 28));
        map.mapPointsV = std::max(0, ReadBeI32(bytes, p + 32));
        map.mapPointsH = std::max(0, ReadBeI32(bytes, p + 36));
        map.mapSpacingV = ReadBeDouble(bytes, p + 40);
        map.mapSpacingH = ReadBeDouble(bytes, p + 48);
        map.mapOriginV = ReadBeDouble(bytes, p + 56);
        map.mapOriginH = ReadBeDouble(bytes, p + 64);
        map.mapPlanes = std::max(1, ReadBeI32(bytes, p + 72));
        const std::size_t gainOffset = p + 76;
        const std::size_t gainCount = static_cast<std::size_t>(map.mapPointsV) *
            static_cast<std::size_t>(map.mapPointsH) * static_cast<std::size_t>(map.mapPlanes);
        if (map.mapPointsV <= 0 || map.mapPointsH <= 0 || map.mapPlanes != 1 ||
            gainOffset + gainCount * sizeof(float) > p + byteCount) {
            ++metadata.dngUnsupportedOpcodeCount;
            p += byteCount;
            continue;
        }
        map.gains.resize(gainCount);
        for (std::size_t g = 0; g < gainCount; ++g) {
            map.gains[g] = ReadBeFloat(bytes, gainOffset + g * sizeof(float));
        }
        metadata.dngGainMaps.push_back(std::move(map));
        p += byteCount;
    }
    metadata.dngGainMapCount = static_cast<int>(metadata.dngGainMaps.size());
    if (metadata.dngGainMapCount > 0) {
        metadata.uploadFormat = "R16UI + DNG GainMap R32F";
    }
}

template <typename T, std::size_t N>
void CopyNumbers(const std::vector<double>& values, std::array<T, N>& target) {
    for (std::size_t i = 0; i < N && i < values.size(); ++i) {
        target[i] = static_cast<T>(values[i]);
    }
}

void ApplyDngSupplement(const std::string& path, RawMetadata& metadata) {
    if (!metadata.isDng) {
        return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        metadata.warnings.push_back("DNG supplement parser could not open file.");
        return;
    }
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    DngTiffReader reader(std::move(bytes));
    if (!reader.Valid()) {
        metadata.warnings.push_back("DNG supplement parser only supports classic TIFF DNG files.");
        return;
    }

    const auto entries = reader.ReadEntries(reader.FirstIfd());
    for (const DngTiffReader::Entry& entry : entries) {
        const std::vector<double> values = reader.NumberValues(entry);
        switch (entry.tag) {
            case 274:
                if (!values.empty()) {
                    const int orientation = static_cast<int>(std::lround(values[0]));
                    if (orientation >= 1 && orientation <= 8) {
                        metadata.orientation = orientation;
                    }
                }
                break;
            case 259: if (!values.empty()) metadata.dngCompression = static_cast<int>(values[0]); break;
            case 262: if (!values.empty()) metadata.dngPhotometricInterpretation = static_cast<int>(values[0]); break;
            case 50706: {
                if (values.size() >= 4) {
                    metadata.isDng = true;
                }
                break;
            }
            case 50708: metadata.dngUniqueCameraModel = reader.StringValue(entry); break;
            case 50710: CopyNumbers(values, metadata.dngCfaPlaneColor); break;
            case 50711: if (!values.empty()) metadata.dngCfaLayout = static_cast<int>(values[0]); break;
            case 50713: CopyNumbers(values, metadata.dngBlackLevelRepeatDim); break;
            case 50714: {
                CopyNumbers(values, metadata.dngBlackLevelPattern);
                if (!values.empty()) {
                    float sum = 0.0f;
                    for (std::size_t i = 0; i < metadata.dngBlackLevelPattern.size(); ++i) {
                        if (i < values.size()) sum += metadata.dngBlackLevelPattern[i];
                    }
                    const float denom = static_cast<float>(std::min<std::size_t>(values.size(), metadata.dngBlackLevelPattern.size()));
                    metadata.blackLevel = denom > 0.0f ? sum / denom : metadata.blackLevel;
                    metadata.perChannelBlack = metadata.dngBlackLevelPattern;
                    metadata.blackLevelSource = "DNG BlackLevel tag";
                }
                break;
            }
            case 50717:
                if (!values.empty()) {
                    metadata.whiteLevel = static_cast<float>(values[0]);
                    metadata.whiteLevelSource = "DNG WhiteLevel tag";
                    metadata.bitDepth = EstimateBitDepth(metadata.whiteLevel);
                }
                break;
            case 50721: CopyNumbers(values, metadata.dngColorMatrix1); metadata.hasDngColorMatrix1 = values.size() >= 9; break;
            case 50722: CopyNumbers(values, metadata.dngColorMatrix2); metadata.hasDngColorMatrix2 = values.size() >= 9; break;
            case 50723: CopyNumbers(values, metadata.dngCameraCalibration1); metadata.hasDngCameraCalibration1 = values.size() >= 9; break;
            case 50724: CopyNumbers(values, metadata.dngCameraCalibration2); metadata.hasDngCameraCalibration2 = values.size() >= 9; break;
            case 50727:
                CopyNumbers(values, metadata.dngAnalogBalance);
                metadata.hasDngAnalogBalance = values.size() >= 3;
                break;
            case 50728:
                CopyNumbers(values, metadata.dngAsShotNeutral);
                metadata.hasDngAsShotNeutral = values.size() >= 3 &&
                    metadata.dngAsShotNeutral[0] > 0.0001f &&
                    metadata.dngAsShotNeutral[1] > 0.0001f &&
                    metadata.dngAsShotNeutral[2] > 0.0001f;
                if (metadata.hasDngAsShotNeutral) {
                    metadata.cameraWhiteBalance[0] = 1.0f / (metadata.dngAsShotNeutral[0] * metadata.dngAnalogBalance[0]);
                    metadata.cameraWhiteBalance[1] = 1.0f / (metadata.dngAsShotNeutral[1] * metadata.dngAnalogBalance[1]);
                    metadata.cameraWhiteBalance[2] = 1.0f / (metadata.dngAsShotNeutral[2] * metadata.dngAnalogBalance[2]);
                    metadata.cameraWhiteBalance[3] = metadata.cameraWhiteBalance[1];
                    metadata.whiteBalanceSource = "DNG AsShotNeutral tag";
                }
                break;
            case 50730:
                if (!values.empty()) {
                    metadata.dngBaselineExposure = static_cast<float>(values[0]);
                    metadata.hasDngBaselineExposure = true;
                }
                break;
            case 50778: if (!values.empty()) metadata.dngIlluminant1 = static_cast<int>(values[0]); break;
            case 50779: if (!values.empty()) metadata.dngIlluminant2 = static_cast<int>(values[0]); break;
            case 50964: CopyNumbers(values, metadata.dngForwardMatrix1); metadata.hasDngForwardMatrix1 = values.size() >= 9; break;
            case 50965: CopyNumbers(values, metadata.dngForwardMatrix2); metadata.hasDngForwardMatrix2 = values.size() >= 9; break;
            case 51009: ParseDngOpcodeList2(reader.RawBytes(entry), metadata); break;
            case 33421: CopyNumbers(values, metadata.dngCfaRepeatPatternDim); break;
            case 33422: CopyNumbers(values, metadata.dngCfaPattern); break;
            default: break;
        }
    }

    if (metadata.dngCfaRepeatPatternDim[0] == 2 && metadata.dngCfaRepeatPatternDim[1] == 2) {
        const CfaPattern dngPattern = PatternFromDngCfa(metadata.dngCfaPattern, metadata.dngCfaPlaneColor);
        if (dngPattern != CfaPattern::Unknown) {
            metadata.cfaPattern = dngPattern;
            metadata.pixelLayout = RawPixelLayout::MosaicBayer;
            metadata.mosaiced = true;
            metadata.dngTypeStatus = "DNG type: Mosaic RAW / 2x2 Bayer";
        }
    } else if (metadata.pixelLayout == RawPixelLayout::MosaicBayer && metadata.dngCfaRepeatPatternDim[0] > 0) {
        metadata.warnings.push_back("Unsupported DNG CFA layout: only 2x2 Bayer is supported in this pass.");
    }

    if (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2) {
        metadata.cameraMatrixSource = "DNG Auto ForwardMatrix";
    } else if (metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2) {
        metadata.cameraMatrixSource = "DNG Auto ColorMatrix inverse";
    }
}

CfaPattern ExtractCfaPattern(LibRaw& processor, const libraw_data_t& image) {
    if (image.idata.colors < 3 || image.idata.filters == 0) {
        return CfaPattern::Unknown;
    }

    const int top = std::max(0, static_cast<int>(image.sizes.top_margin));
    const int left = std::max(0, static_cast<int>(image.sizes.left_margin));
    std::string pattern;
    pattern.reserve(4);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            pattern.push_back(ColorChar(image, processor.COLOR(top + y, left + x)));
        }
    }
    return PatternFromString(pattern);
}

void SetCfaBlackForVisiblePattern(
    LibRaw& processor,
    const libraw_data_t& image,
    const float patternBlack[4],
    RawMetadata& metadata) {
    bool wroteGreen1 = false;
    bool wroteGreen2 = false;
    const int top = std::max(0, static_cast<int>(image.sizes.top_margin));
    const int left = std::max(0, static_cast<int>(image.sizes.left_margin));
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const float black = patternBlack[y * 2 + x];
            const int color = processor.COLOR(top + y, left + x);
            if (color == 0) {
                metadata.perChannelBlack[0] = black;
            } else if (color == 2) {
                metadata.perChannelBlack[2] = black;
            } else if (!wroteGreen1) {
                metadata.perChannelBlack[1] = black;
                wroteGreen1 = true;
            } else if (!wroteGreen2) {
                metadata.perChannelBlack[3] = black;
                wroteGreen2 = true;
            }
        }
    }
    if (metadata.perChannelBlack[3] <= 0.0f) {
        metadata.perChannelBlack[3] = metadata.perChannelBlack[1];
    }
}

void ExtractDngLevels(LibRaw& processor, RawMetadata& metadata) {
    const libraw_data_t& image = processor.imgdata;
    metadata.isDng = image.idata.dng_version != 0;
    if (!metadata.isDng) {
        metadata.blackLevelSource = "LibRaw color.black";
        metadata.whiteLevelSource = "LibRaw color.maximum";
        metadata.whiteBalanceSource = "LibRaw cam_mul";
        metadata.cameraMatrixSource = "LibRaw rgb_cam";
        return;
    }

    const libraw_dng_levels_t& levels = image.color.dng_levels;
    const bool hasDngBlack = (levels.parsedfields & LIBRAW_DNGFM_BLACK) != 0;
    const bool hasDngWhite = (levels.parsedfields & LIBRAW_DNGFM_WHITE) != 0;
    const bool hasAsShotNeutral = (levels.parsedfields & LIBRAW_DNGFM_ASSHOTNEUTRAL) != 0;

    if (hasDngWhite && levels.dng_whitelevel[0] > 0) {
        metadata.whiteLevel = static_cast<float>(levels.dng_whitelevel[0]);
        metadata.whiteLevelSource = "DNG WhiteLevel";
    } else {
        metadata.whiteLevelSource = "LibRaw color.maximum";
    }

    if (hasDngBlack) {
        float patternBlack[4] {
            levels.dng_fcblack[6] > 0.0f ? levels.dng_fcblack[6] : static_cast<float>(levels.dng_cblack[6]),
            levels.dng_fcblack[7] > 0.0f ? levels.dng_fcblack[7] : static_cast<float>(levels.dng_cblack[7]),
            levels.dng_fcblack[8] > 0.0f ? levels.dng_fcblack[8] : static_cast<float>(levels.dng_cblack[8]),
            levels.dng_fcblack[9] > 0.0f ? levels.dng_fcblack[9] : static_cast<float>(levels.dng_cblack[9])
        };
        const bool hasPattern = patternBlack[0] > 0.0f || patternBlack[1] > 0.0f || patternBlack[2] > 0.0f || patternBlack[3] > 0.0f;
        if (hasPattern && metadata.pixelLayout == RawPixelLayout::MosaicBayer) {
            SetCfaBlackForVisiblePattern(processor, image, patternBlack, metadata);
            metadata.blackLevel = (patternBlack[0] + patternBlack[1] + patternBlack[2] + patternBlack[3]) * 0.25f;
            metadata.blackLevelSource = "DNG BlackLevel pattern";
        } else if (levels.dng_fblack > 0.0f || levels.dng_black > 0) {
            metadata.blackLevel = levels.dng_fblack > 0.0f ? levels.dng_fblack : static_cast<float>(levels.dng_black);
            metadata.perChannelBlack = { metadata.blackLevel, metadata.blackLevel, metadata.blackLevel, metadata.blackLevel };
            metadata.blackLevelSource = "DNG BlackLevel";
        } else {
            metadata.blackLevelSource = "LibRaw color.black";
        }
    } else {
        metadata.blackLevelSource = "LibRaw color.black";
    }

    if (hasAsShotNeutral) {
        for (int i = 0; i < 3; ++i) {
            metadata.dngAsShotNeutral[static_cast<std::size_t>(i)] = levels.asshotneutral[i];
        }
        metadata.hasDngAsShotNeutral = metadata.dngAsShotNeutral[0] > 0.0001f &&
            metadata.dngAsShotNeutral[1] > 0.0001f &&
            metadata.dngAsShotNeutral[2] > 0.0001f;
        if (metadata.hasDngAsShotNeutral) {
            metadata.cameraWhiteBalance[0] = 1.0f / metadata.dngAsShotNeutral[0];
            metadata.cameraWhiteBalance[1] = 1.0f / metadata.dngAsShotNeutral[1];
            metadata.cameraWhiteBalance[2] = 1.0f / metadata.dngAsShotNeutral[2];
            metadata.cameraWhiteBalance[3] = metadata.cameraWhiteBalance[1];
            metadata.whiteBalanceSource = "DNG AsShotNeutral";
        }
    }
    if (metadata.whiteBalanceSource.empty()) {
        metadata.whiteBalanceSource = "LibRaw cam_mul";
    }
}

void ExtractDngColorMetadata(const libraw_data_t& image, RawMetadata& metadata) {
    if (!metadata.isDng) {
        return;
    }
    for (int set = 0; set < 2; ++set) {
        const libraw_dng_color_t& color = image.color.dng_color[set];
        const bool hasColorMatrix = (color.parsedfields & LIBRAW_DNGFM_COLORMATRIX) != 0;
        const bool hasForwardMatrix = (color.parsedfields & LIBRAW_DNGFM_FORWARDMATRIX) != 0;
        std::array<float, 9> colorMatrix {};
        std::array<float, 9> forwardMatrix {};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                colorMatrix[static_cast<std::size_t>(r * 3 + c)] = color.colormatrix[r][c];
                forwardMatrix[static_cast<std::size_t>(r * 3 + c)] = color.forwardmatrix[r][c];
            }
        }
        if (set == 0) {
            metadata.dngIlluminant1 = static_cast<int>(color.illuminant);
            metadata.dngColorMatrix1 = colorMatrix;
            metadata.dngForwardMatrix1 = forwardMatrix;
            metadata.hasDngColorMatrix1 = hasColorMatrix && HasMatrix3x3(colorMatrix);
            metadata.hasDngForwardMatrix1 = hasForwardMatrix && HasMatrix3x3(forwardMatrix);
        } else {
            metadata.dngIlluminant2 = static_cast<int>(color.illuminant);
            metadata.dngColorMatrix2 = colorMatrix;
            metadata.dngForwardMatrix2 = forwardMatrix;
            metadata.hasDngColorMatrix2 = hasColorMatrix && HasMatrix3x3(colorMatrix);
            metadata.hasDngForwardMatrix2 = hasForwardMatrix && HasMatrix3x3(forwardMatrix);
        }
    }
    if (metadata.hasDngForwardMatrix1) {
        metadata.cameraMatrixSource = "DNG ForwardMatrix 1";
    } else if (metadata.hasDngForwardMatrix2) {
        metadata.cameraMatrixSource = "DNG ForwardMatrix 2";
    } else if (metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2) {
        metadata.cameraMatrixSource = "DNG ColorMatrix";
    } else {
        metadata.cameraMatrixSource = "LibRaw rgb_cam";
    }
}

void ExtractMetadata(LibRaw& processor, const std::string& path, RawMetadata& metadata) {
    const libraw_data_t& image = processor.imgdata;
    const libraw_imgother_t& capture = image.other;
    metadata.cameraMake = image.idata.make ? image.idata.make : "";
    metadata.cameraModel = image.idata.model ? image.idata.model : "";
    metadata.rawWidth = image.sizes.raw_width;
    metadata.rawHeight = image.sizes.raw_height;
    metadata.visibleWidth = image.sizes.width > 0 ? image.sizes.width : image.sizes.iwidth;
    metadata.visibleHeight = image.sizes.height > 0 ? image.sizes.height : image.sizes.iheight;
    metadata.leftMargin = image.sizes.left_margin;
    metadata.topMargin = image.sizes.top_margin;
    metadata.orientation = image.sizes.flip;
    metadata.isDng = image.idata.dng_version != 0;
    metadata.whiteLevel = SafePositive(static_cast<float>(image.color.maximum), 65535.0f);
    metadata.blackLevel = std::max(0.0f, static_cast<float>(image.color.black));
    metadata.bitDepth = EstimateBitDepth(metadata.whiteLevel);
    metadata.exposureTimeSeconds = std::max(0.0f, capture.shutter);
    metadata.isoSpeed = std::max(0.0f, capture.iso_speed);
    metadata.apertureFNumber = std::max(0.0f, capture.aperture);
    metadata.captureTimestamp = capture.timestamp > 0
        ? static_cast<std::int64_t>(capture.timestamp)
        : 0;
    metadata.hasExposureTime = metadata.exposureTimeSeconds > 0.0f;
    metadata.hasIsoSpeed = metadata.isoSpeed > 0.0f;
    metadata.hasApertureFNumber = metadata.apertureFNumber > 0.0f;
    metadata.hasCaptureTimestamp = metadata.captureTimestamp > 0;
    metadata.mosaiced = image.idata.filters != 0 && image.idata.colors >= 3;
    metadata.pixelLayout = metadata.mosaiced ? RawPixelLayout::MosaicBayer : RawPixelLayout::Unknown;
    metadata.cfaPattern = metadata.mosaiced ? ExtractCfaPattern(processor, image) : CfaPattern::Unknown;
    if (!metadata.mosaiced && image.idata.colors >= 3) {
        metadata.pixelLayout = RawPixelLayout::LinearRgb;
        metadata.linearChannels = std::clamp(image.idata.colors, 3, 4);
        metadata.dngTypeStatus = metadata.isDng ? "DNG type: Linear RGB / demosaic skipped" : "";
        metadata.uploadFormat = "RGBA16F";
    } else if (metadata.pixelLayout == RawPixelLayout::MosaicBayer) {
        metadata.dngTypeStatus = metadata.isDng ? "DNG type: Mosaic RAW" : "";
        metadata.uploadFormat = "R16UI";
    }

    for (int i = 0; i < 4; ++i) {
        metadata.perChannelBlack[i] = static_cast<float>(image.color.cblack[i]);
        metadata.cameraWhiteBalance[i] = SafePositive(image.color.cam_mul[i], 1.0f);
        metadata.daylightWhiteBalance[i] = SafePositive(image.color.pre_mul[i], 1.0f);
    }
    ExtractDngLevels(processor, metadata);

    bool hasMatrix = false;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const float value = image.color.rgb_cam[r][c];
            metadata.cameraToSrgb[static_cast<std::size_t>(r * 3 + c)] = value;
            hasMatrix = hasMatrix || std::abs(value) > 0.000001f;
        }
    }
    metadata.hasCameraMatrix = hasMatrix;
    ExtractDngColorMetadata(image, metadata);
    ApplyDngSupplement(path, metadata);

    if (metadata.visibleWidth <= 0 || metadata.visibleHeight <= 0) {
        metadata.visibleWidth = metadata.rawWidth;
        metadata.visibleHeight = metadata.rawHeight;
    }
    if (metadata.pixelLayout != RawPixelLayout::MosaicBayer && metadata.pixelLayout != RawPixelLayout::LinearRgb) {
        metadata.dngTypeStatus = "DNG type: Unsupported/unknown";
        metadata.error = "Unsupported RAW pixel layout.";
    } else if (metadata.pixelLayout == RawPixelLayout::MosaicBayer && metadata.cfaPattern == CfaPattern::Unknown) {
        metadata.dngTypeStatus = "DNG type: Unsupported/unknown";
        metadata.error = "Unsupported RAW CFA pattern.";
    }
    if (metadata.pixelLayout == RawPixelLayout::MosaicBayer &&
        metadata.dngCfaRepeatPatternDim[0] > 0 &&
        !(metadata.dngCfaRepeatPatternDim[0] == 2 && metadata.dngCfaRepeatPatternDim[1] == 2)) {
        metadata.error = "Unsupported DNG CFA layout. Only 2x2 Bayer mosaics are supported.";
    }
}

void ExtractRawStats(RawImageData& data) {
    if (data.rawBuffer.empty()) {
        return;
    }

    auto [minIt, maxIt] = std::minmax_element(data.rawBuffer.begin(), data.rawBuffer.end());
    data.metadata.rawMinimum = static_cast<float>(*minIt);
    data.metadata.rawMaximum = static_cast<float>(*maxIt);

    const float white = data.metadata.whiteLevel;
    if (white <= 0.0f) {
        data.metadata.defaultWhiteClipPercent = 0.0f;
        return;
    }

    const std::size_t clipped = static_cast<std::size_t>(std::count_if(
        data.rawBuffer.begin(),
        data.rawBuffer.end(),
        [white](std::uint16_t value) { return static_cast<float>(value) >= white; }));
    data.metadata.defaultWhiteClipPercent = 100.0f * static_cast<float>(clipped) / static_cast<float>(data.rawBuffer.size());
}

void ExtractLinearStats(RawImageData& data) {
    if (!data.linearUInt16Buffer.empty()) {
        auto [minIt, maxIt] = std::minmax_element(data.linearUInt16Buffer.begin(), data.linearUInt16Buffer.end());
        data.metadata.rawMinimum = static_cast<float>(*minIt);
        data.metadata.rawMaximum = static_cast<float>(*maxIt);
    } else if (!data.linearFloatBuffer.empty()) {
        auto [minIt, maxIt] = std::minmax_element(data.linearFloatBuffer.begin(), data.linearFloatBuffer.end());
        data.metadata.rawMinimum = *minIt;
        data.metadata.rawMaximum = *maxIt;
    }
    data.metadata.defaultWhiteClipPercent = 0.0f;
}

template <typename T>
void CopyColorImageToUInt16(
    const T* source,
    int width,
    int height,
    int left,
    int top,
    int stridePixels,
    int sourceChannels,
    int outputChannels,
    std::vector<std::uint16_t>& output) {
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    output.assign(pixelCount * static_cast<std::size_t>(outputChannels), 0);
    const int safeStride = stridePixels > 0 ? stridePixels : width;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t outPixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const int inPixel = (top + y) * safeStride + (left + x);
            for (int c = 0; c < outputChannels; ++c) {
            const int srcC = std::min(c, std::max(0, sourceChannels - 1));
                output[outPixel * static_cast<std::size_t>(outputChannels) + static_cast<std::size_t>(c)] =
                    static_cast<std::uint16_t>(source[inPixel][srcC]);
            }
        }
    }
}

template <typename T>
void CopyColorImageToFloat(
    const T* source,
    int width,
    int height,
    int left,
    int top,
    int stridePixels,
    int sourceChannels,
    int outputChannels,
    std::vector<float>& output) {
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    output.assign(pixelCount * static_cast<std::size_t>(outputChannels), 0.0f);
    const int safeStride = stridePixels > 0 ? stridePixels : width;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t outPixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const int inPixel = (top + y) * safeStride + (left + x);
            for (int c = 0; c < outputChannels; ++c) {
            const int srcC = std::min(c, std::max(0, sourceChannels - 1));
                output[outPixel * static_cast<std::size_t>(outputChannels) + static_cast<std::size_t>(c)] =
                    static_cast<float>(source[inPixel][srcC]);
            }
        }
    }
}
#endif

} // namespace

bool DecodeWithLibRaw(const std::string& path, RawImageData& outData) {
    outData = {};
    outData.metadata.sourcePath = path;

#ifndef STACK_ENABLE_LIBRAW
    outData.metadata.error = "LibRaw support is disabled in this build.";
    return false;
#else
    if (path.empty()) {
        outData.metadata.error = "No RAW source path.";
        return false;
    }

    LibRaw processor;
    int status = processor.open_file(path.c_str());
    if (status != LIBRAW_SUCCESS) {
        outData.metadata.error = std::string("LibRaw open_file failed: ") + libraw_strerror(status);
        return false;
    }

    status = processor.unpack();
    if (status != LIBRAW_SUCCESS) {
        outData.metadata.error = std::string("LibRaw unpack failed: ") + libraw_strerror(status);
        processor.recycle();
        return false;
    }

    ExtractMetadata(processor, path, outData.metadata);
    if (!outData.metadata.error.empty()) {
        processor.recycle();
        return false;
    }

    const int rawWidth = outData.metadata.rawWidth;
    const int rawHeight = outData.metadata.rawHeight;
    const std::size_t pixelCount = static_cast<std::size_t>(std::max(0, rawWidth)) * static_cast<std::size_t>(std::max(0, rawHeight));
    if (outData.metadata.pixelLayout == RawPixelLayout::LinearRgb) {
        const int channels = std::clamp(outData.metadata.linearChannels > 0 ? outData.metadata.linearChannels : processor.imgdata.idata.colors, 3, 4);
        outData.metadata.linearChannels = channels;
        const int visibleWidth = outData.metadata.visibleWidth > 0 ? outData.metadata.visibleWidth : rawWidth;
        const int visibleHeight = outData.metadata.visibleHeight > 0 ? outData.metadata.visibleHeight : rawHeight;
        const int left = std::max(0, outData.metadata.leftMargin);
        const int top = std::max(0, outData.metadata.topMargin);
        if (rawWidth <= 0 || rawHeight <= 0 || visibleWidth <= 0 || visibleHeight <= 0) {
            outData.metadata.error = "Linear DNG has invalid dimensions.";
            processor.recycle();
            return false;
        }
        const auto& rawdata = processor.imgdata.rawdata;
        if (rawdata.color3_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(3 * sizeof(std::uint16_t)) : rawWidth;
            CopyColorImageToUInt16(rawdata.color3_image, visibleWidth, visibleHeight, left, top, stride, 3, channels, outData.linearUInt16Buffer);
            outData.metadata.linearSampleFormat = RawSampleFormat::UInt16;
        } else if (rawdata.color4_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(4 * sizeof(std::uint16_t)) : rawWidth;
            CopyColorImageToUInt16(rawdata.color4_image, visibleWidth, visibleHeight, left, top, stride, 4, channels, outData.linearUInt16Buffer);
            outData.metadata.linearSampleFormat = RawSampleFormat::UInt16;
        } else if (rawdata.float3_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(3 * sizeof(float)) : rawWidth;
            CopyColorImageToFloat(rawdata.float3_image, visibleWidth, visibleHeight, left, top, stride, 3, channels, outData.linearFloatBuffer);
            outData.metadata.linearSampleFormat = RawSampleFormat::Float32;
        } else if (rawdata.float4_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(4 * sizeof(float)) : rawWidth;
            CopyColorImageToFloat(rawdata.float4_image, visibleWidth, visibleHeight, left, top, stride, 4, channels, outData.linearFloatBuffer);
            outData.metadata.linearSampleFormat = RawSampleFormat::Float32;
        } else {
            outData.metadata.error = "Linear DNG detected but LibRaw did not expose a color3/color4 or float color buffer.";
            processor.recycle();
            return false;
        }
        ExtractLinearStats(outData);
        const int warnings = processor.imgdata.process_warnings;
        if (warnings != 0) {
            outData.metadata.warnings.push_back("LibRaw reported process warnings: " + std::to_string(warnings));
        }
        processor.recycle();
        return true;
    }

    if (!processor.imgdata.rawdata.raw_image || rawWidth <= 0 || rawHeight <= 0 || pixelCount == 0) {
        outData.metadata.error = "LibRaw did not expose a mosaiced raw_image buffer after unpack().";
        processor.recycle();
        return false;
    }

    outData.rawBuffer.assign(
        processor.imgdata.rawdata.raw_image,
        processor.imgdata.rawdata.raw_image + pixelCount);
    ExtractRawStats(outData);

    const int warnings = processor.imgdata.process_warnings;
    if (warnings != 0) {
        outData.metadata.warnings.push_back("LibRaw reported process warnings: " + std::to_string(warnings));
    }

    processor.recycle();
    return true;
#endif
}

} // namespace Raw
