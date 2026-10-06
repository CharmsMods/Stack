#include "Raw/RawViewportSettings.h"
#include "RawWorkspace.h"
#include "Raw/Internal/RawWorkspaceStorageIO.h"

#include "RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace Stack::RawWorkspace {
using namespace StorageIO;
namespace {

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::string GenericPathKey(const std::filesystem::path& path) {
    return path.generic_u8string();
}

bool IsPathWithinRoot(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root) {
    const std::string candidateKey = ToLowerAscii(
        NormalizePath(candidate).generic_u8string());
    const std::string rootKey = ToLowerAscii(
        NormalizePath(root).generic_u8string());
    if (candidateKey.empty() || rootKey.empty() || candidateKey == rootKey) {
        return false;
    }
    const std::string prefix = rootKey.back() == '/'
        ? rootKey
        : rootKey + "/";
    return candidateKey.rfind(prefix, 0) == 0;
}

std::int64_t FileTimeTicks(const std::filesystem::file_time_type& fileTime) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(fileTime.time_since_epoch()).count();
}

std::int64_t FileTimeUnixSeconds(const std::filesystem::file_time_type& fileTime) {
    const auto age = std::filesystem::file_time_type::clock::now() - fileTime;
    const auto systemTime = std::chrono::system_clock::now() -
        std::chrono::duration_cast<std::chrono::system_clock::duration>(age);
    return std::chrono::duration_cast<std::chrono::seconds>(
        systemTime.time_since_epoch()).count();
}

std::int64_t UnixSecondsNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

const nlohmann::json* FindJsonMember(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || key == nullptr) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &(*it);
}

std::string JsonStringOrDefault(
    const nlohmann::json& object,
    const char* key,
    const std::string& fallback = {}) {
    const nlohmann::json* value = FindJsonMember(object, key);
    if (value == nullptr || value->is_null()) {
        return fallback;
    }
    return value->is_string() ? value->get<std::string>() : fallback;
}

int JsonIntOrDefault(const nlohmann::json& object, const char* key, int fallback = 0) {
    const nlohmann::json* value = FindJsonMember(object, key);
    if (value == nullptr || value->is_null()) {
        return fallback;
    }
    if (value->is_number_integer()) {
        return value->get<int>();
    }
    if (value->is_number_unsigned()) {
        const auto unsignedValue = value->get<unsigned long long>();
        if (unsignedValue <= static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            return static_cast<int>(unsignedValue);
        }
    }
    return fallback;
}

std::int64_t JsonInt64OrDefault(
    const nlohmann::json& object,
    const char* key,
    std::int64_t fallback = 0) {
    const nlohmann::json* value = FindJsonMember(object, key);
    if (value == nullptr || value->is_null()) {
        return fallback;
    }
    if (value->is_number_integer()) {
        return value->get<std::int64_t>();
    }
    if (value->is_number_unsigned()) {
        const auto unsignedValue = value->get<unsigned long long>();
        if (unsignedValue <= static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(unsignedValue);
        }
    }
    return fallback;
}

std::uintmax_t JsonUintMaxOrDefault(
    const nlohmann::json& object,
    const char* key,
    std::uintmax_t fallback = 0) {
    const nlohmann::json* value = FindJsonMember(object, key);
    if (value == nullptr || value->is_null()) {
        return fallback;
    }
    if (value->is_number_unsigned()) {
        return value->get<std::uintmax_t>();
    }
    if (value->is_number_integer()) {
        const auto signedValue = value->get<std::int64_t>();
        if (signedValue >= 0) {
            return static_cast<std::uintmax_t>(signedValue);
        }
    }
    return fallback;
}

bool WriteBinaryFile(const std::filesystem::path& path, const std::vector<unsigned char>& bytes, std::string* outError) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create " + path.parent_path().u8string() + ": " + ec.message();
        }
        return false;
    }

    const std::filesystem::path tempPath = MakeTempPath(path);
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (outError) {
            *outError = "Failed to open " + tempPath.u8string() + " for writing.";
        }
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to write " + tempPath.u8string() + ".";
        }
        out.close();
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    out.close();
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to close " + tempPath.u8string() + ".";
        }
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (!ReplaceFileAtomically(tempPath, path, outError)) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::string SanitizedThumbnailStem(const SourceRecord& source) {
    std::string stem = source.stem.empty() ? source.fileName : source.stem;
    for (char& ch : stem) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) || ch == '_' || ch == '-' || ch == '.')) {
            ch = '_';
        }
    }
    return stem.empty() ? std::string("source") : stem;
}

std::filesystem::path ThumbnailRelativePathForSource(
    const SourceRecord& source,
    const char* suffix = ".thumb.png") {
    std::filesystem::path relative;
    if (!source.parentFolderKey.empty()) {
        relative = std::filesystem::u8path(source.parentFolderKey);
    }
    relative /= SanitizedThumbnailStem(source) + suffix;
    return relative;
}

std::filesystem::path ThumbnailSignatureRelativePathForSource(
    const SourceRecord& source,
    const char* suffix = ".thumb.json") {
    std::filesystem::path relative;
    if (!source.parentFolderKey.empty()) {
        relative = std::filesystem::u8path(source.parentFolderKey);
    }
    relative /= SanitizedThumbnailStem(source) + suffix;
    return relative;
}

nlohmann::json SerializeThumbnailSignature(const ThumbnailSignature& signature) {
    nlohmann::json value = nlohmann::json::object();
    value["schema"] = "stack.rawWorkspace.thumbnailSignature";
    value["schemaVersion"] = signature.schemaVersion;
    value["sourceRelativePath"] = signature.sourceRelativePath;
    value["sourceFileSizeBytes"] = signature.sourceFileSizeBytes;
    value["sourceModifiedTimeTicks"] = signature.sourceModifiedTimeTicks;
    value["sourceFingerprint"] = signature.sourceFingerprint.empty() ? nlohmann::json() : nlohmann::json(signature.sourceFingerprint);
    value["rawLoaderAlgorithmVersion"] = signature.rawLoaderAlgorithmVersion;
    value["neutralPreviewSettingsVersion"] = signature.neutralPreviewSettingsVersion;
    value["thumbnailVersion"] = signature.thumbnailVersion;
    value["maxDimension"] = signature.maxDimension;
    return value;
}

float NormalizeRawValue(float value, const Raw::RawMetadata& metadata) {
    const float black = metadata.blackLevel;
    const float white = std::max(black + 1.0f, metadata.whiteLevel);
    return std::clamp((value - black) / (white - black), 0.0f, 1.0f);
}

unsigned char ToSrgbByte(float linear) {
    const float encoded = std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
    return static_cast<unsigned char>(std::clamp(encoded * 255.0f + 0.5f, 0.0f, 255.0f));
}

int CfaColorAt(const Raw::RawMetadata& metadata, int x, int y) {
    switch (metadata.cfaPattern) {
        case Raw::CfaPattern::RGGB:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 0 : 1) : ((x & 1) == 0 ? 1 : 2);
        case Raw::CfaPattern::BGGR:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 2 : 1) : ((x & 1) == 0 ? 1 : 0);
        case Raw::CfaPattern::GBRG:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 1 : 2) : ((x & 1) == 0 ? 0 : 1);
        case Raw::CfaPattern::GRBG:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 1 : 0) : ((x & 1) == 0 ? 2 : 1);
        case Raw::CfaPattern::Unknown:
        default:
            return -1;
    }
}

bool BuildFastMosaicThumbnailPixels(const Raw::RawImageData& raw, int maxDimension, std::vector<unsigned char>& outPixels, int& outW, int& outH) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int visibleW = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    const int visibleH = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    const int rawW = metadata.rawWidth;
    const int rawH = metadata.rawHeight;
    if (rawW <= 0 || rawH <= 0 || visibleW <= 0 || visibleH <= 0 || raw.rawBuffer.empty()) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) / static_cast<float>(std::max(visibleW, visibleH));
    outW = std::max(1, static_cast<int>(std::floor(static_cast<float>(visibleW) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(static_cast<float>(visibleH) * std::min(1.0f, scale))));
    outPixels.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u, 255);

    const int left = std::clamp(metadata.leftMargin, 0, std::max(0, rawW - 1));
    const int top = std::clamp(metadata.topMargin, 0, std::max(0, rawH - 1));
    const float wbR = metadata.cameraWhiteBalance[0] > 0.001f ? metadata.cameraWhiteBalance[0] : 1.0f;
    const float wbG = metadata.cameraWhiteBalance[1] > 0.001f ? metadata.cameraWhiteBalance[1] : 1.0f;
    const float wbB = metadata.cameraWhiteBalance[2] > 0.001f ? metadata.cameraWhiteBalance[2] : 1.0f;
    const float wbScale = 1.0f / std::max(0.001f, wbG);

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const int centerX = left + std::clamp(static_cast<int>((static_cast<float>(x) + 0.5f) * static_cast<float>(visibleW) / static_cast<float>(outW)), 0, visibleW - 1);
            const int centerY = top + std::clamp(static_cast<int>((static_cast<float>(y) + 0.5f) * static_cast<float>(visibleH) / static_cast<float>(outH)), 0, visibleH - 1);
            const int baseX = std::clamp(centerX & ~1, 0, std::max(0, rawW - 2));
            const int baseY = std::clamp(centerY & ~1, 0, std::max(0, rawH - 2));

            float channels[3] = { 0.0f, 0.0f, 0.0f };
            int counts[3] = { 0, 0, 0 };
            for (int oy = 0; oy < 2; ++oy) {
                for (int ox = 0; ox < 2; ++ox) {
                    const int sx = std::clamp(baseX + ox, 0, rawW - 1);
                    const int sy = std::clamp(baseY + oy, 0, rawH - 1);
                    const int color = CfaColorAt(metadata, sx, sy);
                    const float value = NormalizeRawValue(
                        static_cast<float>(raw.rawBuffer[static_cast<std::size_t>(sy) * rawW + sx]),
                        metadata);
                    if (color >= 0 && color < 3) {
                        channels[color] += value;
                        ++counts[color];
                    } else {
                        channels[0] += value;
                        channels[1] += value;
                        channels[2] += value;
                        ++counts[0];
                        ++counts[1];
                        ++counts[2];
                    }
                }
            }

            float r = counts[0] > 0 ? channels[0] / static_cast<float>(counts[0]) : channels[1];
            float g = counts[1] > 0 ? channels[1] / static_cast<float>(counts[1]) : (r + channels[2]) * 0.5f;
            float b = counts[2] > 0 ? channels[2] / static_cast<float>(counts[2]) : g;
            r = std::clamp(r * wbR * wbScale, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b * wbB * wbScale, 0.0f, 1.0f);

            const std::size_t dst = (static_cast<std::size_t>(y) * outW + static_cast<std::size_t>(x)) * 4u;
            outPixels[dst + 0] = ToSrgbByte(r);
            outPixels[dst + 1] = ToSrgbByte(g);
            outPixels[dst + 2] = ToSrgbByte(b);
            outPixels[dst + 3] = 255;
        }
    }
    return true;
}

