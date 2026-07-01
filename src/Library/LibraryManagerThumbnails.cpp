#include "LibraryManager.h"

#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryImageHelpers.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace {

namespace StackFormat = StackBinaryFormat;
namespace LibraryImage = Stack::Library::ImageHelpers;

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

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

bool PixelsDifferMeaningfully(
    const std::vector<unsigned char>& aPixels,
    int aWidth,
    int aHeight,
    int aChannels,
    const std::vector<unsigned char>& bPixels,
    int bWidth,
    int bHeight,
    int bChannels) {

    if (aPixels.empty() || bPixels.empty() ||
        aWidth <= 0 || aHeight <= 0 ||
        bWidth <= 0 || bHeight <= 0 ||
        aChannels <= 0 || bChannels <= 0) {
        return false;
    }

    const std::size_t aExpected =
        static_cast<std::size_t>(aWidth) * static_cast<std::size_t>(aHeight) * static_cast<std::size_t>(aChannels);
    const std::size_t bExpected =
        static_cast<std::size_t>(bWidth) * static_cast<std::size_t>(bHeight) * static_cast<std::size_t>(bChannels);
    if (aPixels.size() < aExpected || bPixels.size() < bExpected) {
        return false;
    }

    const int sampleColumns = std::min(32, std::max(1, std::min(aWidth, bWidth)));
    const int sampleRows = std::min(32, std::max(1, std::min(aHeight, bHeight)));
    int samples = 0;
    int changedSamples = 0;
    int strongestSampleDiff = 0;
    long long totalSampleDiff = 0;

    auto sampleChannel = [](const std::vector<unsigned char>& pixels, int width, int channels, int x, int y, int channel) {
        const int safeChannel = std::min(channel, channels - 1);
        const std::size_t index =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
                static_cast<std::size_t>(channels) +
            static_cast<std::size_t>(safeChannel);
        return static_cast<int>(pixels[index]);
    };

    for (int sy = 0; sy < sampleRows; ++sy) {
        const int ay = (sampleRows == 1) ? (aHeight / 2) : (sy * (aHeight - 1)) / (sampleRows - 1);
        const int by = (sampleRows == 1) ? (bHeight / 2) : (sy * (bHeight - 1)) / (sampleRows - 1);
        for (int sx = 0; sx < sampleColumns; ++sx) {
            const int ax = (sampleColumns == 1) ? (aWidth / 2) : (sx * (aWidth - 1)) / (sampleColumns - 1);
            const int bx = (sampleColumns == 1) ? (bWidth / 2) : (sx * (bWidth - 1)) / (sampleColumns - 1);

            const int dr = std::abs(sampleChannel(aPixels, aWidth, aChannels, ax, ay, 0) -
                                    sampleChannel(bPixels, bWidth, bChannels, bx, by, 0));
            const int dg = std::abs(sampleChannel(aPixels, aWidth, aChannels, ax, ay, 1) -
                                    sampleChannel(bPixels, bWidth, bChannels, bx, by, 1));
            const int db = std::abs(sampleChannel(aPixels, aWidth, aChannels, ax, ay, 2) -
                                    sampleChannel(bPixels, bWidth, bChannels, bx, by, 2));
            const int sampleDiff = (dr + dg + db) / 3;
            totalSampleDiff += sampleDiff;
            strongestSampleDiff = std::max(strongestSampleDiff, sampleDiff);
            if (sampleDiff > 6) {
                ++changedSamples;
            }
            ++samples;
        }
    }

    if (samples <= 0) {
        return false;
    }

    const double meanSampleDiff = static_cast<double>(totalSampleDiff) / static_cast<double>(samples);
    return meanSampleDiff > 2.0 ||
        (strongestSampleDiff > 18 && changedSamples >= std::max(3, samples / 64));
}

} // namespace

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
    project->thumbnailPixels.clear();
    project->thumbnailPixelWidth = 0;
    project->thumbnailPixelHeight = 0;
    project->thumbnailDecodeState = Async::TaskState::Idle;
    project->thumbnailDecodeAttempted = false;
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
    asset->thumbnailPixels.clear();
    asset->thumbnailPixelWidth = 0;
    asset->thumbnailPixelHeight = 0;
    asset->thumbnailDecodeState = Async::TaskState::Idle;
    asset->thumbnailLoadAttempted = false;
}

