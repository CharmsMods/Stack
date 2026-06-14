#include "LibraryManager.h"
#include "TagManager.h"

#include "Async/TaskSystem.h"
#include "Composite/CompositeModule.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include "Renderer/GLHelpers.h"
#include "Utils/Base64.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"

static std::vector<unsigned char> ReadBinaryJsonHelper(const nlohmann::json& value);
static bool ExtractEmbeddedGraphSourcePng(const nlohmann::json& pipelineData, std::vector<unsigned char>& outPngBytes);

namespace {

namespace StackFormat = StackBinaryFormat;

constexpr std::uintmax_t kMaxSynchronousAssetThumbnailBytes = 8ull * 1024ull * 1024ull;

struct DecodedProjectPreview {
    bool success = false;
    bool renderProject = false;
    std::string projectKind = StackFormat::kEditorProjectKind;
    std::vector<unsigned char> sourcePixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    StackFormat::json pipelineData = StackFormat::json::array();
    bool fallbackAssetSuccess = false;
    std::vector<unsigned char> fallbackAssetPixels;
    int fallbackAssetWidth = 0;
    int fallbackAssetHeight = 0;
};

struct DecodedAssetPreview {
    bool success = false;
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

void png_write_func(void* context, void* data, int size) {
    auto vec = static_cast<std::vector<unsigned char>*>(context);
    auto* bytes = static_cast<unsigned char*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}



bool HasMeaningfulPixels(const std::vector<unsigned char>& pixels) {
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        if (pixels[index + 3] > 4) {
            return true;
        }

        if (pixels[index] > 4 || pixels[index + 1] > 4 || pixels[index + 2] > 4) {
            return true;
        }
    }
    return false;
}

bool LoadRgbaImageFromFile(const std::filesystem::path& path, std::vector<unsigned char>& outPixels, int& outW, int& outH);

std::filesystem::path GetStartupTracePath() {
    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (ec) {
        return std::filesystem::path("StackStartup.log");
    }
    return current / "StackStartup.log";
}

void TraceStartupStep(const std::string& message) {
    std::ofstream file(GetStartupTracePath(), std::ios::app);
    if (!file.is_open()) {
        return;
    }

    file << message << '\n';
    file.flush();
}

bool DecodePreviewBytes(
    const std::vector<unsigned char>& encodedBytes,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH) {
    int channels = 4;
    return !encodedBytes.empty() && LibraryManager::DecodeImageBytes(encodedBytes, outPixels, outW, outH, channels);
}

bool ResolveProjectPreviewPixels(
    const StackFormat::ProjectDocument& project,
    const std::filesystem::path* fallbackAssetPath,
    const std::vector<unsigned char>* fallbackAssetBytes,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    std::string& outStatus) {
    outPixels.clear();
    outW = 0;
    outH = 0;
    outStatus.clear();

    const std::string projectKind = project.metadata.projectKind.empty()
        ? StackFormat::kEditorProjectKind
        : project.metadata.projectKind;
    const bool isComposite = projectKind == StackFormat::kCompositeProjectKind;
    const bool isRender = projectKind == StackFormat::kRenderProjectKind;

    std::vector<unsigned char> sourcePngBytes = project.sourceImageBytes;
    std::vector<unsigned char> graphSourcePngBytes;
    if (projectKind == StackFormat::kEditorProjectKind &&
        ExtractEmbeddedGraphSourcePng(project.pipelineData, graphSourcePngBytes)) {
        sourcePngBytes = std::move(graphSourcePngBytes);
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    const bool hasSourcePixels = DecodePreviewBytes(sourcePngBytes, sourcePixels, sourceW, sourceH);

    if (isComposite || isRender) {
        if (hasSourcePixels) {
            outPixels = std::move(sourcePixels);
            outW = sourceW;
            outH = sourceH;
            outStatus = isComposite ? "Using embedded composite preview." : "Using embedded render preview.";
            return true;
        }
        if (DecodePreviewBytes(project.thumbnailBytes, outPixels, outW, outH)) {
            outStatus = "Using embedded thumbnail preview.";
            return true;
        }
        outStatus = "This project does not contain a usable embedded preview image.";
        return false;
    }

    if (fallbackAssetBytes != nullptr && !fallbackAssetBytes->empty()) {
        if (DecodePreviewBytes(*fallbackAssetBytes, outPixels, outW, outH)) {
            outStatus = "Using the imported saved rendered preview.";
            return true;
        }
    }

    if (fallbackAssetPath != nullptr && LoadRgbaImageFromFile(*fallbackAssetPath, outPixels, outW, outH)) {
        outStatus = "Using the saved rendered preview.";
        return true;
    }

    if (hasSourcePixels && !project.pipelineData.is_null()) {
        EditorModule previewEditor;
        previewEditor.Initialize();
        previewEditor.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, 4);
        previewEditor.DeserializePipeline(project.pipelineData);

        bool hasRawDevelop = false;
        for (const auto& node : previewEditor.GetNodeGraph().GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::RawDevelop) {
                hasRawDevelop = true;
                break;
            }
        }

        if (hasRawDevelop) {
            previewEditor.GetPipeline().ExecuteGraph(previewEditor.BuildGraphSnapshot());
        } else {
            previewEditor.GetPipeline().Execute(previewEditor.GetLayers());
        }

        int renderedW = 0;
        int renderedH = 0;
        std::vector<unsigned char> renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
        const bool renderedLooksBlank =
            !renderedPixels.empty() &&
            !HasMeaningfulPixels(renderedPixels) &&
            HasMeaningfulPixels(sourcePixels);
        if (!renderedPixels.empty() && renderedW > 0 && renderedH > 0 && !renderedLooksBlank) {
            LibraryManager::FlipImageRowsInPlace(renderedPixels, renderedW, renderedH, 4);
            outPixels = std::move(renderedPixels);
            outW = renderedW;
            outH = renderedH;
            outStatus = "Rendered a live preview from the editor pipeline.";
            return true;
        }
    }

    if (DecodePreviewBytes(project.thumbnailBytes, outPixels, outW, outH)) {
        outStatus = "Using embedded thumbnail preview.";
        return true;
    }

    if (hasSourcePixels) {
        outPixels = std::move(sourcePixels);
        outW = sourceW;
        outH = sourceH;
        outStatus = "Using source image fallback.";
        return true;
    }

    outStatus = "Failed to build a preview from the project, its rendered asset, thumbnail, or source image.";
    return false;
}

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

std::vector<unsigned char> ResizePixelsNearest(
    const std::vector<unsigned char>& sourcePixels,
    int sourceWidth,
    int sourceHeight,
    int& outWidth,
    int& outHeight,
    int maxDimension = 400) {

    outWidth = sourceWidth;
    outHeight = sourceHeight;

    if (sourcePixels.empty() || sourceWidth <= 0 || sourceHeight <= 0) {
        return {};
    }

    if (sourceWidth <= maxDimension && sourceHeight <= maxDimension) {
        return sourcePixels;
    }

    if (sourceWidth > sourceHeight) {
        outWidth = maxDimension;
        outHeight = static_cast<int>((static_cast<float>(sourceHeight) / static_cast<float>(sourceWidth)) * outWidth);
    } else {
        outHeight = maxDimension;
        outWidth = static_cast<int>((static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight)) * outHeight);
    }

    outWidth = std::max(outWidth, 1);
    outHeight = std::max(outHeight, 1);

    std::vector<unsigned char> resizedPixels(outWidth * outHeight * 4);
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int srcX = (x * sourceWidth) / outWidth;
            const int srcY = (y * sourceHeight) / outHeight;
            const int srcIdx = (srcY * sourceWidth + srcX) * 4;
            const int dstIdx = (y * outWidth + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                resizedPixels[dstIdx + channel] = sourcePixels[srcIdx + channel];
            }
        }
    }

    return resizedPixels;
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

bool LoadRgbaImageFromFile(const std::filesystem::path& path, std::vector<unsigned char>& outPixels, int& outW, int& outH) {
    outPixels.clear();
    outW = 0;
    outH = 0;

    if (!std::filesystem::exists(path)) {
        return false;
    }

    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load(path.string().c_str(), &outW, &outH, &channels, 4);
    if (!pixels) {
        return false;
    }

    outPixels.assign(pixels, pixels + (outW * outH * 4));
    stbi_image_free(pixels);
    return true;
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

std::vector<unsigned char> DecodeDataUrl(const std::string& dataUrl) {
    if (dataUrl.empty()) return {};
    size_t commaPos = dataUrl.find(',');
    if (commaPos == std::string::npos) return Utils::Base64Decode(dataUrl);
    return Utils::Base64Decode(dataUrl.substr(commaPos + 1));
}

} // namespace

LibraryManager::LibraryManager() {
    m_LibraryPath = std::filesystem::current_path() / "Library";
    m_AssetsPath = m_LibraryPath / "Assets";
    if (!std::filesystem::exists(m_LibraryPath)) {
        std::filesystem::create_directories(m_LibraryPath);
    }
    if (!std::filesystem::exists(m_AssetsPath)) {
        std::filesystem::create_directories(m_AssetsPath);
    }
}

LibraryManager::~LibraryManager() {
    for (auto& project : m_Projects) {
        ReleaseProjectTextures(project);
    }
    for (auto& asset : m_Assets) {
        ReleaseAssetTextures(asset);
    }
}

void LibraryManager::ReleaseProjectTextures(std::shared_ptr<ProjectEntry> project) {
    if (!project) return;

    if (project->thumbnailTex) {
        glDeleteTextures(1, &project->thumbnailTex);
        project->thumbnailTex = 0;
    }
    if (project->sourcePreviewTex) {
        glDeleteTextures(1, &project->sourcePreviewTex);
        project->sourcePreviewTex = 0;
    }
    if (project->fullPreviewTex) {
        glDeleteTextures(1, &project->fullPreviewTex);
        project->fullPreviewTex = 0;
    }
}

void LibraryManager::ReleaseAssetTextures(std::shared_ptr<AssetEntry> asset) {
    if (!asset) return;

    if (asset->thumbnailTex) {
        glDeleteTextures(1, &asset->thumbnailTex);
        asset->thumbnailTex = 0;
    }
    if (asset->fullPreviewTex) {
        glDeleteTextures(1, &asset->fullPreviewTex);
        asset->fullPreviewTex = 0;
    }
}


void LibraryManager::FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    if (pixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return;
    }

    const int rowSize = width * channels;
    std::vector<unsigned char> tempRow(rowSize);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* row1 = &pixels[y * rowSize];
        unsigned char* row2 = &pixels[(height - 1 - y) * rowSize];
        std::memcpy(tempRow.data(), row1, rowSize);
        std::memcpy(row1, row2, rowSize);
        std::memcpy(row2, tempRow.data(), rowSize);
    }
}

bool LibraryManager::DecodeImageBytes(
    const std::vector<unsigned char>& encodedImage,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) {

    outPixels.clear();
    outW = 0;
    outH = 0;
    outChannels = 0;

    if (encodedImage.empty()) return false;

    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load_from_memory(
        encodedImage.data(),
        static_cast<int>(encodedImage.size()),
        &outW,
        &outH,
        &outChannels,
        4);
    if (!pixels) {
        return false;
    }

    outChannels = 4;
    outPixels.assign(pixels, pixels + (outW * outH * 4));
    stbi_image_free(pixels);
    return true;
}