bool BuildFastLinearThumbnailPixels(const Raw::RawImageData& raw, int maxDimension, std::vector<unsigned char>& outPixels, int& outW, int& outH) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int srcW = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    const int srcH = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    const int channels = std::clamp(metadata.linearChannels > 0 ? metadata.linearChannels : 3, 3, 4);
    if (srcW <= 0 || srcH <= 0) {
        return false;
    }

    const bool useUInt16 = raw.linearUInt16Buffer.size() >= static_cast<std::size_t>(srcW) * srcH * channels;
    const bool useFloat = raw.linearFloatBuffer.size() >= static_cast<std::size_t>(srcW) * srcH * channels;
    if (!useUInt16 && !useFloat) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) / static_cast<float>(std::max(srcW, srcH));
    outW = std::max(1, static_cast<int>(std::floor(static_cast<float>(srcW) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(static_cast<float>(srcH) * std::min(1.0f, scale))));
    outPixels.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u, 255);

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const int sx = std::clamp(static_cast<int>((static_cast<float>(x) + 0.5f) * static_cast<float>(srcW) / static_cast<float>(outW)), 0, srcW - 1);
            const int sy = std::clamp(static_cast<int>((static_cast<float>(y) + 0.5f) * static_cast<float>(srcH) / static_cast<float>(outH)), 0, srcH - 1);
            const std::size_t src = (static_cast<std::size_t>(sy) * srcW + static_cast<std::size_t>(sx)) * static_cast<std::size_t>(channels);
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            if (useUInt16) {
                r = static_cast<float>(raw.linearUInt16Buffer[src + 0]) / 65535.0f;
                g = static_cast<float>(raw.linearUInt16Buffer[src + 1]) / 65535.0f;
                b = static_cast<float>(raw.linearUInt16Buffer[src + 2]) / 65535.0f;
            } else {
                r = raw.linearFloatBuffer[src + 0];
                g = raw.linearFloatBuffer[src + 1];
                b = raw.linearFloatBuffer[src + 2];
            }

            const std::size_t dst = (static_cast<std::size_t>(y) * outW + static_cast<std::size_t>(x)) * 4u;
            outPixels[dst + 0] = ToSrgbByte(r);
            outPixels[dst + 1] = ToSrgbByte(g);
            outPixels[dst + 2] = ToSrgbByte(b);
            outPixels[dst + 3] = 255;
        }
    }
    return true;
}

bool BuildHighQualityMosaicThumbnailPixels(
    const Raw::RawImageData& raw,
    int maxDimension,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int visibleW = metadata.visibleWidth > 0
        ? metadata.visibleWidth
        : metadata.rawWidth;
    const int visibleH = metadata.visibleHeight > 0
        ? metadata.visibleHeight
        : metadata.rawHeight;
    const int rawW = metadata.rawWidth;
    const int rawH = metadata.rawHeight;
    if (rawW <= 0 || rawH <= 0 || visibleW <= 0 || visibleH <= 0 ||
        raw.rawBuffer.size() <
            static_cast<std::size_t>(rawW) * static_cast<std::size_t>(rawH)) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) /
        static_cast<float>(std::max(visibleW, visibleH));
    outW = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(visibleW) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(visibleH) * std::min(1.0f, scale))));
    outPixels.assign(
        static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u,
        255);

    const int left = std::clamp(metadata.leftMargin, 0, std::max(0, rawW - 1));
    const int top = std::clamp(metadata.topMargin, 0, std::max(0, rawH - 1));
    const int visibleRight = std::min(rawW, left + visibleW);
    const int visibleBottom = std::min(rawH, top + visibleH);
    const float wbR = metadata.cameraWhiteBalance[0] > 0.001f
        ? metadata.cameraWhiteBalance[0]
        : 1.0f;
    const float wbG = metadata.cameraWhiteBalance[1] > 0.001f
        ? metadata.cameraWhiteBalance[1]
        : 1.0f;
    const float wbB = metadata.cameraWhiteBalance[2] > 0.001f
        ? metadata.cameraWhiteBalance[2]
        : 1.0f;
    const float wbScale = 1.0f / std::max(0.001f, wbG);

    for (int y = 0; y < outH; ++y) {
        int sourceY0 = top + static_cast<int>(
            (static_cast<std::int64_t>(y) * visibleH) / outH);
        int sourceY1 = top + static_cast<int>(
            (static_cast<std::int64_t>(y + 1) * visibleH + outH - 1) / outH);
        sourceY0 = std::clamp(sourceY0, top, std::max(top, visibleBottom - 1));
        sourceY1 = std::clamp(sourceY1, sourceY0 + 1, visibleBottom);
        if (sourceY1 - sourceY0 < 2 && visibleBottom - top >= 2) {
            sourceY0 = std::clamp(sourceY0 - 1, top, visibleBottom - 2);
            sourceY1 = sourceY0 + 2;
        }
        for (int x = 0; x < outW; ++x) {
            int sourceX0 = left + static_cast<int>(
                (static_cast<std::int64_t>(x) * visibleW) / outW);
            int sourceX1 = left + static_cast<int>(
                (static_cast<std::int64_t>(x + 1) * visibleW + outW - 1) / outW);
            sourceX0 = std::clamp(sourceX0, left, std::max(left, visibleRight - 1));
            sourceX1 = std::clamp(sourceX1, sourceX0 + 1, visibleRight);
            if (sourceX1 - sourceX0 < 2 && visibleRight - left >= 2) {
                sourceX0 = std::clamp(sourceX0 - 1, left, visibleRight - 2);
                sourceX1 = sourceX0 + 2;
            }

            double channels[3] = { 0.0, 0.0, 0.0 };
            int counts[3] = { 0, 0, 0 };
            for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY) {
                for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX) {
                    const int color = CfaColorAt(metadata, sourceX, sourceY);
                    const float value = NormalizeRawValue(
                        static_cast<float>(raw.rawBuffer[
                            static_cast<std::size_t>(sourceY) * rawW + sourceX]),
                        metadata);
                    if (color >= 0 && color < 3) {
                        channels[color] += value;
                        ++counts[color];
                    } else {
                        for (int channel = 0; channel < 3; ++channel) {
                            channels[channel] += value;
                            ++counts[channel];
                        }
                    }
                }
            }

            const float fallback = counts[1] > 0
                ? static_cast<float>(channels[1] / counts[1])
                : 0.0f;
            float r = counts[0] > 0
                ? static_cast<float>(channels[0] / counts[0])
                : fallback;
            float g = counts[1] > 0
                ? static_cast<float>(channels[1] / counts[1])
                : fallback;
            float b = counts[2] > 0
                ? static_cast<float>(channels[2] / counts[2])
                : fallback;
            r = std::clamp(r * wbR * wbScale, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b * wbB * wbScale, 0.0f, 1.0f);

            const std::size_t destination =
                (static_cast<std::size_t>(y) * outW + x) * 4u;
            outPixels[destination + 0] = ToSrgbByte(r);
            outPixels[destination + 1] = ToSrgbByte(g);
            outPixels[destination + 2] = ToSrgbByte(b);
            outPixels[destination + 3] = 255;
        }
    }
    return true;
}

bool BuildHighQualityLinearThumbnailPixels(
    const Raw::RawImageData& raw,
    int maxDimension,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int sourceWidth = metadata.visibleWidth > 0
        ? metadata.visibleWidth
        : metadata.rawWidth;
    const int sourceHeight = metadata.visibleHeight > 0
        ? metadata.visibleHeight
        : metadata.rawHeight;
    const int channels = std::clamp(
        metadata.linearChannels > 0 ? metadata.linearChannels : 3,
        3,
        4);
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }
    const bool useUInt16 = raw.linearUInt16Buffer.size() >=
        static_cast<std::size_t>(sourceWidth) * sourceHeight * channels;
    const bool useFloat = raw.linearFloatBuffer.size() >=
        static_cast<std::size_t>(sourceWidth) * sourceHeight * channels;
    if (!useUInt16 && !useFloat) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) /
        static_cast<float>(std::max(sourceWidth, sourceHeight));
    outW = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(sourceWidth) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(sourceHeight) * std::min(1.0f, scale))));
    outPixels.assign(
        static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u,
        255);

    for (int y = 0; y < outH; ++y) {
        const int sourceY0 = static_cast<int>(
            (static_cast<std::int64_t>(y) * sourceHeight) / outH);
        const int sourceY1 = std::max(
            sourceY0 + 1,
            static_cast<int>(
                (static_cast<std::int64_t>(y + 1) * sourceHeight) / outH));
        for (int x = 0; x < outW; ++x) {
            const int sourceX0 = static_cast<int>(
                (static_cast<std::int64_t>(x) * sourceWidth) / outW);
            const int sourceX1 = std::max(
                sourceX0 + 1,
                static_cast<int>(
                    (static_cast<std::int64_t>(x + 1) * sourceWidth) / outW));
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            int sampleCount = 0;
            for (int sourceY = sourceY0;
                 sourceY < std::min(sourceY1, sourceHeight);
                 ++sourceY) {
                for (int sourceX = sourceX0;
                     sourceX < std::min(sourceX1, sourceWidth);
                     ++sourceX) {
                    const std::size_t source =
                        (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) *
                        static_cast<std::size_t>(channels);
                    if (useUInt16) {
                        red += static_cast<double>(raw.linearUInt16Buffer[source + 0]) /
                            65535.0;
                        green += static_cast<double>(raw.linearUInt16Buffer[source + 1]) /
                            65535.0;
                        blue += static_cast<double>(raw.linearUInt16Buffer[source + 2]) /
                            65535.0;
                    } else {
                        red += raw.linearFloatBuffer[source + 0];
                        green += raw.linearFloatBuffer[source + 1];
                        blue += raw.linearFloatBuffer[source + 2];
                    }
                    ++sampleCount;
                }
            }
            const double inverseSamples = 1.0 /
                static_cast<double>(std::max(1, sampleCount));
            const std::size_t destination =
                (static_cast<std::size_t>(y) * outW + x) * 4u;
            outPixels[destination + 0] = ToSrgbByte(
                static_cast<float>(red * inverseSamples));
            outPixels[destination + 1] = ToSrgbByte(
                static_cast<float>(green * inverseSamples));
            outPixels[destination + 2] = ToSrgbByte(
                static_cast<float>(blue * inverseSamples));
            outPixels[destination + 3] = 255;
        }
    }
    return true;
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, 4, pixels.data(), width * 4);
    return pngBytes;
}

std::vector<std::filesystem::path> DeduplicateRecentRoots(const std::vector<std::filesystem::path>& roots, std::size_t maxRecent) {
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    for (const std::filesystem::path& root : roots) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path normalized = NormalizePath(root);
        const std::string key = ToLowerAscii(normalized.u8string());
        if (!seen.insert(key).second) {
            continue;
        }
        result.push_back(normalized);
        if (result.size() >= maxRecent) {
            break;
        }
    }
    return result;
}

} // namespace

bool DefaultRawPathPredicate(const std::filesystem::path& path) {
    const std::string ext = ToLowerAscii(path.extension().u8string());
    return ext == ".arw" ||
        ext == ".srf" ||
        ext == ".sr2" ||
        ext == ".raw" ||
        ext == ".dng";
}

