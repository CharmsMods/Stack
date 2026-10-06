#include "DngMetadataSupplement.h"
#include "Raw/RawImageData.h"
#include "Raw/Internal/DngTiffReader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace Raw {
namespace {
using Internal::DngTiffReader;

CfaPattern PatternFromString(const std::string& pattern) {
    if (pattern == "RGGB") return CfaPattern::RGGB;
    if (pattern == "BGGR") return CfaPattern::BGGR;
    if (pattern == "GBRG") return CfaPattern::GBRG;
    if (pattern == "GRBG") return CfaPattern::GRBG;
    return CfaPattern::Unknown;
}

int EstimateBitDepth(float whiteLevel) {
    if (whiteLevel <= 0.0f) {
        return 0;
    }
    return static_cast<int>(std::ceil(std::log2(whiteLevel + 1.0f)));
}

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

int CountDngOpcodes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 4) return 0;
    const std::uint32_t declared = ReadBeU32(bytes, 0);
    std::size_t p = 4;
    int count = 0;
    for (std::uint32_t i = 0; i < declared && p + 16 <= bytes.size(); ++i) {
        const std::uint32_t byteCount = ReadBeU32(bytes, p + 12);
        p += 16;
        if (p + byteCount > bytes.size()) break;
        ++count;
        p += byteCount;
    }
    return count;
}

void ParseDngOpcodeList2(const std::vector<std::uint8_t>& bytes, RawMetadata& metadata,
    const std::function<bool()>& shouldCancel) {
    if (bytes.size() < 4) return;
    metadata.dngOpcodeCount[1] = CountDngOpcodes(bytes);
    const std::uint32_t count = ReadBeU32(bytes, 0);
    std::size_t p = 4;
    for (std::uint32_t i = 0; i < count && p + 16 <= bytes.size(); ++i) {
        if (shouldCancel && shouldCancel()) return;
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
        map.planes = ReadBeI32(bytes, p + 20);
        map.rowPitch = ReadBeI32(bytes, p + 24);
        map.colPitch = ReadBeI32(bytes, p + 28);
        map.mapPointsV = ReadBeI32(bytes, p + 32);
        map.mapPointsH = ReadBeI32(bytes, p + 36);
        map.mapSpacingV = ReadBeDouble(bytes, p + 40);
        map.mapSpacingH = ReadBeDouble(bytes, p + 48);
        map.mapOriginV = ReadBeDouble(bytes, p + 56);
        map.mapOriginH = ReadBeDouble(bytes, p + 64);
        map.mapPlanes = ReadBeI32(bytes, p + 72);
        const std::size_t gainOffset = p + 76;
        const bool validShape =
            map.top < map.bottom &&
            map.left < map.right &&
            map.plane >= 0 &&
            map.planes > 0 &&
            map.rowPitch > 0 &&
            map.colPitch > 0 &&
            map.mapPointsV > 0 &&
            map.mapPointsH > 0 &&
            map.mapPlanes == 1 &&
            std::isfinite(map.mapSpacingV) &&
            std::isfinite(map.mapSpacingH) &&
            std::isfinite(map.mapOriginV) &&
            std::isfinite(map.mapOriginH) &&
            map.mapSpacingV > 0.0 &&
            map.mapSpacingH > 0.0;
        if (!validShape) {
            ++metadata.dngUnsupportedOpcodeCount;
            p += byteCount;
            continue;
        }
        const std::size_t gainCount = static_cast<std::size_t>(map.mapPointsV) *
            static_cast<std::size_t>(map.mapPointsH);
        if (gainCount >
                (std::numeric_limits<std::size_t>::max() - gainOffset) / sizeof(float) ||
            gainOffset + gainCount * sizeof(float) > p + byteCount) {
            ++metadata.dngUnsupportedOpcodeCount;
            p += byteCount;
            continue;
        }
        map.gains.resize(gainCount);
        for (std::size_t g = 0; g < gainCount; ++g) {
            if ((g % 4096) == 0 && shouldCancel && shouldCancel()) return;
            map.gains[g] = ReadBeFloat(bytes, gainOffset + g * sizeof(float));
        }
        if (!std::all_of(map.gains.begin(), map.gains.end(), [](float gain) {
                return std::isfinite(gain) && gain >= 0.0f;
            })) {
            ++metadata.dngUnsupportedOpcodeCount;
            p += byteCount;
            continue;
        }
        metadata.dngGainMaps.push_back(std::move(map));
        p += byteCount;
    }
    metadata.dngGainMapCount = static_cast<int>(metadata.dngGainMaps.size());
    metadata.dngAppliedOpcodeCountByList[1] = metadata.dngGainMapCount;
    metadata.dngUnsupportedOpcodeCountByList[1] =
        std::max(0, metadata.dngOpcodeCount[1] - metadata.dngAppliedOpcodeCountByList[1]);
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

// Only these tags have numeric consumers. Blob and unknown tags must not be
// expanded to doubles while scanning the directory.
std::size_t NumericTagLimit(std::uint16_t tag) {
    switch (tag) {
        case 259: case 262: case 274: case 50711: case 50730: case 50731:
        case 50734: case 50778: case 50779: return 1;
        case 33421: case 50713: return 2;
        case 50710: case 50727: case 50728: return 3;
        case 33422: case 50706: case 50829: return 4;
        case 50721: case 50722: case 50723: case 50724: case 50964: case 50965: return 9;
        case 50712: case 50714: case 50715: case 50716: case 50717: case 50830: case 51041:
            return std::numeric_limits<std::size_t>::max();
        default: return 0;
    }
}

bool ValidTagNumbers(std::uint16_t tag, const std::vector<double>& values) {
    const bool integer = tag == 259 || tag == 262 || tag == 274 || tag == 33421 || tag == 33422 ||
        tag == 50710 || tag == 50711 || tag == 50713 || tag == 50778 || tag == 50779 ||
        tag == 50829 || tag == 50830;
    return std::all_of(values.begin(), values.end(), [integer](double value) {
        return std::isfinite(value) && (integer
            ? value >= 0 && value <= std::numeric_limits<int>::max()
            : std::abs(value) <= std::numeric_limits<float>::max());
    });
}

} // namespace