bool LibraryManager::DecodeImageBytes(
    const std::vector<unsigned char>& encodedImage,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) {
    return LibraryImage::DecodeImageBytes(encodedImage, outPixels, outW, outH, outChannels);
}

void LibraryManager::FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    LibraryImage::FlipImageRowsInPlace(pixels, width, height, channels);
}

void LibraryManager::SetThumbnailWarmupPriority(std::vector<std::string> projectFileNames, std::vector<std::string> assetFileNames) {
    m_ProjectThumbnailPriority = std::move(projectFileNames);
    m_AssetThumbnailPriority = std::move(assetFileNames);
}

LibraryTextureUploadStats LibraryManager::UploadLibraryTextures(double maxMainThreadMs) {
    LibraryTextureUploadStats stats;
    const auto startedAt = std::chrono::steady_clock::now();
    const double budgetMs = std::max(0.25, maxMainThreadMs);

    const auto elapsedMs = [&]() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
    };
    const auto budgetHit = [&]() {
        return elapsedMs() >= budgetMs;
    };

    constexpr int kProjectDecodeQueueBudget = 8;
    constexpr int kAssetDecodeQueueBudget = 2;
    constexpr std::size_t kRoundRobinProjectCandidates = 96;
    constexpr std::size_t kRoundRobinAssetCandidates = 48;

    std::lock_guard<std::mutex> lock(m_ProjectsMutex);
    stats.projectCount = static_cast<int>(m_Projects.size());
    stats.assetCount = static_cast<int>(m_Assets.size());

    std::vector<std::shared_ptr<ProjectEntry>> projectCandidates;
    std::vector<std::shared_ptr<AssetEntry>> assetCandidates;
    projectCandidates.reserve(m_ProjectThumbnailPriority.size() + kRoundRobinProjectCandidates);
    assetCandidates.reserve(m_AssetThumbnailPriority.size() + kRoundRobinAssetCandidates);

    if (!m_ProjectThumbnailPriority.empty()) {
        std::unordered_set<std::string> priorityNames(m_ProjectThumbnailPriority.begin(), m_ProjectThumbnailPriority.end());
        for (const auto& project : m_Projects) {
            if (project && priorityNames.count(project->fileName) > 0) {
                projectCandidates.push_back(project);
            }
        }
    }

    if (!m_AssetThumbnailPriority.empty()) {
        std::unordered_set<std::string> priorityNames(m_AssetThumbnailPriority.begin(), m_AssetThumbnailPriority.end());
        for (const auto& asset : m_Assets) {
            if (asset && priorityNames.count(asset->fileName) > 0) {
                assetCandidates.push_back(asset);
            }
        }
    }

    if (!m_Projects.empty()) {
        const std::size_t count = m_Projects.size();
        const std::size_t inspectCount = std::min(count, kRoundRobinProjectCandidates);
        for (std::size_t i = 0; i < inspectCount; ++i) {
            const std::size_t index = (m_ProjectThumbnailWarmupCursor + i) % count;
            if (m_Projects[index]) {
                projectCandidates.push_back(m_Projects[index]);
            }
        }
        m_ProjectThumbnailWarmupCursor = (m_ProjectThumbnailWarmupCursor + inspectCount) % count;
    }

    if (!m_Assets.empty()) {
        const std::size_t count = m_Assets.size();
        const std::size_t inspectCount = std::min(count, kRoundRobinAssetCandidates);
        for (std::size_t i = 0; i < inspectCount; ++i) {
            const std::size_t index = (m_AssetThumbnailWarmupCursor + i) % count;
            if (m_Assets[index]) {
                assetCandidates.push_back(m_Assets[index]);
            }
        }
        m_AssetThumbnailWarmupCursor = (m_AssetThumbnailWarmupCursor + inspectCount) % count;
    }

    auto uploadProjectIfReady = [&](const std::shared_ptr<ProjectEntry>& project) {
        if (!project || project->thumbnailTex != 0) {
            return;
        }
        if (project->thumbnailDecodeState == Async::TaskState::Ready) {
            if (budgetHit()) {
                stats.budgetHit = true;
                ++stats.pendingReadyUploads;
                return;
            }
            InitializeThumbnail(project);
            if (project->thumbnailTex != 0) {
                ++stats.projectUploads;
            }
            if (budgetHit()) {
                stats.budgetHit = true;
            }
        } else if (Async::IsBusy(project->thumbnailDecodeState)) {
            ++stats.pendingProjectDecodes;
        }
    };

    auto uploadAssetIfReady = [&](const std::shared_ptr<AssetEntry>& asset) {
        if (!asset || asset->thumbnailTex != 0) {
            return;
        }
        if (asset->thumbnailDecodeState == Async::TaskState::Ready) {
            if (budgetHit()) {
                stats.budgetHit = true;
                ++stats.pendingReadyUploads;
                return;
            }
            InitializeAssetThumbnail(asset);
            if (asset->thumbnailTex != 0) {
                ++stats.assetUploads;
            }
            if (budgetHit()) {
                stats.budgetHit = true;
            }
        } else if (Async::IsBusy(asset->thumbnailDecodeState)) {
            ++stats.pendingAssetDecodes;
        }
    };

    for (const auto& project : projectCandidates) {
        uploadProjectIfReady(project);
        if (stats.budgetHit) {
            break;
        }
    }

    if (!stats.budgetHit) {
        for (const auto& asset : assetCandidates) {
            uploadAssetIfReady(asset);
            if (stats.budgetHit) {
                break;
            }
        }
    }

    int queuedProjectDecodes = 0;
    if (!stats.budgetHit) {
        for (const auto& project : projectCandidates) {
            if (!project ||
                project->thumbnailTex != 0 ||
                project->thumbnailBytes.empty() ||
                project->thumbnailDecodeAttempted ||
                project->thumbnailDecodeState == Async::TaskState::Ready ||
                Async::IsBusy(project->thumbnailDecodeState)) {
                continue;
            }
            QueueProjectThumbnailDecode(project);
            ++queuedProjectDecodes;
            ++stats.projectDecodeQueued;
            if (queuedProjectDecodes >= kProjectDecodeQueueBudget) {
                break;
            }
        }
    }

    int queuedAssetDecodes = 0;
    if (!stats.budgetHit) {
        for (const auto& asset : assetCandidates) {
            if (!asset ||
                asset->thumbnailTex != 0 ||
                asset->thumbnailLoadAttempted ||
                asset->thumbnailDecodeState == Async::TaskState::Ready ||
                Async::IsBusy(asset->thumbnailDecodeState)) {
                continue;
            }
            QueueAssetThumbnailDecode(asset);
            ++queuedAssetDecodes;
            ++stats.assetDecodeQueued;
            if (queuedAssetDecodes >= kAssetDecodeQueueBudget) {
                break;
            }
        }
    }

    stats.elapsedMs = elapsedMs();
    return stats;
}