ScanResult ScanWorkspace(
    const std::filesystem::path& workspaceRoot,
    RawPathPredicate isRawPath,
    ScanProgressCallback progressCallback,
    CancellationPredicate shouldCancel,
    ScanSourceReadyCallback sourceReadyCallback,
    SourceIdentityReuseCallback reuseSourceIdentity) {
    ScanResult result;
    result.layout = BuildManagedLayout(workspaceRoot);
    result.progress.stage = ScanProgress::Stage::Preparing;
    result.progress.statusText = "Preparing folder scan...";

    auto isCancelled = [&]() {
        return shouldCancel && shouldCancel();
    };

    if (workspaceRoot.empty()) {
        result.errorMessage = "No Workspace folder selected.";
        return result;
    }

    std::error_code ec;
    if (!std::filesystem::exists(result.layout.workspaceRoot, ec) || ec) {
        result.errorMessage = "Workspace folder does not exist.";
        return result;
    }
    if (!std::filesystem::is_directory(result.layout.workspaceRoot, ec) || ec) {
        result.errorMessage = "Workspace path is not a folder.";
        return result;
    }

    std::string folderError;
    if (!EnsureManagedFolders(result.layout.workspaceRoot, &folderError)) {
        result.errorMessage = folderError;
        return result;
    }

    if (!isRawPath) {
        isRawPath = DefaultRawPathPredicate;
    }

    if (isCancelled()) {
        result.errorMessage = "Workspace scan canceled.";
        result.progress.statusText = result.errorMessage;
        return result;
    }

    int entriesSinceProgress = 0;
    auto reportProgress = [&](bool force = false) {
        ++entriesSinceProgress;
        if (progressCallback && (force || entriesSinceProgress >= 32)) {
            progressCallback(result.progress);
            entriesSinceProgress = 0;
        }
    };

    try {
        result.progress.stage = ScanProgress::Stage::Scanning;
        std::filesystem::recursive_directory_iterator it(
            result.layout.workspaceRoot,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        std::filesystem::recursive_directory_iterator end;
        if (ec) {
            result.errorMessage = "Failed to scan Workspace: " + ec.message();
            return result;
        }

        for (; it != end; it.increment(ec)) {
            if (isCancelled()) {
                result.errorMessage = "Workspace scan canceled.";
                result.progress.statusText = result.errorMessage;
                return result;
            }

            if (ec) {
                ec.clear();
                continue;
            }

            const std::filesystem::directory_entry& entry = *it;
            const std::filesystem::path entryPath = entry.path();
            if (entry.is_directory(ec)) {
                ++result.progress.directoriesVisited;
                result.progress.currentItem = entryPath.filename().u8string();
                result.progress.statusText = "Scanning " + result.progress.currentItem;
                const bool managed = ToLowerAscii(NormalizePath(entryPath).generic_u8string()) ==
                    ToLowerAscii(result.layout.dataDirectory.generic_u8string());
                if (managed) {
                    ++result.progress.managedDirectoriesSkipped;
                    it.disable_recursion_pending();
                }
                reportProgress(managed);
                continue;
            }
            ec.clear();

            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            ++result.progress.filesVisited;
            result.progress.currentItem = entryPath.filename().u8string();
            if (!isRawPath(entryPath)) {
                reportProgress();
                continue;
            }

            result.progress.stage = ScanProgress::Stage::Scanning;

            SourceRecord record;
            record.absolutePath = NormalizePath(entryPath);
            std::error_code relEc;
            record.relativePath = std::filesystem::relative(record.absolutePath, result.layout.workspaceRoot, relEc);
            if (relEc || record.relativePath.empty()) {
                record.relativePath = record.absolutePath.filename();
            }
            record.relativePathKey = GenericPathKey(record.relativePath);
            record.fileName = record.absolutePath.filename().u8string();
            record.stem = record.absolutePath.stem().u8string();
            record.extension = record.absolutePath.extension().u8string();
            record.parentFolderKey = record.relativePath.has_parent_path()
                ? GenericPathKey(record.relativePath.parent_path())
                : std::string();

            std::error_code statEc;
            record.fileSizeBytes = std::filesystem::file_size(record.absolutePath, statEc);
            if (statEc) {
                record.fileSizeBytes = 0;
            }
            statEc.clear();
            const auto modifiedTime = std::filesystem::last_write_time(record.absolutePath, statEc);
            if (statEc) {
                record.modifiedTimeTicks = 0;
            } else {
                record.modifiedTimeTicks = FileTimeTicks(modifiedTime);
                record.modifiedUnixSeconds = FileTimeUnixSeconds(modifiedTime);
            }

            const bool identityReused = reuseSourceIdentity &&
                reuseSourceIdentity(record) && !record.fingerprint.empty() &&
                record.sourceIdentityAlgorithmVersion ==
                    kSourceIdentityAlgorithmVersion;
            if (identityReused) {
                ++result.progress.reusedSourceIdentityCount;
            } else {
                const Stack::RawEvidence::SourceIdentity identity =
                    Stack::RawEvidence::ComputeSourceIdentity(
                        record.absolutePath, isCancelled);
                if (!identity.valid) {
                    if (isCancelled()) {
                        result.errorMessage = "Workspace scan canceled.";
                    } else {
                        result.errorMessage = "Could not verify RAW file " +
                            record.relativePath.generic_u8string() + ": " +
                            identity.reason;
                    }
                    result.progress.stage = ScanProgress::Stage::Failed;
                    result.progress.statusText = result.errorMessage;
                    reportProgress(true);
                    return result;
                }
                record.fingerprint = identity.sha256;
                record.sourceIdentityAlgorithmVersion =
                    kSourceIdentityAlgorithmVersion;
                result.progress.verifiedRawBytes += identity.byteSize;
                ++result.progress.hashedRawCount;
            }
            if (!record.captureMetadataChecked && !isCancelled()) {
                Raw::RawMetadata metadata;
                if (Raw::RawLoader::LoadMetadata(record.absolutePath.u8string(), metadata)) {
                    record.captureTimestamp = metadata.hasCaptureTimestamp
                        ? metadata.captureTimestamp : 0;
                    record.captureMetadataChecked = true;
                }
            }
            result.sources.push_back(std::move(record));
            result.progress.discoveredRawCount = static_cast<int>(result.sources.size());
            result.progress.statusText = "Discovered " + std::to_string(result.progress.discoveredRawCount) + " RAW files";
            reportProgress(true);
            if (sourceReadyCallback) {
                sourceReadyCallback(
                    result.layout, result.sources.back(), result.progress);
            }
        }
    } catch (const std::exception& error) {
        result.errorMessage = error.what();
        return result;
    } catch (...) {
        result.errorMessage = "Failed to scan Workspace.";
        return result;
    }

    if (isCancelled()) {
        result.errorMessage = "Workspace scan canceled.";
        result.progress.statusText = result.errorMessage;
        return result;
    }

    std::sort(result.sources.begin(), result.sources.end(), [](const SourceRecord& a, const SourceRecord& b) {
        return ToLowerAscii(a.relativePathKey) < ToLowerAscii(b.relativePathKey);
    });

    result.progress.discoveredRawCount = static_cast<int>(result.sources.size());
    result.progress.currentItem.clear();
    result.progress.stage = ScanProgress::Stage::Complete;
    result.progress.statusText = result.sources.empty()
        ? "Workspace scan found no RAW files."
        : "Workspace scan complete.";
    reportProgress(true);
    result.success = true;
    return result;
}

bool SelectSourceByKey(WorkspaceState& state, const std::string& sourceKey) {
    const auto it = std::find_if(state.sources.begin(), state.sources.end(), [&](const SourceRecord& source) {
        return source.relativePathKey == sourceKey;
    });
    if (it == state.sources.end()) {
        return false;
    }
    state.selectedSourceKey = sourceKey;
    return true;
}

void AddRecentWorkspace(WorkspaceState& state, const std::filesystem::path& workspaceRoot, std::size_t maxRecent) {
    if (workspaceRoot.empty()) {
        return;
    }

    std::vector<std::filesystem::path> roots;
    roots.push_back(workspaceRoot);
    roots.insert(roots.end(), state.recentWorkspaceRoots.begin(), state.recentWorkspaceRoots.end());
    state.recentWorkspaceRoots = DeduplicateRecentRoots(roots, maxRecent);
}

ThumbnailSignature BuildThumbnailSignature(const SourceRecord& source, int maxDimension) {
    ThumbnailSignature signature;
    signature.sourceRelativePath = source.relativePathKey;
    signature.sourceFileSizeBytes = source.fileSizeBytes;
    signature.sourceModifiedTimeTicks = source.modifiedTimeTicks;
    signature.sourceFingerprint = source.fingerprint;
    signature.maxDimension = maxDimension;
    return signature;
}

ThumbnailInfo BuildThumbnailInfo(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension) {
    (void)maxDimension;
    ThumbnailInfo info;
    info.relativePath = ThumbnailRelativePathForSource(source);
    info.signatureRelativePath = ThumbnailSignatureRelativePathForSource(source);
    info.absolutePath = layout.thumbnailsDirectory / info.relativePath;
    info.signaturePath = layout.thumbnailsDirectory / info.signatureRelativePath;
    info.status = ThumbnailStatus::Unknown;
    return info;
}

ThumbnailInfo BuildTransientThumbnailInfo(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension) {
    (void)maxDimension;
    ThumbnailInfo info;
    info.relativePath = ThumbnailRelativePathForSource(
        source,
        ".preview.png");
    info.signatureRelativePath = ThumbnailSignatureRelativePathForSource(
        source,
        ".preview.json");
    info.absolutePath = layout.transientThumbnailsDirectory / info.relativePath;
    info.signaturePath =
        layout.transientThumbnailsDirectory / info.signatureRelativePath;
    info.status = ThumbnailStatus::Unknown;
    return info;
}

bool ThumbnailSignatureMatches(const ThumbnailSignature& expected, const nlohmann::json& actual) {
    if (!actual.is_object()) {
        return false;
    }
    const std::string actualFingerprint = JsonStringOrDefault(actual, "sourceFingerprint");
    return JsonIntOrDefault(actual, "schemaVersion") == expected.schemaVersion &&
        JsonStringOrDefault(actual, "sourceRelativePath") == expected.sourceRelativePath &&
        JsonUintMaxOrDefault(actual, "sourceFileSizeBytes") == expected.sourceFileSizeBytes &&
        actualFingerprint == expected.sourceFingerprint &&
        JsonIntOrDefault(actual, "rawLoaderAlgorithmVersion") == expected.rawLoaderAlgorithmVersion &&
        JsonIntOrDefault(actual, "neutralPreviewSettingsVersion") == expected.neutralPreviewSettingsVersion &&
        JsonIntOrDefault(actual, "thumbnailVersion") == expected.thumbnailVersion &&
        JsonIntOrDefault(actual, "maxDimension") == expected.maxDimension;
}

ThumbnailStatus ClassifyThumbnailMetadata(
    const ManagedLayout& layout,
    SourceRecord& source,
    int maxDimension) {
    source.thumbnail = BuildThumbnailInfo(layout, source, maxDimension);

    std::error_code ec;
    const bool pngExists =
        std::filesystem::is_regular_file(source.thumbnail.absolutePath, ec) &&
        !ec;
    ec.clear();
    const bool signatureExists = std::filesystem::is_regular_file(
        source.thumbnail.signaturePath, ec) && !ec;
    if (!pngExists && !signatureExists) {
        source.thumbnail.status = ThumbnailStatus::Missing;
        return source.thumbnail.status;
    }
    if (!pngExists || !signatureExists) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        return source.thumbnail.status;
    }
    ec.clear();
    const std::uintmax_t pngSize = std::filesystem::file_size(
        source.thumbnail.absolutePath, ec);
    if (ec || pngSize == 0) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage = "RAW thumbnail PNG is empty.";
        return source.thumbnail.status;
    }

    nlohmann::json signatureJson;
    const ThumbnailSignature expected = BuildThumbnailSignature(
        source, maxDimension);
    if (!ReadJsonFile(source.thumbnail.signaturePath, signatureJson) ||
        !ThumbnailSignatureMatches(expected, signatureJson)) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage =
            "RAW thumbnail signature is missing or stale.";
        return source.thumbnail.status;
    }

    source.thumbnail.width = JsonIntOrDefault(
        signatureJson, "thumbnailWidth");
    source.thumbnail.height = JsonIntOrDefault(
        signatureJson, "thumbnailHeight");
    const nlohmann::json* descriptorJson = FindJsonMember(
        signatureJson, "similarityDescriptor");
    const bool descriptorLoaded = descriptorJson != nullptr &&
        DeserializeRawGallerySimilarityDescriptor(
            *descriptorJson,
            source.thumbnail.similarityDescriptor);
    if (source.thumbnail.width <= 0 || source.thumbnail.height <= 0 ||
        !descriptorLoaded) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage =
            "RAW thumbnail metadata needs to be rebuilt.";
        return source.thumbnail.status;
    }

    source.thumbnail.status = ThumbnailStatus::Valid;
    source.thumbnail.errorMessage.clear();
    return source.thumbnail.status;
}