void LibraryManager::PrepareConflictPreview(int index) {
    if (index < 0 || index >= (int)m_PendingConflicts.size()) return;
    auto& conflict = m_PendingConflicts[index];
    if (conflict.previewsReady || conflict.previewFailed) return;

    if (conflict.importedProjectIndex < 0 || conflict.importedProjectIndex >= (int)m_ActiveImportBundle.projects.size()) {
        return;
    }

    const auto& importedProjectEntry = m_ActiveImportBundle.projects[conflict.importedProjectIndex];
    const auto& importedProjectObj = importedProjectEntry.project;

    StackFormat::ProjectLoadOptions loadOptions;
    loadOptions.includeThumbnail = true;
    loadOptions.includeSourceImage = true;
    loadOptions.includePipelineData = true;

    StackFormat::ProjectDocument localProjectObj;
    if (!LoadProjectDocument(conflict.localProjectFileName, localProjectObj, loadOptions)) {
        return;
    }

    std::vector<unsigned char> importedAssetBytes;
    for (const auto& asset : m_ActiveImportBundle.assets) {
        if (asset.projectFileName == importedProjectEntry.fileName && !asset.imageBytes.empty()) {
            importedAssetBytes = asset.imageBytes;
            break;
        }
    }
    const std::filesystem::path localAssetPath = BuildAssetPathForProjectFile(conflict.localProjectFileName);

    std::vector<unsigned char> localPixels;
    int localW = 0;
    int localH = 0;
    std::string localStatus;
    const bool localPreviewOk = ResolveProjectPreviewPixels(
        localProjectObj,
        &localAssetPath,
        nullptr,
        localPixels,
        localW,
        localH,
        localStatus);

    std::vector<unsigned char> importedPixels;
    int importedW = 0;
    int importedH = 0;
    std::string importedStatus;
    const bool importedPreviewOk = ResolveProjectPreviewPixels(
        importedProjectObj,
        nullptr,
        importedAssetBytes.empty() ? nullptr : &importedAssetBytes,
        importedPixels,
        importedW,
        importedH,
        importedStatus);

    if (localPreviewOk && !localPixels.empty()) {
        conflict.localPreviewTex = GLHelpers::CreateTextureFromPixels(localPixels.data(), localW, localH, 4);
    }
    if (importedPreviewOk && !importedPixels.empty()) {
        conflict.importedPreviewTex = GLHelpers::CreateTextureFromPixels(importedPixels.data(), importedW, importedH, 4);
    }

    conflict.previewsReady = conflict.localPreviewTex != 0 && conflict.importedPreviewTex != 0;
    conflict.previewFailed = !conflict.previewsReady;
    if (conflict.previewFailed) {
        conflict.previewStatusText = "Preview generation failed.";
        if (!localStatus.empty()) {
            conflict.previewStatusText += "\nLocal: " + localStatus;
        }
        if (!importedStatus.empty()) {
            conflict.previewStatusText += "\nImported: " + importedStatus;
        }
        std::cerr << "[LibraryManager] Preview generation failed for conflict at index " << index << "\n";
    } else {
        conflict.previewStatusText.clear();
    }
}

void LibraryManager::ResetConflictPreview(int index) {
    if (index < 0 || index >= static_cast<int>(m_PendingConflicts.size())) {
        return;
    }

    auto& conflict = m_PendingConflicts[static_cast<std::size_t>(index)];
    if (conflict.localPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        conflict.localPreviewTex = 0;
    }
    if (conflict.importedPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        conflict.importedPreviewTex = 0;
    }
    conflict.previewsReady = false;
    conflict.previewFailed = false;
    conflict.previewStatusText.clear();
}

void LibraryManager::ClearAssetConflicts() {
    for (auto& conflict : m_PendingAssetConflicts) {
        if (conflict.localPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        }
        if (conflict.importedPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        }
    }
    m_PendingAssetConflicts.clear();
}

void LibraryManager::PrepareAssetConflictPreview(int index) {
    if (index < 0 || index >= static_cast<int>(m_PendingAssetConflicts.size())) {
        return;
    }

    auto& conflict = m_PendingAssetConflicts[index];
    if (conflict.previewsReady) {
        return;
    }

    std::vector<unsigned char> localPixels;
    int localW = 0;
    int localH = 0;
    if (LoadRgbaImageFromFile(m_AssetsPath / conflict.localAssetFileName, localPixels, localW, localH)) {
        conflict.localPreviewTex = GLHelpers::CreateTextureFromPixels(localPixels.data(), localW, localH, 4);
    }

    std::vector<unsigned char> importedPixels;
    int importedW = 0;
    int importedH = 0;
    int importedC = 4;
    if (DecodeImageBytes(conflict.importedImageBytes, importedPixels, importedW, importedH, importedC)) {
        conflict.importedPreviewTex = GLHelpers::CreateTextureFromPixels(importedPixels.data(), importedW, importedH, 4);
    }

    conflict.previewsReady = true;
}

void LibraryManager::ResolveAssetConflict(int index, AssetConflictAction action) {
    if (index < 0 || index >= static_cast<int>(m_PendingAssetConflicts.size())) {
        return;
    }

    auto conflict = m_PendingAssetConflicts[static_cast<std::size_t>(index)];
    bool resolved = false;

    auto buildUniqueAssetFileName = [this](const std::string& preferredStem) {
        const std::string safeStem = SanitizeFileStem(preferredStem.empty() ? "imported_asset" : preferredStem);
        std::string candidate = safeStem + ".png";
        int suffix = 1;
        while (std::filesystem::exists(m_AssetsPath / candidate)) {
            candidate = safeStem + "_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(suffix++) + ".png";
        }
        return candidate;
    };

    if (action == AssetConflictAction::UseExisting) {
        resolved = true;
    } else if (action == AssetConflictAction::Replace) {
        resolved = WriteFileBytes(m_AssetsPath / conflict.localAssetFileName, conflict.importedImageBytes);
    } else if (action == AssetConflictAction::KeepBoth) {
        const std::string baseStem = std::filesystem::path(
            conflict.importedAssetFileName.empty() ? conflict.importedDisplayName : conflict.importedAssetFileName).stem().string();
        const std::string targetFileName = buildUniqueAssetFileName(baseStem);
        resolved = WriteFileBytes(m_AssetsPath / targetFileName, conflict.importedImageBytes);
    }

    if (!resolved) {
        return;
    }

    if (conflict.localPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
    }
    if (conflict.importedPreviewTex) {
        m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
    }

    m_PendingAssetConflicts.erase(m_PendingAssetConflicts.begin() + index);
    if (action != AssetConflictAction::UseExisting) {
        m_LastLibrarySignature = 0;
        RefreshLibrary();
    }
}

static std::string ComputeImageHash(const std::vector<unsigned char>& data) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char b : data) {
        hash ^= b;
        hash *= 1099511628211ULL;
    }
    char hex[32];
    sprintf_s(hex, "%016llx", hash);
    return std::string(hex);
}

static std::vector<unsigned char> ReadBinaryJsonHelper(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }
    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

static bool ExtractEmbeddedGraphSourcePng(const nlohmann::json& pipelineData, std::vector<unsigned char>& outPngBytes) {
    outPngBytes.clear();
    if (!pipelineData.is_object()) {
        return false;
    }

    const nlohmann::json graphJson = pipelineData.value("nodeGraph", nlohmann::json::object());
    if (!graphJson.is_object()) {
        return false;
    }

    const int activeImageNodeId = graphJson.value("activeImageNodeId", 0);
    const nlohmann::json nodes = graphJson.value("nodes", nlohmann::json::array());
    if (!nodes.is_array()) {
        return false;
    }

    auto readImageNodePng = [&](const nlohmann::json& item) {
        if (!item.is_object() ||
            item.value("kind", std::string()) != "Image" ||
            !item.contains("pngBytes")) {
            return false;
        }
        outPngBytes = ReadBinaryJsonHelper(item.at("pngBytes"));
        return !outPngBytes.empty();
    };

    if (activeImageNodeId > 0) {
        for (const nlohmann::json& item : nodes) {
            if (item.value("id", 0) == activeImageNodeId && readImageNodePng(item)) {
                return true;
            }
        }
    }

    for (const nlohmann::json& item : nodes) {
        if (readImageNodePng(item)) {
            return true;
        }
    }

    outPngBytes.clear();
    return false;
}

std::vector<std::string> LibraryManager::SyncProjectAssets(const std::string& projectFileName, const StackBinaryFormat::ProjectDocument& document) {
    std::vector<std::string> syncedFileNames;
    
    if (projectFileName.empty()) {
        return syncedFileNames;
    }

    std::filesystem::path projectPath(projectFileName);
    std::string projectStem = projectPath.stem().string();

    // 1. Sync the project's main source image (sourceImageBytes)
    if (!document.sourceImageBytes.empty()) {
        std::string hash = ComputeImageHash(document.sourceImageBytes);
        std::string assetFileName = projectStem + ".png";
        std::filesystem::path pngPath = m_AssetsPath / assetFileName;
        std::filesystem::path hashPath = m_AssetsPath / (assetFileName + ".hash");

        bool needWrite = true;
        if (std::filesystem::exists(pngPath) && std::filesystem::exists(hashPath)) {
            try {
                std::ifstream f(hashPath);
                if (f.is_open()) {
                    nlohmann::json meta;
                    f >> meta;
                    if (meta.value("hash", "") == hash && meta.value("projectFileName", "") == projectFileName) {
                        needWrite = false;
                    }
                }
            } catch (...) {}
        }

        if (needWrite) {
            WriteFileBytes(pngPath, document.sourceImageBytes);

            int w = 0, h = 0, c = 4;
            std::vector<unsigned char> pixels;
            DecodeImageBytes(document.sourceImageBytes, pixels, w, h, c);

            nlohmann::json meta = {
                {"hash", hash},
                {"projectFileName", projectFileName},
                {"displayName", document.metadata.projectName.empty() ? projectStem : document.metadata.projectName},
                {"timestamp", document.metadata.timestamp},
                {"width", w},
                {"height", h}
            };
            std::ofstream f(hashPath);
            if (f.is_open()) {
                f << meta.dump(4);
            }
        }

        syncedFileNames.push_back(assetFileName);
    }

    // 2. Sync all embedded Image nodes in the graph
    if (document.pipelineData.is_object()) {
        const nlohmann::json& graphJson = document.pipelineData.value("nodeGraph", nlohmann::json::object());
        const auto& nodes = graphJson.value("nodes", nlohmann::json::array());
        for (const auto& item : nodes) {
            if (item.value("kind", "") == "Image" && item.contains("pngBytes")) {
                std::vector<unsigned char> pngBytes = ReadBinaryJsonHelper(item.at("pngBytes"));
                if (!pngBytes.empty()) {
                    std::string hash = ComputeImageHash(pngBytes);
                    std::string label = item.value("label", item.value("title", "Image"));
                    std::string sanitizedLabel = SanitizeFileStem(label);
                    if (sanitizedLabel.empty()) {
                        sanitizedLabel = "image";
                    }
                    
                    std::string hashPrefix = hash.substr(0, 8);
                    std::string assetFileName = projectStem + "_" + sanitizedLabel + "_" + hashPrefix + ".png";
                    std::filesystem::path pngPath = m_AssetsPath / assetFileName;
                    std::filesystem::path hashPath = m_AssetsPath / (assetFileName + ".hash");

                    bool needWrite = true;
                    if (std::filesystem::exists(pngPath) && std::filesystem::exists(hashPath)) {
                        try {
                            std::ifstream f(hashPath);
                            if (f.is_open()) {
                                nlohmann::json meta;
                                f >> meta;
                                if (meta.value("hash", "") == hash && meta.value("projectFileName", "") == projectFileName) {
                                    needWrite = false;
                                }
                            }
                        } catch (...) {}
                    }

                    if (needWrite) {
                        WriteFileBytes(pngPath, pngBytes);

                        int w = 0, h = 0, c = 4;
                        std::vector<unsigned char> pixels;
                        DecodeImageBytes(pngBytes, pixels, w, h, c);

                        nlohmann::json meta = {
                            {"hash", hash},
                            {"projectFileName", projectFileName},
                            {"displayName", label},
                            {"timestamp", document.metadata.timestamp},
                            {"width", w},
                            {"height", h}
                        };
                        std::ofstream f(hashPath);
                        if (f.is_open()) {
                            f << meta.dump(4);
                        }
                    }

                    syncedFileNames.push_back(assetFileName);
                }
            }
        }
    }

    return syncedFileNames;
}

