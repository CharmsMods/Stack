#include "Async/ForegroundIO.h"
#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Editor/GalleryPresetPreview.h"
#include "Persistence/ProjectIndex.h"
#include "App/UiFrameBudget.h"
#include "Utils/DisplayRefreshRate.h"
#include "Raw/RawLoader.h"
#include "Raw/GalleryTileCache.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <limits>
#include <vector>

namespace {

const std::string kOriginalDetailKey = ":gallery-original-detail:";
constexpr std::size_t kRawWorkspaceThumbnailTextureDecodeRequestsPerFrame = 12;
constexpr std::size_t kRawWorkspaceThumbnailTextureUploadsPerFrame = 3;
constexpr std::size_t kRawWorkspaceThumbnailTextureUploadBytesPerFrame =
    8u * 1024u * 1024u;
constexpr std::size_t kRawWorkspaceThumbnailTextureDeletesPerFrame = 8;
constexpr std::size_t kRawWorkspaceThumbnailTextureMaxActiveDecodes = 24;
constexpr std::size_t kRawWorkspaceThumbnailTextureMaxResidentCount = 192;
constexpr std::size_t kRawWorkspaceThumbnailTextureMaxResidentBytes =
    96u * 1024u * 1024u;
constexpr std::size_t kGalleryThumbnailMaxResidentCount = 512;
constexpr std::size_t kGalleryThumbnailMaxResidentBytes = 256u * 1024u * 1024u;

bool NeedsCameraGroupingPreview(const Stack::RawWorkspace::SourceRecord& source) {
    return (source.browsePreview || source.thumbnail.camera.bytes) &&
        source.thumbnail.status != Stack::RawWorkspace::ThumbnailStatus::Failed &&
        !source.thumbnail.similarityDescriptor.IsValid() &&
        !source.transientThumbnail.similarityDescriptor.IsValid();
}

std::size_t RawWorkspaceThumbnailResidentBytes(int width, int height, unsigned bytesPerPixel = 4) {
    const std::size_t base = static_cast<std::size_t>(std::max(0, width)) *
        static_cast<std::size_t>(std::max(0, height)) * bytesPerPixel;
    // Full mip chains use approximately one third more memory than level 0.
    return base + base / 3u;
}

} // namespace

bool EditorModule::UsesGalleryPresetPreviews() const {
    using Mode = Stack::RawWorkspace::GalleryContentMode;
    return m_RawWorkspaceGalleryContentMode == Mode::Unlinked || m_RawWorkspaceGalleryContentMode == Mode::AllProjects;
}

void EditorModule::ChooseGalleryComparison(const std::filesystem::path& path) {
    const auto generation=++m_GalleryComparisonGeneration;
    m_GalleryPresetThumbnails.clear();
    if (path.empty()) {
        m_GalleryComparisonEnabled = true;
        m_GalleryComparisonImage.reset(); m_GalleryComparisonName.clear(); return;
    }
    const auto owner=m_Project;
    ProjectTasks().Submit("Loading comparison image",[this,owner,generation,path] {
        std::shared_ptr<const Raw::RawImageData> image;
        std::string error;
        try { image=Stack::GalleryPresets::Comparison(path); } catch(const std::exception& e) { error=e.what(); }
        owner->tasks.PostToMain([this,generation,image,path,error] {
            if (generation!=m_GalleryComparisonGeneration) return;
            if (!image) { PostNotification(UiNotificationSeverity::Error,error,"preset-comparison"); return; }
            ++m_GalleryComparisonGeneration;
            m_GalleryComparisonEnabled = true;
            m_GalleryComparisonImage=image; m_GalleryComparisonName=path.filename().u8string();
            m_GalleryPresetThumbnails.clear();
        });
    });
}

const Stack::RawWorkspace::SourceRecord* EditorModule::GalleryPresetThumbnail(const std::string& key) {
    if (!m_GalleryComparisonEnabled) return nullptr;
    const auto it=m_GalleryPresetProjects.find(key);
    if (it==m_GalleryPresetProjects.end() || it->second>=m_RawWorkspace.sourceSetProjects.size()) return nullptr;
    const auto& doc=m_RawWorkspace.sourceSetProjects[it->second];
    const auto master=doc.masterSource;
    if (!master && !doc.projectMetadata) return nullptr;
    auto& slot=m_GalleryPresetThumbnails[key];
    const auto revision=master ? static_cast<std::uint64_t>(master->modifiedTimeTicks) : doc.projectMetadata->storageRevision;
    if (slot.revision!=revision) { slot={}; slot.revision=revision; }
    if (!slot.attempted && m_GalleryComparisonJobs<2) {
        const auto generation=m_GalleryComparisonGeneration;
        const auto owner=m_Project;
        const auto layout=Stack::RawWorkspace::BuildGalleryLayout(doc.sourceWorkspaceRoot);
        const bool queued=ProjectTasks().Submit("Previewing project edits",
            [this,owner,generation,key,revision,layout,master,path=doc.absolutePath,image=m_GalleryComparisonImage] {
            std::filesystem::path rendered; std::string error;
            try {
                if (master) {
                    auto source=*master;
                    using namespace Stack::RawWorkspace;
                    if (ClassifyThumbnail(layout,source)==ThumbnailStatus::Valid) rendered=source.thumbnail.absolutePath;
                    else {
                        const auto preview=GenerateNeutralThumbnail(layout,source);
                        if (preview.success) rendered=preview.thumbnail.absolutePath;
                        else error=preview.errorMessage;
                    }
                } else rendered=Stack::GalleryPresets::Render(path,revision,layout.thumbnailsDirectory,image);
            }
            catch(const std::exception& e) { error=e.what(); }
            owner->tasks.PostToMain([this,generation,key,revision,rendered,error] {
                if (m_GalleryComparisonJobs) --m_GalleryComparisonJobs;
                const auto found=m_GalleryPresetThumbnails.find(key);
                if (generation!=m_GalleryComparisonGeneration || found==m_GalleryPresetThumbnails.end() ||
                    revision!=found->second.revision) return;
                found->second.error=error;
                if (rendered.empty()) {
                    if (found->second.error.empty()) found->second.error = "Preview unavailable";
                    InvalidateRawWorkspaceGalleryPresentation(false);
                    return;
                }
                auto& source=found->second.textureSource;
                source.relativePathKey="preset-thumbnail:"+rendered.stem().u8string();
                source.thumbnail.absolutePath=rendered;
                source.thumbnail.status=Stack::RawWorkspace::ThumbnailStatus::Ready;
            });
        });
        if (queued) ++m_GalleryComparisonJobs;
        slot.attempted=queued;
    }
    return slot.textureSource.relativePathKey.empty() ? nullptr : &slot.textureSource;
}