ThumbnailStatus ClassifyThumbnail(
    const ManagedLayout& layout,
    SourceRecord& source,
    int maxDimension) {
    source.thumbnail = BuildThumbnailInfo(layout, source, maxDimension);

    std::error_code ec;
    const bool pngExists = std::filesystem::exists(source.thumbnail.absolutePath, ec) && !ec;
    ec.clear();
    const bool signatureExists = std::filesystem::exists(source.thumbnail.signaturePath, ec) && !ec;
    if (!pngExists && !signatureExists) {
        source.thumbnail.status = ThumbnailStatus::Missing;
        return source.thumbnail.status;
    }
    if (!pngExists || !signatureExists) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        return source.thumbnail.status;
    }

    nlohmann::json signatureJson;
    const ThumbnailSignature expected = BuildThumbnailSignature(source, maxDimension);
    if (!ReadJsonFile(source.thumbnail.signaturePath, signatureJson) ||
        !ThumbnailSignatureMatches(expected, signatureJson)) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage =
            "RAW thumbnail signature is missing or stale.";
        return source.thumbnail.status;
    }

    int pngWidth = 0;
    int pngHeight = 0;
    int pngChannels = 0;
    std::ifstream pngFile(source.thumbnail.absolutePath, std::ios::binary);
    const std::vector<unsigned char> pngBytes{
        std::istreambuf_iterator<char>(pngFile), std::istreambuf_iterator<char>()};
    if (pngBytes.empty() || pngBytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        stbi_info_from_memory(
            pngBytes.data(), static_cast<int>(pngBytes.size()),
            &pngWidth,
            &pngHeight,
            &pngChannels) == 0 ||
        pngWidth <= 0 || pngHeight <= 0) {
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage =
            "RAW thumbnail PNG is invalid or incomplete.";
        return source.thumbnail.status;
    }

    unsigned char* decoded = stbi_load_from_memory(
        pngBytes.data(), static_cast<int>(pngBytes.size()),
        &pngWidth,
        &pngHeight,
        &pngChannels,
        4);
    const bool decodedSuccessfully = decoded != nullptr &&
        pngWidth > 0 && pngHeight > 0;
    if (!decodedSuccessfully) {
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage =
            "RAW thumbnail PNG could not be decoded.";
        return source.thumbnail.status;
    }

    const int signatureWidth = JsonIntOrDefault(
        signatureJson,
        "thumbnailWidth");
    const int signatureHeight = JsonIntOrDefault(
        signatureJson,
        "thumbnailHeight");
    if ((signatureWidth > 0 && signatureWidth != pngWidth) ||
        (signatureHeight > 0 && signatureHeight != pngHeight)) {
        stbi_image_free(decoded);
        source.thumbnail.status = ThumbnailStatus::Stale;
        source.thumbnail.errorMessage =
            "RAW thumbnail dimensions do not match its signature.";
        return source.thumbnail.status;
    }

    bool descriptorLoaded = false;
    const nlohmann::json* descriptorJson = FindJsonMember(
        signatureJson,
        "similarityDescriptor");
    if (descriptorJson != nullptr) {
        descriptorLoaded = DeserializeRawGallerySimilarityDescriptor(
            *descriptorJson,
            source.thumbnail.similarityDescriptor);
    }
    if (!descriptorLoaded) {
        source.thumbnail.similarityDescriptor =
            BuildRawGallerySimilarityDescriptor(
                decoded,
                pngWidth,
                pngHeight);
        if (source.thumbnail.similarityDescriptor.IsValid()) {
            signatureJson["similarityDescriptor"] =
                SerializeRawGallerySimilarityDescriptor(
                    source.thumbnail.similarityDescriptor);
            std::string ignoredBackfillError;
            WriteJsonFile(
                source.thumbnail.signaturePath,
                signatureJson,
                &ignoredBackfillError);
        }
    }
    stbi_image_free(decoded);

    source.thumbnail.status = ThumbnailStatus::Valid;
    source.thumbnail.width = pngWidth;
    source.thumbnail.height = pngHeight;
    source.thumbnail.errorMessage.clear();
    return source.thumbnail.status;
}

bool ClassifyThumbnails(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    int maxDimension,
    CancellationPredicate shouldCancel) {
    for (SourceRecord& source : sources) {
        if (shouldCancel && shouldCancel()) {
            return false;
        }
        ClassifyThumbnail(layout, source, maxDimension);
    }
    return true;
}

ThumbnailProgress BuildThumbnailProgress(const std::vector<SourceRecord>& sources) {
    ThumbnailProgress progress;
    progress.total = static_cast<int>(sources.size());
    for (const SourceRecord& source : sources) {
        const bool standardReady =
            source.thumbnail.status == ThumbnailStatus::Valid ||
            source.thumbnail.status == ThumbnailStatus::Ready;
        const bool quickReady = standardReady ||
            source.transientThumbnail.status == ThumbnailStatus::Ready;
        if (standardReady) ++progress.standardAvailable;
        if (quickReady) ++progress.quickAvailable;
        switch (source.thumbnail.status) {
            case ThumbnailStatus::Valid:
            case ThumbnailStatus::Ready:
                ++progress.valid;
                if (source.thumbnail.status == ThumbnailStatus::Ready) {
                    ++progress.completed;
                }
                break;
            case ThumbnailStatus::Queued:
            case ThumbnailStatus::Generating:
            case ThumbnailStatus::Missing:
            case ThumbnailStatus::Stale:
                ++progress.queued;
                break;
            case ThumbnailStatus::Failed:
                ++progress.failed;
                break;
            case ThumbnailStatus::Unknown:
            default:
                break;
        }
    }
    progress.statusText = progress.total == 0
        ? "No RAW thumbnails needed."
        : "RAW thumbnails: " + std::to_string(progress.valid) + " ready, " +
            std::to_string(progress.queued) + " queued, " +
            std::to_string(progress.failed) + " failed.";
    return progress;
}

ThumbnailGenerationResult GenerateNeutralThumbnailImpl(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension,
    CancellationPredicate shouldCancel,
    bool fastPreview) {
    ThumbnailGenerationResult result;
    result.thumbnail = fastPreview
        ? BuildTransientThumbnailInfo(layout, source, maxDimension)
        : BuildThumbnailInfo(layout, source, maxDimension);
    result.thumbnail.status = ThumbnailStatus::Generating;
    auto isCancelled = [&]() {
        return shouldCancel && shouldCancel();
    };
    auto markCancelled = [&]() {
        result.errorMessage = "RAW thumbnail generation canceled.";
        result.thumbnail.status = ThumbnailStatus::Queued;
        result.thumbnail.errorMessage.clear();
    };

    if (isCancelled()) {
        markCancelled();
        return result;
    }

    Raw::RawImageData raw;
    if (!Raw::RawLoader::LoadFile(source.absolutePath.u8string(), raw, isCancelled)) {
        if (isCancelled()) {
            markCancelled();
            return result;
        }
        result.errorMessage = raw.metadata.error.empty()
            ? "RAW file could not be decoded."
            : raw.metadata.error;
        result.thumbnail.status = ThumbnailStatus::Failed;
        result.thumbnail.errorMessage = result.errorMessage;
        return result;
    }
    if (isCancelled()) {
        markCancelled();
        return result;
    }

    std::vector<unsigned char> rgba;
    int width = 0;
    int height = 0;
    bool built = false;
    if (raw.metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
        built = fastPreview
            ? BuildFastLinearThumbnailPixels(
                  raw, maxDimension, rgba, width, height)
            : BuildHighQualityLinearThumbnailPixels(
                  raw, maxDimension, rgba, width, height);
    } else {
        built = fastPreview
            ? BuildFastMosaicThumbnailPixels(
                  raw, maxDimension, rgba, width, height)
            : BuildHighQualityMosaicThumbnailPixels(
                  raw, maxDimension, rgba, width, height);
    }
    if (isCancelled()) {
        markCancelled();
        return result;
    }
    if (!built || rgba.empty() || width <= 0 || height <= 0) {
        result.errorMessage = "RAW thumbnail pixels could not be generated.";
        result.thumbnail.status = ThumbnailStatus::Failed;
        result.thumbnail.errorMessage = result.errorMessage;
        return result;
    }

    result.thumbnail.similarityDescriptor =
        BuildRawGallerySimilarityDescriptor(
            rgba.data(),
            width,
            height);

    std::vector<unsigned char> pngBytes = EncodePngBytes(rgba, width, height);
    if (isCancelled()) {
        markCancelled();
        return result;
    }
    if (pngBytes.empty()) {
        result.errorMessage = "RAW thumbnail PNG encoding failed.";
        result.thumbnail.status = ThumbnailStatus::Failed;
        result.thumbnail.errorMessage = result.errorMessage;
        return result;
    }

    std::string writeError;
    if (isCancelled()) {
        markCancelled();
        return result;
    }
    if (!WriteBinaryFile(result.thumbnail.absolutePath, pngBytes, &writeError)) {
        result.errorMessage = writeError;
        result.thumbnail.status = ThumbnailStatus::Failed;
        result.thumbnail.errorMessage = result.errorMessage;
        return result;
    }

    nlohmann::json signature = SerializeThumbnailSignature(BuildThumbnailSignature(source, maxDimension));
    signature["thumbnailRelativePath"] = result.thumbnail.relativePath.generic_u8string();
    signature["thumbnailWidth"] = width;
    signature["thumbnailHeight"] = height;
    if (result.thumbnail.similarityDescriptor.IsValid()) {
        signature["similarityDescriptor"] =
            SerializeRawGallerySimilarityDescriptor(
                result.thumbnail.similarityDescriptor);
    }
    if (isCancelled()) {
        markCancelled();
        return result;
    }
    if (!WriteJsonFile(result.thumbnail.signaturePath, signature, &writeError)) {
        result.errorMessage = writeError;
        result.thumbnail.status = ThumbnailStatus::Failed;
        result.thumbnail.errorMessage = result.errorMessage;
        return result;
    }

    result.success = true;
    result.thumbnail.status = ThumbnailStatus::Ready;
    result.thumbnail.width = width;
    result.thumbnail.height = height;
    return result;
}

ThumbnailGenerationResult GenerateNeutralThumbnail(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension,
    CancellationPredicate shouldCancel) {
    return GenerateNeutralThumbnailImpl(
        layout,
        source,
        maxDimension,
        std::move(shouldCancel),
        false);
}

ThumbnailGenerationResult GenerateFastNeutralThumbnail(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension,
    CancellationPredicate shouldCancel) {
    return GenerateNeutralThumbnailImpl(
        layout,
        source,
        maxDimension,
        std::move(shouldCancel),
        true);
}

bool RemoveTransientThumbnailCache(
    const ManagedLayout& layout,
    std::string* outError) {
    if (layout.transientThumbnailsDirectory.empty()) {
        return true;
    }
    const bool safeTarget =
        !layout.workspaceRoot.empty() &&
        layout.transientThumbnailsDirectory ==
            BuildManagedLayout(layout.workspaceRoot).transientThumbnailsDirectory;
    if (!safeTarget) {
        if (outError != nullptr) {
            *outError = "Refused to remove an invalid transient thumbnail cache path.";
        }
        return false;
    }
    std::error_code error;
    std::filesystem::remove_all(layout.transientThumbnailsDirectory, error);
    if (!error) {
        return true;
    }
    if (outError != nullptr) {
        *outError = "Failed to remove transient RAW thumbnails from " +
            layout.transientThumbnailsDirectory.u8string() + ": " +
            error.message();
    }
    return false;
}

void FilterGalleryProjectCards(GalleryPresentation& presentation, GalleryContentMode mode) {
    // Projects contains every saved document. Bracket is a narrower view of
    // that population, not a separate home that hides projects elsewhere.
    if (mode != GalleryContentMode::Bracket) return;
    auto& projects = presentation.projects;
    projects.erase(std::remove_if(projects.begin(), projects.end(),
        [](const auto& project) { return !project.bracketingProject; }), projects.end());
}

