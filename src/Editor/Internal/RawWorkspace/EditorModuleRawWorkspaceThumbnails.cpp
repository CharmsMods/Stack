#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Editor/GalleryPresetPreview.h"
#include "Persistence/ProjectIndex.h"
#include "App/UiFrameBudget.h"
#include "Utils/DisplayRefreshRate.h"
#include "Raw/RawLoader.h"
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

std::size_t RawWorkspaceThumbnailResidentBytes(int width, int height) {
    const std::size_t base = static_cast<std::size_t>(std::max(0, width)) *
        static_cast<std::size_t>(std::max(0, height)) * 4u;
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
    if (path.empty()) { m_GalleryComparisonImage.reset(); m_GalleryComparisonName.clear(); return; }
    const auto owner=m_Project;
    ProjectTasks().Submit("Loading comparison image",[this,owner,generation,path] {
        std::shared_ptr<const Raw::RawImageData> image;
        std::string error;
        try { image=Stack::GalleryPresets::Comparison(path); } catch(const std::exception& e) { error=e.what(); }
        owner->tasks.PostToMain([this,generation,image,path,error] {
            if (generation!=m_GalleryComparisonGeneration) return;
            if (!image) { PostNotification(UiNotificationSeverity::Error,error,"preset-comparison"); return; }
            ++m_GalleryComparisonGeneration;
            m_GalleryComparisonImage=image; m_GalleryComparisonName=path.filename().u8string();
            m_GalleryPresetThumbnails.clear();
        });
    });
}