unsigned int EditorModule::GetGalleryOriginalDetail(const Stack::RawWorkspace::SourceRecord& source,
    ImVec2 available, int& width, int& height) {
    const auto& camera = source.thumbnail.camera;
    if (!camera.bytes || width <= 0 || height <= 0 || available.x <= 0 || available.y <= 0) return 0;
    const double now = ImGui::GetTime();
    if (m_GalleryDetailSourceKey != source.relativePathKey) {
        m_GalleryDetailSourceKey = source.relativePathKey;
        m_GalleryDetailChangedAt = now;
        ++m_GalleryDetailRequest;
        const auto old = m_RawWorkspaceThumbnailTextures.find(kOriginalDetailKey);
        if (old != m_RawWorkspaceThumbnailTextures.end()) {
            ReleaseGalleryHdrThumbnail(old->second);
            QueueRawWorkspaceThumbnailTextureDelete(old->second.texture);
            m_RawWorkspaceThumbnailTextures.erase(old);
        }
    }
    if (now - m_GalleryDetailChangedAt < .18 || ImGui::IsAnyItemActive()) return 0;
    const auto dpi = ImGui::GetIO().DisplayFramebufferScale;
    const float scale = std::min(available.x * dpi.x / width, available.y * dpi.y / height);
    if (scale <= 1.f) return 0;
    const int demand = std::min(2048, int(std::ceil(std::max(width, height) * scale)));
    const int native = std::max(camera.width, camera.height);
    // Group nearby sizes while retaining a JPEG that already covers the view.
    int edge = std::min(2048, ((demand + 127) / 128) * 128);
    if (native >= demand) edge = std::min(native, edge);
    int detailWidth = 0, detailHeight = 0;
    const auto texture = GetRawWorkspaceThumbnailTexture(source, &detailWidth, &detailHeight, true, edge);
    if (texture) { width = detailWidth; height = detailHeight; }
    return texture;
}

unsigned int EditorModule::GetRawWorkspaceThumbnailTexture(
    const Stack::RawWorkspace::SourceRecord& source,
    int* outWidth,
    int* outHeight,
    bool prioritize, int detailEdge, const std::string* presetCardKey) {
    if (source.relativePathKey.rfind("project-overlay:", 0) != 0)
        return GetRawWorkspaceSourceThumbnailTexture(source, outWidth, outHeight,
            prioritize, detailEdge, presetCardKey);

    const auto found = m_GalleryPresetProjects.find(source.relativePathKey);
    const auto* project = found != m_GalleryPresetProjects.end() && found->second < m_RawWorkspace.sourceSetProjects.size()
        ? &m_RawWorkspace.sourceSetProjects[found->second] : nullptr;
    const auto* original = project ? (project->thumbnailOriginal ? project->thumbnailOriginal.get() :
        project->linkedOriginal ? project->linkedOriginal.get() : project->masterSource.get()) : nullptr;
    // Comparison rendering is an explicit user action. Merely browsing Saved
    // Projects, Brackets or Unlinked never loads and evaluates their graphs.
    if (project && m_GalleryComparisonEnabled && UsesGalleryPresetPreviews() &&
        !project->masterSource && !project->hasSavedBracketingResult) {
        if (const auto* preset = GalleryPresetThumbnail(source.relativePathKey)) {
            if (const auto texture = GetRawWorkspaceSourceThumbnailTexture(*preset,
                    outWidth, outHeight, prioritize, 0, &source.relativePathKey)) return texture;
        }
    }
    if (!project || !project->coverThumbnailCachePath.empty()) {
        if (const auto texture = GetRawWorkspaceSourceThumbnailTexture(source,
                outWidth, outHeight, prioritize, detailEdge, presetCardKey)) return texture;
    }
    // Use the original's canonical cache key, so a resident original appears
    // immediately and different saved versions share one decode/upload.
    if (original && original->relativePathKey != source.relativePathKey)
        return GetRawWorkspaceSourceThumbnailTexture(*original, outWidth, outHeight,
            prioritize, detailEdge, nullptr);
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    return 0;
}

