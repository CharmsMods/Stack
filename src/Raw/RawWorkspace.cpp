#include "RawWorkspace.h"

#include "RawLoader.h"
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
#include <unordered_set>

namespace Stack::RawWorkspace {
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
    return path.generic_string();
}

std::int64_t FileTimeTicks(const std::filesystem::file_time_type& fileTime) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(fileTime.time_since_epoch()).count();
}

std::int64_t UnixSecondsNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool WriteJsonFile(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError);

bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& json, std::string* outError) {
    return WriteJsonFile(path, json, nullptr, outError);
}

std::filesystem::path MakeJsonTempPath(const std::filesystem::path& path) {
    const auto nowTicks = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const std::size_t threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::filesystem::path temp = path;
    temp += ".tmp.";
    temp += std::to_string(nowTicks);
    temp += ".";
    temp += std::to_string(threadHash);
    return temp;
}

bool WriteJsonFile(
    const std::filesystem::path& path,
    const nlohmann::json& json,
    const PersistenceCommitPredicate& shouldCommit,
    std::string* outError) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create " + path.parent_path().string() + ": " + ec.message();
        }
        return false;
    }

    const std::filesystem::path tempPath = MakeJsonTempPath(path);
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (outError) {
            *outError = "Failed to open " + tempPath.string() + " for writing.";
        }
        return false;
    }

    out << json.dump(2);
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to write " + tempPath.string() + ".";
        }
        out.close();
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    out.close();
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to close " + tempPath.string() + ".";
        }
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (shouldCommit && !shouldCommit()) {
        std::filesystem::remove(tempPath, ec);
        return true;
    }

    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            if (outError) {
                *outError = "Failed to replace " + path.string() + ": " + ec.message();
            }
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }
    return true;
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    outJson = nlohmann::json::parse(in, nullptr, false);
    if (outJson.is_discarded() || !outJson.is_object()) {
        outJson = nlohmann::json::object();
        return false;
    }
    return true;
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
            *outError = "Failed to create " + path.parent_path().string() + ": " + ec.message();
        }
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (outError) {
            *outError = "Failed to open " + path.string() + " for writing.";
        }
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out.good()) {
        if (outError) {
            *outError = "Failed to write " + path.string() + ".";
        }
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

std::filesystem::path ThumbnailRelativePathForSource(const SourceRecord& source) {
    std::filesystem::path relative;
    if (!source.parentFolderKey.empty()) {
        relative = std::filesystem::path(source.parentFolderKey);
    }
    relative /= SanitizedThumbnailStem(source) + ".thumb.png";
    return relative;
}

std::filesystem::path ThumbnailSignatureRelativePathForSource(const SourceRecord& source) {
    std::filesystem::path relative;
    if (!source.parentFolderKey.empty()) {
        relative = std::filesystem::path(source.parentFolderKey);
    }
    relative /= SanitizedThumbnailStem(source) + ".thumb.json";
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

bool BuildMosaicThumbnailPixels(const Raw::RawImageData& raw, int maxDimension, std::vector<unsigned char>& outPixels, int& outW, int& outH) {
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

bool BuildLinearThumbnailPixels(const Raw::RawImageData& raw, int maxDimension, std::vector<unsigned char>& outPixels, int& outW, int& outH) {
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
        const std::string key = ToLowerAscii(normalized.string());
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

ManagedLayout BuildManagedLayout(const std::filesystem::path& workspaceRoot) {
    ManagedLayout layout;
    layout.workspaceRoot = NormalizePath(workspaceRoot);
    layout.thumbnailsDirectory = layout.workspaceRoot / kThumbnailsFolderName;
    layout.projectsDirectory = layout.workspaceRoot / kProjectsFolderName;
    layout.catalogDirectory = layout.workspaceRoot / kCatalogFolderName;
    layout.catalogPath = layout.catalogDirectory / kCatalogFileName;
    layout.ratingsPath = layout.catalogDirectory / kRatingsFileName;
    return layout;
}

bool IsManagedFolderName(const std::string& folderName) {
    const std::string lowered = ToLowerAscii(folderName);
    return lowered == ToLowerAscii(kThumbnailsFolderName) ||
        lowered == ToLowerAscii(kProjectsFolderName) ||
        lowered == ToLowerAscii(kCatalogFolderName);
}

bool EnsureManagedFolders(const std::filesystem::path& workspaceRoot, std::string* outError) {
    const ManagedLayout layout = BuildManagedLayout(workspaceRoot);
    std::error_code ec;
    std::filesystem::create_directories(layout.thumbnailsDirectory, ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create " + layout.thumbnailsDirectory.string() + ": " + ec.message();
        }
        return false;
    }
    std::filesystem::create_directories(layout.projectsDirectory, ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create " + layout.projectsDirectory.string() + ": " + ec.message();
        }
        return false;
    }
    std::filesystem::create_directories(layout.catalogDirectory, ec);
    if (ec) {
        if (outError) {
            *outError = "Failed to create " + layout.catalogDirectory.string() + ": " + ec.message();
        }
        return false;
    }
    return true;
}

bool DefaultRawPathPredicate(const std::filesystem::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
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
    CancellationPredicate shouldCancel) {
    ScanResult result;
    result.layout = BuildManagedLayout(workspaceRoot);
    result.progress.statusText = "Scanning Workspace...";

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

    auto reportProgress = [&]() {
        if (progressCallback) {
            progressCallback(result.progress);
        }
    };

    try {
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
                result.progress.currentItem = entryPath.filename().string();
                result.progress.statusText = "Scanning " + result.progress.currentItem;
                if (IsManagedFolderName(entryPath.filename().string())) {
                    ++result.progress.managedDirectoriesSkipped;
                    it.disable_recursion_pending();
                }
                reportProgress();
                continue;
            }
            ec.clear();

            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            ++result.progress.filesVisited;
            result.progress.currentItem = entryPath.filename().string();
            if (!isRawPath(entryPath)) {
                reportProgress();
                continue;
            }

            SourceRecord record;
            record.absolutePath = NormalizePath(entryPath);
            std::error_code relEc;
            record.relativePath = std::filesystem::relative(record.absolutePath, result.layout.workspaceRoot, relEc);
            if (relEc || record.relativePath.empty()) {
                record.relativePath = record.absolutePath.filename();
            }
            record.relativePathKey = GenericPathKey(record.relativePath);
            record.fileName = record.absolutePath.filename().string();
            record.stem = record.absolutePath.stem().string();
            record.extension = record.absolutePath.extension().string();
            record.parentFolderKey = record.relativePath.has_parent_path()
                ? GenericPathKey(record.relativePath.parent_path())
                : std::string();

            std::error_code statEc;
            record.fileSizeBytes = std::filesystem::file_size(record.absolutePath, statEc);
            if (statEc) {
                record.fileSizeBytes = 0;
            }
            statEc.clear();
            record.modifiedTimeTicks = FileTimeTicks(std::filesystem::last_write_time(record.absolutePath, statEc));
            if (statEc) {
                record.modifiedTimeTicks = 0;
            }

            result.sources.push_back(std::move(record));
            result.progress.discoveredRawCount = static_cast<int>(result.sources.size());
            result.progress.statusText = "Discovered " + std::to_string(result.progress.discoveredRawCount) + " RAW files";
            reportProgress();
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
    result.progress.statusText = result.sources.empty()
        ? "Workspace scan found no RAW files."
        : "Workspace scan complete.";
    reportProgress();
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

bool ThumbnailSignatureMatches(const ThumbnailSignature& expected, const nlohmann::json& actual) {
    if (!actual.is_object()) {
        return false;
    }
    const std::string actualFingerprint = JsonStringOrDefault(actual, "sourceFingerprint");
    return JsonIntOrDefault(actual, "schemaVersion") == expected.schemaVersion &&
        JsonStringOrDefault(actual, "sourceRelativePath") == expected.sourceRelativePath &&
        JsonUintMaxOrDefault(actual, "sourceFileSizeBytes") == expected.sourceFileSizeBytes &&
        JsonInt64OrDefault(actual, "sourceModifiedTimeTicks") == expected.sourceModifiedTimeTicks &&
        actualFingerprint == expected.sourceFingerprint &&
        JsonIntOrDefault(actual, "rawLoaderAlgorithmVersion") == expected.rawLoaderAlgorithmVersion &&
        JsonIntOrDefault(actual, "neutralPreviewSettingsVersion") == expected.neutralPreviewSettingsVersion &&
        JsonIntOrDefault(actual, "thumbnailVersion") == expected.thumbnailVersion &&
        JsonIntOrDefault(actual, "maxDimension") == expected.maxDimension;
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
        return source.thumbnail.status;
    }

    source.thumbnail.status = ThumbnailStatus::Valid;
    source.thumbnail.width = JsonIntOrDefault(signatureJson, "thumbnailWidth");
    source.thumbnail.height = JsonIntOrDefault(signatureJson, "thumbnailHeight");
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

ThumbnailGenerationResult GenerateNeutralThumbnail(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension,
    CancellationPredicate shouldCancel) {
    ThumbnailGenerationResult result;
    result.thumbnail = BuildThumbnailInfo(layout, source, maxDimension);
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
    if (!Raw::RawLoader::LoadFile(source.absolutePath.string(), raw, isCancelled)) {
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
        built = BuildLinearThumbnailPixels(raw, maxDimension, rgba, width, height);
    } else {
        built = BuildMosaicThumbnailPixels(raw, maxDimension, rgba, width, height);
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
    signature["thumbnailRelativePath"] = result.thumbnail.relativePath.generic_string();
    signature["thumbnailWidth"] = width;
    signature["thumbnailHeight"] = height;
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

GalleryPresentation BuildGalleryPresentation(const WorkspaceState& state) {
    GalleryPresentation presentation;
    presentation.totalSources = static_cast<int>(state.sources.size());
    presentation.selectedSourceKey = state.selectedSourceKey;

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
        case RawProjectMode::RecipeBacked: return "Recipe Backed";
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
        case RawProjectMode::RecipeBacked:
            return "recipe-backed";
        case RawProjectMode::Unknown:
        default:
            return "unknown";
    }
}

RawProjectMode RawProjectModeFromString(const std::string& value) {
    if (value == "recipe-backed") return RawProjectMode::RecipeBacked;
    if (value == "managed-decomposed") return RawProjectMode::ManagedDecomposed;
    if (value == "custom-graph") return RawProjectMode::CustomGraph;
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
    item["absolutePath"] = source.absolutePath.string();
    item["relativePath"] = source.relativePathKey;
    item["fileName"] = source.fileName;
    item["stem"] = source.stem;
    item["extension"] = source.extension;
    item["parentFolder"] = source.parentFolderKey;
    item["fileSizeBytes"] = source.fileSizeBytes;
    item["modifiedTimeTicks"] = source.modifiedTimeTicks;
    item["fingerprint"] = source.fingerprint.empty() ? nlohmann::json() : nlohmann::json(source.fingerprint);
    item["thumbnail"] = {
        { "status", ThumbnailStatusToString(source.thumbnail.status) },
        { "relativePath", source.thumbnail.relativePath.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.relativePath.generic_string()) },
        { "signatureRelativePath", source.thumbnail.signatureRelativePath.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.signatureRelativePath.generic_string()) },
        { "width", source.thumbnail.width },
        { "height", source.thumbnail.height },
        { "error", source.thumbnail.errorMessage.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.errorMessage) }
    };
    item["project"] = {
        { "status", ProjectStatusToString(source.project.status) },
        { "relativePath", source.project.relativePath.empty() ? nlohmann::json() : nlohmann::json(source.project.relativePath.generic_string()) },
        { "mode", RawProjectModeToString(source.project.mode) },
        { "projectModifiedTimeTicks", source.project.projectModifiedTimeTicks },
        { "linkedRaw", source.project.linkedRaw },
        { "embeddedRaw", source.project.embeddedRaw },
        { "readOnlyReason", source.project.readOnlyReason.empty() ? nlohmann::json() : nlohmann::json(source.project.readOnlyReason) },
        { "associationReason", source.project.associationReason.empty() ? nlohmann::json() : nlohmann::json(source.project.associationReason) },
        { "error", source.project.errorMessage.empty() ? nlohmann::json() : nlohmann::json(source.project.errorMessage) }
    };
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
    item["absolutePath"] = source.absolutePath.string();
    item["relativePath"] = source.relativePathKey;
    item["fileName"] = source.fileName;
    item["stem"] = source.stem;
    item["extension"] = source.extension;
    item["parentFolder"] = source.parentFolderKey;
    item["fileSizeBytes"] = source.fileSizeBytes;
    item["modifiedTimeTicks"] = source.modifiedTimeTicks;
    item["fingerprint"] = source.fingerprint.empty() ? nlohmann::json() : nlohmann::json(source.fingerprint);
    item["thumbnail"] = {
        { "status", ThumbnailStatusToString(source.thumbnail.status) },
        { "relativePath", source.thumbnail.relativePath.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.relativePath.generic_string()) },
        { "signatureRelativePath", source.thumbnail.signatureRelativePath.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.signatureRelativePath.generic_string()) },
        { "width", source.thumbnail.width },
        { "height", source.thumbnail.height },
        { "error", source.thumbnail.errorMessage.empty() ? nlohmann::json() : nlohmann::json(source.thumbnail.errorMessage) }
    };
    item["project"] = {
        { "status", ProjectStatusToString(source.project.status) },
        { "relativePath", source.project.relativePath.empty() ? nlohmann::json() : nlohmann::json(source.project.relativePath.generic_string()) },
        { "mode", RawProjectModeToString(source.project.mode) },
        { "projectModifiedTimeTicks", source.project.projectModifiedTimeTicks },
        { "linkedRaw", source.project.linkedRaw },
        { "embeddedRaw", source.project.embeddedRaw },
        { "readOnlyReason", source.project.readOnlyReason.empty() ? nlohmann::json() : nlohmann::json(source.project.readOnlyReason) },
        { "associationReason", source.project.associationReason.empty() ? nlohmann::json() : nlohmann::json(source.project.associationReason) },
        { "error", source.project.errorMessage.empty() ? nlohmann::json() : nlohmann::json(source.project.errorMessage) }
    };
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
        sourceArray.push_back(SerializeCatalogSourceRecord(source));
    }

    nlohmann::json catalog = nlohmann::json::object();
    catalog["schema"] = "stack.rawWorkspace.catalog";
    catalog["schemaVersion"] = 1;
    catalog["workspaceRoot"] = layout.workspaceRoot.string();
    catalog["lastScanUnixSeconds"] = UnixSecondsNow();
    catalog["lastSelectedSource"] = selectedSourceKey.empty() ? nlohmann::json() : nlohmann::json(selectedSourceKey);
    catalog["managedFolders"] = {
        { "thumbnails", kThumbnailsFolderName },
        { "projects", kProjectsFolderName },
        { "catalog", kCatalogFolderName }
    };
    catalog["sources"] = std::move(sourceArray);

    if (!WriteJsonFile(layout.catalogPath, catalog, shouldCommit, outError)) {
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
        recent.push_back(root.string());
    }

    nlohmann::json root = nlohmann::json::object();
    root["schema"] = "stack.rawWorkspace.appState";
    root["schemaVersion"] = 1;
    root["lastWorkspaceRoot"] = state.lastWorkspaceRoot.empty() ? nlohmann::json() : nlohmann::json(state.lastWorkspaceRoot.string());
    root["lastSelectedSource"] = state.lastSelectedSourceKey.empty() ? nlohmann::json() : nlohmann::json(state.lastSelectedSourceKey);
    root["recentWorkspaces"] = std::move(recent);
    if (state.controlsPanelWidth > 0.0f && std::isfinite(state.controlsPanelWidth)) {
        root["controlsPanelWidth"] = state.controlsPanelWidth;
    }
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
                *outError = "Failed to open " + path.string() + ".";
            }
            return false;
        }

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return true;
        }

        const std::string lastRoot = JsonStringOrDefault(root, "lastWorkspaceRoot");
        if (!lastRoot.empty()) {
            outState.lastWorkspaceRoot = NormalizePath(lastRoot);
        }
        outState.lastSelectedSourceKey = JsonStringOrDefault(root, "lastSelectedSource");
        const nlohmann::json* controlsPanelWidth = FindJsonMember(root, "controlsPanelWidth");
        if (controlsPanelWidth != nullptr && controlsPanelWidth->is_number()) {
            const float width = controlsPanelWidth->get<float>();
            if (width > 0.0f && std::isfinite(width)) {
                outState.controlsPanelWidth = width;
            }
        }

        const nlohmann::json* recent = FindJsonMember(root, "recentWorkspaces");
        if (recent != nullptr && recent->is_array()) {
            for (const nlohmann::json& item : *recent) {
                if (!item.is_string()) {
                    continue;
                }
                const std::string value = item.get<std::string>();
                if (!value.empty()) {
                    outState.recentWorkspaceRoots.push_back(NormalizePath(value));
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