bool LibraryManager::HasPendingThumbnailWarmup() const {
    std::lock_guard<std::mutex> lock(m_ProjectsMutex);
    for (const auto& project : m_Projects) {
        if (!project || project->thumbnailTex != 0 || project->thumbnailBytes.empty()) {
            continue;
        }
        if (project->thumbnailDecodeState != Async::TaskState::Failed) {
            return true;
        }
    }

    for (const auto& asset : m_Assets) {
        if (!asset || asset->thumbnailTex != 0) {
            continue;
        }
        if (!asset->thumbnailLoadAttempted ||
            Async::IsBusy(asset->thumbnailDecodeState) ||
            asset->thumbnailDecodeState == Async::TaskState::Ready) {
            return true;
        }
    }

    return false;
}

std::vector<unsigned char> LibraryManager::GenerateThumbnailBytes(const std::vector<unsigned char>& pixels, int width, int height) {
    int thumbW = 0;
    int thumbH = 0;
    std::vector<unsigned char> thumbPixels = LibraryImage::ResizePixelsNearest(pixels, width, height, thumbW, thumbH);
    if (thumbPixels.empty()) return {};

    std::vector<unsigned char> pngData;
    stbi_write_png_to_func(PngWriteCallback, &pngData, thumbW, thumbH, 4, thumbPixels.data(), thumbW * 4);
    return pngData;
}