unsigned int EditorModule::GetRawWorkspaceSourceThumbnailTexture(
    const Stack::RawWorkspace::SourceRecord& source, int* outWidth, int* outHeight,
    bool prioritize, int detailEdge, const std::string* presetCardKey) {
    // Grid, strip and folder cards all represent current viewport demand.
    // A stream of ordinary card completions must not jump an older visible
    // tile in the upload queue or bypass the per-frame request limit.
    if (m_PermanentGalleryWorkspace && !detailEdge) prioritize = false;
    if (outWidth) {
        *outWidth = 0;
    }
    if (outHeight) {
        *outHeight = 0;
    }

    const auto isDisplayable = [](const Stack::RawWorkspace::ThumbnailInfo& info) {
        return (info.status == Stack::RawWorkspace::ThumbnailStatus::Valid ||
                info.status == Stack::RawWorkspace::ThumbnailStatus::Ready) &&
            !info.absolutePath.empty();
    };
    const Stack::RawWorkspace::ThumbnailInfo* displayThumbnail = nullptr;
    // A camera JPEG is immediately displayable, even while neutral similarity
    // analysis is pending. Explicit project covers always own their pixels.
    const bool cameraBacked = !isDisplayable(source.thumbnail) &&
        !isDisplayable(source.transientThumbnail) && !source.absolutePath.empty();
    if (cameraBacked || isDisplayable(source.thumbnail)) {
        displayThumbnail = &source.thumbnail;
    } else if (isDisplayable(source.transientThumbnail)) {
        displayThumbnail = &source.transientThumbnail;
    }

    const auto& cacheKey = detailEdge ? kOriginalDetailKey : source.relativePathKey;
    const auto clearCached = [&]() {
        auto it = m_RawWorkspaceThumbnailTextures.find(cacheKey);
        if (it != m_RawWorkspaceThumbnailTextures.end()) {
            if(it->second.cancelTile)it->second.cancelTile->store(true);
            m_GalleryPendingThumbnailKeys.erase(cacheKey);
            ReleaseGalleryHdrThumbnail(it->second);
            if (it->second.texture != 0) {
                QueueRawWorkspaceThumbnailTextureDelete(it->second.texture);
                it->second.texture = 0;
            }
            m_RawWorkspaceThumbnailTextures.erase(it);
        }
    };

    if (displayThumbnail == nullptr) {
        clearCached();
        return 0;
    }

    const auto& displayPath = cameraBacked ? source.absolutePath : displayThumbnail->absolutePath;
    const auto displayStatus = cameraBacked ? Stack::RawWorkspace::ThumbnailStatus::Ready : displayThumbnail->status;
    const int demandFrame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    auto existing = m_RawWorkspaceThumbnailTextures.find(cacheKey);
    const bool projectCover = !cameraBacked && cacheKey.rfind("project-overlay:",0)==0;
    // Saved covers own their pixel identity. Different browsing consumers may
    // carry an original reference, or no reference, for the same project.
    const auto sameSource = [&](const RawWorkspaceThumbnailTexture& texture) {
        if (texture.detailEdge != detailEdge) return false;
        if (projectCover) return texture.absolutePath == displayPath;
        return texture.sourcePath.native() == source.absolutePath.native() &&
            texture.sourceModified == source.modifiedTimeTicks && texture.sourceBytes == source.fileSizeBytes &&
            (texture.sourceFingerprint == source.fingerprint || texture.sourceFingerprint.empty() || source.fingerprint.empty());
    };
    const auto setSource = [&](RawWorkspaceThumbnailTexture& entry) {
        entry.absolutePath = displayPath;
        if (!projectCover) entry.sourcePath = source.absolutePath;
        entry.sourceIdentity = source.absolutePath.lexically_normal().generic_u8string();
        entry.sourceModified = source.modifiedTimeTicks;
        entry.sourceBytes = source.fileSizeBytes;
        entry.sourceFingerprint = source.fingerprint;
        entry.detailEdge = detailEdge;
        entry.status = displayStatus;
    };
    if (existing != m_RawWorkspaceThumbnailTextures.end() && !sameSource(existing->second)) {
        // A file replacement or a different project reference cannot inherit
        // an earlier image's pixels or camera properties under the same key.
        ReleaseGalleryHdrThumbnail(existing->second);
        QueueRawWorkspaceThumbnailTextureDelete(existing->second.texture);
        if(existing->second.cancelTile)existing->second.cancelTile->store(true);
        m_GalleryPendingThumbnailKeys.erase(cacheKey);
        existing->second = {};
        setSource(existing->second);
        m_RawWorkspaceThumbnailTextureUploadQueue.erase(
            std::remove(m_RawWorkspaceThumbnailTextureUploadQueue.begin(),
                m_RawWorkspaceThumbnailTextureUploadQueue.end(), cacheKey),
            m_RawWorkspaceThumbnailTextureUploadQueue.end());
    }
    if (existing != m_RawWorkspaceThumbnailTextures.end()) {
        existing->second.lastDemandFrame = demandFrame;
        if (existing->second.sourceFingerprint.empty() && !source.fingerprint.empty())
            existing->second.sourceFingerprint = source.fingerprint;
        existing->second.lastUseSerial =
            ++m_RawWorkspaceThumbnailTextureUseSerial;
        if (prioritize) {
            existing->second.priority = true;
        }
        const bool cacheMatches =
            existing->second.absolutePath == displayPath;
        if (!cacheMatches) {
            // Keep the currently visible quick texture alive while the final
            // file is decoded and uploaded. The request generation prevents a
            // superseded decode from replacing the newer target.
            ReleaseGalleryHdrThumbnail(existing->second);
            existing->second.absolutePath = displayPath;
            existing->second.status = displayStatus;
            existing->second.requestGeneration =
                ++m_RawWorkspaceThumbnailTextureRequestGeneration;
            existing->second.decodeState = Async::TaskState::Idle;
            existing->second.uploadPending = false;
            existing->second.uploadQueued = false;
            existing->second.transparentCover = false;
            existing->second.decodedPixels.clear();
            existing->second.decodedWidth = 0;
            existing->second.decodedHeight = 0;
            m_RawWorkspaceThumbnailTextureUploadQueue.erase(
                std::remove(
                    m_RawWorkspaceThumbnailTextureUploadQueue.begin(),
                    m_RawWorkspaceThumbnailTextureUploadQueue.end(),
                    cacheKey),
                m_RawWorkspaceThumbnailTextureUploadQueue.end());
        } else if (existing->second.texture != 0) {
            existing->second.status = displayStatus;
            return GalleryThumbnailForDisplay(cacheKey,existing->second,source,presetCardKey,outWidth,outHeight);
        }
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureRequestFrame) {
            m_RawWorkspaceThumbnailTextureRequestFrame = frame;
            m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
        }
    }
    if (existing == m_RawWorkspaceThumbnailTextures.end()) {
        if(m_GalleryPendingThumbnailKeys.size()>=kRawWorkspaceThumbnailTextureMaxActiveDecodes ||
            m_RawWorkspaceThumbnailTextureRequestsThisFrame>=kRawWorkspaceThumbnailTextureDecodeRequestsPerFrame)return 0;
        RawWorkspaceThumbnailTexture entry;
        setSource(entry);
        entry.priority = prioritize;
        entry.lastUseSerial = ++m_RawWorkspaceThumbnailTextureUseSerial;
        existing = m_RawWorkspaceThumbnailTextures.emplace(cacheKey, std::move(entry)).first;
    }

    RawWorkspaceThumbnailTexture& entry = existing->second;
    const auto returnResidentTexture = [&]() {
        if (entry.texture != 0) {
            if (outWidth) {
                *outWidth = entry.width;
            }
            if (outHeight) {
                *outHeight = entry.height;
            }
        }
        return entry.texture;
    };
    if (entry.decodeState == Async::TaskState::Queued ||
        entry.decodeState == Async::TaskState::Running ||
        entry.uploadPending) {
        return returnResidentTexture();
    }
    if (entry.decodeState == Async::TaskState::Failed) {
        return returnResidentTexture();
    }
    if (entry.transparentCover) return 0;
    const bool foregroundGestureActive =
        IsRawWorkspaceUiInteractionActive() ||
        (ImGui::GetCurrentContext() && ImGui::IsAnyItemActive());
    if (foregroundGestureActive && !prioritize && !m_PermanentGalleryWorkspace) {
        return returnResidentTexture();
    }

    if (!prioritize &&
        m_RawWorkspaceThumbnailTextureRequestsThisFrame >=
        kRawWorkspaceThumbnailTextureDecodeRequestsPerFrame) {
        return returnResidentTexture();
    }
    const auto activeDecodes = m_GalleryPendingThumbnailKeys.size();
    if (activeDecodes >= kRawWorkspaceThumbnailTextureMaxActiveDecodes) {
        return returnResidentTexture();
    }
    Async::ForegroundIO::Demand();
    ++m_RawWorkspaceThumbnailTextureRequestsThisFrame;

    const std::uint64_t requestGeneration = ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    const std::uint64_t resetGeneration =
        m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
    entry.requestGeneration = requestGeneration;
    entry.lastDemandFrame = demandFrame;
    entry.decodeState = Async::TaskState::Queued;
    m_GalleryPendingThumbnailKeys.insert(cacheKey);
    entry.uploadQueued = false;
    entry.decodedPixels.clear();
    entry.decodedWidth = 0;
    entry.decodedHeight = 0;

    const std::string sourceKey = cacheKey;
    if (detailEdge) m_GalleryDetailRequest.store(requestGeneration);
    const std::filesystem::path sourcePath = projectCover ? std::filesystem::path{} : source.absolutePath;
    const std::filesystem::path thumbnailPath = displayPath;
    const Stack::RawWorkspace::ThumbnailStatus thumbnailStatus =
        displayStatus;
    bool submitted = false;
    try {
        entry.cancelTile = std::make_shared<std::atomic<bool>>(false);
        auto decode = [
            this,
            requestGeneration,
            resetGeneration,
            sourceKey,
            sourcePath,
            detailEdge,
            projectCover,
            direct = cameraBacked,
            bytes = source.fileSizeBytes,
            modified = source.modifiedTimeTicks,
            cancelTile = entry.cancelTile,
            analyzeTile = !detailEdge && cameraBacked && cacheKey.rfind("project-overlay:", 0) != 0 &&
                cacheKey.rfind("bracket-sidebar:", 0) != 0 && NeedsCameraGroupingPreview(source),
            thumbnailPath,
            thumbnailStatus,
            camera = cameraBacked ? displayThumbnail->camera : Stack::RawWorkspace::CameraThumbnail{}
        ]() mutable {
        auto resetRequested = [this, resetGeneration, requestGeneration, detailEdge, &cancelTile]() {
            return cancelTile->load(std::memory_order_relaxed) || (detailEdge && m_GalleryDetailRequest.load(std::memory_order_relaxed) != requestGeneration) || resetGeneration !=
                m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
        };
        if (resetRequested()) {
            return;
        }

        if(!Async::ForegroundIO::WaitForOpen(resetRequested))return;
        Async::ForegroundIO::TileRead foregroundRead;
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> decodedPixels;
        std::shared_ptr<const Stack::RawWorkspace::RawGallerySimilarityDescriptor> groupingPreview;
        try {
            if (direct) {
                using namespace Stack::RawWorkspace;
                const int edge = detailEdge ? detailEdge : std::min(512, kNeutralThumbnailMaxDimension);
                const auto tileKey = detailEdge ? std::string{} : Stack::GalleryTileCache::Key(sourcePath, bytes, modified, edge);
                const bool cached = !detailEdge && Stack::GalleryTileCache::Read(tileKey, decodedPixels, width, height);
                if (cached) {
                    // The tile is local: no original-drive probes on a warm hit.
                } else if (IsGalleryRasterPath(sourcePath)) {
                    if (!DecodeRasterThumbnail(sourcePath, edge, decodedPixels, width, height))
                        LoadGalleryRasterThumbnail(sourcePath, edge, decodedPixels, width, height);
                } else {
                    if (!camera.bytes) {
                        std::error_code ec;
                        const auto length = bytes ? bytes : std::filesystem::file_size(sourcePath, ec);
                        if (!ec) camera = FindCameraThumbnail(sourcePath, length, edge, resetRequested);
                    }
                    LoadOriginalGalleryPixels(sourcePath, camera, edge, detailEdge != 0, decodedPixels, width, height, resetRequested);
                }
                if (resetRequested()) return;
                if (!cached && !detailEdge && !decodedPixels.empty())
                    Stack::GalleryTileCache::Write(tileKey, decodedPixels, width, height);
                if (!decodedPixels.empty()) {
                    // The matcher expects display orientation, before the GL row flip.
                    if (analyzeTile)
                        groupingPreview = std::make_shared<RawGallerySimilarityDescriptor>(
                            BuildRawGallerySimilarityDescriptor(decodedPixels.data(), width, height));
                    const std::size_t stride = std::size_t(width) * 4;
                    for (int y = 0; y < height / 2; ++y) {
                        auto* row = decodedPixels.data() + std::size_t(y) * stride;
                        std::swap_ranges(row, row + stride, decodedPixels.data() + std::size_t(height - y - 1) * stride);
                    }
                }
            } else {
                stbi_set_flip_vertically_on_load_thread(1);
                std::ifstream file(thumbnailPath, std::ios::binary | std::ios::ate);
                const std::streamoff length = file.tellg();
                unsigned char* pixels = nullptr;
                if (length > 0 && length <= std::numeric_limits<int>::max()) {
                    std::vector<unsigned char> encoded(static_cast<std::size_t>(length));
                    file.seekg(0);
                    if (file.read(reinterpret_cast<char*>(encoded.data()), length))
                        pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(length), &width, &height, &channels, 4);
                }
                if (resetRequested()) {
                    if (pixels != nullptr) {
                        stbi_image_free(pixels);
                    }
                    return;
                }
                if (pixels != nullptr && width > 0 && height > 0) {
                    if (!Stack::PixelBuffer::CopyInterleavedPixels(
                            pixels, width, height, 4, decodedPixels)) {
                        width = 0;
                        height = 0;
                    }
                }
                if (pixels != nullptr) {
                    stbi_image_free(pixels);
                }
            }
        } catch (...) {
            decodedPixels.clear();
            width = 0;
            height = 0;
        }
        if (resetRequested()) {
            return;
        }

        // Empty documents may legitimately save a fully transparent cover.
        // Detect it once on the decode worker; drawing then uses a visible
        // project placeholder or borrows the original instead of an invisible
        // image. This does not alter or reject the saved document.
        bool transparentCover = projectCover && !decodedPixels.empty();
        if (transparentCover) {
            for (std::size_t alpha = 3; alpha < decodedPixels.size(); alpha += 4) {
                if (decodedPixels[alpha] != 0) { transparentCover = false; break; }
            }
            if (transparentCover) decodedPixels.clear();
        }

        ProjectTasks().PostToMain([
                this,
                requestGeneration,
                sourceKey,
                thumbnailPath,
                thumbnailStatus,
                width,
                height,
                transparentCover,
                camera, groupingPreview = std::move(groupingPreview),
                decodedPixels = std::move(decodedPixels)
            ]() mutable {
                auto it = m_RawWorkspaceThumbnailTextures.find(sourceKey);
                if (it == m_RawWorkspaceThumbnailTextures.end()) {
                    return;
                }
                RawWorkspaceThumbnailTexture& target = it->second;
                if (target.requestGeneration != requestGeneration ||
                    target.absolutePath != thumbnailPath ||
                    target.status != thumbnailStatus) {
                    return;
                }
                if (transparentCover) {
                    m_GalleryPendingThumbnailKeys.erase(sourceKey);
                    target.cancelTile.reset();
                    target.transparentCover = true;
                    target.decodeState = Async::TaskState::Ready;
                    target.uploadPending = target.uploadQueued = false;
                    QueueRawWorkspaceThumbnailTextureDelete(target.texture);
                    target.texture = 0;
                    target.width = target.height = 0;
                    m_RawWorkspaceThumbnailTextureTrimNeeded = true;
                    return;
                }
                if (decodedPixels.empty() || width <= 0 || height <= 0) {
                    m_GalleryPendingThumbnailKeys.erase(sourceKey);
                    m_RawWorkspaceThumbnailTextureTrimNeeded = true;
                    target.decodeState = Async::TaskState::Failed;
                    target.uploadPending = false;
                    target.uploadQueued = false;
                    std::vector<unsigned char>().swap(target.decodedPixels);
                    target.decodedWidth = 0;
                    target.decodedHeight = 0;
                    target.status = Stack::RawWorkspace::ThumbnailStatus::Stale;
                    const auto card = m_GalleryPresetProjects.find(sourceKey);
                    if (card != m_GalleryPresetProjects.end() && card->second < m_RawWorkspace.sourceSetProjects.size()) {
                        auto& project = m_RawWorkspace.sourceSetProjects[card->second];
                        if (project.coverThumbnailCachePath == thumbnailPath)
                            project.coverThumbnailCachePath.clear();
                        InvalidateRawWorkspaceGalleryPresentation(false);
                    } else {
                        HandleRawWorkspaceThumbnailDecodeFailure(sourceKey, thumbnailPath);
                    }
                    return;
                }

                target.cancelTile.reset();
                target.decodedPixels = std::move(decodedPixels);
                if (groupingPreview && groupingPreview->IsValid()) {
                    // A folder card or an old upload can share the key. Only
                    // publish to the exact original in the current catalog.
                    const auto found = std::find_if(m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
                        [&](const auto& record) { return record.relativePathKey == sourceKey; });
                    if (found != m_RawWorkspace.sources.end() && found->absolutePath == target.sourcePath &&
                        (target.sourceFingerprint.empty() || found->fingerprint == target.sourceFingerprint) && found->modifiedTimeTicks == target.sourceModified &&
                        found->fileSizeBytes == target.sourceBytes && NeedsCameraGroupingPreview(*found)) {
                        if (found->browsePreview && camera.bytes) found->thumbnail.camera = camera;
                        found->transientThumbnail.similarityDescriptor = *groupingPreview;
                        RequestRawWorkspaceSimilarityRebuild();
                    }
                }
                // A generated file is not repaired until the decoder can read it.
                m_RawWorkspaceThumbnailDecodeRepairAttempts.erase(sourceKey);
                target.decodedWidth = width;
                target.decodedHeight = height;
                try {
                    if (!target.uploadQueued) {
                        if (target.priority) {
                            m_RawWorkspaceThumbnailTextureUploadQueue.push_front(sourceKey);
                        } else {
                            m_RawWorkspaceThumbnailTextureUploadQueue.push_back(sourceKey);
                        }
                        target.uploadQueued = true;
                    }
                    target.decodeState = Async::TaskState::Applying;
                    target.uploadPending = true;
                } catch (...) {
                    m_GalleryPendingThumbnailKeys.erase(sourceKey);
                    m_RawWorkspaceThumbnailTextureTrimNeeded = true;
                    target.decodeState = Async::TaskState::Failed;
                    target.uploadPending = false;
                    target.uploadQueued = false;
                    std::vector<unsigned char>().swap(target.decodedPixels);
                    target.decodedWidth = 0;
                    target.decodedHeight = 0;
                }
            });

        };
        submitted = ProjectTasks().SubmitThumbnail(entry.cancelTile, std::move(decode));
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        auto rejected = m_RawWorkspaceThumbnailTextures.find(sourceKey);
        if (rejected != m_RawWorkspaceThumbnailTextures.end() &&
            rejected->second.requestGeneration == requestGeneration) {
            rejected->second.decodeState = Async::TaskState::Idle;
            rejected->second.cancelTile.reset();
            m_GalleryPendingThumbnailKeys.erase(sourceKey);
            rejected->second.uploadPending = false;
            rejected->second.uploadQueued = false;
            if(!rejected->second.texture) {m_RawWorkspaceThumbnailTextures.erase(rejected);return 0;}
        }
    }

    return returnResidentTexture();
}

