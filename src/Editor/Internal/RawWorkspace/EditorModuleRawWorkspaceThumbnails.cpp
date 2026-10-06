#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Raw/RawLoader.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace {

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

std::size_t RawWorkspaceThumbnailResidentBytes(int width, int height) {
    const std::size_t base = static_cast<std::size_t>(std::max(0, width)) *
        static_cast<std::size_t>(std::max(0, height)) * 4u;
    // Full mip chains use approximately one third more memory than level 0.
    return base + base / 3u;
}

} // namespace

unsigned int EditorModule::GetRawWorkspaceThumbnailTexture(
    const Stack::RawWorkspace::SourceRecord& source,
    int* outWidth,
    int* outHeight,
    bool prioritize) {
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
    if (isDisplayable(source.thumbnail)) {
        displayThumbnail = &source.thumbnail;
    } else if (isDisplayable(source.transientThumbnail)) {
        displayThumbnail = &source.transientThumbnail;
    }

    const auto clearCached = [&]() {
        auto it = m_RawWorkspaceThumbnailTextures.find(source.relativePathKey);
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

    auto existing = m_RawWorkspaceThumbnailTextures.find(source.relativePathKey);
    const std::string metadataIdentity = source.absolutePath.lexically_normal().generic_u8string() + "|" +
        source.fingerprint + "|" + std::to_string(source.modifiedTimeTicks) + "|" +
        std::to_string(source.fileSizeBytes);
    if (existing != m_RawWorkspaceThumbnailTextures.end() &&
        existing->second.metadataIdentity != metadataIdentity) {
        // A file replacement or a different project reference cannot inherit
        // an earlier image's pixels or camera properties under the same key.
        QueueRawWorkspaceThumbnailTextureDelete(existing->second.texture);
        existing->second = {};
        existing->second.absolutePath = displayThumbnail->absolutePath;
        existing->second.metadataIdentity = metadataIdentity;
        existing->second.status = displayThumbnail->status;
        m_RawWorkspaceThumbnailTextureUploadQueue.erase(
            std::remove(m_RawWorkspaceThumbnailTextureUploadQueue.begin(),
                m_RawWorkspaceThumbnailTextureUploadQueue.end(), source.relativePathKey),
            m_RawWorkspaceThumbnailTextureUploadQueue.end());
    }
    if (existing != m_RawWorkspaceThumbnailTextures.end()) {
        existing->second.lastUseSerial =
            ++m_RawWorkspaceThumbnailTextureUseSerial;
        if (prioritize) {
            existing->second.priority = true;
        }
        const bool cacheMatches =
            existing->second.absolutePath == displayThumbnail->absolutePath;
        if (!cacheMatches) {
            // Keep the currently visible quick texture alive while the final
            // file is decoded and uploaded. The request generation prevents a
            // superseded decode from replacing the newer target.
            existing->second.absolutePath = displayThumbnail->absolutePath;
            existing->second.status = displayThumbnail->status;
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
                    source.relativePathKey),
                m_RawWorkspaceThumbnailTextureUploadQueue.end());
        } else if (existing->second.texture != 0) {
            existing->second.status = displayThumbnail->status;
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
        entry.absolutePath = displayThumbnail->absolutePath;
        entry.metadataIdentity = metadataIdentity;
        entry.status = displayThumbnail->status;
        entry.priority = prioritize;
        entry.lastUseSerial = ++m_RawWorkspaceThumbnailTextureUseSerial;
        existing = m_RawWorkspaceThumbnailTextures.emplace(source.relativePathKey, std::move(entry)).first;
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
    if (foregroundGestureActive && !prioritize) {
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
    const std::size_t activeDecodes = static_cast<std::size_t>(std::count_if(
        m_RawWorkspaceThumbnailTextures.begin(),
        m_RawWorkspaceThumbnailTextures.end(),
        [](const auto& item) {
            const RawWorkspaceThumbnailTexture& texture = item.second;
            return texture.decodeState == Async::TaskState::Queued ||
                texture.decodeState == Async::TaskState::Running ||
                texture.decodeState == Async::TaskState::Applying ||
                texture.uploadPending;
        }));
    if (activeDecodes >= kRawWorkspaceThumbnailTextureMaxActiveDecodes) {
        return returnResidentTexture();
    }
    ++m_RawWorkspaceThumbnailTextureRequestsThisFrame;

    const std::uint64_t requestGeneration = ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    const std::uint64_t resetGeneration =
        m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
    entry.requestGeneration = requestGeneration;
    entry.decodeState = Async::TaskState::Queued;
    entry.uploadQueued = false;
    entry.decodedPixels.clear();
    entry.decodedWidth = 0;
    entry.decodedHeight = 0;

    const std::string sourceKey = source.relativePathKey;
    const std::filesystem::path sourcePath = source.absolutePath;
    const std::filesystem::path thumbnailPath = displayThumbnail->absolutePath;
    const Stack::RawWorkspace::ThumbnailStatus thumbnailStatus =
        displayThumbnail->status;
    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Loading thumbnails", [
            this,
            requestGeneration,
            resetGeneration,
            sourceKey,
            sourcePath,
            metadataIdentity,
            metadata = entry.metadata,
            metadataChecked = entry.metadataChecked,
            thumbnailPath,
            thumbnailStatus
        ]() mutable {
        auto resetRequested = [this, resetGeneration]() {
            return resetGeneration !=
                m_RawWorkspaceThumbnailTextureResetGeneration.load(std::memory_order_relaxed);
        };
        if (resetRequested()) {
            return;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> decodedPixels;
        try {
            stbi_set_flip_vertically_on_load_thread(1);
            std::ifstream file(thumbnailPath, std::ios::binary);
            const std::vector<unsigned char> encoded{
                std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            unsigned char* pixels = encoded.empty() || encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max())
                ? nullptr : stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 4);
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
        } catch (...) {
            decodedPixels.clear();
            width = 0;
            height = 0;
        }
        if (resetRequested()) {
            return;
        }

        if (!metadataChecked && !decodedPixels.empty()) {
            try {
                auto candidate = std::make_shared<Raw::RawMetadata>();
                if (!sourcePath.empty() && Raw::RawLoader::LoadMetadata(sourcePath.u8string(), *candidate))
                    metadata = std::move(candidate);
            } catch (...) {
                // An unavailable camera header must not hide a valid thumbnail.
            }
            metadataChecked = true;
        }
        if (resetRequested()) return;

            ProjectTasks().PostToMain([
                this,
                requestGeneration,
                sourceKey,
                thumbnailPath,
                thumbnailStatus,
                metadataIdentity,
                metadata = std::move(metadata),
                metadataChecked,
                width,
                height,
                decodedPixels = std::move(decodedPixels)
            ]() mutable {
                auto it = m_RawWorkspaceThumbnailTextures.find(sourceKey);
                if (it == m_RawWorkspaceThumbnailTextures.end()) {
                    return;
                }
                RawWorkspaceThumbnailTexture& target = it->second;
                if (target.requestGeneration != requestGeneration ||
                    target.absolutePath != thumbnailPath ||
                    target.status != thumbnailStatus ||
                    target.metadataIdentity != metadataIdentity) {
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
                    HandleRawWorkspaceThumbnailDecodeFailure(
                        sourceKey,
                        thumbnailPath);
                    return;
                }

                // Publish both before making the texture available to any view.
                target.metadata = std::move(metadata);
                target.metadataChecked = metadataChecked;
                target.decodedPixels = std::move(decodedPixels);
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
        });
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
    const std::string& thumbnailSourceKey) const {
    const auto found = m_RawWorkspaceThumbnailTextures.find(thumbnailSourceKey);
    return found == m_RawWorkspaceThumbnailTextures.end() ? nullptr : found->second.metadata;
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
        InvalidateRawWorkspaceGalleryPresentation();
        PersistRawWorkspaceCatalog();
        return;
    }

    ++attempts;
    sourceIt->thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Stale;
    sourceIt->thumbnail.errorMessage =
        "RAW thumbnail failed to decode; regenerating.";
    InvalidateRawWorkspaceGalleryPresentation();
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
    if ((foregroundGestureActive || filmstripLayoutMoving) &&
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
        pendingEntry.width = pendingEntry.decodedWidth;
        pendingEntry.height = pendingEntry.decodedHeight;
        pendingEntry.decodedWidth = 0;
        pendingEntry.decodedHeight = 0;
        pendingEntry.decodeState = Async::TaskState::Idle;
        pendingEntry.priority = false;
        ++m_RawWorkspaceThumbnailTextureUploadsThisFrame;
        m_RawWorkspaceThumbnailTextureUploadBytesThisFrame += uploadBytes;
    }
    TrimRawWorkspaceThumbnailTextureCache();
}

void EditorModule::QueueRawWorkspaceThumbnailTextureDelete(unsigned int texture) {
    if (texture != 0) {
        m_RawWorkspaceThumbnailTextureDeleteQueue.push_back(texture);
    }
}

void EditorModule::PumpRawWorkspaceThumbnailTextureDeletes(bool drainAll) {
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
    // Gallery keeps a browsing history across both layouts. The byte limit
    // matters more than count for portrait and unusually large thumbnails.
    const std::size_t maxCount = m_PermanentGalleryWorkspace
        ? kGalleryThumbnailMaxResidentCount : kRawWorkspaceThumbnailTextureMaxResidentCount;
    const std::size_t maxBytes = m_PermanentGalleryWorkspace
        ? kGalleryThumbnailMaxResidentBytes : kRawWorkspaceThumbnailTextureMaxResidentBytes;
    struct ResidentTexture {
        std::string key;
        std::uint64_t lastUseSerial = 0;
        std::size_t bytes = 0;
    };

    std::size_t residentCount = 0;
    std::size_t residentBytes = 0;
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
        return;
    }

    std::vector<ResidentTexture> resident;
    resident.reserve(residentCount);
    for (const auto& [key, entry] : m_RawWorkspaceThumbnailTextures) {
        if (entry.texture != 0 && entry.width > 0 && entry.height > 0) {
            resident.push_back({
                key,
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
        auto entry = m_RawWorkspaceThumbnailTextures.find(candidate.key);
        if (entry == m_RawWorkspaceThumbnailTextures.end() ||
            entry->second.texture == 0 || entry->second.uploadPending ||
            (entry->second.lastVisibleFrame >= 0 &&
                entry->second.lastVisibleFrame >= ImGui::GetFrameCount() - 1) ||
            m_RawWorkspaceGalleryLayoutAnimation.Protects(candidate.key)) {
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
}

void EditorModule::ClearRawWorkspaceThumbnailTextures(bool immediate) {
    m_RawWorkspaceGalleryLayoutAnimation.Reset();
    m_RawWorkspaceThumbnailTextureResetGeneration.fetch_add(1, std::memory_order_relaxed);
    ++m_RawWorkspaceThumbnailTextureRequestGeneration;
    m_RawWorkspaceThumbnailTextureUseSerial = 0;
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

