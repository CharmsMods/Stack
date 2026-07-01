#include "Library/Internal/LibraryStorageHelpers.h"

#include "Library/ProjectData.h"
#include "Utils/Base64.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace Stack::Library::StorageHelpers {

namespace StackFormat = StackBinaryFormat;

std::string BuildTimestampString(std::time_t now) {
    std::tm timeInfo {};
#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif

    char buffer[100] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo)) {
        return buffer;
    }
    return "Unknown";
}

std::string BuildTimestampString() {
    return BuildTimestampString(std::time(nullptr));
}

std::string BuildTimestampStringFromFileTime(const std::filesystem::file_time_type& writeTime) {
    const auto sysNow = std::chrono::system_clock::now();
    const auto fileNow = std::filesystem::file_time_type::clock::now();
    const auto converted = std::chrono::time_point_cast<std::chrono::system_clock::duration>(writeTime - fileNow + sysNow);
    return BuildTimestampString(std::chrono::system_clock::to_time_t(converted));
}

std::string TrimWhitespace(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

std::string SanitizeFileStem(const std::string& value) {
    std::string safe = TrimWhitespace(value);
    if (safe.empty()) {
        return "Untitled_Project";
    }

    for (char& ch : safe) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) {
            ch = '_';
        }
    }

    while (safe.find("__") != std::string::npos) {
        safe.replace(safe.find("__"), 2, "_");
    }

    return safe;
}

std::string DisplayNameFromStem(const std::string& stem) {
    std::string display = stem;
    std::replace(display.begin(), display.end(), '_', ' ');
    return display.empty() ? "Rendered Asset" : display;
}

bool IsSupportedAssetExtension(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".png" ||
           extension == ".jpg" ||
           extension == ".jpeg" ||
           extension == ".bmp" ||
           extension == ".tga";
}

bool IsSupportedProjectExtension(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".stack";
}

bool IsSupportedAssetMetadataExtension(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    return filename.size() > 5 && filename.substr(filename.size() - 5) == ".hash";
}

std::string DefaultProjectExtensionForKind(const std::string& projectKind) {
    (void)projectKind;
    return ".stack";
}

std::string EnsureProjectFileNameForKind(
    const std::string& fileName,
    const std::string& fallbackStem,
    const std::string& projectKind) {

    std::filesystem::path resolved = fileName.empty() ? std::filesystem::path(fallbackStem) : std::filesystem::path(fileName);
    if (!IsSupportedProjectExtension(resolved)) {
        resolved = resolved.stem().string() + DefaultProjectExtensionForKind(projectKind);
    }

    return resolved.filename().string();
}

std::string EnsureProjectFileName(const std::string& fileName, const std::string& fallbackName) {
    return EnsureProjectFileNameForKind(fileName, std::filesystem::path(fallbackName).stem().string(), StackFormat::kEditorProjectKind);
}

std::string EnsureAssetFileName(const std::string& fileName, const std::string& fallbackName) {
    std::string resolved = fileName.empty() ? fallbackName : fileName;
    if (std::filesystem::path(resolved).extension() != ".png") {
        resolved = std::filesystem::path(resolved).stem().string() + ".png";
    }
    return resolved;
}

std::string ResolveAssetProjectFileName(const std::string& stem, const std::vector<std::shared_ptr<ProjectEntry>>& projects) {
    for (const auto& project : projects) {
        if (project && std::filesystem::path(project->fileName).stem().string() == stem) {
            return project->fileName;
        }
    }
    return {};
}

std::string ResolveAssetProjectFileName(const std::string& stem, const std::vector<StackFormat::BundledProjectDocument>& projects) {
    for (const auto& project : projects) {
        if (std::filesystem::path(project.fileName).stem().string() == stem) {
            return project.fileName;
        }
    }
    return {};
}

std::string NormalizeComparableName(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

std::uint64_t ComputeExactPixelFingerprint(const std::vector<unsigned char>& pixels) {
    constexpr std::uint64_t kOffset = 1469598103934665603ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    for (const unsigned char byte : pixels) {
        hash ^= byte;
        hash *= kPrime;
    }
    return hash;
}

std::uint64_t ComputeAverageHash64(const std::vector<unsigned char>& pixels, const int width, const int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return 0;
    }

    std::array<float, 64> luminance {};
    float total = 0.0f;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int srcX = std::clamp((x * width) / 8, 0, width - 1);
            const int srcY = std::clamp((y * height) / 8, 0, height - 1);
            const std::size_t index =
                (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(srcX)) * 4;
            const float r = pixels[index + 0] / 255.0f;
            const float g = pixels[index + 1] / 255.0f;
            const float b = pixels[index + 2] / 255.0f;
            const float a = pixels[index + 3] / 255.0f;
            const float value = ((r * 0.299f) + (g * 0.587f) + (b * 0.114f)) * a;
            luminance[static_cast<std::size_t>(y) * 8 + static_cast<std::size_t>(x)] = value;
            total += value;
        }
    }

    const float average = total / 64.0f;
    std::uint64_t hash = 0;
    for (std::size_t index = 0; index < luminance.size(); ++index) {
        if (luminance[index] >= average) {
            hash |= (1ull << index);
        }
    }
    return hash;
}