std::shared_ptr<const Raw::RawMetadata> EditorModule::GetRawWorkspaceThumbnailMetadata(
    const std::string& sourceKey) {
    auto found = m_RawWorkspaceThumbnailTextures.find(sourceKey);
    if (found == m_RawWorkspaceThumbnailTextures.end()) return {};
    auto& entry = found->second;
    if (entry.metadataTicket || entry.sourcePath.empty()) return entry.metadata;
    // Only a consumer of camera properties requests them. Scrolling past a
    // tile never opens its RAW again just to populate an unused property card.
    const auto ticket = ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    entry.metadataTicket = ticket;
    ProjectTasks().Submit("Reading camera properties", [this, ticket, sourceKey, path = entry.sourcePath] {
        std::shared_ptr<const Raw::RawMetadata> metadata;
        try {
            auto value = std::make_shared<Raw::RawMetadata>();
            if (Raw::RawLoader::LoadMetadata(path.u8string(), *value)) metadata = std::move(value);
        } catch (...) { }
        ProjectTasks().PostToMain([this, ticket, sourceKey, metadata = std::move(metadata)]() mutable {
            auto target = m_RawWorkspaceThumbnailTextures.find(sourceKey);
            if (target != m_RawWorkspaceThumbnailTextures.end() && target->second.metadataTicket == ticket)
                target->second.metadata = std::move(metadata);
        });
    });
    return {};
}