void LibraryManager::InitializeThumbnail(std::shared_ptr<ProjectEntry> project) {
    if (!project ||
        project->thumbnailDecodeState != Async::TaskState::Ready ||
        project->thumbnailPixels.empty() ||
        project->thumbnailPixelWidth <= 0 ||
        project->thumbnailPixelHeight <= 0) {
        return;
    }

    project->thumbnailTex = GLHelpers::CreateTextureFromPixels(
        project->thumbnailPixels.data(),
        project->thumbnailPixelWidth,
        project->thumbnailPixelHeight,
        4);
    project->thumbnailPixels.clear();
    project->thumbnailPixelWidth = 0;
    project->thumbnailPixelHeight = 0;
    if (project->thumbnailTex != 0) {
        project->thumbnailDecodeState = Async::TaskState::Idle;
    } else {
        project->thumbnailDecodeState = Async::TaskState::Failed;
    }
}

void LibraryManager::InitializeAssetThumbnail(std::shared_ptr<AssetEntry> asset) {
    if (!asset ||
        asset->thumbnailDecodeState != Async::TaskState::Ready ||
        asset->thumbnailPixels.empty() ||
        asset->thumbnailPixelWidth <= 0 ||
        asset->thumbnailPixelHeight <= 0) {
        return;
    }

    asset->thumbnailTex = GLHelpers::CreateTextureFromPixels(
        asset->thumbnailPixels.data(),
        asset->thumbnailPixelWidth,
        asset->thumbnailPixelHeight,
        4);
    asset->thumbnailPixels.clear();
    asset->thumbnailPixelWidth = 0;
    asset->thumbnailPixelHeight = 0;
    if (asset->thumbnailTex != 0) {
        asset->thumbnailDecodeState = Async::TaskState::Idle;
    } else {
        asset->thumbnailDecodeState = Async::TaskState::Failed;
    }
}

void LibraryManager::QueueProjectThumbnailDecode(const std::shared_ptr<ProjectEntry>& project) {
    if (!project ||
        project->thumbnailTex != 0 ||
        project->thumbnailBytes.empty() ||
        project->thumbnailDecodeAttempted ||
        Async::IsBusy(project->thumbnailDecodeState)) {
        return;
    }

    project->thumbnailDecodeAttempted = true;
    project->thumbnailDecodeState = Async::TaskState::Queued;
    std::vector<unsigned char> thumbnailBytes = project->thumbnailBytes;

    Async::TaskSystem::Get().Submit([project, thumbnailBytes = std::move(thumbnailBytes)]() mutable {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        int channels = 0;
        const bool decoded = LibraryManager::DecodeImageBytes(thumbnailBytes, pixels, width, height, channels);

        Async::TaskSystem::Get().PostToMain([
            project,
            decoded,
            pixels = std::move(pixels),
            width,
            height]() mutable {
            if (!project || project->thumbnailTex != 0) {
                return;
            }

            if (!decoded || pixels.empty() || width <= 0 || height <= 0) {
                project->thumbnailDecodeState = Async::TaskState::Failed;
                project->thumbnailPixels.clear();
                project->thumbnailPixelWidth = 0;
                project->thumbnailPixelHeight = 0;
                return;
            }

            project->thumbnailPixels = std::move(pixels);
            project->thumbnailPixelWidth = width;
            project->thumbnailPixelHeight = height;
            project->thumbnailDecodeState = Async::TaskState::Ready;
        });
    });
}