void LibraryManager::CleanupOrphanedAssets(const std::vector<std::string>& activeAssetFileNames) {
    if (!std::filesystem::exists(m_AssetsPath)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string filename = entry.path().filename().string();
        
        std::string checkName = filename;
        if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".hash") {
            checkName = filename.substr(0, filename.size() - 5);
        }

        if (std::find(activeAssetFileNames.begin(), activeAssetFileNames.end(), checkName) == activeAssetFileNames.end()) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

void LibraryManager::QueueLooseAssetSave(
    const std::string& displayName,
    const std::vector<unsigned char>& imageBytes,
    const std::string& preferredFileName,
    const std::string& projectFileName,
    const std::string& projectName,
    const std::string& projectKind) {

    if (imageBytes.empty()) {
        return;
    }

    const std::string trimmedDisplayName = TrimWhitespace(displayName).empty() ? "Imported Asset" : TrimWhitespace(displayName);
    const std::string fallbackFileName = SanitizeFileStem(trimmedDisplayName) + ".png";
    const std::string resolvedPreferredFileName = EnsureAssetFileName(preferredFileName, fallbackFileName);

    Async::TaskSystem::Get().Submit([this,
                                     trimmedDisplayName,
                                     imageBytes,
                                     resolvedPreferredFileName,
                                     projectFileName,
                                     projectName,
                                     projectKind]() mutable {
        std::vector<unsigned char> importedPixels;
        int importedW = 0;
        int importedH = 0;
        int importedC = 4;
        if (!DecodeImageBytes(imageBytes, importedPixels, importedW, importedH, importedC)) {
            return;
        }

        if (!std::filesystem::exists(m_AssetsPath)) {
            std::filesystem::create_directories(m_AssetsPath);
        }

        // Direct check for a perfect exact match first
        const std::filesystem::path preferredPath = m_AssetsPath / resolvedPreferredFileName;
        if (std::filesystem::exists(preferredPath)) {
            std::vector<unsigned char> existingPixels;
            int existingW = 0;
            int existingH = 0;
            if (LoadRgbaImageFromFile(preferredPath, existingPixels, existingW, existingH)) {
                if (existingW == importedW &&
                    existingH == importedH &&
                    existingPixels.size() == importedPixels.size() &&
                    ComputeExactPixelFingerprint(existingPixels) == ComputeExactPixelFingerprint(importedPixels) &&
                    existingPixels == importedPixels) {
                    // Silent success: asset already exists with identical pixels. No conflict or duplicate files needed.
                    return;
                }
            }
        }

        bool foundConflict = false;
        AssetImportConflict pendingConflict;
        int bestHashDistance = std::numeric_limits<int>::max();
        float bestNameScore = -1.0f;

        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath)) {
            if (!IsSupportedAssetExtension(entry.path())) {
                continue;
            }

            std::vector<unsigned char> localPixels;
            int localW = 0;
            int localH = 0;
            if (!LoadRgbaImageFromFile(entry.path(), localPixels, localW, localH)) {
                continue;
            }

            const std::string localDisplayName = DisplayNameFromStem(entry.path().stem().string());
            bool exactMatch = false;
            if (!ShouldQueueAssetConflict(
                    localDisplayName,
                    localPixels,
                    localW,
                    localH,
                    trimmedDisplayName,
                    importedPixels,
                    importedW,
                    importedH,
                    exactMatch)) {
                continue;
            }

            const int hashDistance = CountHammingDistance64(
                ComputeAverageHash64(localPixels, localW, localH),
                ComputeAverageHash64(importedPixels, importedW, importedH));
            const float nameScore = ComputeNameSimilarityScore(localDisplayName, trimmedDisplayName);

            if (!foundConflict ||
                exactMatch ||
                hashDistance < bestHashDistance ||
                (hashDistance == bestHashDistance && nameScore > bestNameScore)) {
                foundConflict = true;
                bestHashDistance = hashDistance;
                bestNameScore = nameScore;

                pendingConflict = AssetImportConflict {};
                pendingConflict.localAssetFileName = entry.path().filename().string();
                pendingConflict.localDisplayName = localDisplayName;
                pendingConflict.localWidth = localW;
                pendingConflict.localHeight = localH;
                pendingConflict.importedAssetFileName = resolvedPreferredFileName;
                pendingConflict.importedDisplayName = trimmedDisplayName;
                pendingConflict.importedWidth = importedW;
                pendingConflict.importedHeight = importedH;
                pendingConflict.importedProjectFileName = projectFileName;
                pendingConflict.importedProjectName = projectName;
                pendingConflict.importedProjectKind = projectKind;
                pendingConflict.importedImageBytes = imageBytes;
                pendingConflict.areIdentical = exactMatch;

                std::error_code ec;
                const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
                pendingConflict.localTimestamp = ec ? "Unknown" : BuildTimestampStringFromFileTime(writeTime);
                pendingConflict.importedTimestamp = BuildTimestampString();
            }
        }

        if (foundConflict) {
            Async::TaskSystem::Get().PostToMain([this, conflict = std::move(pendingConflict)]() mutable {
                const auto alreadyQueued = std::find_if(
                    m_PendingAssetConflicts.begin(),
                    m_PendingAssetConflicts.end(),
                    [&](const AssetImportConflict& existing) {
                        return existing.localAssetFileName == conflict.localAssetFileName &&
                               existing.importedDisplayName == conflict.importedDisplayName &&
                               existing.importedWidth == conflict.importedWidth &&
                               existing.importedHeight == conflict.importedHeight;
                    });
                if (alreadyQueued == m_PendingAssetConflicts.end()) {
                    m_PendingAssetConflicts.push_back(std::move(conflict));
                }
            });
            return;
        }

        std::string targetFileName = resolvedPreferredFileName;
        int suffix = 1;
        while (std::filesystem::exists(m_AssetsPath / targetFileName)) {
            targetFileName = SanitizeFileStem(std::filesystem::path(resolvedPreferredFileName).stem().string())
                + "_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(suffix++) + ".png";
        }

        if (!WriteFileBytes(m_AssetsPath / targetFileName, imageBytes)) {
            return;
        }

        Async::TaskSystem::Get().PostToMain([this]() {
            m_LastLibrarySignature = 0;
            RefreshLibrary();
        });
    });
}

void LibraryManager::MirrorCompositeEmbeddedAssets(const StackFormat::ProjectDocument& document) {
    if (document.metadata.projectKind != StackFormat::kCompositeProjectKind || !document.pipelineData.is_object()) {
        return;
    }

    const StackFormat::json layers = document.pipelineData.value("layers", StackFormat::json::array());
    for (const auto& item : layers) {
        if (!item.is_object()) {
            continue;
        }

        const bool generatedFromImage = item.value("generatedFromImage", false);
        const bool isImageLayer = item.value("kind", std::string("image")) == "image";
        if (!isImageLayer && !generatedFromImage) {
            continue;
        }

        std::vector<unsigned char> imageBytes;
        if (item.contains("sourcePng") && item["sourcePng"].is_binary()) {
            imageBytes = item["sourcePng"].get_binary();
        } else if (item.contains("imagePng") && item["imagePng"].is_binary()) {
            imageBytes = item["imagePng"].get_binary();
        }

        if (imageBytes.empty()) {
            continue;
        }

        const std::string layerName = item.value("name", std::string("Composite Asset"));
        QueueLooseAssetSave(layerName, imageBytes, SanitizeFileStem(layerName) + ".png");
    }
}

void LibraryManager::ProcessDeferredDeletions() {
    if (m_DeferredTextureDeletions.empty()) return;
    glDeleteTextures(static_cast<GLsizei>(m_DeferredTextureDeletions.size()), m_DeferredTextureDeletions.data());
    m_DeferredTextureDeletions.clear();
}

std::uintmax_t LibraryManager::BuildLibrarySignature() const {
    if (!std::filesystem::exists(m_LibraryPath) && !std::filesystem::exists(m_AssetsPath)) {
        return 0;
    }

    std::uintmax_t fileCount = 0;
    std::uintmax_t totalSize = 0;
    std::uintmax_t latestWrite = 0;
    std::uintmax_t nameHash = 0;

    const auto accumulateEntry = [&](const std::filesystem::directory_entry& entry) {
        std::error_code ec;
        ++fileCount;
        totalSize += std::filesystem::file_size(entry.path(), ec);
        if (ec) ec.clear();

        const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec) {
            const auto stamp = static_cast<std::uintmax_t>(writeTime.time_since_epoch().count());
            if (stamp > latestWrite) latestWrite = stamp;
        }

        nameHash ^= static_cast<std::uintmax_t>(std::hash<std::string>{}(entry.path().string()));
    };

    std::error_code ec;
    if (std::filesystem::exists(m_LibraryPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath, ec)) {
            if (ec) break;
            if (!IsSupportedProjectExtension(entry.path())) continue;
            accumulateEntry(entry);
        }
    }

    ec.clear();
    if (std::filesystem::exists(m_AssetsPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
            if (ec) break;
            if (!IsSupportedAssetExtension(entry.path())) continue;
            accumulateEntry(entry);
        }
    }

    return (fileCount * 1315423911ull) ^ (totalSize * 2654435761ull) ^ latestWrite ^ nameHash;
}

int LibraryManager::GetProjectCount() const {
    int count = 0;
    std::error_code ec;
    if (std::filesystem::exists(m_LibraryPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath, ec)) {
            if (!ec && IsSupportedProjectExtension(entry.path())) count++;
        }
    }
    if (std::filesystem::exists(m_AssetsPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
            if (!ec && IsSupportedAssetExtension(entry.path())) count++;
        }
    }
    return count;
}

bool LibraryManager::ConsumeSavedProjectEvent(std::string& outFileName, std::string& outProjectKind) {
    if (m_PendingSavedProjectFileName.empty()) {
        outFileName.clear();
        outProjectKind.clear();
        return false;
    }

    outFileName = m_PendingSavedProjectFileName;
    outProjectKind = m_PendingSavedProjectKind;
    m_PendingSavedProjectFileName.clear();
    m_PendingSavedProjectKind.clear();
    return true;
}

bool LibraryManager::ConsumeUiNotification(UiNotificationEvent& outEvent) {
    if (m_UiNotifications.empty()) {
        return false;
    }
    outEvent = std::move(m_UiNotifications.front());
    m_UiNotifications.pop_front();
    return true;
}

void LibraryManager::QueueSavedProjectEvent(const std::string& fileName, const std::string& projectKind) {
    m_PendingSavedProjectFileName = fileName;
    m_PendingSavedProjectKind = projectKind;
}

void LibraryManager::QueueUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    if (message.empty()) {
        return;
    }
    m_UiNotifications.push_back(UiNotificationEvent{
        severity,
        std::move(message),
        std::move(dedupeKey)
    });
}