GalleryPresentation BuildGalleryPresentation(const WorkspaceState& state) {
    GalleryPresentation presentation;
    presentation.totalSources = static_cast<int>(state.sources.size());
    presentation.selectedSourceKey = state.selectedSourceKey;

    presentation.projects.reserve(state.sourceSetProjects.size());
    for (std::size_t index = 0; index < state.sourceSetProjects.size(); ++index) {
        const SourceSetProjectCatalogEntry& project = state.sourceSetProjects[index];
        GalleryProjectView view;
        view.projectIndex = index;
        view.projectId = project.projectId;
        view.projectName = project.projectName.empty()
            ? project.absolutePath.stem().u8string()
            : project.projectName;
        view.projectPath = project.absolutePath;
        view.coverThumbnailCachePath = project.coverThumbnailCachePath;
        view.referenceSourceKey = project.referenceSourceKey;
        view.frameCount = project.totalFrameCount;
        view.status = project.status;
        view.multiFrameProject = project.multiFrameProject;
        view.bracketingProject = project.bracketingProject;
        presentation.projects.push_back(std::move(view));
    }

    std::unordered_map<std::string, const GalleryProjectView*> projectsById;
    std::unordered_map<std::string, const GalleryProjectView*> projectsByPath;
    for (const GalleryProjectView& project : presentation.projects) {
        if (!project.projectId.empty()) {
            projectsById[project.projectId] = &project;
        }
        if (!project.projectPath.empty()) {
            projectsByPath[ToLowerAscii(
                NormalizePath(project.projectPath).generic_u8string())] = &project;
        }
    }
    const auto savedProjectKey = [](const GalleryProjectView* project,
                                    const std::string& fallbackId,
                                    const std::filesystem::path& fallbackPath) {
        if (project != nullptr && !project->projectId.empty()) {
            return std::string("id:") + ToLowerAscii(project->projectId);
        }
        if (!fallbackId.empty()) {
            return std::string("id:") + ToLowerAscii(fallbackId);
        }
        const std::filesystem::path path =
            project != nullptr ? project->projectPath : fallbackPath;
        return path.empty()
            ? std::string()
            : std::string("path:") + ToLowerAscii(
                  NormalizePath(path).generic_u8string());
    };

    std::unordered_map<std::string, std::size_t> groupIndexes;
    for (std::size_t index = 0; index < state.sources.size(); ++index) {
        const SourceRecord& source = state.sources[index];
        const std::string folderKey = source.parentFolderKey;
        auto groupIt = groupIndexes.find(folderKey);
        if (groupIt == groupIndexes.end()) {
            GalleryFolderGroup group;
            group.folderKey = folderKey;
            group.label = folderKey.empty() ? "Workspace root" : folderKey;
            presentation.groups.push_back(std::move(group));
            groupIt = groupIndexes.emplace(folderKey, presentation.groups.size() - 1).first;
        }

        GallerySourceView view;
        view.sourceIndex = index;
        view.relativePathKey = source.relativePathKey;
        view.fileName = source.fileName;
        view.folderKey = folderKey;
        view.fileSizeBytes = source.fileSizeBytes;
        view.thumbnailStatus = source.thumbnail.status;
        view.thumbnailRelativePath = source.thumbnail.relativePath;
        view.projectStatus = source.project.status;
        view.selected = !state.selectedSourceKey.empty() && source.relativePathKey == state.selectedSourceKey;
        view.multiSelected = std::find(
            state.selectedSourceKeys.begin(),
            state.selectedSourceKeys.end(),
            source.relativePathKey) != state.selectedSourceKeys.end();
        view.sourceSetProjectMembershipCount =
            static_cast<std::uint64_t>(source.sourceSetProjectMemberships.size());

        const GalleryProjectView* primaryProject = nullptr;
        int primaryScore = -1;
        std::unordered_set<std::string> savedProjectKeys;
        for (const SourceSetProjectMembership& membership :
             source.sourceSetProjectMemberships) {
            const GalleryProjectView* candidate = nullptr;
            const auto byId = projectsById.find(membership.projectId);
            if (byId != projectsById.end()) {
                candidate = byId->second;
            } else if (!membership.projectPath.empty()) {
                const auto byPath = projectsByPath.find(ToLowerAscii(
                    NormalizePath(membership.projectPath).generic_u8string()));
                if (byPath != projectsByPath.end()) {
                    candidate = byPath->second;
                }
            }
            const std::string savedKey = savedProjectKey(
                candidate, membership.projectId, membership.projectPath);
            if (!savedKey.empty()) savedProjectKeys.insert(savedKey);
            if (candidate == nullptr) continue;
            // The Gallery tile represents the image first, so prefer its
            // single-image edit over a capture set that also contains it.
            // Within that class, prefer an already-generated edited cover.
            const int score = (candidate->multiFrameProject ? 0 : 4) +
                (candidate->coverThumbnailCachePath.empty() ? 0 : 2) +
                (candidate->status == ProjectStatus::Existing ? 1 : 0);
            if (score > primaryScore) {
                primaryProject = candidate;
                primaryScore = score;
            }
        }

        view.savedProjectCount = static_cast<std::uint64_t>(
            savedProjectKeys.size());
        if (primaryProject != nullptr) {
            view.representsProject = true;
            view.projectIsMultiFrame = primaryProject->multiFrameProject;
            view.projectId = primaryProject->projectId;
            view.projectName = primaryProject->projectName;
            view.projectPath = primaryProject->projectPath;
            view.projectCoverThumbnailCachePath =
                primaryProject->coverThumbnailCachePath;
            view.projectStatus = primaryProject->status;
        }
        presentation.groups[groupIt->second].sources.push_back(std::move(view));

        switch (source.thumbnail.status) {
            case ThumbnailStatus::Valid:
            case ThumbnailStatus::Ready:
                ++presentation.readyThumbnailCount;
                break;
            case ThumbnailStatus::Queued:
            case ThumbnailStatus::Generating:
            case ThumbnailStatus::Missing:
            case ThumbnailStatus::Stale:
                ++presentation.queuedThumbnailCount;
                break;
            case ThumbnailStatus::Failed:
                ++presentation.failedThumbnailCount;
                break;
            case ThumbnailStatus::Unknown:
            default:
                break;
        }
    }

    presentation.hasSelection = std::any_of(
        state.sources.begin(),
        state.sources.end(),
        [&](const SourceRecord& source) {
            return !state.selectedSourceKey.empty() && source.relativePathKey == state.selectedSourceKey;
        });
    return presentation;
}

RawPanelState BuildRawPanelState(const SourceRecord* source) {
    RawPanelState state;
    if (source == nullptr) {
        state.statusText = "No RAW selected.";
        state.graphTooltip = "Select an edited RAW project first.";
        return state;
    }

    state.hasSelection = true;
    state.projectStatus = source->project.status;
    state.mode = source->project.mode;
    state.hasProject =
        source->project.status == ProjectStatus::Existing ||
        source->project.status == ProjectStatus::Embedded;
    state.openGraphEnabled = state.hasProject;

    if (source->project.status == ProjectStatus::NoProject) {
        state.recipeControlsEditable = true;
        state.editCreatesProject = true;
        state.statusText = "Preview only";
        state.graphTooltip = "Make an edit to create this RAW project first.";
        return state;
    }

    if (source->project.status == ProjectStatus::Existing ||
        source->project.status == ProjectStatus::Embedded) {
        state.statusText = source->project.status == ProjectStatus::Embedded
            ? "Embedded RAW project"
            : "Recipe project";
        state.graphTooltip = "Open this RAW project in the Editor graph.";

        if (source->project.mode == RawProjectMode::Unknown) {
            state.recipeControlsEditable = false;
            state.readOnlyMessage = source->project.errorMessage.empty()
                ? "This RAW project uses an unsupported mode. RAW tab editing is read-only until the project is repaired or upgraded."
                : source->project.errorMessage;
        } else if (source->project.mode == RawProjectMode::CustomGraph) {
            state.recipeControlsEditable = false;
            state.readOnlyMessage = source->project.readOnlyReason.empty()
                ? "This RAW chain has been customized in the graph. RAW tab editing is read-only for this image until the chain is repaired or re-adopted."
                : source->project.readOnlyReason;
        } else {
            state.recipeControlsEditable = true;
        }
        return state;
    }

    state.recipeControlsEditable = false;
    state.openGraphEnabled = false;
    state.statusText = ProjectStatusLabel(source->project.status);
    state.graphTooltip = "This RAW project cannot be opened until its project status is repaired.";
    state.readOnlyMessage = source->project.errorMessage.empty()
        ? source->project.readOnlyReason
        : source->project.errorMessage;
    return state;
}

GalleryPlacementMode ResolveExclusiveGalleryPlacement(
    GalleryPlacementMode requested,
    GalleryPlacementMode fallback) {
    switch (requested) {
        case GalleryPlacementMode::RightGallery:
        case GalleryPlacementMode::BottomFilmstrip:
            return requested;
        default:
            return fallback;
    }
}

const char* GalleryDisplayModeLabel(GalleryDisplayMode mode) {
    switch (mode) {
        case GalleryDisplayMode::Grid: return "Grid";
        case GalleryDisplayMode::List: return "List";
        default: return "Grid";
    }
}

const char* GalleryPlacementModeLabel(GalleryPlacementMode mode) {
    switch (mode) {
        case GalleryPlacementMode::RightGallery: return "Right";
        case GalleryPlacementMode::BottomFilmstrip: return "Filmstrip";
        default: return "Right";
    }
}

const char* ProjectStatusLabel(ProjectStatus status) {
    switch (status) {
        case ProjectStatus::NoProject: return "Not Edited";
        case ProjectStatus::Existing: return "Edited";
        case ProjectStatus::MissingSource: return "Source Missing";
        case ProjectStatus::Conflict: return "Conflict";
        case ProjectStatus::Embedded: return "Embedded";
        case ProjectStatus::Invalid: return "Invalid";
        case ProjectStatus::Unknown:
        default:
            return "Unknown";
    }
}

const char* RawProjectModeLabel(RawProjectMode mode) {
    switch (mode) {
        case RawProjectMode::UnifiedLayers: return "Layer graphs";
        case RawProjectMode::ManagedDecomposed: return "Managed Decomposed";
        case RawProjectMode::CustomGraph: return "Custom Graph";
        case RawProjectMode::Unknown:
        default:
            return "Unknown";
    }
}

std::string ProjectStatusToString(ProjectStatus status) {
    switch (status) {
        case ProjectStatus::NoProject: return "noProject";
        case ProjectStatus::Existing: return "existing";
        case ProjectStatus::MissingSource: return "missingSource";
        case ProjectStatus::Conflict: return "conflict";
        case ProjectStatus::Embedded: return "embedded";
        case ProjectStatus::Invalid: return "invalid";
        case ProjectStatus::Unknown:
        default:
            return "unknown";
    }
}

ProjectStatus ProjectStatusFromString(const std::string& value) {
    if (value == "noProject") return ProjectStatus::NoProject;
    if (value == "existing") return ProjectStatus::Existing;
    if (value == "missingSource") return ProjectStatus::MissingSource;
    if (value == "conflict") return ProjectStatus::Conflict;
    if (value == "embedded") return ProjectStatus::Embedded;
    if (value == "invalid") return ProjectStatus::Invalid;
    return ProjectStatus::Unknown;
}

std::string RawProjectModeToString(RawProjectMode mode) {
    switch (mode) {
        case RawProjectMode::ManagedDecomposed: return "managed-decomposed";
        case RawProjectMode::CustomGraph: return "custom-graph";
        case RawProjectMode::UnifiedLayers:
            return "unified-layer-graphs";
        case RawProjectMode::Unknown:
        default:
            return "unknown";
    }
}

RawProjectMode RawProjectModeFromString(const std::string& value) {
    if (value == "unified-layer-graphs") return RawProjectMode::UnifiedLayers;
    return RawProjectMode::Unknown;
}

const char* ThumbnailStatusLabel(ThumbnailStatus status) {
    switch (status) {
        case ThumbnailStatus::Missing: return "Missing";
        case ThumbnailStatus::Stale: return "Stale";
        case ThumbnailStatus::Valid: return "Ready";
        case ThumbnailStatus::Queued: return "Queued";
        case ThumbnailStatus::Generating: return "Generating";
        case ThumbnailStatus::Ready: return "Ready";
        case ThumbnailStatus::Failed: return "Failed";
        case ThumbnailStatus::Unknown:
        default:
            return "Placeholder";
    }
}