void LibraryManager::QueueAssetThumbnailDecode(const std::shared_ptr<AssetEntry>& asset) {
    if (!asset ||
        asset->thumbnailTex != 0 ||
        asset->thumbnailLoadAttempted ||
        Async::IsBusy(asset->thumbnailDecodeState)) {
        return;
    }

    asset->thumbnailLoadAttempted = true;
    asset->thumbnailDecodeState = Async::TaskState::Queued;
    const std::filesystem::path assetPath = m_AssetsPath / asset->fileName;

    Async::TaskSystem::Get().Submit([asset, assetPath]() mutable {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> thumbPixels;
        int thumbW = 0;
        int thumbH = 0;
        bool decoded = false;

        std::error_code sizeError;
        const std::uintmax_t fileBytes = std::filesystem::file_size(assetPath, sizeError);
        if (sizeError || fileBytes <= kMaxSynchronousAssetThumbnailBytes) {
            decoded = LibraryImage::LoadRgbaImageFromFile(assetPath, pixels, width, height);
            if (decoded && width > 0 && height > 0) {
                thumbPixels = LibraryImage::ResizePixelsNearest(pixels, width, height, thumbW, thumbH);
            }
        }

        Async::TaskSystem::Get().PostToMain([
            asset,
            decoded,
            width,
            height,
            thumbPixels = std::move(thumbPixels),
            thumbW,
            thumbH]() mutable {
            if (!asset || asset->thumbnailTex != 0) {
                return;
            }

            if (!decoded || thumbPixels.empty() || thumbW <= 0 || thumbH <= 0) {
                asset->thumbnailDecodeState = Async::TaskState::Failed;
                asset->thumbnailPixels.clear();
                asset->thumbnailPixelWidth = 0;
                asset->thumbnailPixelHeight = 0;
                return;
            }

            asset->width = width;
            asset->height = height;
            asset->thumbnailPixels = std::move(thumbPixels);
            asset->thumbnailPixelWidth = thumbW;
            asset->thumbnailPixelHeight = thumbH;
            asset->thumbnailDecodeState = Async::TaskState::Ready;
        });
    });
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
                LibraryImage::ExtractEmbeddedGraphSourcePng(document.pipelineData, graphSourcePngBytes)) {
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
                    preview.fallbackAssetSuccess = LibraryImage::LoadRgbaImageFromFile(
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
            project->previewStatusText = preview.renderProject
                ? "Loading saved beauty preview..."
                : (preview.projectKind == StackFormat::kCompositeProjectKind
                        ? "Loading saved composite export preview..."
                        : "Rendering full-quality preview...");
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

                if (previewEditor.GetNodeGraph().IsOutputConnected()) {
                    previewEditor.GetPipeline().ExecuteGraph(previewEditor.BuildGraphSnapshot());
                    renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
                    comparePixels = previewEditor.GetPipeline().GetCompareSourcePixels(compareW, compareH);
                } else {
                    previewEditor.GetPipeline().Execute(previewEditor.GetLayers());
                    renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
                }

                livePreviewLooksBlank =
                    !renderedPixels.empty() &&
                    !LibraryImage::HasMeaningfulPixels(renderedPixels) &&
                    LibraryImage::HasMeaningfulPixels(preview.sourcePixels);
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

            const bool livePreviewUsable =
                !renderedPixels.empty() &&
                renderedW > 0 &&
                renderedH > 0 &&
                !livePreviewLooksBlank;
            const bool savedPreviewDiffersFromSource =
                preview.fallbackAssetSuccess &&
                !preview.fallbackAssetPixels.empty() &&
                PixelsDifferMeaningfully(
                    preview.fallbackAssetPixels,
                    preview.fallbackAssetWidth,
                    preview.fallbackAssetHeight,
                    4,
                    preview.sourcePixels,
                    preview.width,
                    preview.height,
                    preview.channels);

            if (preview.renderProject || preview.projectKind == StackFormat::kCompositeProjectKind) {
                finalPreviewWidth = preview.width;
                finalPreviewHeight = preview.height;
                finalPreviewPixels = preview.sourcePixels;
            } else if (livePreviewUsable) {
                finalPreviewWidth = renderedW;
                finalPreviewHeight = renderedH;
                finalPreviewPixels = std::move(renderedPixels);
                FlipImageRowsInPlace(finalPreviewPixels, finalPreviewWidth, finalPreviewHeight, 4);
            } else if (savedPreviewDiffersFromSource) {
                finalPreviewWidth = preview.fallbackAssetWidth;
                finalPreviewHeight = preview.fallbackAssetHeight;
                finalPreviewPixels = std::move(preview.fallbackAssetPixels);
                usedSavedAsset = true;
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
        preview.success = LibraryImage::LoadRgbaImageFromFile(m_AssetsPath / asset->fileName, preview.pixels, preview.width, preview.height);

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