void LibraryManager::RefreshLibrary(
    std::function<void(int current, int total, const std::string& name)> progressCallback,
    bool syncEmbeddedProjectAssets) {
    std::lock_guard<std::mutex> lock(m_ProjectsMutex);
    TraceStartupStep("[LibraryManager] RefreshLibrary begin");

    TagManager::Get().SetLibraryPath(m_LibraryPath);
    TagManager::Get().Load();

    for (auto& project : m_Projects) {
        ReleaseProjectTextures(project);
    }
    m_Projects.clear();

    for (auto& asset : m_Assets) {
        ReleaseAssetTextures(asset);
    }
    m_Assets.clear();

    if (!std::filesystem::exists(m_LibraryPath)) {
        std::filesystem::create_directories(m_LibraryPath);
    }
    if (!std::filesystem::exists(m_AssetsPath)) {
        std::filesystem::create_directories(m_AssetsPath);
    }

    int totalItems = GetProjectCount();
    int currentItem = 0;
    TraceStartupStep("[LibraryManager] Refresh counts: " + std::to_string(totalItems));

    std::vector<std::string> activeAssetFiles;

    for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath)) {
        if (!IsSupportedProjectExtension(entry.path())) continue;

        currentItem++;
        TraceStartupStep("[LibraryManager] Loading project: " + entry.path().filename().string());
        if (progressCallback) progressCallback(currentItem, totalItems, entry.path().filename().string());

        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = true;
        options.includeSourceImage = syncEmbeddedProjectAssets;
        options.includePipelineData = syncEmbeddedProjectAssets;
        options.verifyChecksum = syncEmbeddedProjectAssets;

        StackFormat::ProjectDocument document;
        if (!LoadProjectDocument(entry.path().filename().string(), document, options)) {
            std::cerr << "Failed to parse project: " << entry.path() << std::endl;
            continue;
        }

        auto project = std::make_shared<ProjectEntry>();
        project->fileName = entry.path().filename().string();
        project->projectName = document.metadata.projectName;
        project->timestamp = document.metadata.timestamp;
        project->projectKind = document.metadata.projectKind;
        project->thumbnailBytes = std::move(document.thumbnailBytes);
        project->sourceWidth = document.metadata.sourceWidth;
        project->sourceHeight = document.metadata.sourceHeight;

        m_Projects.push_back(project);

        if (syncEmbeddedProjectAssets) {
            // Full asset sync is intentionally opt-in; startup only needs metadata and thumbnails.
            std::vector<std::string> projectAssets = SyncProjectAssets(project->fileName, document);
            activeAssetFiles.insert(activeAssetFiles.end(), projectAssets.begin(), projectAssets.end());
        }
    }

    if (syncEmbeddedProjectAssets) {
        CleanupOrphanedAssets(activeAssetFiles);
    }

    // Populate m_Assets directly from the synced .hash files
    if (std::filesystem::exists(m_AssetsPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath)) {
            std::string filename = entry.path().filename().string();
            if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".hash") {
                try {
                    std::ifstream f(entry.path());
                    if (f.is_open()) {
                        nlohmann::json meta;
                        f >> meta;

                        std::string assetPngName = filename.substr(0, filename.size() - 5);
                        std::filesystem::path pngPath = m_AssetsPath / assetPngName;

                        if (std::filesystem::exists(pngPath)) {
                            auto asset = std::make_shared<AssetEntry>();
                            asset->fileName = assetPngName;
                            asset->displayName = meta.value("displayName", assetPngName);
                            asset->projectFileName = meta.value("projectFileName", "");
                            asset->timestamp = meta.value("timestamp", "Unknown");
                            asset->width = meta.value("width", 0);
                            asset->height = meta.value("height", 0);

                            const auto projectIt = std::find_if(
                                m_Projects.begin(),
                                m_Projects.end(),
                                [&](const std::shared_ptr<ProjectEntry>& p) {
                                    return p && !asset->projectFileName.empty() && p->fileName == asset->projectFileName;
                                });

                            if (projectIt != m_Projects.end() && *projectIt) {
                                asset->projectName = (*projectIt)->projectName;
                                asset->projectKind = (*projectIt)->projectKind;
                            }

                            m_Assets.push_back(asset);
                        }
                    }
                } catch (...) {}
            }
        }
    }

    m_LastLibrarySignature = BuildLibrarySignature();
    TraceStartupStep("[LibraryManager] RefreshLibrary complete");
}

void LibraryManager::UploadLibraryTextures(int projectBudget, int assetBudget) {
    if (projectBudget <= 0 && assetBudget <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_ProjectsMutex);

    if (projectBudget > 0) {
        int uploadedProjectCount = 0;
        for (auto& project : m_Projects) {
            if (project && project->thumbnailTex == 0 && !project->thumbnailBytes.empty()) {
                InitializeThumbnail(project);
                ++uploadedProjectCount;
                if (uploadedProjectCount >= projectBudget) {
                    break;
                }
            }
        }
    }

    if (assetBudget > 0) {
        int uploadedAssetCount = 0;
        for (auto& asset : m_Assets) {
            if (asset && asset->thumbnailTex == 0 && !asset->thumbnailLoadAttempted) {
                // Asset thumbnails may require decoding large source images, so keep
                // this budget tiny and never run it before the main window is usable.
                InitializeAssetThumbnail(asset);
                ++uploadedAssetCount;
                if (uploadedAssetCount >= assetBudget) {
                    break;
                }
            }
        }
    }
}

void LibraryManager::TickAutoRefresh() {
    ProcessDeferredDeletions();
    const std::uintmax_t latestSignature = BuildLibrarySignature();
    if (latestSignature != m_LastLibrarySignature) {
        RefreshLibrary();
    }
}

bool LibraryManager::LoadProjectDocument(
    const std::string& fileName,
    StackFormat::ProjectDocument& outDocument,
    const StackFormat::ProjectLoadOptions& options) {

    const std::filesystem::path path = m_LibraryPath / fileName;
    if (!std::filesystem::exists(path)) return false;

    if (StackFormat::ReadProjectFile(path, outDocument, options)) {
        return true;
    }

    return LoadLegacyProjectDocument(path, outDocument, options);
}

std::vector<unsigned char> LibraryManager::GenerateThumbnailBytes(const std::vector<unsigned char>& pixels, int width, int height) {
    int thumbW = 0;
    int thumbH = 0;
    std::vector<unsigned char> thumbPixels = ResizePixelsNearest(pixels, width, height, thumbW, thumbH);
    if (thumbPixels.empty()) return {};

    std::vector<unsigned char> pngData;
    stbi_write_png_to_func(png_write_func, &pngData, thumbW, thumbH, 4, thumbPixels.data(), thumbW * 4);
    return pngData;
}

bool LibraryManager::OverwriteEditorProject(
    const std::string& fileName,
    const std::string& projectName,
    const std::vector<unsigned char>& sourcePngBytes,
    const StackFormat::json& pipelineData,
    const std::vector<unsigned char>& renderedPixels,
    const int renderedW,
    const int renderedH) {

    if (fileName.empty() ||
        sourcePngBytes.empty() ||
        renderedPixels.empty() ||
        renderedW <= 0 ||
        renderedH <= 0) {
        return false;
    }

    const std::string trimmedName = TrimWhitespace(projectName).empty() ? "Untitled Project" : TrimWhitespace(projectName);
    const std::string resolvedFileName = EnsureProjectFileName(fileName, SanitizeFileStem(trimmedName) + ".stack");
    const std::filesystem::path projectPath = m_LibraryPath / resolvedFileName;
    const std::filesystem::path assetPath = BuildAssetPathForProjectFile(resolvedFileName);

    int sourceW = 0;
    int sourceH = 0;
    int sourceChannels = 0;
    std::vector<unsigned char> decodedSourcePixels;
    if (!DecodeImageBytes(sourcePngBytes, decodedSourcePixels, sourceW, sourceH, sourceChannels)) {
        return false;
    }

    StackFormat::ProjectDocument document;
    document.metadata.projectKind = StackFormat::kEditorProjectKind;
    document.metadata.projectName = trimmedName;
    document.metadata.timestamp = BuildTimestampString();
    document.metadata.sourceWidth = sourceW;
    document.metadata.sourceHeight = sourceH;
    document.thumbnailBytes = GenerateThumbnailBytes(renderedPixels, renderedW, renderedH);
    document.sourceImageBytes = sourcePngBytes;
    document.pipelineData = pipelineData;

    std::vector<unsigned char> renderedPngBytes;
    stbi_write_png_to_func(
        png_write_func,
        &renderedPngBytes,
        renderedW,
        renderedH,
        4,
        renderedPixels.data(),
        renderedW * 4);
    if (renderedPngBytes.empty()) {
        return false;
    }

    try {
        if (!StackFormat::WriteProjectFile(projectPath, document)) {
            return false;
        }
        if (!WriteFileBytes(assetPath, renderedPngBytes)) {
            return false;
        }
    } catch (...) {
        return false;
    }

    m_LastLibrarySignature = 0;
    return true;
}

void LibraryManager::RequestSaveProject(const std::string& name, EditorModule* editor, const std::string& existingFileName, std::function<void(bool)> onComplete) {
    if (!editor || Async::IsBusy(m_SaveTaskState)) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to save the project to the library.", "library-save-project");
        if (onComplete) onComplete(false);
        return;
    }

    m_SaveTaskState = Async::TaskState::Applying;
    m_SaveStatusText = "Capturing the project snapshot for the library...";

    const std::string trimmedName = TrimWhitespace(name).empty() ? "Untitled Project" : TrimWhitespace(name);
    const StackFormat::json pipeline = editor->SerializePipeline();

    int renderedW = 0;
    int renderedH = 0;
    std::vector<unsigned char> renderedPixels;
    int sourceW = 0;
    int sourceH = 0;
    std::vector<unsigned char> sourcePixels;
    std::vector<unsigned char> sourcePngBytesOverride;

    const bool compositeProject = editor->IsCompositeViewportMode();
    if (compositeProject) {
        editor->BuildCompositeExportRaster(renderedPixels, renderedW, renderedH);
        if (!renderedPixels.empty() && renderedW > 0 && renderedH > 0) {
            sourceW = renderedW;
            sourceH = renderedH;
            sourcePixels = std::vector<unsigned char>(static_cast<size_t>(sourceW) * static_cast<size_t>(sourceH) * 4ull, 0u);
        }
    } else {
        if (editor->IsRenderOnlyUpToActive()) {
            editor->GetPipeline().Execute(editor->GetLayers());
        }
        renderedPixels = editor->GetPipeline().GetOutputPixels(renderedW, renderedH);
        if ((renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) && editor->GetNodeGraph().IsOutputConnected()) {
            editor->BuildSingleOutputExportRaster(renderedPixels, renderedW, renderedH);
        }
        if (!renderedPixels.empty() && renderedW > 0 && renderedH > 0) {
            sourcePixels = editor->GetPipeline().GetSourcePixels(sourceW, sourceH);
            std::vector<unsigned char> graphSourcePngBytes;
            if (ExtractEmbeddedGraphSourcePng(pipeline, graphSourcePngBytes)) {
                std::vector<unsigned char> decodedGraphSourcePixels;
                int graphSourceW = 0;
                int graphSourceH = 0;
                int graphSourceChannels = 0;
                if (DecodeImageBytes(graphSourcePngBytes, decodedGraphSourcePixels, graphSourceW, graphSourceH, graphSourceChannels) &&
                    !decodedGraphSourcePixels.empty() &&
                    graphSourceW > 0 &&
                    graphSourceH > 0) {
                    sourcePngBytesOverride = std::move(graphSourcePngBytes);
                    sourcePixels = std::move(decodedGraphSourcePixels);
                    sourceW = graphSourceW;
                    sourceH = graphSourceH;
                }
            }
            if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
                sourceW = renderedW;
                sourceH = renderedH;
                sourcePixels.assign(static_cast<size_t>(sourceW) * static_cast<size_t>(sourceH) * 4ull, 0u);
            }
        }
    }

    if (renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) {
        m_SaveTaskState = Async::TaskState::Failed;
        m_SaveStatusText = "Failed to capture the rendered result for saving.";
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the rendered result for saving.", "library-save-project");
        return;
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        m_SaveTaskState = Async::TaskState::Failed;
        m_SaveStatusText = "Failed to capture the source image for saving.";
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the source image for saving.", "library-save-project");
        return;
    }

    std::string fileName = existingFileName;
    if (fileName.empty() && editor && !editor->GetCurrentProjectFileName().empty()) {
        fileName = editor->GetCurrentProjectFileName();
    }
    if (fileName.empty()) {
        const std::string safeStem = SanitizeFileStem(trimmedName);
        fileName = safeStem + "_" + std::to_string(std::time(nullptr)) + ".stack";
    }
    const std::string assetFileName = BuildAssetPathForProjectFile(fileName).filename().string();

    editor->SetCurrentProjectName(trimmedName);
    editor->SetCurrentProjectFileName(fileName);
    editor->ClearDirty();

    ++m_SaveGeneration;
    const std::uint64_t generation = m_SaveGeneration;
    m_SaveTaskState = Async::TaskState::Running;
    m_SaveStatusText = "Packaging and writing project files in the background...";

    Async::TaskSystem::Get().Submit([this,
                                     generation,
                                     trimmedName,
                                     fileName,
                                     assetFileName,
                                     pipeline = std::move(pipeline),
                                     renderedW,
                                     renderedH,
                                     renderedPixels = std::move(renderedPixels),
                                     sourceW,
                                     sourceH,
                                     sourcePixels = std::move(sourcePixels),
                                     sourcePngBytesOverride = std::move(sourcePngBytesOverride),
                                     editor,
                                     onComplete = std::move(onComplete)]() mutable {
        bool wroteProject = false;
        bool wroteAsset = false;

        try {
            std::vector<unsigned char> sourcePngBytes = std::move(sourcePngBytesOverride);
            if (sourcePngBytes.empty()) {
                stbi_write_png_to_func(png_write_func, &sourcePngBytes, sourceW, sourceH, 4, sourcePixels.data(), sourceW * 4);
            }

            StackFormat::ProjectDocument document;
            document.metadata.projectKind = StackFormat::kEditorProjectKind;
            document.metadata.projectName = trimmedName;
            document.metadata.timestamp = BuildTimestampString();
            document.metadata.sourceWidth = sourceW;
            document.metadata.sourceHeight = sourceH;
            document.thumbnailBytes = GenerateThumbnailBytes(renderedPixels, renderedW, renderedH);
            document.sourceImageBytes = std::move(sourcePngBytes);
            document.pipelineData = pipeline;

            wroteProject = StackFormat::WriteProjectFile(m_LibraryPath / fileName, document);
            if (wroteProject) {
                wroteAsset = stbi_write_png(
                    (m_AssetsPath / assetFileName).string().c_str(),
                    renderedW,
                    renderedH,
                    4,
                    renderedPixels.data(),
                    renderedW * 4) != 0;
                
                if (wroteAsset) {
                    std::vector<unsigned char> renderedPngBytes;
                    stbi_write_png_to_func(png_write_func, &renderedPngBytes, renderedW, renderedH, 4, renderedPixels.data(), renderedW * 4);
                    std::string hash = ComputeImageHash(renderedPngBytes);
                    nlohmann::json meta = {
                        {"hash", hash},
                        {"projectFileName", fileName},
                        {"displayName", trimmedName},
                        {"timestamp", document.metadata.timestamp},
                        {"width", renderedW},
                        {"height", renderedH}
                    };
                    std::ofstream f(m_AssetsPath / (assetFileName + ".hash"));
                    if (f.is_open()) {
                        f << meta.dump(4);
                    }
                }

                // Sync all other embedded assets inside the project
                SyncProjectAssets(fileName, document);
            }
        } catch (...) {
            wroteProject = false;
            wroteAsset = false;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, wroteProject, wroteAsset, editor, onComplete = std::move(onComplete)]() {
            if (generation != m_SaveGeneration) {
                return;
            }

            if (wroteProject && wroteAsset) {
                m_SaveTaskState = Async::TaskState::Idle;
                m_SaveStatusText = "Project saved to the library.";
                QueueUiNotification(UiNotificationSeverity::Success, "Project saved to the library.", "library-save-project");
                m_LastLibrarySignature = 0;
                if (editor) {
                    editor->ClearDirty();
                }
                if (onComplete) onComplete(true);
            } else {
                m_SaveTaskState = Async::TaskState::Failed;
                m_SaveStatusText = "Failed to save the project to the library.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to save the project to the library.", "library-save-project");
                if (editor) {
                    editor->MarkDirty();
                }
                if (onComplete) onComplete(false);
            }
        });
    });
}