std::string ThumbnailStatusToString(ThumbnailStatus status) {
    switch (status) {
        case ThumbnailStatus::Missing: return "missing";
        case ThumbnailStatus::Stale: return "stale";
        case ThumbnailStatus::Valid: return "valid";
        case ThumbnailStatus::Queued: return "queued";
        case ThumbnailStatus::Generating: return "generating";
        case ThumbnailStatus::Ready: return "ready";
        case ThumbnailStatus::Failed: return "failed";
        case ThumbnailStatus::Unknown:
        default:
            return "unknown";
    }
}

ThumbnailStatus ThumbnailStatusFromString(const std::string& value) {
    if (value == "missing") return ThumbnailStatus::Missing;
    if (value == "stale") return ThumbnailStatus::Stale;
    if (value == "valid") return ThumbnailStatus::Valid;
    if (value == "queued") return ThumbnailStatus::Queued;
    if (value == "generating") return ThumbnailStatus::Generating;
    if (value == "ready") return ThumbnailStatus::Ready;
    if (value == "failed") return ThumbnailStatus::Failed;
    return ThumbnailStatus::Unknown;
}

nlohmann::json SerializeSourceRecord(const SourceRecord& source) {
    nlohmann::json item = nlohmann::json::object();
    item["relativePath"] = source.relativePathKey;
    item["fileName"] = source.fileName;
    item["stem"] = source.stem;
    item["extension"] = source.extension;
    item["parentFolder"] = source.parentFolderKey;
    item["fileSizeBytes"] = source.fileSizeBytes;
    item["modifiedTimeTicks"] = source.modifiedTimeTicks;
    item["modifiedUnixSeconds"] = source.modifiedUnixSeconds;
    item["captureTimestamp"] = source.captureTimestamp;
    item["captureMetadataChecked"] = source.captureMetadataChecked;
    item["fingerprint"] = source.fingerprint.empty() ? nlohmann::json() : nlohmann::json(source.fingerprint);
    item["thumbnail"] = {
        { "status", ThumbnailStatusToString(source.thumbnail.status) },
        { "relativePath", source.thumbnail.relativePath.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.relativePath.generic_u8string()) },
        { "signatureRelativePath", source.thumbnail.signatureRelativePath.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.signatureRelativePath.generic_u8string()) },
        { "width", source.thumbnail.width },
        { "height", source.thumbnail.height },
        { "error", source.thumbnail.errorMessage.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.errorMessage) }
    };
    item["project"] = {
        { "status", ProjectStatusToString(source.project.status) },
        { "relativePath", source.project.relativePath.empty() ? nlohmann::json() : nlohmann::json(source.project.relativePath.generic_u8string()) },
        { "mode", RawProjectModeToString(source.project.mode) },
        { "projectModifiedTimeTicks", source.project.projectModifiedTimeTicks },
        { "linkedRaw", source.project.linkedRaw },
        { "embeddedRaw", source.project.embeddedRaw },
        { "readOnlyReason", source.project.readOnlyReason.empty() ? nlohmann::json() : nlohmann::json(source.project.readOnlyReason) },
        { "associationReason", source.project.associationReason.empty() ? nlohmann::json() : nlohmann::json(source.project.associationReason) },
        { "error", source.project.errorMessage.empty() ? nlohmann::json() : nlohmann::json(source.project.errorMessage) }
    };
    item["sourceSetProjectMemberships"] = nlohmann::json::array();
    for (const SourceSetProjectMembership& membership :
         source.sourceSetProjectMemberships) {
        item["sourceSetProjectMemberships"].push_back({
            { "projectId", membership.projectId },
            { "projectName", membership.projectName },
            { "projectPath", membership.projectPath.u8string() },
            { "sourceSetId", membership.sourceSetId },
            { "sourceSetName", membership.sourceSetName },
            { "projectIsMultiFrame", membership.projectIsMultiFrame },
            { "coverCacheFileName",
              membership.projectCoverThumbnailCachePath.empty()
                ? nlohmann::json()
                : nlohmann::json(
                    membership.projectCoverThumbnailCachePath.filename().u8string()) }
        });
    }
    return item;
}

CatalogSourceRecord BuildCatalogSourceRecord(const SourceRecord& source) {
    CatalogSourceRecord record;
    record.absolutePath = source.absolutePath;
    record.relativePathKey = source.relativePathKey;
    record.fileName = source.fileName;
    record.stem = source.stem;
    record.extension = source.extension;
    record.parentFolderKey = source.parentFolderKey;
    record.fileSizeBytes = source.fileSizeBytes;
    record.modifiedTimeTicks = source.modifiedTimeTicks;
    record.modifiedUnixSeconds = source.modifiedUnixSeconds;
    record.captureTimestamp = source.captureTimestamp;
    record.captureMetadataChecked = source.captureMetadataChecked;
    record.fingerprint = source.fingerprint;
    record.thumbnail.relativePath = source.thumbnail.relativePath;
    record.thumbnail.signatureRelativePath = source.thumbnail.signatureRelativePath;
    record.thumbnail.status = source.thumbnail.status;
    record.thumbnail.width = source.thumbnail.width;
    record.thumbnail.height = source.thumbnail.height;
    record.thumbnail.errorMessage = source.thumbnail.errorMessage;
    record.project.relativePath = source.project.relativePath;
    record.project.status = source.project.status;
    record.project.mode = source.project.mode;
    record.project.projectModifiedTimeTicks = source.project.projectModifiedTimeTicks;
    record.project.linkedRaw = source.project.linkedRaw;
    record.project.embeddedRaw = source.project.embeddedRaw;
    record.project.readOnlyReason = source.project.readOnlyReason;
    record.project.associationReason = source.project.associationReason;
    record.project.errorMessage = source.project.errorMessage;
    record.sourceSetProjectMemberships = source.sourceSetProjectMemberships;
    return record;
}

std::vector<CatalogSourceRecord> BuildCatalogSourceRecords(const std::vector<SourceRecord>& sources) {
    std::vector<CatalogSourceRecord> records;
    records.reserve(sources.size());
    for (const SourceRecord& source : sources) {
        records.push_back(BuildCatalogSourceRecord(source));
    }
    return records;
}

nlohmann::json SerializeCatalogSourceRecord(const CatalogSourceRecord& source) {
    nlohmann::json item = nlohmann::json::object();
    item["relativePath"] = source.relativePathKey;
    item["fileSizeBytes"] = source.fileSizeBytes;
    item["modifiedTimeTicks"] = source.modifiedTimeTicks;
    item["modifiedUnixSeconds"] = source.modifiedUnixSeconds;
    item["captureTimestamp"] = source.captureTimestamp;
    item["captureMetadataChecked"] = source.captureMetadataChecked;
    item["fingerprint"] = source.fingerprint.empty() ? nlohmann::json() : nlohmann::json(source.fingerprint);
    item["thumbnail"] = {
        { "status", ThumbnailStatusToString(source.thumbnail.status) },
        { "width", source.thumbnail.width },
        { "height", source.thumbnail.height }
    };
    if (!source.thumbnail.errorMessage.empty()) {
        item["thumbnail"]["error"] = source.thumbnail.errorMessage;
    }
    item["project"] = {
        { "status", ProjectStatusToString(source.project.status) },
        { "mode", RawProjectModeToString(source.project.mode) }
    };
    if (!source.project.relativePath.empty()) {
        item["project"]["relativePath"] =
            source.project.relativePath.generic_u8string();
    }
    if (source.project.projectModifiedTimeTicks != 0) {
        item["project"]["projectModifiedTimeTicks"] =
            source.project.projectModifiedTimeTicks;
    }
    if (!source.project.linkedRaw) item["project"]["linkedRaw"] = false;
    if (source.project.embeddedRaw) item["project"]["embeddedRaw"] = true;
    if (!source.project.readOnlyReason.empty())
        item["project"]["readOnlyReason"] = source.project.readOnlyReason;
    if (!source.project.associationReason.empty())
        item["project"]["associationReason"] = source.project.associationReason;
    if (!source.project.errorMessage.empty())
        item["project"]["error"] = source.project.errorMessage;
    item["sourceSetProjectMemberships"] = nlohmann::json::array();
    for (const SourceSetProjectMembership& membership :
         source.sourceSetProjectMemberships) {
        item["sourceSetProjectMemberships"].push_back({
            { "projectId", membership.projectId },
            { "projectName", membership.projectName },
            { "projectPath", membership.projectPath.u8string() },
            { "sourceSetId", membership.sourceSetId },
            { "sourceSetName", membership.sourceSetName },
            { "projectIsMultiFrame", membership.projectIsMultiFrame },
            { "coverCacheFileName",
              membership.projectCoverThumbnailCachePath.empty()
                ? nlohmann::json()
                : nlohmann::json(
                    membership.projectCoverThumbnailCachePath.filename().u8string()) }
        });
    }
    return item;
}

bool WriteCatalogSkeleton(
    const ManagedLayout& layout,
    const std::vector<SourceRecord>& sources,
    const std::string& selectedSourceKey,
    std::string* outError) {
    return WriteCatalogSkeletonIfCurrent(
        layout,
        BuildCatalogSourceRecords(sources),
        selectedSourceKey,
        nullptr,
        outError);
}

bool WriteCatalogSkeleton(
    const ManagedLayout& layout,
    const std::vector<CatalogSourceRecord>& sources,
    const std::string& selectedSourceKey,
    std::string* outError) {
    return WriteCatalogSkeletonIfCurrent(layout, sources, selectedSourceKey, nullptr, outError);
}

bool WriteCatalogSkeletonIfCurrent(
    const ManagedLayout& layout,
    const std::vector<SourceRecord>& sources,
    const std::string& selectedSourceKey,
    PersistenceCommitPredicate shouldCommit,
    std::string* outError) {
    return WriteCatalogSkeletonIfCurrent(
        layout,
        BuildCatalogSourceRecords(sources),
        selectedSourceKey,
        shouldCommit,
        outError);
}

bool WriteCatalogSkeletonIfCurrent(
    const ManagedLayout& layout,
    const std::vector<CatalogSourceRecord>& sources,
    const std::string& selectedSourceKey,
    PersistenceCommitPredicate shouldCommit,
    std::string* outError) {
    nlohmann::json sourceArray = nlohmann::json::array();
    for (const CatalogSourceRecord& source : sources) {
        auto item = SerializeCatalogSourceRecord(source);
        for (auto& membership : item["sourceSetProjectMemberships"]) {
            const std::filesystem::path path = std::filesystem::u8path(membership.value("projectPath", std::string()));
            membership["projectPath"] = path.lexically_relative(layout.projectsDirectory).generic_u8string();
        }
        sourceArray.push_back(std::move(item));
    }

    nlohmann::json catalog = nlohmann::json::object();
    catalog["schema"] = "stack.rawWorkspace.catalog";
    catalog["schemaVersion"] = 2;
    catalog["sourceIdentityAlgorithmVersion"] =
        kSourceIdentityAlgorithmVersion;
    catalog["lastScanUnixSeconds"] = UnixSecondsNow();
    catalog["lastSelectedSource"] = selectedSourceKey.empty() ? nlohmann::json() : nlohmann::json(selectedSourceKey);
    catalog["managedFolders"] = {
        { "thumbnails", "Thumbnails/Standard" },
        { "projects", "Projects" },
        { "catalog", "Catalog" }
    };
    catalog["sources"] = std::move(sourceArray);

    if (!WriteCompactJsonFile(
            layout.catalogPath, catalog, shouldCommit, outError)) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(layout.ratingsPath, ec) || ec) {
        ec.clear();
        nlohmann::json ratings = nlohmann::json::object();
        ratings["schema"] = "stack.rawWorkspace.ratings";
        ratings["schemaVersion"] = 1;
        ratings["ratings"] = nlohmann::json::object();
        if (!WriteJsonFile(layout.ratingsPath, ratings, shouldCommit, outError)) {
            return false;
        }
    }

    return true;
}