void EditorModule::PrioritizeRawWorkspaceThumbnailSource(
    const std::string& sourceKey) {
    if (sourceKey.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
    if (m_RawWorkspaceThumbnailWorkerActive) {
        m_RawWorkspaceThumbnailScheduler.Promote(sourceKey);
    }
}

void EditorModule::PrioritizeRawWorkspaceThumbnailSources(
    const std::vector<std::string>& sourceKeys) {
    if (sourceKeys.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
    if (!m_RawWorkspaceThumbnailWorkerActive) {
        return;
    }
    m_RawWorkspaceThumbnailScheduler.Promote(sourceKeys);
}

void EditorModule::QueueRawWorkspaceThumbnailRepair(
    const std::string& sourceKey) {
    if (sourceKey.empty()) {
        return;
    }

    const auto sourceIt = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& candidate) {
            return candidate.relativePathKey == sourceKey;
        });
    if (sourceIt == m_RawWorkspace.sources.end()) {
        return;
    }

    RawWorkspaceThumbnailWorkItem item;
    item.sourceIndex = static_cast<std::size_t>(
        std::distance(m_RawWorkspace.sources.begin(), sourceIt));
    item.absolutePath = sourceIt->absolutePath;
    item.relativePath = sourceIt->relativePath;
    item.sourceKey = sourceIt->relativePathKey;
    item.fileName = sourceIt->fileName;
    item.stem = sourceIt->stem;
    item.extension = sourceIt->extension;
    item.parentFolderKey = sourceIt->parentFolderKey;
    item.fileSizeBytes = sourceIt->fileSizeBytes;
    item.modifiedTimeTicks = sourceIt->modifiedTimeTicks;
    item.fingerprint = sourceIt->fingerprint;
    item.sourceWorkspaceRoot = sourceIt->sourceWorkspaceRoot.empty() ? m_RawWorkspace.workspaceRoot : sourceIt->sourceWorkspaceRoot;
    item.sourceRelativePathKey = sourceIt->sourceRelativePathKey;
    item.camera = sourceIt->thumbnail.camera;

    bool queuedOnActiveWorker = false;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        if (m_RawWorkspaceThumbnailWorkerActive) {
            m_RawWorkspaceThumbnailScheduler.PushFront(std::move(item));
            queuedOnActiveWorker = true;
        }
    }

    if (!queuedOnActiveWorker) {
        RequestRawWorkspaceThumbnailGeneration();
    }
}