void LibraryManager::RequestLoadProject(const std::string& fileName, EditorModule* editor, std::function<void(bool)> onComplete) {
    if (fileName.empty() || !editor) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading the project in the background...";

    Async::TaskSystem::Get().Submit([this, generation, fileName, editor, onComplete = std::move(onComplete)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        EditorModule::LoadedProjectData loadedProject;
        bool success = LoadProjectDocument(fileName, document, options);
        if (success) {
            if (document.metadata.projectKind == StackFormat::kRenderProjectKind
                || document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
                success = false;
            }
        }
        if (success) {
            int width = 0;
            int height = 0;
            int channels = 0;
            success = DecodeImageBytes(document.sourceImageBytes, loadedProject.sourcePixels, width, height, channels);
            if (success) {
                loadedProject.width = width;
                loadedProject.height = height;
                loadedProject.channels = channels;
                loadedProject.pipelineData = document.pipelineData.is_null() ? StackFormat::json::array() : document.pipelineData;
                loadedProject.projectName = document.metadata.projectName;
                loadedProject.projectFileName = fileName;
            }
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             editor,
                                             onComplete = std::move(onComplete),
                                             loadedProject = std::move(loadedProject),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load the selected project.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
                if (onComplete) onComplete(false);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Applying project data to the editor...";

            const bool applied = editor->ApplyLoadedProject(loadedProject);
            if (applied) {
                m_ProjectLoadTaskState = Async::TaskState::Idle;
                m_ProjectLoadStatusText = "Project loaded into the editor.";
                QueueUiNotification(UiNotificationSeverity::Success, "Project loaded into the editor.", "library-load-project");
            } else {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to apply the loaded project.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply the loaded project.", "library-load-project");
            }

            if (onComplete) onComplete(applied);
        });
    });
}


void LibraryManager::RequestLoadProjectDeferredApply(
    const std::string& fileName,
    EditorModule* editor,
    std::function<void(bool, std::function<bool()>)> onReady) {
    if (fileName.empty() || !editor) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
        if (onReady) onReady(false, {});
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading the project in the background...";

    Async::TaskSystem::Get().Submit([this, generation, fileName, editor, onReady = std::move(onReady)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        auto loadedProject = std::make_shared<EditorModule::LoadedProjectData>();
        bool success = LoadProjectDocument(fileName, document, options);
        if (success) {
            if (document.metadata.projectKind == StackFormat::kRenderProjectKind
                || document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
                success = false;
            }
        }
        if (success) {
            int width = 0;
            int height = 0;
            int channels = 0;
            success = DecodeImageBytes(document.sourceImageBytes, loadedProject->sourcePixels, width, height, channels);
            if (success) {
                loadedProject->width = width;
                loadedProject->height = height;
                loadedProject->channels = channels;
                loadedProject->pipelineData = document.pipelineData.is_null() ? StackFormat::json::array() : document.pipelineData;
                loadedProject->projectName = document.metadata.projectName;
                loadedProject->projectFileName = fileName;
            }
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             editor,
                                             onReady = std::move(onReady),
                                             loadedProject,
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load the selected project.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected project.", "library-load-project");
                if (onReady) onReady(false, {});
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Project ready to apply...";

            std::function<bool()> applyDecodedProject = [this, generation, editor, loadedProject]() mutable {
                if (generation != m_ProjectLoadGeneration || !editor || !loadedProject) {
                    return false;
                }

                m_ProjectLoadTaskState = Async::TaskState::Applying;
                m_ProjectLoadStatusText = "Applying project data to the editor...";

                const bool applied = editor->ApplyLoadedProject(*loadedProject);
                if (applied) {
                    m_ProjectLoadTaskState = Async::TaskState::Idle;
                    m_ProjectLoadStatusText = "Project loaded into the editor.";
                    QueueUiNotification(UiNotificationSeverity::Success, "Project loaded into the editor.", "library-load-project");
                } else {
                    m_ProjectLoadTaskState = Async::TaskState::Failed;
                    m_ProjectLoadStatusText = "Failed to apply the loaded project.";
                    QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply the loaded project.", "library-load-project");
                }
                return applied;
            };

            if (onReady) {
                onReady(true, std::move(applyDecodedProject));
            }
        });
    });
}



void LibraryManager::RequestSaveCompositeProject(
    const std::string& name,
    CompositeModule* composite,
    const std::string& existingFileName,
    std::function<void(bool)> onComplete) {
    if (!composite || Async::IsBusy(m_SaveTaskState) || !composite->HasLayers()) {
        if (!composite || !composite->HasLayers()) {
            m_SaveTaskState = Async::TaskState::Failed;
            m_SaveStatusText = "Add at least one layer before saving a composite project.";
            QueueUiNotification(UiNotificationSeverity::Error, "Add at least one layer before saving a composite project.", "library-save-composite");
        }
        if (onComplete) onComplete(false);
        return;
    }

    m_SaveTaskState = Async::TaskState::Applying;
    m_SaveStatusText = "Building composite project for the library...";

    const std::string trimmedName = TrimWhitespace(name).empty() ? "Untitled Composite" : TrimWhitespace(name);
    StackFormat::ProjectDocument document;
    if (!composite->BuildProjectDocumentForSave(trimmedName, document)) {
        m_SaveTaskState = Async::TaskState::Failed;
        m_SaveStatusText = "Could not rasterize the composite for saving.";
        QueueUiNotification(UiNotificationSeverity::Error, "Could not rasterize the composite for saving.", "library-save-composite");
        return;
    }

    document.metadata.timestamp = BuildTimestampString();

    std::string fileName = existingFileName;
    if (fileName.empty() && composite && !composite->GetCurrentProjectFileName().empty()) {
        fileName = composite->GetCurrentProjectFileName();
    }
    const std::string safeStem = SanitizeFileStem(trimmedName);
    const std::string fallbackStem = safeStem + "_" + std::to_string(std::time(nullptr));
    fileName = EnsureProjectFileNameForKind(
        fileName,
        fallbackStem,
        StackFormat::kCompositeProjectKind);
    const std::string legacyProjectFileToDelete =
        (!existingFileName.empty() &&
         existingFileName != fileName &&
         std::filesystem::path(existingFileName).extension() == ".stack")
            ? existingFileName
            : std::string();

    ++m_SaveGeneration;
    const std::uint64_t generation = m_SaveGeneration;
    m_SaveTaskState = Async::TaskState::Running;
    m_SaveStatusText = "Writing composite project files in the background...";

    Async::TaskSystem::Get().Submit([this,
                                     generation,
                                     fileName,
                                     legacyProjectFileToDelete,
                                     trimmedName,
                                     document = std::move(document),
                                     composite,
                                     onComplete = std::move(onComplete)]() mutable {
        bool wroteProject = false;

        try {
            wroteProject = StackFormat::WriteProjectFile(m_LibraryPath / fileName, document);
            if (wroteProject && !legacyProjectFileToDelete.empty()) {
                std::error_code ec;
                std::filesystem::remove(m_LibraryPath / legacyProjectFileToDelete, ec);
            }
        } catch (...) {
            wroteProject = false;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, wroteProject, fileName, trimmedName, composite, onComplete = std::move(onComplete)]() {
            if (generation != m_SaveGeneration) {
                return;
            }

            if (wroteProject) {
                m_SaveTaskState = Async::TaskState::Idle;
                m_SaveStatusText = "Composite project saved to the library.";
                QueueUiNotification(UiNotificationSeverity::Success, "Composite project saved to the library.", "library-save-composite");
                m_LastLibrarySignature = 0;
                RefreshLibrary();
                QueueSavedProjectEvent(fileName, StackFormat::kCompositeProjectKind);
                if (composite) {
                    composite->SetCurrentProjectName(trimmedName);
                    composite->SetCurrentProjectFileName(fileName);
                    composite->ClearDirty();
                }
                if (onComplete) onComplete(true);
            } else {
                m_SaveTaskState = Async::TaskState::Failed;
                m_SaveStatusText = "Failed to save the composite project to the library.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to save the composite project to the library.", "library-save-composite");
                if (onComplete) onComplete(false);
            }
        });
    });
}

void LibraryManager::RequestLoadCompositeProject(
    const std::string& fileName,
    CompositeModule* composite,
    std::function<void(bool)> onComplete) {
    if (fileName.empty() || !composite) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the composite project.", "library-load-composite");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading composite project...";

    Async::TaskSystem::Get().Submit([this, generation, fileName, composite, onComplete = std::move(onComplete)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        bool success = LoadProjectDocument(fileName, document, options);
        if (success) {
            success = document.metadata.projectKind == StackFormat::kCompositeProjectKind;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             fileName,
                                             composite,
                                             onComplete = std::move(onComplete),
                                             document = std::move(document),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load the composite project.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the composite project.", "library-load-composite");
                if (onComplete) onComplete(false);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Applying composite project...";

            const bool applied = composite->ApplyLibraryProject(document);
            if (applied) {
                composite->SetCurrentProjectFileName(fileName);
                composite->SetCurrentProjectName(document.metadata.projectName);
                m_ProjectLoadTaskState = Async::TaskState::Idle;
                m_ProjectLoadStatusText = "Composite project loaded.";
                QueueUiNotification(UiNotificationSeverity::Success, "Composite project loaded.", "library-load-composite");
            } else {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to apply composite project data.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply composite project data.", "library-load-composite");
            }

            if (onComplete) onComplete(applied);
        });
    });
}