int CountHammingDistance64(const std::uint64_t a, const std::uint64_t b) {
    return static_cast<int>(std::bitset<64>(a ^ b).count());
}

float ComputeNameSimilarityScore(const std::string& a, const std::string& b) {
    const std::string normalizedA = NormalizeComparableName(a);
    const std::string normalizedB = NormalizeComparableName(b);
    if (normalizedA.empty() || normalizedB.empty()) {
        return 0.0f;
    }
    if (normalizedA == normalizedB) {
        return 1.0f;
    }
    if (normalizedA.find(normalizedB) != std::string::npos || normalizedB.find(normalizedA) != std::string::npos) {
        return 0.9f;
    }

    std::size_t prefix = 0;
    while (prefix < normalizedA.size() && prefix < normalizedB.size() && normalizedA[prefix] == normalizedB[prefix]) {
        ++prefix;
    }
    return static_cast<float>(prefix) / static_cast<float>(std::max(normalizedA.size(), normalizedB.size()));
}

bool ShouldQueueAssetConflict(
    const std::string& localName,
    const std::vector<unsigned char>& localPixels,
    const int localWidth,
    const int localHeight,
    const std::string& importedName,
    const std::vector<unsigned char>& importedPixels,
    const int importedWidth,
    const int importedHeight,
    bool& outExactMatch) {

    outExactMatch = false;
    if (localPixels.empty() || importedPixels.empty() || localWidth <= 0 || localHeight <= 0 || importedWidth <= 0 || importedHeight <= 0) {
        return false;
    }

    if (localWidth == importedWidth &&
        localHeight == importedHeight &&
        localPixels.size() == importedPixels.size() &&
        ComputeExactPixelFingerprint(localPixels) == ComputeExactPixelFingerprint(importedPixels) &&
        localPixels == importedPixels) {
        outExactMatch = true;
        return false;
    }

    const std::uint64_t localHash = ComputeAverageHash64(localPixels, localWidth, localHeight);
    const std::uint64_t importedHash = ComputeAverageHash64(importedPixels, importedWidth, importedHeight);
    const int hashDistance = CountHammingDistance64(localHash, importedHash);
    const float nameScore = ComputeNameSimilarityScore(localName, importedName);
    return hashDistance <= 4 || (hashDistance <= 8 && nameScore >= 0.55f);
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& outBytes) {
    outBytes.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);

    outBytes.resize(static_cast<std::size_t>(size));
    if (!outBytes.empty()) {
        file.read(reinterpret_cast<char*>(outBytes.data()), size);
    }

    return file.good() || file.eof();
}

bool WriteFileBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    return file.good();
}

bool LoadLegacyProjectDocument(
    const std::filesystem::path& path,
    StackFormat::ProjectDocument& outDocument,
    const StackFormat::ProjectLoadOptions& options) {

    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        StackFormat::json root = StackFormat::json::parse(file);
        outDocument.metadata.projectKind = StackFormat::kEditorProjectKind;
        outDocument.metadata.projectName = root.value("name", "Untitled Project");
        outDocument.metadata.timestamp = root.value("timestamp", "Unknown");
        outDocument.metadata.sourceWidth = root.value("width", 0);
        outDocument.metadata.sourceHeight = root.value("height", 0);

        if (options.includeThumbnail) {
            const std::string thumbnailB64 = root.value("thumbnail", "");
            if (!thumbnailB64.empty()) {
                outDocument.thumbnailBytes = Utils::Base64Decode(thumbnailB64);
            } else {
                outDocument.thumbnailBytes.clear();
            }
        } else {
            outDocument.thumbnailBytes.clear();
        }

        if (options.includeSourceImage) {
            const std::string sourceB64 = root.value("source", "");
            if (!sourceB64.empty()) {
                outDocument.sourceImageBytes = Utils::Base64Decode(sourceB64);
            } else {
                outDocument.sourceImageBytes.clear();
            }
        } else {
            outDocument.sourceImageBytes.clear();
        }

        outDocument.pipelineData = options.includePipelineData
            ? root.value("pipeline", StackFormat::json::array())
            : StackFormat::json();

        return true;
    } catch (...) {
        return false;
    }
}

std::vector<unsigned char> DecodeDataUrl(const std::string& dataUrl) {
    if (dataUrl.empty()) return {};
    size_t commaPos = dataUrl.find(',');
    if (commaPos == std::string::npos) return Utils::Base64Decode(dataUrl);
    return Utils::Base64Decode(dataUrl.substr(commaPos + 1));
}

std::string ComputeImageHash(const std::vector<unsigned char>& data) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char b : data) {
        hash ^= b;
        hash *= 1099511628211ULL;
    }
    char hex[32];
#ifdef _WIN32
    sprintf_s(hex, "%016llx", hash);
#else
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
#endif
    return std::string(hex);
}

} // namespace Stack::Library::StorageHelpers