bool ApplyDngSupplement(const std::filesystem::path& path, RawMetadata& metadata,
    const std::function<bool()>& shouldCancel) {
    if (!metadata.isDng) return true;
    DngTiffReader reader(path, shouldCancel);
    if (!reader.Valid()) {
        if (!reader.Warning().empty()) metadata.warnings.push_back(reader.Warning());
        return !reader.Cancelled();
    }

    const std::vector<std::uint32_t> ifdOffsets = reader.ReadIfdTree();
    std::uint32_t rawIfd = reader.FirstIfd();
    int bestScore = std::numeric_limits<int>::min();
    std::uint64_t bestArea = 0;
    for (std::uint32_t offset : ifdOffsets) {
        if (reader.Cancelled()) return false;
        const std::vector<DngTiffReader::Entry> entries = reader.ReadEntries(offset);
        int score = 0;
        std::uint64_t width = 0;
        std::uint64_t height = 0;
        for (const DngTiffReader::Entry& entry : entries) {
            switch (entry.tag) {
                case 254: case 256: case 257: case 262: case 50714: case 50717: case 50829: break;
                default: continue;
            }
            const std::vector<double> values = reader.NumberValues(entry, 1);
            if (!values.empty() && (values[0] < 0 || values[0] > std::numeric_limits<std::uint32_t>::max())) continue;
            if (entry.tag == 262 && !values.empty() && values[0] == 32803) score += 100;
            if ((entry.tag == 50714 || entry.tag == 50717 || entry.tag == 50829) && !values.empty()) score += 20;
            if (entry.tag == 254 && !values.empty() && static_cast<std::uint32_t>(values[0]) == 0) score += 10;
            if (entry.tag == 256 && !values.empty()) width = static_cast<std::uint64_t>(values[0]);
            if (entry.tag == 257 && !values.empty()) height = static_cast<std::uint64_t>(values[0]);
        }
        const std::uint64_t area = width * height;
        if (score > bestScore || (score == bestScore && area > bestArea)) {
            bestScore = score;
            bestArea = area;
            rawIfd = offset;
        }
    }

    std::vector<std::uint32_t> selectedIfds { reader.FirstIfd() };
    if (rawIfd != reader.FirstIfd()) selectedIfds.push_back(rawIfd);
    for (std::uint32_t offset : selectedIfds) {
        if (reader.Cancelled()) return false;
        const std::vector<DngTiffReader::Entry> entries = reader.ReadEntries(offset);
        for (const DngTiffReader::Entry& entry : entries) {
            if (reader.Cancelled()) return false;
            const auto limit = NumericTagLimit(entry.tag);
            const std::vector<double> values = limit > 0 ? reader.NumberValues(entry, limit) : std::vector<double> {};
            if (limit > 0 && values.empty()) continue;
            if (!ValidTagNumbers(entry.tag, values)) {
                metadata.warnings.push_back("DNG supplement skipped out-of-range values in tag " + std::to_string(entry.tag) + ".");
                continue;
            }
            switch (entry.tag) {
                case 34665: { // ExifIFD pointer. SubjectDistance is not copied by LibRaw.
                    const auto offsets=reader.NumberValues(entry,1);
                    if(offsets.empty()||offsets[0]<=0||offsets[0]>std::numeric_limits<std::uint32_t>::max()) break;
                    for(const auto& exif:reader.ReadEntries(static_cast<std::uint32_t>(offsets[0]))) {
                        if(exif.tag!=37382) continue;
                        const auto distance=reader.NumberValues(exif,1);
                        if(!distance.empty()&&distance[0]>0&&distance[0]<1e8) {
                            metadata.focusDistanceMeters=static_cast<float>(distance[0]);
                            metadata.hasFocusDistance=true;
                        }
                    }
                    break;
                }
                case 274:
                    if (!values.empty()) {
                        const int orientation = static_cast<int>(std::lround(values[0]));
                        if (orientation >= 1 && orientation <= 8) metadata.orientation = orientation;
                    }
                    break;
                case 259: if (!values.empty()) metadata.dngCompression = static_cast<int>(values[0]); break;
                case 262: if (!values.empty()) metadata.dngPhotometricInterpretation = static_cast<int>(values[0]); break;
                case 50706: if (values.size() >= 4) metadata.isDng = true; break;
                case 50708: metadata.dngUniqueCameraModel = reader.StringValue(entry); break;
                case 50710: CopyNumbers(values, metadata.dngCfaPlaneColor); break;
                case 50711: if (!values.empty()) metadata.dngCfaLayout = static_cast<int>(values[0]); break;
                case 50712:
                    metadata.dngLinearizationTable.clear();
                    metadata.dngLinearizationTable.reserve(values.size());
                    for (double value : values) {
                        metadata.dngLinearizationTable.push_back(static_cast<std::uint16_t>(
                            std::lround(std::clamp(value, 0.0, 65535.0))));
                    }
                    break;
                case 50713: CopyNumbers(values, metadata.dngBlackLevelRepeatDim); break;
                case 50714: {
                    CopyNumbers(values, metadata.dngBlackLevelPattern);
                    metadata.dngBlackLevelValues.clear();
                    metadata.dngBlackLevelValues.reserve(values.size());
                    for (double value : values) metadata.dngBlackLevelValues.push_back(static_cast<float>(value));
                    if (!values.empty()) {
                        const double sum = std::accumulate(values.begin(), values.end(), 0.0);
                        metadata.blackLevel = static_cast<float>(sum / static_cast<double>(values.size()));
                        metadata.perChannelBlack = metadata.dngBlackLevelPattern;
                        metadata.blackLevelSource = "DNG BlackLevel tag";
                    }
                    break;
                }
                case 50715:
                    metadata.dngBlackLevelDeltaH.clear();
                    for (double value : values) metadata.dngBlackLevelDeltaH.push_back(static_cast<float>(value));
                    break;
                case 50716:
                    metadata.dngBlackLevelDeltaV.clear();
                    for (double value : values) metadata.dngBlackLevelDeltaV.push_back(static_cast<float>(value));
                    break;
                case 50717:
                    metadata.dngWhiteLevelValues.clear();
                    for (double value : values) metadata.dngWhiteLevelValues.push_back(static_cast<float>(value));
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
                        for (std::size_t plane = 0; plane < 3; ++plane) {
                            const int mappedColor = metadata.dngCfaPlaneColor[plane];
                            const std::size_t color = mappedColor >= 0 && mappedColor < 3
                                ? static_cast<std::size_t>(mappedColor)
                                : plane;
                            metadata.cameraWhiteBalance[color] =
                                1.0f / (metadata.dngAsShotNeutral[plane] * metadata.dngAnalogBalance[plane]);
                        }
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
                case 50731:
                    if (!values.empty() && std::isfinite(values[0]) && values[0] > 0.0) {
                        metadata.dngBaselineNoise = static_cast<float>(values[0]);
                        metadata.hasDngBaselineNoise = true;
                    }
                    break;
                case 50734:
                    if (!values.empty() && std::isfinite(values[0]) && values[0] > 0.0 && values[0] <= 1.0) {
                        metadata.dngLinearResponseLimit = static_cast<float>(values[0]);
                        metadata.hasDngLinearResponseLimit = true;
                    }
                    break;
                case 50829:
                    if (values.size() >= 4) {
                        metadata.dngActiveArea = {
                            static_cast<int>(values[0]), static_cast<int>(values[1]),
                            static_cast<int>(values[2]), static_cast<int>(values[3])
                        };
                        metadata.hasDngActiveArea = true;
                    }
                    break;
                case 50830:
                    metadata.dngMaskedAreas.clear();
                    for (std::size_t i = 0; i + 3 < values.size(); i += 4) {
                        metadata.dngMaskedAreas.push_back({
                            static_cast<int>(values[i]), static_cast<int>(values[i + 1]),
                            static_cast<int>(values[i + 2]), static_cast<int>(values[i + 3])
                        });
                    }
                    break;
                case 50778: if (!values.empty()) metadata.dngIlluminant1 = static_cast<int>(values[0]); break;
                case 50779: if (!values.empty()) metadata.dngIlluminant2 = static_cast<int>(values[0]); break;
                case 50964: CopyNumbers(values, metadata.dngForwardMatrix1); metadata.hasDngForwardMatrix1 = values.size() >= 9; break;
                case 50965: CopyNumbers(values, metadata.dngForwardMatrix2); metadata.hasDngForwardMatrix2 = values.size() >= 9; break;
                case 51008: {
                    const std::vector<std::uint8_t> bytes = reader.RawBytes(entry);
                    metadata.dngOpcodeCount[0] = CountDngOpcodes(bytes);
                    metadata.dngUnsupportedOpcodeCountByList[0] = metadata.dngOpcodeCount[0];
                    break;
                }
                case 51009: ParseDngOpcodeList2(reader.RawBytes(entry), metadata, [&reader] { return reader.Cancelled(); }); break;
                case 51022: {
                    const std::vector<std::uint8_t> bytes = reader.RawBytes(entry);
                    metadata.dngOpcodeCount[2] = CountDngOpcodes(bytes);
                    metadata.dngUnsupportedOpcodeCountByList[2] = metadata.dngOpcodeCount[2];
                    break;
                }
                case 51041:
                    metadata.dngNoiseProfile.clear();
                    if (values.size() >= 2 && (values.size() == 2 || (values.size() % 2) == 0)) {
                        for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
                            metadata.dngNoiseProfile.push_back({ values[i], values[i + 1] });
                        }
                        metadata.hasDngNoiseProfile = std::all_of(
                            metadata.dngNoiseProfile.begin(), metadata.dngNoiseProfile.end(),
                            [](const DngNoiseProfilePlane& plane) {
                                return std::isfinite(plane.shotScale) && plane.shotScale > 0.0 &&
                                    std::isfinite(plane.readNoiseVariance) && plane.readNoiseVariance >= 0.0;
                            });
                    }
                    break;
                case 52525: metadata.hasDngProfileGainTableMap = entry.count > 0; break;
                case 52544: metadata.hasDngProfileGainTableMap2 = entry.count > 0; break;
                case 33421: CopyNumbers(values, metadata.dngCfaRepeatPatternDim); break;
                case 33422: CopyNumbers(values, metadata.dngCfaPattern); break;
                default: break;
            }
        }
    }

    if (!reader.Warning().empty()) metadata.warnings.push_back(reader.Warning());
    if (reader.Cancelled()) return false;

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
    return true;
}

} // namespace Raw