void LibraryManager::RequestLoadCompositeProjectFromPath(
    const std::filesystem::path& absolutePath,
    CompositeModule* composite,
    std::function<void(bool)> onComplete) {
    if (absolutePath.empty() || !composite) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load composite project from disk.", "library-load-composite-path");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading composite project from disk...";

    Async::TaskSystem::Get().Submit([this, generation, absolutePath, composite, onComplete = std::move(onComplete)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        bool success = false;
        if (std::filesystem::exists(absolutePath)) {
            if (StackFormat::ReadProjectFile(absolutePath, document, options)) {
                success = true;
            }
        }

        if (success) {
            success = document.metadata.projectKind == StackFormat::kCompositeProjectKind;
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             composite,
                                             onComplete = std::move(onComplete),
                                             document = std::move(document),
                                             fileName = absolutePath.filename().string(),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load composite project from disk.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load composite project from disk.", "library-load-composite-path");
                if (onComplete) onComplete(false);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Applying composite project...";

            const bool applied = composite->ApplyLibraryProject(document);
            if (applied) {
                composite->SetCurrentProjectFileName(fileName);
                composite->SetCurrentProjectName(document.metadata.projectName);
                m_ProjectLoadTaskState = Async::TaskState::Idle;
                m_ProjectLoadStatusText = "Composite project loaded.";
                QueueUiNotification(UiNotificationSeverity::Success, "Composite project loaded.", "library-load-composite-path");
            } else {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to apply composite project data.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply composite project data.", "library-load-composite-path");
            }

            if (onComplete) onComplete(applied);
        });
    });
}

void LibraryManager::InitializeThumbnail(std::shared_ptr<ProjectEntry> project) {
    if (!project || project->thumbnailBytes.empty()) return;

    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
    if (DecodeImageBytes(project->thumbnailBytes, pixels, width, height, channels)) {
        project->thumbnailTex = GLHelpers::CreateTextureFromPixels(pixels.data(), width, height, 4);
    }
}

void LibraryManager::InitializeAssetThumbnail(std::shared_ptr<AssetEntry> asset) {
    if (!asset) return;
    if (asset->thumbnailLoadAttempted) return;
    asset->thumbnailLoadAttempted = true;

    const std::filesystem::path assetPath = m_AssetsPath / asset->fileName;
    std::error_code sizeError;
    const std::uintmax_t fileBytes = std::filesystem::file_size(assetPath, sizeError);
    if (!sizeError && fileBytes > kMaxSynchronousAssetThumbnailBytes) {
        return;
    }

    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!LoadRgbaImageFromFile(assetPath, pixels, width, height)) {
        return;
    }

    asset->width = width;
    asset->height = height;

    int thumbW = 0;
    int thumbH = 0;
    std::vector<unsigned char> thumbPixels = ResizePixelsNearest(pixels, width, height, thumbW, thumbH);
    if (!thumbPixels.empty()) {
        asset->thumbnailTex = GLHelpers::CreateTextureFromPixels(thumbPixels.data(), thumbW, thumbH, 4);
    }
}

void LibraryManager::RequestProjectPreview(const std::shared_ptr<ProjectEntry>& project) {
    if (!project) return;

    if (project->sourcePreviewTex != 0 && project->fullPreviewTex != 0 && !project->pipelineData.is_null()) {
        project->previewTaskState = Async::TaskState::Idle;
        project->previewStatusText = "Preview ready.";
        return;
    }

    ++m_ProjectPreviewGeneration;
    const std::uint64_t generation = m_ProjectPreviewGeneration;
    project->previewRequestGeneration = generation;
    project->previewTaskState = Async::TaskState::Queued;
    project->previewStatusText = "Preparing project preview...";

    Async::TaskSystem::Get().Submit([this, generation, project]() {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        DecodedProjectPreview preview;
        preview.success = LoadProjectDocument(project->fileName, document, options);
        if (preview.success) {
            preview.projectKind = document.metadata.projectKind.empty()
                ? StackFormat::kEditorProjectKind
                : document.metadata.projectKind;
            preview.renderProject = document.metadata.projectKind == StackFormat::kRenderProjectKind;
            std::vector<unsigned char> sourcePngBytes = document.sourceImageBytes;
            std::vector<unsigned char> graphSourcePngBytes;
            if (preview.projectKind == StackFormat::kEditorProjectKind &&
                ExtractEmbeddedGraphSourcePng(document.pipelineData, graphSourcePngBytes)) {
                sourcePngBytes = std::move(graphSourcePngBytes);
            }
            preview.success = DecodeImageBytes(
                sourcePngBytes,
                preview.sourcePixels,
                preview.width,
                preview.height,
                preview.channels);
            if (preview.success) {
                const bool isComposite = preview.projectKind == StackFormat::kCompositeProjectKind;
                preview.pipelineData = document.pipelineData.is_null()
                    ? ((preview.renderProject || isComposite) ? StackFormat::json::object() : StackFormat::json::array())
                    : document.pipelineData;
                if (!isComposite) {
                    preview.fallbackAssetSuccess = LoadRgbaImageFromFile(
                        BuildAssetPathForProjectFile(project->fileName),
                        preview.fallbackAssetPixels,
                        preview.fallbackAssetWidth,
                        preview.fallbackAssetHeight);
                }
            }
        }

        Async::TaskSystem::Get().PostToMain([this, generation, project, preview = std::move(preview)]() mutable {
            if (!project ||
                generation != m_ProjectPreviewGeneration ||
                project->previewRequestGeneration != generation) {
                return;
            }

            if (!preview.success || preview.sourcePixels.empty()) {
                project->previewTaskState = Async::TaskState::Failed;
                project->previewStatusText = "Preview render failed.";
                return;
            }

            project->previewTaskState = Async::TaskState::Applying;
            project->previewStatusText = preview.fallbackAssetSuccess
                ? "Loading the saved rendered preview..."
                : (preview.renderProject
                        ? "Loading saved beauty preview..."
                        : (preview.projectKind == StackFormat::kCompositeProjectKind
                                ? "Loading saved composite export preview..."
                                : "Rendering full-quality preview..."));
            project->pipelineData = preview.pipelineData;
            project->projectKind = preview.projectKind;
            project->sourceWidth = preview.width;
            project->sourceHeight = preview.height;

            int renderedW = 0;
            int renderedH = 0;
            std::vector<unsigned char> renderedPixels;
            std::vector<unsigned char> comparePixels;
            int compareW = 0;
            int compareH = 0;
            bool livePreviewLooksBlank = false;

            const bool shouldBuildEditorPreview =
                !preview.renderProject &&
                preview.projectKind != StackFormat::kCompositeProjectKind;
            if (shouldBuildEditorPreview) {
                EditorModule previewEditor;
                previewEditor.Initialize();
                previewEditor.LoadSourceFromPixels(
                    preview.sourcePixels.data(),
                    preview.width,
                    preview.height,
                    preview.channels);
                previewEditor.DeserializePipeline(project->pipelineData);

                bool hasRawDevelop = false;
                for (const auto& node : previewEditor.GetNodeGraph().GetNodes()) {
                    if (node.kind == EditorNodeGraph::NodeKind::RawDevelop) {
                        hasRawDevelop = true;
                        break;
                    }
                }

                if (hasRawDevelop) {
                    previewEditor.GetPipeline().ExecuteGraph(previewEditor.BuildGraphSnapshot());
                    renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
                    comparePixels = previewEditor.GetPipeline().GetCompareSourcePixels(compareW, compareH);
                } else if (!preview.fallbackAssetSuccess || preview.fallbackAssetPixels.empty()) {
                    previewEditor.GetPipeline().Execute(previewEditor.GetLayers());
                    renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
                }

                livePreviewLooksBlank =
                    !renderedPixels.empty() &&
                    !HasMeaningfulPixels(renderedPixels) &&
                    HasMeaningfulPixels(preview.sourcePixels);
            }

            unsigned int newSourcePreviewTex = 0;
            if (!comparePixels.empty() && compareW > 0 && compareH > 0) {
                FlipImageRowsInPlace(comparePixels, compareW, compareH, 4);
                newSourcePreviewTex = GLHelpers::CreateTextureFromPixels(
                    comparePixels.data(),
                    compareW,
                    compareH,
                    4);
            } else {
                newSourcePreviewTex = GLHelpers::CreateTextureFromPixels(
                    preview.sourcePixels.data(),
                    preview.width,
                    preview.height,
                    preview.channels);
            }

            if (newSourcePreviewTex == 0) {
                project->previewTaskState = Async::TaskState::Failed;
                project->previewStatusText = "Preview render failed.";
                return;
            }

            std::vector<unsigned char> finalPreviewPixels;
            int finalPreviewWidth = 0;
            int finalPreviewHeight = 0;
            bool usedSavedAsset = false;

            if (preview.fallbackAssetSuccess && !preview.fallbackAssetPixels.empty()) {
                finalPreviewWidth = preview.fallbackAssetWidth;
                finalPreviewHeight = preview.fallbackAssetHeight;
                finalPreviewPixels = std::move(preview.fallbackAssetPixels);
                usedSavedAsset = true;
            } else if (preview.renderProject || preview.projectKind == StackFormat::kCompositeProjectKind) {
                finalPreviewWidth = preview.width;
                finalPreviewHeight = preview.height;
                finalPreviewPixels = preview.sourcePixels;
            } else if (!renderedPixels.empty() && !livePreviewLooksBlank) {
                finalPreviewWidth = renderedW;
                finalPreviewHeight = renderedH;
                finalPreviewPixels = std::move(renderedPixels);
                FlipImageRowsInPlace(finalPreviewPixels, finalPreviewWidth, finalPreviewHeight, 4);
            }

            if (finalPreviewPixels.empty() || finalPreviewWidth <= 0 || finalPreviewHeight <= 0) {
                glDeleteTextures(1, &newSourcePreviewTex);
                project->previewTaskState = Async::TaskState::Failed;
                project->previewStatusText = "Preview render failed.";
                return;
            }

            unsigned int newFullPreviewTex = GLHelpers::CreateTextureFromPixels(
                finalPreviewPixels.data(),
                finalPreviewWidth,
                finalPreviewHeight,
                4);
            if (newFullPreviewTex == 0) {
                glDeleteTextures(1, &newSourcePreviewTex);
                project->previewTaskState = Async::TaskState::Failed;
                project->previewStatusText = "Preview render failed.";
                return;
            }

            if (project->sourcePreviewTex) {
                glDeleteTextures(1, &project->sourcePreviewTex);
            }
            if (project->fullPreviewTex) {
                glDeleteTextures(1, &project->fullPreviewTex);
            }

            project->sourcePreviewTex = newSourcePreviewTex;
            project->fullPreviewTex = newFullPreviewTex;
            project->previewTaskState = Async::TaskState::Idle;
            project->previewStatusText = usedSavedAsset
                ? "Preview ready (saved render)."
                : "Preview ready.";
        });
    });
}

void LibraryManager::RequestAssetPreview(const std::shared_ptr<AssetEntry>& asset) {
    if (!asset) return;

    if (asset->fullPreviewTex != 0) {
        asset->previewTaskState = Async::TaskState::Idle;
        asset->previewStatusText = "Preview ready.";
        return;
    }

    ++m_AssetPreviewGeneration;
    const std::uint64_t generation = m_AssetPreviewGeneration;
    asset->previewRequestGeneration = generation;
    asset->previewTaskState = Async::TaskState::Queued;
    asset->previewStatusText = "Loading asset preview...";

    Async::TaskSystem::Get().Submit([this, generation, asset]() {
        DecodedAssetPreview preview;
        preview.success = LoadRgbaImageFromFile(m_AssetsPath / asset->fileName, preview.pixels, preview.width, preview.height);

        Async::TaskSystem::Get().PostToMain([this, generation, asset, preview = std::move(preview)]() mutable {
            if (!asset ||
                generation != m_AssetPreviewGeneration ||
                asset->previewRequestGeneration != generation) {
                return;
            }

            if (!preview.success || preview.pixels.empty()) {
                asset->previewTaskState = Async::TaskState::Failed;
                asset->previewStatusText = "Asset preview failed.";
                return;
            }

            asset->previewTaskState = Async::TaskState::Applying;
            asset->previewStatusText = "Uploading asset preview...";
            asset->width = preview.width;
            asset->height = preview.height;

            if (asset->fullPreviewTex) {
                glDeleteTextures(1, &asset->fullPreviewTex);
                asset->fullPreviewTex = 0;
            }

            asset->fullPreviewTex = GLHelpers::CreateTextureFromPixels(preview.pixels.data(), preview.width, preview.height, 4);
            asset->previewTaskState = Async::TaskState::Idle;
            asset->previewStatusText = "Preview ready.";
        });
    });
}