const Stack::RawWorkspace::SourceRecord* EditorModule::GalleryPresetThumbnail(const std::string& key) {
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
    bool prioritize, int detailEdge) {
    bool preview=UsesGalleryPresetPreviews();
    if (source.relativePathKey.compare(0,16,"project-overlay:")==0) {
        if (!preview && m_RawWorkspaceGalleryContentMode==Stack::RawWorkspace::GalleryContentMode::Bracket) {
            const auto card=m_GalleryPresetProjects.find(source.relativePathKey);
            preview=card!=m_GalleryPresetProjects.end() && card->second<m_RawWorkspace.sourceSetProjects.size() &&
                m_RawWorkspace.sourceSetProjects[card->second].standaloneMaster;
        }
    } else preview=false;
    if (preview) {
        if (const auto* preset=GalleryPresetThumbnail(source.relativePathKey))
            return GetRawWorkspaceThumbnailTexture(*preset,outWidth,outHeight,prioritize);
        if (outWidth) *outWidth=0;
        if (outHeight) *outHeight=0;
        return 0;
    }
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
    const bool cameraBacked = ((source.browsePreview && !isDisplayable(source.thumbnail)) ||
        (source.thumbnail.camera.bytes && source.thumbnail.absolutePath.empty())) &&
        !source.absolutePath.empty();
    if (cameraBacked || isDisplayable(source.thumbnail)) {
        displayThumbnail = &source.thumbnail;
    } else if (isDisplayable(source.transientThumbnail)) {
        displayThumbnail = &source.transientThumbnail;
    }

    const auto& cacheKey = detailEdge ? kOriginalDetailKey : source.relativePathKey;
    const auto clearCached = [&]() {
        auto it = m_RawWorkspaceThumbnailTextures.find(cacheKey);
        if (it != m_RawWorkspaceThumbnailTextures.end()) {
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
            (texture.sourceFingerprint == source.fingerprint || (texture.sourceFingerprint.empty() && !source.fingerprint.empty()));
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
        QueueRawWorkspaceThumbnailTextureDelete(existing->second.texture);
        existing->second = {};
        setSource(existing->second);
        m_RawWorkspaceThumbnailTextureUploadQueue.erase(
            std::remove(m_RawWorkspaceThumbnailTextureUploadQueue.begin(),
                m_RawWorkspaceThumbnailTextureUploadQueue.end(), cacheKey),
            m_RawWorkspaceThumbnailTextureUploadQueue.end());
    }
    if (existing != m_RawWorkspaceThumbnailTextures.end()) {
        if (existing->second.cancelTile || existing->second.uploadPending) existing->second.lastDemandFrame = demandFrame;
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
            existing->second.absolutePath = displayPath;
            existing->second.status = displayStatus;
            existing->second.requestGeneration =
                ++m_RawWorkspaceThumbnailTextureRequestGeneration;
            existing->second.decodeState = Async::TaskState::Idle;
            existing->second.uploadPending = false;
            existing->second.uploadQueued = false;
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
            if (outWidth) {
                *outWidth = existing->second.width;
            }
            if (outHeight) {
                *outHeight = existing->second.height;
            }
            return existing->second.texture;
        }
    }

    if (existing == m_RawWorkspaceThumbnailTextures.end()) {
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
    const bool foregroundGestureActive =
        IsRawWorkspaceUiInteractionActive() ||
        (ImGui::GetCurrentContext() && ImGui::IsAnyItemActive());
    if (foregroundGestureActive && !prioritize && !m_PermanentGalleryWorkspace) {
        return returnResidentTexture();
    }

    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (frame != m_RawWorkspaceThumbnailTextureRequestFrame) {
            m_RawWorkspaceThumbnailTextureRequestFrame = frame;
            m_RawWorkspaceThumbnailTextureRequestsThisFrame = 0;
        }
    }
    if (!prioritize &&
        m_RawWorkspaceThumbnailTextureRequestsThisFrame >=
        kRawWorkspaceThumbnailTextureDecodeRequestsPerFrame) {
        return returnResidentTexture();
    }
    auto activeDecodes = static_cast<std::size_t>(std::count_if(
        m_RawWorkspaceThumbnailTextures.begin(),
        m_RawWorkspaceThumbnailTextures.end(),
        [](const auto& item) {
            const RawWorkspaceThumbnailTexture& texture = item.second;
            return texture.decodeState == Async::TaskState::Queued ||
                texture.decodeState == Async::TaskState::Running ||
                texture.decodeState == Async::TaskState::Applying ||
                texture.uploadPending;
        }));
    if (activeDecodes >= kRawWorkspaceThumbnailTextureMaxActiveDecodes && m_PermanentGalleryWorkspace && demandFrame >= 0) {
        // Release slots from the old viewport before admitting its replacement.
        bool discardedUploads = false;
        for (auto& item : m_RawWorkspaceThumbnailTextures) {
            auto& old = item.second;
            if (old.lastDemandFrame + 1 >= demandFrame ||
                (!old.uploadPending && !(old.decodeState == Async::TaskState::Queued && old.cancelTile))) continue;
            if (old.cancelTile) old.cancelTile->store(true);
            old.cancelTile.reset();
            if (old.uploadPending) {
                discardedUploads = true;
                old.uploadPending = old.uploadQueued = false;
                std::vector<unsigned char>().swap(old.decodedPixels);
            }
            old.requestGeneration = ++m_RawWorkspaceThumbnailTextureRequestGeneration;
            old.decodeState = Async::TaskState::Idle;
            --activeDecodes;
        }
        if (discardedUploads) {
            auto& queue = m_RawWorkspaceThumbnailTextureUploadQueue;
            queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const auto& key) {
                const auto pending = m_RawWorkspaceThumbnailTextures.find(key);
                return pending == m_RawWorkspaceThumbnailTextures.end() || !pending->second.uploadQueued;
            }), queue.end());
        }
    }
    if (activeDecodes >= kRawWorkspaceThumbnailTextureMaxActiveDecodes) {
        return returnResidentTexture();
    }
    ++m_RawWorkspaceThumbnailTextureRequestsThisFrame;

    const std::uint64_t requestGeneration = ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    const std::uint64_t resetGeneration =
        m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
    entry.requestGeneration = requestGeneration;
    entry.lastDemandFrame = demandFrame;
    entry.decodeState = Async::TaskState::Queued;
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
            direct = cameraBacked,
            bytes = source.fileSizeBytes,
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

        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> decodedPixels;
        std::shared_ptr<const Stack::RawWorkspace::RawGallerySimilarityDescriptor> groupingPreview;
        try {
            if (direct) {
                using namespace Stack::RawWorkspace;
                const int edge = detailEdge ? detailEdge : kNeutralThumbnailMaxDimension;
                if (IsGalleryRasterPath(sourcePath)) {
                    if (!DecodeRasterThumbnail(sourcePath, edge, decodedPixels, width, height))
                        LoadGalleryRasterThumbnail(sourcePath, edge, decodedPixels, width, height);
                } else {
                    if (!camera.bytes) {
                        std::error_code ec;
                        const auto length = bytes ? bytes : std::filesystem::file_size(sourcePath, ec);
                        if (!ec) camera = FindCameraThumbnail(sourcePath, length, edge);
                    }
                    LoadOriginalGalleryPixels(sourcePath, camera, edge, detailEdge != 0, decodedPixels, width, height, resetRequested);
                }
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

        ProjectTasks().PostToMain([
                this,
                requestGeneration,
                sourceKey,
                thumbnailPath,
                thumbnailStatus,
                width,
                height,
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
                if (decodedPixels.empty() || width <= 0 || height <= 0) {
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
                    target.decodeState = Async::TaskState::Failed;
                    target.uploadPending = false;
                    target.uploadQueued = false;
                    std::vector<unsigned char>().swap(target.decodedPixels);
                    target.decodedWidth = 0;
                    target.decodedHeight = 0;
                }
            });

        };
        submitted = m_PermanentGalleryWorkspace ? ProjectTasks().SubmitHighPriority("Loading thumbnails", std::move(decode))
            : ProjectTasks().Submit("Loading thumbnails", std::move(decode));
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        auto rejected = m_RawWorkspaceThumbnailTextures.find(sourceKey);
        if (rejected != m_RawWorkspaceThumbnailTextures.end() &&
            rejected->second.requestGeneration == requestGeneration) {
            rejected->second.decodeState = Async::TaskState::Failed;
            rejected->second.uploadPending = false;
            rejected->second.uploadQueued = false;
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

void EditorModule::PumpRawWorkspaceThumbnailTextureUploads() {
    TickRawWorkspacePersistence();
    TickRawWorkspacePreviewStaging();
    PumpRawWorkspaceThumbnailTextureDeletes();

    if (m_RawWorkspaceThumbnailTextureUploadQueue.empty()) {
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

    std::size_t residentCount = m_RetiredGalleryThumbnails.size();
    std::size_t residentBytes = m_RetiredGalleryThumbnailBytes;
    for (const auto& [key, entry] : m_RawWorkspaceThumbnailTextures) {
        if (entry.texture == 0 || entry.width <= 0 || entry.height <= 0) {
            continue;
        }
        const std::size_t bytes = RawWorkspaceThumbnailResidentBytes(
            entry.width,
            entry.height);
        ++residentCount;
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
                RawWorkspaceThumbnailResidentBytes(entry.width, entry.height)
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
            (entry->second.lastVisibleFrame >= 0 &&
                entry->second.lastVisibleFrame >= ImGui::GetFrameCount() - 1) ||
            m_RawWorkspaceGalleryLayoutAnimation.Protects(*candidate.key)) {
            continue;
        }
        QueueRawWorkspaceThumbnailTextureDelete(entry->second.texture);
        entry->second.texture = 0;
        m_RawWorkspaceThumbnailTextures.erase(entry);
        residentBytes = residentBytes > candidate.bytes
            ? residentBytes - candidate.bytes
            : 0;
        --residentCount;
    }
    // Protected visible/leased entries cannot be reclaimed now. Retry on the
    // next upload or lease retirement, instead of sorting the map every frame.
    m_RawWorkspaceThumbnailTextureTrimNeeded = false;
}

void EditorModule::ClearRawWorkspaceThumbnailTextures(bool immediate) {
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
        if (entry.texture != 0) {
            QueueRawWorkspaceThumbnailTextureDelete(entry.texture);
            entry.texture = 0;
        }
        entry.decodedPixels.clear();
        entry.uploadPending = false;
    }
    m_RawWorkspaceThumbnailTextures.clear();
    if (immediate) {
        PumpRawWorkspaceThumbnailTextureDeletes(true);
    }
}