void EditorModule::HandleRawWorkspaceThumbnailDecodeFailure(
    const std::string& sourceKey,
    const std::filesystem::path& thumbnailPath) {
    auto sourceIt = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& candidate) {
            return candidate.relativePathKey == sourceKey &&
                candidate.thumbnail.absolutePath == thumbnailPath;
        });
    if (sourceIt == m_RawWorkspace.sources.end()) {
        return;
    }

    int& attempts = m_RawWorkspaceThumbnailDecodeRepairAttempts[sourceKey];
    if (attempts >= 1) {
        sourceIt->thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Failed;
        sourceIt->thumbnail.errorMessage =
            "RAW thumbnail could not be decoded after repair.";
        InvalidateRawWorkspaceGalleryPresentation(false);
        PersistRawWorkspaceCatalog();
        return;
    }

    ++attempts;
    sourceIt->thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Stale;
    sourceIt->thumbnail.errorMessage =
        "RAW thumbnail failed to decode; regenerating.";
    InvalidateRawWorkspaceGalleryPresentation(false);
    PersistRawWorkspaceCatalog();
    QueueRawWorkspaceThumbnailRepair(sourceKey);
}

void EditorModule::CancelRawWorkspaceThumbnailDemand() {
    if(m_GalleryPendingThumbnailKeys.empty())return;
    for(const auto& key:m_GalleryPendingThumbnailKeys) {
        const auto found=m_RawWorkspaceThumbnailTextures.find(key);
        if(found==m_RawWorkspaceThumbnailTextures.end())continue;
        auto& entry=found->second;
        if(entry.cancelTile)entry.cancelTile->store(true);
        entry.cancelTile.reset();
        entry.requestGeneration=++m_RawWorkspaceThumbnailTextureRequestGeneration;
        entry.uploadPending=entry.uploadQueued=false;entry.decodeState=Async::TaskState::Idle;
        std::vector<unsigned char>().swap(entry.decodedPixels);
        if(!entry.texture)m_RawWorkspaceThumbnailTextures.erase(found);
    }
    m_GalleryPendingThumbnailKeys.clear();
    m_RawWorkspaceThumbnailTextureUploadQueue.clear();
}

void EditorModule::PumpRawWorkspaceThumbnailTextureUploads() {
    TickRawWorkspacePersistence();
    TickRawWorkspacePreviewStaging();
    PumpRawWorkspaceThumbnailTextureDeletes();

    // Once per frame, visit only outstanding demand (at most 24 entries).
    // Old queued/running reads observe the token; old decoded packets are freed
    // before new requests are admitted. No catalog or resident-cache scan.
    const int demandFrame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    if (demandFrame >= 0 && demandFrame != m_GalleryThumbnailRetireFrame) {
        m_GalleryThumbnailRetireFrame = demandFrame;
        for (auto pending=m_GalleryPendingThumbnailKeys.begin(); pending!=m_GalleryPendingThumbnailKeys.end();) {
            auto found=m_RawWorkspaceThumbnailTextures.find(*pending);
            if(found==m_RawWorkspaceThumbnailTextures.end()) {pending=m_GalleryPendingThumbnailKeys.erase(pending);continue;}
            auto& entry=found->second;
            if(entry.lastDemandFrame+2>=demandFrame) {++pending;continue;}
            if(entry.cancelTile) entry.cancelTile->store(true);
            entry.cancelTile.reset();
            entry.requestGeneration=++m_RawWorkspaceThumbnailTextureRequestGeneration;
            entry.uploadPending=entry.uploadQueued=false;
            entry.decodeState=Async::TaskState::Idle;
            std::vector<unsigned char>().swap(entry.decodedPixels);
            if(!entry.texture) m_RawWorkspaceThumbnailTextures.erase(found);
            pending=m_GalleryPendingThumbnailKeys.erase(pending);
        }
        auto& uploads=m_RawWorkspaceThumbnailTextureUploadQueue;
        uploads.erase(std::remove_if(uploads.begin(),uploads.end(),[&](const auto& key){
            const auto it=m_RawWorkspaceThumbnailTextures.find(key);
            return it==m_RawWorkspaceThumbnailTextures.end()||!it->second.uploadQueued;
        }),uploads.end());
    }
    CancelObsoleteGalleryHdrJob();
    const bool hdrReady=m_GalleryHdrJob && m_GalleryHdrJob->complete.load(std::memory_order_acquire);
    if (m_RawWorkspaceThumbnailTextureUploadQueue.empty() && !hdrReady) {
        TrimRawWorkspaceThumbnailTextureCache();
        return;
    }

    const bool priorityUploadPending = std::any_of(
        m_RawWorkspaceThumbnailTextureUploadQueue.begin(),
        m_RawWorkspaceThumbnailTextureUploadQueue.end(),
        [&](const std::string& sourceKey) {
            const auto it = m_RawWorkspaceThumbnailTextures.find(sourceKey);
            return it != m_RawWorkspaceThumbnailTextures.end() &&
                it->second.priority;
        });
    const bool foregroundGestureActive =
        m_RawWorkspaceLabUi.previewPanning ||
        m_RawWorkspaceLabUi.colorWarpInteractionActive ||
        (ImGui::GetCurrentContext() && ImGui::IsAnyItemActive());
    const bool filmstripLayoutMoving =
        m_RawWorkspaceRootTabActive &&
        m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip &&
        (m_RawWorkspaceLabFilmstripResizeDirty ||
         m_RawWorkspaceLabRailResizeDirty ||
         (m_RawWorkspaceLabEditingSurfaceReveal > 0.01f &&
          m_RawWorkspaceLabEditingSurfaceReveal < 0.99f) ||
         std::abs(m_RawWorkspaceLabFilmstripDrawerAnimatedHeight -
             (m_RawWorkspaceLabFilmstripDrawerState.open
                 ? m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight
                 : m_RawWorkspaceLabAnimatedFilmstripHeight)) > 0.5f);
    // The Gallery is the foreground surface. Keep drawing visible tiles while
    // its rails settle or the scrollbar is held, within the upload budget below.
    if (!m_PermanentGalleryWorkspace && (foregroundGestureActive || filmstripLayoutMoving) &&
        !priorityUploadPending) {
        return;
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureUploadFrame) {
            m_RawWorkspaceThumbnailTextureUploadFrame = frame;
            m_RawWorkspaceThumbnailTextureUploadsThisFrame = 0;
            m_RawWorkspaceThumbnailTextureUploadBytesThisFrame = 0;
        }
    }

    const std::size_t uploadLimit = filmstripLayoutMoving
        ? 1u : kRawWorkspaceThumbnailTextureUploadsPerFrame;
    const auto uploadStarted = std::chrono::steady_clock::now();
    const auto* viewport = ImGui::GetCurrentContext() ? ImGui::GetMainViewport() : nullptr;
    const int refresh = viewport ? DisplayRefreshRate::ForWindow(
        static_cast<GLFWwindow*>(viewport->PlatformHandle)) : 60;
    const double uploadBudgetMs = Stack::App::MainThreadCompletionBudgetMs(refresh);
    while (m_RawWorkspaceThumbnailTextureUploadsThisFrame < uploadLimit &&
        !m_RawWorkspaceThumbnailTextureUploadQueue.empty()) {
        auto queueItem = m_RawWorkspaceThumbnailTextureUploadQueue.begin();
        const auto priorityItem = std::find_if(
            queueItem,
            m_RawWorkspaceThumbnailTextureUploadQueue.end(),
            [&](const std::string& sourceKey) {
                const auto candidate =
                    m_RawWorkspaceThumbnailTextures.find(sourceKey);
                return candidate != m_RawWorkspaceThumbnailTextures.end() &&
                    candidate->second.priority;
            });
        if (priorityItem != m_RawWorkspaceThumbnailTextureUploadQueue.end()) {
            queueItem = priorityItem;
        }
        const std::string sourceKey = std::move(*queueItem);
        m_RawWorkspaceThumbnailTextureUploadQueue.erase(queueItem);
        auto it = m_RawWorkspaceThumbnailTextures.find(sourceKey);
        if (it == m_RawWorkspaceThumbnailTextures.end()) {
            continue;
        }
        RawWorkspaceThumbnailTexture& pendingEntry = it->second;
        pendingEntry.uploadQueued = false;
        if (!pendingEntry.uploadPending ||
            pendingEntry.decodedPixels.empty() ||
            pendingEntry.decodedWidth <= 0 ||
            pendingEntry.decodedHeight <= 0) {
            pendingEntry.uploadPending = false;
            continue;
        }
        const std::size_t uploadBytes =
            static_cast<std::size_t>(pendingEntry.decodedWidth) *
            static_cast<std::size_t>(pendingEntry.decodedHeight) * 4u;
        if (m_RawWorkspaceThumbnailTextureUploadsThisFrame > 0u &&
            m_RawWorkspaceThumbnailTextureUploadBytesThisFrame +
                    uploadBytes >
                kRawWorkspaceThumbnailTextureUploadBytesPerFrame) {
            // Keep the decoded packet queued; foreground work gets the next
            // frame before another upload consumes GL bandwidth.
            m_RawWorkspaceThumbnailTextureUploadQueue.push_front(sourceKey);
            pendingEntry.uploadQueued = true;
            break;
        }

        const unsigned int texture = GLHelpers::CreateTextureFromPixels(
            pendingEntry.decodedPixels.data(),
            pendingEntry.decodedWidth,
            pendingEntry.decodedHeight,
            4,
            true);
        std::vector<unsigned char>().swap(pendingEntry.decodedPixels);
        pendingEntry.uploadPending = false;
        if (texture == 0) {
            m_GalleryPendingThumbnailKeys.erase(sourceKey);
            pendingEntry.decodeState = Async::TaskState::Failed;
            pendingEntry.decodedWidth = 0;
            pendingEntry.decodedHeight = 0;
            return;
        }

        if (pendingEntry.texture != 0) {
            QueueRawWorkspaceThumbnailTextureDelete(pendingEntry.texture);
        }
        pendingEntry.texture = texture;
        m_RawWorkspaceThumbnailTextureTrimNeeded = true;
        pendingEntry.width = pendingEntry.decodedWidth;
        pendingEntry.height = pendingEntry.decodedHeight;
        pendingEntry.decodedWidth = 0;
        pendingEntry.decodedHeight = 0;
        m_GalleryPendingThumbnailKeys.erase(sourceKey);
        pendingEntry.decodeState = Async::TaskState::Idle;
        pendingEntry.priority = false;
        ++m_RawWorkspaceThumbnailTextureUploadsThisFrame;
        m_RawWorkspaceThumbnailTextureUploadBytesThisFrame += uploadBytes;
        if (std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - uploadStarted).count() >= uploadBudgetMs) {
            // A second pump in the same frame must not renew this allowance.
            m_RawWorkspaceThumbnailTextureUploadsThisFrame = kRawWorkspaceThumbnailTextureUploadsPerFrame;
            break;
        }
    }
    // Quick tiles keep priority. The second rendition shares their per-frame
    // transfer count, byte limit and elapsed upload allowance.
    if (hdrReady && m_RawWorkspaceThumbnailTextureUploadsThisFrame < uploadLimit &&
        m_RawWorkspaceThumbnailTextureUploadBytesThisFrame + m_GalleryHdrJob->pixels.size()*sizeof(float)
            <= kRawWorkspaceThumbnailTextureUploadBytesPerFrame &&
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStarted).count()<uploadBudgetMs)
        UploadGalleryHdrThumbnail();
    TrimRawWorkspaceThumbnailTextureCache();
}