void LibraryManager::CancelProjectPreviewRequests() {
    ++m_ProjectPreviewGeneration;
}

void LibraryManager::CancelAssetPreviewRequests() {
    ++m_AssetPreviewGeneration;
}

bool LibraryManager::RenameProject(const std::string& fileName, const std::string& newName) {
    const std::string trimmedName = TrimWhitespace(newName);
    if (trimmedName.empty()) return false;

    StackFormat::ProjectDocument document;
    if (!LoadProjectDocument(fileName, document, {})) return false;

    document.metadata.projectName = trimmedName;
    document.metadata.timestamp = BuildTimestampString();

    if (!StackFormat::WriteProjectFile(m_LibraryPath / fileName, document)) {
        return false;
    }

    for (auto& project : m_Projects) {
        if (project && project->fileName == fileName) {
            project->projectName = trimmedName;
            project->timestamp = document.metadata.timestamp;
            break;
        }
    }

    for (auto& asset : m_Assets) {
        if (asset && asset->projectFileName == fileName) {
            asset->displayName = trimmedName;
            asset->projectName = trimmedName;
        }
    }

    m_LastLibrarySignature = 0;
    return true;
}

bool LibraryManager::DeleteProject(const std::string& fileName) {
    try {
        const std::filesystem::path projectPath = m_LibraryPath / fileName;
        if (!std::filesystem::exists(projectPath)) return false;

        StackFormat::ProjectDocument document;
        StackFormat::ProjectLoadOptions metadataOnly { true, false, false };
        const bool loadedMetadata = LoadProjectDocument(fileName, document, metadataOnly);
        const bool removedProject = std::filesystem::remove(projectPath);
        if (!loadedMetadata || document.metadata.projectKind != StackFormat::kCompositeProjectKind) {
            const std::filesystem::path assetPath = BuildAssetPathForProjectFile(fileName);
            if (std::filesystem::exists(assetPath)) {
                std::error_code ec;
                std::filesystem::remove(assetPath, ec);
            }
        }

        m_LastLibrarySignature = 0;
        return removedProject;
    } catch (...) {
        return false;
    }
}

bool LibraryManager::ExportAsset(const std::string& fileName, const std::string& destinationPath) {
    if (fileName.empty() || destinationPath.empty()) return false;

    try {
        const std::filesystem::path sourcePath = m_AssetsPath / fileName;
        if (!std::filesystem::exists(sourcePath)) return false;

        const std::filesystem::path destination = destinationPath;
        if (destination.has_parent_path()) {
            std::filesystem::create_directories(destination.parent_path());
        }

        std::filesystem::copy_file(sourcePath, destination, std::filesystem::copy_options::overwrite_existing);
        QueueUiNotification(UiNotificationSeverity::Success, "Asset exported.", "library-export-asset");
        return true;
    } catch (...) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to export the asset.", "library-export-asset");
        return false;
    }
}

bool LibraryManager::ExportProject(const std::string& fileName, const std::string& destinationPath) {
    if (fileName.empty() || destinationPath.empty()) return false;

    try {
        const std::filesystem::path sourcePath = m_LibraryPath / fileName;
        if (!std::filesystem::exists(sourcePath)) return false;

        const std::filesystem::path destination = destinationPath;
        if (destination.has_parent_path()) {
            std::filesystem::create_directories(destination.parent_path());
        }

        std::filesystem::copy_file(sourcePath, destination, std::filesystem::copy_options::overwrite_existing);
        QueueUiNotification(UiNotificationSeverity::Success, "Project exported.", "library-export-project");
        return true;
    } catch (...) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to export the project.", "library-export-project");
        return false;
    }
}

void LibraryManager::RequestExportLibraryBundle(const std::string& destinationPath) {
    if (destinationPath.empty() || Async::IsBusy(m_ExportTaskState)) return;

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Exporting the library bundle in the background...";

    Async::TaskSystem::Get().Submit([this, generation, destinationPath]() {
        const bool success = WriteLibraryBundle(destinationPath);
        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Library export completed.";
                QueueUiNotification(UiNotificationSeverity::Success, "Library export completed.", "library-export-bundle");
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = "Failed to export the library bundle.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to export the library bundle.", "library-export-bundle");
            }
        });
    });
}

void LibraryManager::ClearConflicts() {
    for (auto& conflict : m_PendingConflicts) {
        if (conflict.localPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        }
        if (conflict.importedPreviewTex) {
            m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        }
    }
    m_PendingConflicts.clear();
    m_ActiveImportBundle = {};
}

void LibraryManager::ResolveConflict(int index, ConflictAction action, const std::string& newName) {
    if (index < 0 || index >= (int)m_PendingConflicts.size()) return;

    auto& conflict = m_PendingConflicts[index];
    bool resolved = false;

    if (conflict.importedProjectIndex < 0 || conflict.importedProjectIndex >= (int)m_ActiveImportBundle.projects.size()) {
        m_PendingConflicts.erase(m_PendingConflicts.begin() + index);
        return;
    }

    const auto& importedBundledProject = m_ActiveImportBundle.projects[conflict.importedProjectIndex];

    if (action == ConflictAction::Ignore) {
        resolved = true;
    } else if (action == ConflictAction::Replace) {
        const std::string fileName = EnsureProjectFileNameForKind(
            importedBundledProject.fileName,
            SanitizeFileStem(importedBundledProject.project.metadata.projectName),
            importedBundledProject.project.metadata.projectKind);
        if (StackFormat::WriteProjectFile(m_LibraryPath / fileName, importedBundledProject.project)) {
            resolved = true;
        }
    } else if (action == ConflictAction::KeepBoth) {
        std::string targetName = newName.empty() ? importedBundledProject.project.metadata.projectName + " (Imported)" : newName;
        std::string fileName = EnsureProjectFileNameForKind(
            std::string(),
            SanitizeFileStem(targetName) + "_" + std::to_string(std::time(nullptr)),
            importedBundledProject.project.metadata.projectKind);
        
        StackFormat::ProjectDocument doc = importedBundledProject.project;
        doc.metadata.projectName = targetName;
        doc.metadata.timestamp = BuildTimestampString();
        
        if (StackFormat::WriteProjectFile(m_LibraryPath / fileName, doc)) {
            resolved = true;
        }
    }

    if (resolved) {
        if (conflict.localPreviewTex) m_DeferredTextureDeletions.push_back(conflict.localPreviewTex);
        if (conflict.importedPreviewTex) m_DeferredTextureDeletions.push_back(conflict.importedPreviewTex);
        m_PendingConflicts.erase(m_PendingConflicts.begin() + index);
        
        if (m_PendingConflicts.empty()) {
            // Finalize any assets that weren't part of conflicting projects?
            // Actually, for simplicity, we assume assets are imported if their project is resolved.
            // But assets in the bundle might not have projects.
            // For now, let's just refresh.
            m_ImportStatusText = "Conflict resolution completed.";
            m_LastLibrarySignature = 0;
            RefreshLibrary();
        }
    }
}

void LibraryManager::RequestImportLibraryBundle(const std::string& sourcePath) {
    if (sourcePath.empty() || Async::IsBusy(m_ImportTaskState)) return;

    ++m_ImportGeneration;
    const std::uint64_t generation = m_ImportGeneration;
    m_ImportTaskState = Async::TaskState::Running;
    m_ImportStatusText = "Importing the library bundle in the background...";

    Async::TaskSystem::Get().Submit([this, generation, sourcePath]() {
        const bool success = ImportLibraryBundle(sourcePath);
        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ImportGeneration) {
                return;
            }

            if (success) {
                m_ImportTaskState = Async::TaskState::Idle;
                m_ImportStatusText = "Library import completed.";
                QueueUiNotification(UiNotificationSeverity::Success, "Library import completed.", "library-import-bundle");
                m_LastLibrarySignature = 0;
                RefreshLibrary();
            } else {
                m_ImportTaskState = Async::TaskState::Failed;
                m_ImportStatusText = "Failed to import the library bundle.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to import the library bundle.", "library-import-bundle");
            }
        });
    });
}

bool LibraryManager::WriteLibraryBundle(const std::string& destinationPath) {
    if (destinationPath.empty()) return false;

    try {
        StackFormat::LibraryBundleDocument bundle;
        bundle.timestamp = BuildTimestampString();

        const StackFormat::ProjectLoadOptions fullProjectLoad {
            true,
            true,
            true
        };

        for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath)) {
            if (!IsSupportedProjectExtension(entry.path())) continue;

            StackFormat::ProjectDocument document;
            if (!LoadProjectDocument(entry.path().filename().string(), document, fullProjectLoad)) {
                continue;
            }

            StackFormat::BundledProjectDocument bundledProject;
            bundledProject.fileName = entry.path().filename().string();
            bundledProject.project = std::move(document);
            bundle.projects.push_back(std::move(bundledProject));
        }

        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath)) {
            if (!IsSupportedAssetExtension(entry.path())) continue;

            StackFormat::AssetDocument asset;
            asset.fileName = entry.path().filename().string();
            asset.displayName = DisplayNameFromStem(entry.path().stem().string());
            asset.projectFileName = ResolveAssetProjectFileName(entry.path().stem().string(), bundle.projects);

            const auto projectIt = std::find_if(
                bundle.projects.begin(),
                bundle.projects.end(),
                [&](const StackFormat::BundledProjectDocument& project) {
                    return !asset.projectFileName.empty() && project.fileName == asset.projectFileName;
                });

            if (projectIt != bundle.projects.end()) {
                if (projectIt->project.metadata.projectKind != StackFormat::kCompositeProjectKind) {
                    asset.projectName = projectIt->project.metadata.projectName;
                    asset.displayName = projectIt->project.metadata.projectName;
                } else {
                    asset.projectFileName.clear();
                }
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            if (stbi_info(entry.path().string().c_str(), &width, &height, &channels)) {
                asset.width = width;
                asset.height = height;
            }

            std::error_code ec;
            const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
            asset.timestamp = ec ? "Unknown" : BuildTimestampStringFromFileTime(writeTime);

            if (!ReadFileBytes(entry.path(), asset.imageBytes)) {
                continue;
            }

            bundle.assets.push_back(std::move(asset));
        }

        return StackFormat::WriteLibraryBundle(destinationPath, bundle);
    } catch (...) {
        return false;
    }
}