bool LoadCatalogSnapshot(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& outSources,
    std::string* outSelectedSourceKey,
    std::string* outError,
    std::vector<SourceSetProjectCatalogEntry>* outProjects) {
    outSources.clear();
    if (outProjects != nullptr) {
        outProjects->clear();
    }
    if (outSelectedSourceKey != nullptr) {
        outSelectedSourceKey->clear();
    }

    nlohmann::json catalog;
    if (!ReadJsonFile(layout.catalogPath, catalog)) {
        if (outError != nullptr) {
            *outError = "RAW Workspace catalog is missing or unreadable.";
        }
        return false;
    }
    if (JsonStringOrDefault(catalog, "schema") != "stack.rawWorkspace.catalog" ||
        JsonIntOrDefault(catalog, "schemaVersion") != 2) {
        if (outError != nullptr) {
            *outError = "RAW Workspace catalog has an unsupported schema.";
        }
        return false;
    }

    const nlohmann::json* sourceArray = FindJsonMember(catalog, "sources");
    if (sourceArray == nullptr || !sourceArray->is_array()) {
        if (outError != nullptr) {
            *outError = "RAW Workspace catalog does not contain a source list.";
        }
        return false;
    }

    const int sourceIdentityAlgorithmVersion = JsonIntOrDefault(
        catalog,
        "sourceIdentityAlgorithmVersion");
    std::vector<SourceRecord> loaded;
    loaded.reserve(sourceArray->size());
    std::unordered_set<std::string> sourceKeys;
    for (const nlohmann::json& item : *sourceArray) {
        if (!item.is_object()) {
            continue;
        }
        const std::filesystem::path relativePath =
            std::filesystem::u8path(JsonStringOrDefault(item, "relativePath"))
                .lexically_normal();
        if (relativePath.empty() || relativePath.has_root_path() ||
            (relativePath.begin() != relativePath.end() &&
             *relativePath.begin() == "..")) {
            continue;
        }

        SourceRecord source;
        source.relativePath = relativePath;
        source.relativePathKey = GenericPathKey(relativePath);
        const std::string dedupeKey = ToLowerAscii(source.relativePathKey);
        if (!sourceKeys.insert(dedupeKey).second) {
            continue;
        }
        source.absolutePath =
            (layout.workspaceRoot / relativePath).lexically_normal();
        source.fileName = JsonStringOrDefault(
            item, "fileName", source.absolutePath.filename().u8string());
        source.stem = JsonStringOrDefault(
            item, "stem", source.absolutePath.stem().u8string());
        source.extension = JsonStringOrDefault(
            item, "extension", source.absolutePath.extension().u8string());
        source.parentFolderKey = JsonStringOrDefault(
            item,
            "parentFolder",
            relativePath.has_parent_path()
                ? GenericPathKey(relativePath.parent_path())
                : std::string());
        source.fileSizeBytes = JsonUintMaxOrDefault(item, "fileSizeBytes");
        source.modifiedTimeTicks = JsonInt64OrDefault(item, "modifiedTimeTicks");
        source.modifiedUnixSeconds = JsonInt64OrDefault(item, "modifiedUnixSeconds");
        if (source.modifiedUnixSeconds == 0) {
            std::error_code timeError;
            const auto modifiedTime = std::filesystem::last_write_time(
                source.absolutePath, timeError);
            if (!timeError) source.modifiedUnixSeconds = FileTimeUnixSeconds(modifiedTime);
        }
        source.captureTimestamp = JsonInt64OrDefault(item, "captureTimestamp");
        source.captureMetadataChecked = item.value("captureMetadataChecked", false);
        source.fingerprint = JsonStringOrDefault(item, "fingerprint");
        source.sourceIdentityAlgorithmVersion =
            sourceIdentityAlgorithmVersion;

        const nlohmann::json* thumbnail = FindJsonMember(item, "thumbnail");
        if (thumbnail != nullptr && thumbnail->is_object()) {
            source.thumbnail.relativePath = std::filesystem::u8path(
                JsonStringOrDefault(*thumbnail, "relativePath"))
                    .lexically_normal();
            source.thumbnail.signatureRelativePath = std::filesystem::u8path(
                JsonStringOrDefault(*thumbnail, "signatureRelativePath"))
                    .lexically_normal();
            source.thumbnail.status = ThumbnailStatusFromString(
                JsonStringOrDefault(*thumbnail, "status"));
            // A previous process cannot still own in-flight generation work.
            if (source.thumbnail.status == ThumbnailStatus::Generating) {
                source.thumbnail.status = ThumbnailStatus::Queued;
            }
            source.thumbnail.width = JsonIntOrDefault(*thumbnail, "width");
            source.thumbnail.height = JsonIntOrDefault(*thumbnail, "height");
            source.thumbnail.errorMessage = JsonStringOrDefault(*thumbnail, "error");
        }
        if (source.thumbnail.relativePath.empty()) {
            const ThumbnailStatus savedStatus = source.thumbnail.status;
            const int savedWidth = source.thumbnail.width;
            const int savedHeight = source.thumbnail.height;
            std::string savedError =
                std::move(source.thumbnail.errorMessage);
            source.thumbnail = BuildThumbnailInfo(layout, source);
            source.thumbnail.status = savedStatus;
            source.thumbnail.width = savedWidth;
            source.thumbnail.height = savedHeight;
            source.thumbnail.errorMessage = std::move(savedError);
        } else {
            const auto safeRelative = [](const std::filesystem::path& path) {
                return !path.has_root_path() &&
                    std::find(path.begin(), path.end(), "..") == path.end();
            };
            if (!safeRelative(source.thumbnail.relativePath) ||
                !safeRelative(source.thumbnail.signatureRelativePath)) {
                source.thumbnail = BuildThumbnailInfo(layout, source);
            }
            source.thumbnail.absolutePath =
                (layout.thumbnailsDirectory / source.thumbnail.relativePath)
                    .lexically_normal();
            if (source.thumbnail.signatureRelativePath.empty()) {
                source.thumbnail.signatureRelativePath =
                    ThumbnailSignatureRelativePathForSource(source);
            }
            source.thumbnail.signaturePath =
                (layout.thumbnailsDirectory /
                 source.thumbnail.signatureRelativePath)
                    .lexically_normal();
        }

        source.project = BuildExpectedProjectInfo(layout, source);
        const nlohmann::json* project = FindJsonMember(item, "project");
        if (project != nullptr && project->is_object()) {
            const std::filesystem::path projectRelative = std::filesystem::u8path(
                JsonStringOrDefault(*project, "relativePath"))
                    .lexically_normal();
            if (!projectRelative.empty() && !projectRelative.is_absolute() &&
                (projectRelative.begin() == projectRelative.end() ||
                 *projectRelative.begin() != "..")) {
                source.project.relativePath = projectRelative;
                source.project.absolutePath =
                    (layout.projectsDirectory / projectRelative)
                        .lexically_normal();
            }
            source.project.status = ProjectStatusFromString(
                JsonStringOrDefault(*project, "status"));
            source.project.mode = RawProjectModeFromString(
                JsonStringOrDefault(*project, "mode"));
            source.project.projectModifiedTimeTicks =
                JsonInt64OrDefault(*project, "projectModifiedTimeTicks");
            const nlohmann::json* linkedRaw = FindJsonMember(*project, "linkedRaw");
            if (linkedRaw != nullptr && linkedRaw->is_boolean()) {
                source.project.linkedRaw = linkedRaw->get<bool>();
            }
            const nlohmann::json* embeddedRaw = FindJsonMember(*project, "embeddedRaw");
            if (embeddedRaw != nullptr && embeddedRaw->is_boolean()) {
                source.project.embeddedRaw = embeddedRaw->get<bool>();
            }
            source.project.readOnlyReason =
                JsonStringOrDefault(*project, "readOnlyReason");
            source.project.associationReason =
                JsonStringOrDefault(*project, "associationReason");
            source.project.errorMessage = JsonStringOrDefault(*project, "error");
        }
        if (source.project.status != ProjectStatus::NoProject &&
            !source.project.absolutePath.empty()) {
            std::error_code projectError;
            if (!std::filesystem::exists(
                    source.project.absolutePath, projectError) ||
                projectError) {
                // A catalog from the former global project layout must not
                // make a missing bundle look like a current-workspace edit
                // during warm start. Project discovery will repopulate this
                // record from the managed Projects folder.
                source.project = BuildExpectedProjectInfo(layout, source);
            }
        }
        source.project.sourceRelativePathKey = source.relativePathKey;
        source.project.sourceFingerprint = source.fingerprint;
        source.project.sourceFileSizeBytes = source.fileSizeBytes;
        source.project.sourceModifiedTimeTicks = source.modifiedTimeTicks;
        const nlohmann::json* memberships = FindJsonMember(
            item, "sourceSetProjectMemberships");
        if (memberships != nullptr && memberships->is_array()) {
            for (const nlohmann::json& membershipValue : *memberships) {
                if (!membershipValue.is_object()) continue;
                SourceSetProjectMembership membership;
                membership.projectId = JsonStringOrDefault(
                    membershipValue, "projectId");
                membership.projectName = JsonStringOrDefault(
                    membershipValue, "projectName");
                const std::filesystem::path storedProjectPath =
                    std::filesystem::u8path(JsonStringOrDefault(
                        membershipValue, "projectPath"));
                if (storedProjectPath.empty() || storedProjectPath.has_root_path()) {
                    continue;
                }
                membership.projectPath = storedProjectPath.is_absolute()
                    ? NormalizePath(storedProjectPath)
                    : NormalizePath(
                          layout.projectsDirectory / storedProjectPath);
                // Catalogs can outlive older storage layouts. Never warm-start
                // a project membership that points outside the active
                // workspace's managed Projects folder; discovery will rebuild
                // memberships from the local bundles below.
                if (!IsPathWithinRoot(
                        membership.projectPath,
                        layout.projectsDirectory)) {
                    continue;
                }
                std::error_code projectError;
                if (!std::filesystem::exists(
                        membership.projectPath, projectError) ||
                    projectError) {
                    continue;
                }
                membership.sourceSetId = JsonStringOrDefault(
                    membershipValue, "sourceSetId");
                membership.sourceSetName = JsonStringOrDefault(
                    membershipValue, "sourceSetName");
                const nlohmann::json* multiFrame = FindJsonMember(
                    membershipValue, "projectIsMultiFrame");
                membership.projectIsMultiFrame = multiFrame != nullptr &&
                    multiFrame->is_boolean() && multiFrame->get<bool>();
                const std::filesystem::path coverFileName =
                    std::filesystem::u8path(JsonStringOrDefault(
                        membershipValue, "coverCacheFileName"))
                        .filename();
                if (!coverFileName.empty()) {
                    membership.projectCoverThumbnailCachePath =
                        layout.projectCoversDirectory /
                        coverFileName;
                }
                if (!membership.projectId.empty() ||
                    !membership.projectPath.empty()) {
                    source.sourceSetProjectMemberships.push_back(
                        std::move(membership));
                }
            }
        }
        loaded.push_back(std::move(source));
    }

    if (outProjects != nullptr) {
        std::unordered_map<std::string, std::size_t> projectIndexes;
        std::vector<std::unordered_set<std::string>> sourceSetIds;
        for (const SourceRecord& source : loaded) {
            for (const SourceSetProjectMembership& membership :
                 source.sourceSetProjectMemberships) {
                const std::string key = !membership.projectId.empty()
                    ? "id:" + ToLowerAscii(membership.projectId)
                    : "path:" + ToLowerAscii(
                        NormalizePath(membership.projectPath).generic_u8string());
                auto project = projectIndexes.find(key);
                if (project == projectIndexes.end()) {
                    SourceSetProjectCatalogEntry entry;
                    entry.projectId = membership.projectId;
                    entry.projectName = membership.projectName;
                    entry.absolutePath = membership.projectPath;
                    entry.status = ProjectStatus::Existing;
                    entry.multiFrameProject = membership.projectIsMultiFrame;
                    entry.referenceSourceKey = source.relativePathKey;
                    entry.coverThumbnailCachePath =
                        membership.projectCoverThumbnailCachePath;
                    outProjects->push_back(std::move(entry));
                    sourceSetIds.emplace_back();
                    project = projectIndexes.emplace(
                        key, outProjects->size() - 1).first;
                }
                SourceSetProjectCatalogEntry& entry =
                    (*outProjects)[project->second];
                ++entry.totalFrameCount;
                if (!membership.sourceSetId.empty() &&
                    sourceSetIds[project->second].insert(
                        membership.sourceSetId).second) {
                    ++entry.sourceSetCount;
                }
            }
        }
    }

    if (outSelectedSourceKey != nullptr) {
        *outSelectedSourceKey = JsonStringOrDefault(catalog, "lastSelectedSource");
    }
    outSources = std::move(loaded);
    return true;
}