void EditorModule::QueueRawWorkspaceThumbnailTextureDelete(unsigned int texture) {
    if (texture != 0) {
        const auto pinned=m_GalleryThumbnailLeases.find(texture);
        if(pinned!=m_GalleryThumbnailLeases.end()) {
            if(auto lease=pinned->second.lock()) {
                m_RetiredGalleryThumbnailBytes+=lease->bytes;
                m_RetiredGalleryThumbnails.push_back({std::move(lease),ImGui::GetCurrentContext()?ImGui::GetFrameCount():-1});
                m_GalleryThumbnailLeases.erase(pinned);
                return;
            }
            m_GalleryThumbnailLeases.erase(pinned);
        }
        m_RawWorkspaceThumbnailTextureDeleteQueue.push_back(texture);
    }
}

void EditorModule::PumpRawWorkspaceThumbnailTextureDeletes(bool drainAll) {
    // Only the small set of navigation leases is inspected here. Expiration
    // retries budget trimming without a full-cache scan on every motion frame.
    for(auto it=m_GalleryThumbnailLeases.begin();it!=m_GalleryThumbnailLeases.end();) {
        if(it->second.expired()) {
            it=m_GalleryThumbnailLeases.erase(it);
            m_RawWorkspaceThumbnailTextureTrimNeeded=true;
        } else ++it;
    }
    const int currentFrame=ImGui::GetCurrentContext()?ImGui::GetFrameCount():-1;
    for(auto it=m_RetiredGalleryThumbnails.begin();it!=m_RetiredGalleryThumbnails.end();) {
        if(it->lease.use_count()==1 && (drainAll || it->submittedFrame<currentFrame)) {
            m_RawWorkspaceThumbnailTextureDeleteQueue.push_back(it->lease->texture);
            m_RetiredGalleryThumbnailBytes-=it->lease->bytes;
            it=m_RetiredGalleryThumbnails.erase(it);
            m_RawWorkspaceThumbnailTextureTrimNeeded=true;
        } else ++it;
    }
    if (m_RawWorkspaceThumbnailTextureDeleteQueue.empty()) {
        return;
    }

    if (drainAll) {
        while (!m_RawWorkspaceThumbnailTextureDeleteQueue.empty()) {
            unsigned int texture = m_RawWorkspaceThumbnailTextureDeleteQueue.front();
            m_RawWorkspaceThumbnailTextureDeleteQueue.pop_front();
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
        m_RawWorkspaceThumbnailTextureDeleteFrame = -1;
        m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
        return;
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureDeleteFrame) {
            m_RawWorkspaceThumbnailTextureDeleteFrame = frame;
            m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
        }
    }

    while (!m_RawWorkspaceThumbnailTextureDeleteQueue.empty() &&
        m_RawWorkspaceThumbnailTextureDeletesThisFrame <
            kRawWorkspaceThumbnailTextureDeletesPerFrame) {
        unsigned int texture = m_RawWorkspaceThumbnailTextureDeleteQueue.front();
        m_RawWorkspaceThumbnailTextureDeleteQueue.pop_front();
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
        ++m_RawWorkspaceThumbnailTextureDeletesThisFrame;
    }
}