bool LibraryManager::ImportLibraryBundle(const std::string& sourcePath) {
    if (sourcePath.empty()) return false;

    try {
        StackFormat::LibraryBundleDocument bundle;
        if (!StackFormat::ReadLibraryBundle(sourcePath, bundle)) {
            return false;
        }

        std::vector<ImportConflict> conflicts;
        std::vector<int> immediateIndices;

        for (int i = 0; i < (int)bundle.projects.size(); ++i) {
            const auto& project = bundle.projects[i];
            const std::string fileName = EnsureProjectFileNameForKind(
                project.fileName,
                SanitizeFileStem(project.project.metadata.projectName),
                project.project.metadata.projectKind);

            if (std::filesystem::exists(m_LibraryPath / fileName)) {
                ImportConflict conflict;
                conflict.importedProjectIndex = i;
                conflict.localProjectFileName = fileName;
                
                // Store metadata for UI
                conflict.importedName = project.project.metadata.projectName;
                conflict.importedTimestamp = project.project.metadata.timestamp;
                conflict.importedWidth = project.project.metadata.sourceWidth;
                conflict.importedHeight = project.project.metadata.sourceHeight;

                StackFormat::ProjectLoadOptions options;
                options.includeThumbnail = true;
                options.includeSourceImage = true;
                options.includePipelineData = true;
                
                StackFormat::ProjectDocument localDoc;
                if (LoadProjectDocument(fileName, localDoc, options)) {
                    conflict.areIdentical = StackFormat::AreProjectsIdentical(project.project, localDoc);
                    
                    conflict.localName = localDoc.metadata.projectName;
                    conflict.localTimestamp = localDoc.metadata.timestamp;
                    conflict.localWidth = localDoc.metadata.sourceWidth;
                    conflict.localHeight = localDoc.metadata.sourceHeight;
                } else {
                    conflict.areIdentical = false;
                    conflict.localName = fileName;
                    conflict.localTimestamp = "Unknown";
                }
                conflicts.push_back(std::move(conflict));
            } else {
                immediateIndices.push_back(i);
            }
        }

        if (conflicts.empty()) {
            FinalizeImport(bundle, {});
            return true;
        } else {
            Async::TaskSystem::Get().PostToMain([this, bundle = std::move(bundle), conflicts = std::move(conflicts)]() mutable {
                ClearConflicts();
                m_ActiveImportBundle = std::move(bundle);
                m_PendingConflicts = std::move(conflicts);
                m_ImportTaskState = Async::TaskState::Idle;
                m_ImportStatusText = "Conflicts detected. Please resolve them to complete the import.";
                
                // Note: Previews will be generated on-demand in the UI
            });
            return true;
        }
    } catch (...) {
        return false;
    }
}

void LibraryManager::FinalizeImport(const StackFormat::LibraryBundleDocument& bundle, const std::vector<int>& skippedProjectIndices) {
    if (!std::filesystem::exists(m_LibraryPath)) {
        std::filesystem::create_directories(m_LibraryPath);
    }
    if (!std::filesystem::exists(m_AssetsPath)) {
        std::filesystem::create_directories(m_AssetsPath);
    }

    for (int i = 0; i < (int)bundle.projects.size(); ++i) {
        bool skip = false;
        for (int skipIdx : skippedProjectIndices) {
            if (i == skipIdx) {
                skip = true;
                break;
            }
        }
        if (skip) continue;

        const auto& project = bundle.projects[i];
        const std::string fileName = EnsureProjectFileNameForKind(
            project.fileName,
            SanitizeFileStem(project.project.metadata.projectName),
            project.project.metadata.projectKind);

        StackFormat::WriteProjectFile(m_LibraryPath / fileName, project.project);
    }

    for (const auto& asset : bundle.assets) {
        const std::string fallbackName = std::filesystem::path(asset.projectFileName.empty() ? "imported_asset" : asset.projectFileName).stem().string() + ".png";
        const std::string fileName = EnsureAssetFileName(asset.fileName, fallbackName);
        WriteFileBytes(m_AssetsPath / fileName, asset.imageBytes);
    }
}

std::filesystem::path LibraryManager::BuildAssetPathForProjectFile(const std::string& projectFileName) const {
    return m_AssetsPath / (std::filesystem::path(projectFileName).stem().string() + ".png");
}

void LibraryManager::RequestLoadProjectFromPath(const std::filesystem::path& absolutePath, EditorModule* editor, std::function<void(bool)> onComplete) {
    if (absolutePath.empty() || !editor) {
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the project from disk.", "library-load-project-path");
        if (onComplete) onComplete(false);
        return;
    }

    ++m_ProjectLoadGeneration;
    const std::uint64_t generation = m_ProjectLoadGeneration;
    m_ProjectLoadTaskState = Async::TaskState::Queued;
    m_ProjectLoadStatusText = "Loading the project from path...";

    Async::TaskSystem::Get().Submit([this, generation, absolutePath, editor, onComplete = std::move(onComplete)]() mutable {
        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = true;
        options.includePipelineData = true;

        StackFormat::ProjectDocument document;
        EditorModule::LoadedProjectData loadedProject;

        bool success = false;
        if (std::filesystem::exists(absolutePath)) {
            if (StackFormat::ReadProjectFile(absolutePath, document, options)) {
                success = true;
            } else {
                success = LoadLegacyProjectDocument(absolutePath, document, options);
            }
        }

        if (success) {
            if (document.metadata.projectKind == StackFormat::kRenderProjectKind
                || document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
                success = false;
            }
        }
        if (success) {
            int width = 0;
            int height = 0;
            int channels = 0;
            success = DecodeImageBytes(document.sourceImageBytes, loadedProject.sourcePixels, width, height, channels);
            if (success) {
                loadedProject.width = width;
                loadedProject.height = height;
                loadedProject.channels = channels;
                loadedProject.pipelineData = document.pipelineData.is_null() ? StackFormat::json::array() : document.pipelineData;
                loadedProject.projectName = document.metadata.projectName;
                loadedProject.projectFileName = absolutePath.filename().string();
            }
        }

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             editor,
                                             onComplete = std::move(onComplete),
                                             loadedProject = std::move(loadedProject),
                                             success]() mutable {
            if (generation != m_ProjectLoadGeneration) {
                return;
            }

            if (!success) {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to load the project from disk.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the project from disk.", "library-load-project-path");
                if (onComplete) onComplete(false);
                return;
            }

            m_ProjectLoadTaskState = Async::TaskState::Applying;
            m_ProjectLoadStatusText = "Applying project data...";

            const bool applied = editor->ApplyLoadedProject(loadedProject);
            if (applied) {
                m_ProjectLoadTaskState = Async::TaskState::Idle;
                m_ProjectLoadStatusText = "Project loaded successfully.";
                QueueUiNotification(UiNotificationSeverity::Success, "Project loaded successfully.", "library-load-project-path");
            } else {
                m_ProjectLoadTaskState = Async::TaskState::Failed;
                m_ProjectLoadStatusText = "Failed to apply project data.";
                QueueUiNotification(UiNotificationSeverity::Error, "Failed to apply project data.", "library-load-project-path");
            }

            if (onComplete) onComplete(applied);
        });
    });
}



void LibraryManager::RequestImportAndLoad(
    const std::string& sourcePath,
    EditorModule* editor,
    CompositeModule* composite,
    std::function<void(int)> onTabSwitchRequested) {
    if (sourcePath.empty()) return;

    const std::filesystem::path path(sourcePath);
    if (!std::filesystem::exists(path)) return;

    if (path.extension() == ".stacklib") {
        RequestImportLibraryBundle(sourcePath);
        return;
    }

    if (IsSupportedProjectExtension(path)) {
        // 0. Tab IDs (Library=0, Editor=1, Render=3)
        // These can be passed via the callback
        
        // 1. Read metadata to see what kind it is
        StackFormat::ProjectDocument document;
        StackFormat::ProjectLoadOptions metadataOnly { true, false, false };
        bool success = false;
        if (StackFormat::ReadProjectFile(path, document, metadataOnly)) {
            success = true;
        } else {
            success = LoadLegacyProjectDocument(path, document, metadataOnly);
        }

        if (!success) {
            m_ImportStatusText = "Failed to read project file metadata.";
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to read project file metadata.", "library-import-project");
            return;
        }

        if (document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
            m_ImportStatusText = "Legacy standalone composite projects are no longer supported.";
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, "Legacy standalone composite projects are no longer supported.", "library-import-project");
            return;
        }

        // 2. Check for collision
        const std::string fileName = EnsureProjectFileNameForKind(
            path.filename().string(),
            SanitizeFileStem(document.metadata.projectName),
            document.metadata.projectKind);
        if (std::filesystem::exists(m_LibraryPath / fileName)) {
            // Trigger Conflict resolution
            m_ImportStatusText = "Project already exists in library. Resolving conflict...";
            
            ImportConflict conflict;
            conflict.importedProjectIndex = 0;
            conflict.localProjectFileName = fileName;
            conflict.importedName = document.metadata.projectName;
            conflict.importedTimestamp = document.metadata.timestamp;
            conflict.importedWidth = document.metadata.sourceWidth;
            conflict.importedHeight = document.metadata.sourceHeight;
            
            StackFormat::ProjectDocument localDoc;
            StackFormat::ProjectLoadOptions fullLoad { true, true, true };
            if (LoadProjectDocument(fileName, localDoc, fullLoad)) {
                conflict.areIdentical = StackFormat::AreProjectsIdentical(document, localDoc);
                conflict.localName = localDoc.metadata.projectName;
                conflict.localTimestamp = localDoc.metadata.timestamp;
                conflict.localWidth = localDoc.metadata.sourceWidth;
                conflict.localHeight = localDoc.metadata.sourceHeight;
            } else {
                conflict.areIdentical = false;
                conflict.localName = fileName;
            }

            // Reload full doc for the imported one to ensure we have all data
            if (!StackFormat::ReadProjectFile(path, document, fullLoad)) {
                LoadLegacyProjectDocument(path, document, fullLoad);
            }

            StackFormat::LibraryBundleDocument bundle;
            bundle.timestamp = BuildTimestampString();
            StackFormat::BundledProjectDocument bundled;
            bundled.fileName = fileName;
            bundled.project = std::move(document);
            bundle.projects.push_back(std::move(bundled));

            ClearConflicts();
            m_ActiveImportBundle = std::move(bundle);
            m_PendingConflicts.push_back(std::move(conflict));
            m_ImportTaskState = Async::TaskState::Idle;
            return;
        }

        // 3. No collision, copy and load
        std::error_code ec;
        std::filesystem::copy_file(path, m_LibraryPath / fileName, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            m_ImportStatusText = "Failed to copy project to library: " + ec.message();
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, m_ImportStatusText, "library-import-project");
            return;
        }

        m_LastLibrarySignature = 0;
        RefreshLibrary();

        if (document.metadata.projectKind == StackFormat::kRenderProjectKind) {
            m_ImportStatusText = "Render projects are no longer supported.";
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, "Render projects are no longer supported.", "library-import-project");
            return;
        } else {
            if (document.metadata.projectKind == StackFormat::kCompositeProjectKind) {
                m_ImportStatusText = "Legacy standalone composite projects are no longer supported.";
                m_ImportTaskState = Async::TaskState::Failed;
                QueueUiNotification(UiNotificationSeverity::Error, "Legacy standalone composite projects are no longer supported.", "library-import-project");
                return;
            }
            if (onTabSwitchRequested) onTabSwitchRequested(1);
            RequestLoadProjectFromPath(m_LibraryPath / fileName, editor);
        }
        return;
    }

    if (IsSupportedAssetExtension(path)) {
        // Import image as asset
        const std::string fileName = EnsureAssetFileName(path.filename().string(), path.filename().string());
        std::error_code ec;
        std::filesystem::copy_file(path, m_AssetsPath / fileName, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            m_ImportStatusText = "Failed to copy image to library: " + ec.message();
            m_ImportTaskState = Async::TaskState::Failed;
            QueueUiNotification(UiNotificationSeverity::Error, m_ImportStatusText, "library-import-asset");
            return;
        }

        m_LastLibrarySignature = 0;
        RefreshLibrary();

        // Load into editor
        if (editor) {
            if (onTabSwitchRequested) onTabSwitchRequested(1);
            editor->RequestLoadSourceImage((m_AssetsPath / fileName).string());
        }
        return;
    }

    m_ImportStatusText = "Unsupported file type dropped.";
    m_ImportTaskState = Async::TaskState::Failed;
    QueueUiNotification(UiNotificationSeverity::Error, "Unsupported file type dropped.", "library-import-asset");
}

bool LibraryManager::DeleteAsset(const std::string& fileName) {
    auto path = m_AssetsPath / fileName;
    std::error_code ec;
    if (!std::filesystem::remove(path, ec)) {
        std::cerr << "[LibraryManager] Failed to delete asset: " << fileName << " (" << ec.message() << ")\n";
        return false;
    }
    std::cout << "[LibraryManager] Deleted asset: " << fileName << "\n";
    m_LastLibrarySignature = 0;
    return true;
}