bool SaveAppState(const std::filesystem::path& path, const AppState& state, std::string* outError) {
    return SaveAppStateIfCurrent(path, state, nullptr, outError);
}

bool SaveAppStateIfCurrent(
    const std::filesystem::path& path,
    const AppState& state,
    PersistenceCommitPredicate shouldCommit,
    std::string* outError) {
    nlohmann::json recent = nlohmann::json::array();
    for (const std::filesystem::path& root : DeduplicateRecentRoots(state.recentWorkspaceRoots, 8)) {
        recent.push_back(root.u8string());
    }

    nlohmann::json root = nlohmann::json::object();
    root["schema"] = "stack.rawWorkspace.appState";
    root["schemaVersion"] = 2;
    root["lastWorkspaceRoot"] = state.lastWorkspaceRoot.empty() ? nlohmann::json() : nlohmann::json(state.lastWorkspaceRoot.u8string());
    root["recentWorkspaces"] = std::move(recent);
    if (state.controlsPanelWidth > 0.0f && std::isfinite(state.controlsPanelWidth)) {
        root["controlsPanelWidth"] = state.controlsPanelWidth;
    }
    if (state.rawLabToolRailWidth > 0.0f && std::isfinite(state.rawLabToolRailWidth)) {
        root["rawLabToolRailWidth"] = state.rawLabToolRailWidth;
    }
    if (state.rawLabLowerShelfHeight > 0.0f && std::isfinite(state.rawLabLowerShelfHeight)) {
        root["rawLabLowerShelfHeight"] = state.rawLabLowerShelfHeight;
    }
    if (state.rawLabFilmstripHeight > 0.0f && std::isfinite(state.rawLabFilmstripHeight)) {
        root["rawLabFilmstripHeight"] = state.rawLabFilmstripHeight;
    }
    if (state.rawLabGalleryThumbnailScale > 0.0f &&
        std::isfinite(state.rawLabGalleryThumbnailScale)) {
        root["rawLabGalleryThumbnailScale"] = std::clamp(
            state.rawLabGalleryThumbnailScale, 0.65f, 1.75f);
    }
    root["rawLabLowerShelfOpen"] = state.rawLabLowerShelfOpen;
    root["rawLabToolRailOnRight"] = state.rawLabToolRailOnRight;
    root["rawLabActiveTool"] = state.rawLabActiveTool == 10
        ? 10 : state.rawLabActiveTool == 9
        ? 9
        : std::clamp(state.rawLabActiveTool, 0, 7);
    root["rawLabActivePointCurve"] = std::clamp(state.rawLabActivePointCurve, 0, 3);
    root["rawLabColorWarpLiveCloud"] = state.rawLabColorWarpLiveCloud;
    root["rawLabLastGalleryHost"] = state.rawLabLastGalleryHost == 3 ? 3 : 1;
    root["rawLabGalleryDisplayMode"] = std::clamp(state.rawLabGalleryDisplayMode, 0, 1);
    root["rawLabGalleryContentMode"] = std::clamp(state.rawLabGalleryContentMode, 0, 2);
    return WriteJsonFile(path, root, shouldCommit, outError);
}

bool LoadAppState(const std::filesystem::path& path, AppState& outState, std::string* outError) {
    outState = {};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return true;
    }

    try {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            if (outError) {
                *outError = "Failed to open " + path.u8string() + ".";
            }
            return false;
        }

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            if (outError) outError->clear();
            return true;
        }
        if (root.value("schema", std::string()) !=
                "stack.rawWorkspace.appState" ||
            root.value("schemaVersion", 0) != 2) {
            if (outError) {
                *outError = "RAW Workspace app state does not use the current schema.";
            }
            return false;
        }

        const std::string lastRoot = JsonStringOrDefault(root, "lastWorkspaceRoot");
        if (!lastRoot.empty()) {
            outState.lastWorkspaceRoot = NormalizePath(std::filesystem::u8path(lastRoot));
        }
        const nlohmann::json* controlsPanelWidth = FindJsonMember(root, "controlsPanelWidth");
        if (controlsPanelWidth != nullptr && controlsPanelWidth->is_number()) {
            const float width = controlsPanelWidth->get<float>();
            if (width > 0.0f && std::isfinite(width)) {
                outState.controlsPanelWidth = width;
            }
        }
        const nlohmann::json* rawLabToolRailWidth = FindJsonMember(root, "rawLabToolRailWidth");
        if (rawLabToolRailWidth != nullptr && rawLabToolRailWidth->is_number()) {
            const float width = rawLabToolRailWidth->get<float>();
            if (width > 0.0f && std::isfinite(width)) {
                outState.rawLabToolRailWidth = width;
            }
        }
        const nlohmann::json* rawLabLowerShelfHeight = FindJsonMember(root, "rawLabLowerShelfHeight");
        if (rawLabLowerShelfHeight != nullptr && rawLabLowerShelfHeight->is_number()) {
            const float height = rawLabLowerShelfHeight->get<float>();
            if (height > 0.0f && std::isfinite(height)) {
                outState.rawLabLowerShelfHeight = height;
            }
        }
        if (const auto* smooth = FindJsonMember(root, "smoothRawViewportUpdates"); smooth && smooth->is_boolean())
            outState.smoothRawViewportUpdates = smooth->get<bool>();
        if (const auto* fps = FindJsonMember(root, "rawViewportFadeBelowFps"); fps && fps->is_number()) {
            const double value = fps->get<double>();
            if (std::isfinite(value)) outState.rawViewportFadeBelowFps = static_cast<int>(std::clamp(value,
                static_cast<double>(Raw::kMinimumViewportFadeBelowFps),
                static_cast<double>(Raw::kMaximumViewportFadeBelowFps)));
        }
        if (const auto* fps = FindJsonMember(root, "rawViewportTargetFps");
            fps != nullptr && fps->is_number()) {
            const double value = fps->get<double>();
            if (std::isfinite(value)) outState.rawViewportTargetFps =
                static_cast<int>(std::clamp(value,
                    static_cast<double>(Raw::kMinimumViewportTargetFps),
                    static_cast<double>(Raw::kMaximumViewportTargetFps)));
        }
        const nlohmann::json* rawLabToolRailOnRight =
            FindJsonMember(root, "rawLabToolRailOnRight");
        if (rawLabToolRailOnRight != nullptr &&
            rawLabToolRailOnRight->is_boolean()) {
            outState.rawLabToolRailOnRight =
                rawLabToolRailOnRight->get<bool>();
        }
        const nlohmann::json* rawLabFilmstripHeight = FindJsonMember(root, "rawLabFilmstripHeight");
        if (rawLabFilmstripHeight != nullptr && rawLabFilmstripHeight->is_number()) {
            const float height = rawLabFilmstripHeight->get<float>();
            if (height > 0.0f && std::isfinite(height)) {
                outState.rawLabFilmstripHeight = height;
            }
        }
        const nlohmann::json* rawLabGalleryThumbnailScale =
            FindJsonMember(root, "rawLabGalleryThumbnailScale");
        if (rawLabGalleryThumbnailScale != nullptr &&
            rawLabGalleryThumbnailScale->is_number()) {
            const float scale = rawLabGalleryThumbnailScale->get<float>();
            if (scale > 0.0f && std::isfinite(scale)) {
                outState.rawLabGalleryThumbnailScale = std::clamp(
                    scale, 0.65f, 1.75f);
            }
        }
        const nlohmann::json* rawLabLowerShelfOpen = FindJsonMember(root, "rawLabLowerShelfOpen");
        if (rawLabLowerShelfOpen != nullptr && rawLabLowerShelfOpen->is_boolean()) {
            outState.rawLabLowerShelfOpen = rawLabLowerShelfOpen->get<bool>();
        }
        const nlohmann::json* rawLabActiveTool = FindJsonMember(root, "rawLabActiveTool");
        if (rawLabActiveTool != nullptr && rawLabActiveTool->is_number_integer()) {
            const int activeTool = rawLabActiveTool->get<int>();
            outState.rawLabActiveTool = activeTool == 10
                ? 10 : activeTool == 9
                ? 9
                : std::clamp(activeTool, 0, 7);
        }
        const nlohmann::json* rawLabActivePointCurve =
            FindJsonMember(root, "rawLabActivePointCurve");
        if (rawLabActivePointCurve != nullptr && rawLabActivePointCurve->is_number_integer()) {
            outState.rawLabActivePointCurve =
                std::clamp(rawLabActivePointCurve->get<int>(), 0, 3);
        }
        const nlohmann::json* rawLabColorWarpLiveCloud =
            FindJsonMember(root, "rawLabColorWarpLiveCloud");
        if (rawLabColorWarpLiveCloud != nullptr &&
            rawLabColorWarpLiveCloud->is_boolean()) {
            outState.rawLabColorWarpLiveCloud =
                rawLabColorWarpLiveCloud->get<bool>();
        }
        const nlohmann::json* rawLabLastGalleryHost = FindJsonMember(root, "rawLabLastGalleryHost");
        if (rawLabLastGalleryHost != nullptr && rawLabLastGalleryHost->is_number_integer()) {
            const int host = rawLabLastGalleryHost->get<int>();
            if (host != 1 && host != 3) {
                if (outError) {
                    *outError = "RAW Workspace app state contains an unsupported Gallery host.";
                }
                outState = {};
                return false;
            }
            outState.rawLabLastGalleryHost = host;
        }
        const nlohmann::json* rawLabGalleryDisplayMode = FindJsonMember(root, "rawLabGalleryDisplayMode");
        if (rawLabGalleryDisplayMode != nullptr && rawLabGalleryDisplayMode->is_number_integer()) {
            outState.rawLabGalleryDisplayMode = std::clamp(rawLabGalleryDisplayMode->get<int>(), 0, 1);
        }
        const nlohmann::json* rawLabGalleryContentMode =
            FindJsonMember(root, "rawLabGalleryContentMode");
        if (rawLabGalleryContentMode != nullptr &&
            rawLabGalleryContentMode->is_number_integer()) {
            outState.rawLabGalleryContentMode = std::clamp(
                rawLabGalleryContentMode->get<int>(), 0, 2);
        }

        const nlohmann::json* recent = FindJsonMember(root, "recentWorkspaces");
        if (recent != nullptr && recent->is_array()) {
            for (const nlohmann::json& item : *recent) {
                if (!item.is_string()) {
                    continue;
                }
                const std::string value = item.get<std::string>();
                if (!value.empty()) {
                    outState.recentWorkspaceRoots.push_back(NormalizePath(std::filesystem::u8path(value)));
                }
            }
        }
        outState.recentWorkspaceRoots = DeduplicateRecentRoots(outState.recentWorkspaceRoots, 8);
        return true;
    } catch (const std::exception& error) {
        if (outError) {
            *outError = error.what();
        }
        outState = {};
        return false;
    } catch (...) {
        if (outError) {
            *outError = "Failed to load RAW Workspace app state.";
        }
        outState = {};
        return false;
    }
}

} // namespace Stack::RawWorkspace