void EditorModule::TrimRawWorkspaceThumbnailTextureCache() {
    // Only an upload can increase residency. Once within budget, ordinary
    // drawing/hovering must not rescan the entire thumbnail map every frame.
    if (!m_RawWorkspaceThumbnailTextureTrimNeeded) return;
    for(auto it=m_RawWorkspaceThumbnailTextures.begin();it!=m_RawWorkspaceThumbnailTextures.end();) {
        const auto& entry=it->second;
        if(!entry.texture && !entry.cancelTile && !entry.uploadPending &&
            ImGui::GetCurrentContext() && entry.lastDemandFrame+2<ImGui::GetFrameCount())it=m_RawWorkspaceThumbnailTextures.erase(it);
        else ++it;
    }
    // Gallery keeps a browsing history across both layouts. The byte limit
    // matters more than count for portrait and unusually large thumbnails.
    const std::size_t maxCount = m_PermanentGalleryWorkspace
        ? kGalleryThumbnailMaxResidentCount : kRawWorkspaceThumbnailTextureMaxResidentCount;
    const std::size_t maxBytes = m_PermanentGalleryWorkspace
        ? kGalleryThumbnailMaxResidentBytes : kRawWorkspaceThumbnailTextureMaxResidentBytes;
    struct ResidentTexture {
        // Map keys remain stable until their own candidate is erased below.
        const std::string* key = nullptr;
        std::uint64_t lastUseSerial = 0;
        std::size_t bytes = 0;
    };

    const auto cachedBytes=[](const RawWorkspaceThumbnailTexture& entry) {
        return RawWorkspaceThumbnailResidentBytes(entry.width,entry.height) +
            RawWorkspaceThumbnailResidentBytes(entry.hdrWidth,entry.hdrHeight,8);
    };
    std::size_t residentCount = m_RetiredGalleryThumbnails.size();
    std::size_t residentBytes = m_RetiredGalleryThumbnailBytes;
    for (const auto& [key, entry] : m_RawWorkspaceThumbnailTextures) {
        if (entry.texture == 0 || entry.width <= 0 || entry.height <= 0) {
            continue;
        }
        const std::size_t bytes = cachedBytes(entry);
        residentCount += entry.hdrTexture ? 2 : 1;
        residentBytes += bytes;
    }
    if (residentCount <= maxCount && residentBytes <= maxBytes) {
        m_RawWorkspaceThumbnailTextureTrimNeeded = false;
        return;
    }

    std::vector<ResidentTexture> resident;
    resident.reserve(residentCount);
    for (const auto& [key, entry] : m_RawWorkspaceThumbnailTextures) {
        if (entry.texture != 0 && entry.width > 0 && entry.height > 0) {
            resident.push_back({
                &key,
                entry.lastUseSerial,
                cachedBytes(entry)
            });
        }
    }
    std::sort(
        resident.begin(), resident.end(),
        [](const ResidentTexture& left, const ResidentTexture& right) {
            return left.lastUseSerial < right.lastUseSerial;
        });
    for (const ResidentTexture& candidate : resident) {
        if (residentCount <= maxCount && residentBytes <= maxBytes) {
            break;
        }
        auto entry = m_RawWorkspaceThumbnailTextures.find(*candidate.key);
        if (entry == m_RawWorkspaceThumbnailTextures.end() ||
            entry->second.texture == 0 || entry->second.uploadPending ||
            (m_GalleryThumbnailLeases.count(entry->second.texture) &&
                !m_GalleryThumbnailLeases.at(entry->second.texture).expired()) ||
            (m_GalleryThumbnailLeases.count(entry->second.hdrTexture) &&
                !m_GalleryThumbnailLeases.at(entry->second.hdrTexture).expired()) ||
            (entry->second.lastVisibleFrame >= 0 &&
                entry->second.lastVisibleFrame >= ImGui::GetFrameCount() - 1) ||
            m_RawWorkspaceGalleryLayoutAnimation.Protects(*candidate.key)) {
            continue;
        }
        residentCount -= entry->second.hdrTexture ? 2 : 1;
        ReleaseGalleryHdrThumbnail(entry->second);
        QueueRawWorkspaceThumbnailTextureDelete(entry->second.texture);
        entry->second.texture = 0;
        m_RawWorkspaceThumbnailTextures.erase(entry);
        residentBytes = residentBytes > candidate.bytes
            ? residentBytes - candidate.bytes
            : 0;
    }
    // Protected visible/leased entries cannot be reclaimed now. Retry on the
    // next upload or lease retirement, instead of sorting the map every frame.
    m_RawWorkspaceThumbnailTextureTrimNeeded = false;
}

void EditorModule::ClearRawWorkspaceThumbnailTextures(bool immediate) {
    if(m_GalleryHdrJob) m_GalleryHdrJob->canceled.store(true);
    m_RawWorkspaceGalleryLayoutAnimation.Reset();
    m_RawWorkspaceThumbnailTextureResetGeneration.fetch_add(1, std::memory_order_relaxed);
    ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    m_GalleryDetailSourceKey.clear();
    ++m_GalleryDetailRequest;
    m_RawWorkspaceThumbnailTextureUseSerial = 0;
    m_RawWorkspaceThumbnailTextureTrimNeeded = false;
    m_RawWorkspaceThumbnailTextureRequestFrame = -1;
    m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
    m_RawWorkspaceThumbnailTextureUploadQueue.clear();
    m_RawWorkspaceThumbnailTextureUploadFrame = -1;
    m_RawWorkspaceThumbnailTextureUploadsThisFrame = 0;
    m_RawWorkspaceThumbnailTextureUploadBytesThisFrame = 0;
    m_RawWorkspaceThumbnailTextureDeleteFrame = -1;
    m_RawWorkspaceThumbnailTextureDeletesThisFrame = 0;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        m_RawWorkspaceThumbnailScheduler.Clear();
        m_RawWorkspaceThumbnailWorkerActive = false;
        m_RawWorkspaceThumbnailDecodeRepairAttempts.clear();
    }
    for (auto& [key, entry] : m_RawWorkspaceThumbnailTextures) {
        (void)key;
        ReleaseGalleryHdrThumbnail(entry);
        if (entry.texture != 0) {
            QueueRawWorkspaceThumbnailTextureDelete(entry.texture);
            entry.texture = 0;
        }
        if(entry.cancelTile) entry.cancelTile->store(true);
        entry.decodedPixels.clear();
        entry.uploadPending = false;
    }
    m_RawWorkspaceThumbnailTextures.clear();
    m_GalleryPendingThumbnailKeys.clear();
    if (immediate) {
        PumpRawWorkspaceThumbnailTextureDeletes(true);
    }
}

