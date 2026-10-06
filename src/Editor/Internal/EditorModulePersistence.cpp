#include "App/WorkspacePresentation.h"
#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h"
#include "Editor/Timeline/TimelinePersistence.h"
#include "Raw/LibRawRuntime.h"
#include "Raw/RawLoader.h"
#include "NodeMath/PngMetadataWriter.h"
#include "ThirdParty/stb_image.h"
#include "Utils/PixelBufferUtils.h"
#include "Utils/PngEncodingUtils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <Windows.h>
#endif

namespace {

namespace StackFormat = StackBinaryFormat;

struct DecodedImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
    int originalChannels = 4;
};

bool DecodeImageFromFile(const std::string& path, DecodedImageData& outImage) {
    outImage = {};

    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.originalChannels = channels;
    const bool copied = Stack::PixelBuffer::CopyInterleavedPixels(
        pixels, width, height, 4, outImage.pixels);
    stbi_image_free(pixels);
    if (!copied) {
        outImage = {};
    }
    return copied;
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    return Stack::PngEncoding::EncodeInterleaved(
        pixels, width, height, channels);
}

std::vector<unsigned char> EncodePngBytesForImageStorageOwned(
    std::vector<unsigned char> bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0 || channels <= 0) {
        return {};
    }

    LibraryManager::FlipImageRowsInPlace(bottomLeftPixels, width, height, std::max(1, channels));
    return EncodePngBytes(bottomLeftPixels, width, height, channels);
}

bool ReplaceExportFileAtomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error) {
#if defined(_WIN32)
    if (MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    error = "Windows could not publish the completed PNG: " +
        std::system_category().message(static_cast<int>(GetLastError()));
    return false;
#else
    std::error_code filesystemError;
    std::filesystem::rename(temporary, destination, filesystemError);
    if (!filesystemError) return true;
    error = "Could not publish the completed PNG: " +
        filesystemError.message();
    return false;
#endif
}

bool WriteExportFileAtomically(
    const std::filesystem::path& destination,
    const std::vector<unsigned char>& bytes,
    std::string& error) {
    if (bytes.empty()) {
        error = "PNG encoding produced no data.";
        return false;
    }
    if (destination.empty() || destination.filename().empty()) {
        error = "The export destination does not name a file.";
        return false;
    }

    std::error_code filesystemError;
    if (destination.has_parent_path()) {
        std::filesystem::create_directories(
            destination.parent_path(), filesystemError);
        if (filesystemError) {
            error = "Could not create the export folder: " +
                filesystemError.message();
            return false;
        }
    }

    std::filesystem::path temporary = destination;
    temporary += ".stack-exporting";
    filesystemError.clear();
    std::filesystem::remove(temporary, filesystemError);
    if (filesystemError) {
        error = "Could not clear an incomplete prior export: " +
            filesystemError.message();
        return false;
    }

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        error = "The temporary export file could not be opened for writing.";
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output.good()) {
        error = "The PNG bytes could not be completely written to disk.";
        filesystemError.clear();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }

    filesystemError.clear();
    const std::uintmax_t temporarySize =
        std::filesystem::file_size(temporary, filesystemError);
    if (filesystemError || temporarySize != bytes.size()) {
        error = filesystemError
            ? "The completed PNG could not be verified: " +
                filesystemError.message()
            : "The completed PNG size did not match the encoded image.";
        filesystemError.clear();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }

    if (!ReplaceExportFileAtomically(temporary, destination, error)) {
        filesystemError.clear();
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }

    filesystemError.clear();
    const std::uintmax_t publishedSize =
        std::filesystem::file_size(destination, filesystemError);
    if (filesystemError || publishedSize != bytes.size()) {
        error = filesystemError
            ? "The published PNG could not be verified: " +
                filesystemError.message()
            : "The published PNG size did not match the encoded image.";
        return false;
    }
    return true;
}

std::string FileNameFromPath(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path;
    }
}

std::string BuildTimestampString() {
    std::time_t now = std::time(nullptr);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0) {
        return {};
    }
    return buffer;
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    return Stack::PixelBuffer::BuildTransparentRgbaPixels(width, height);
}

bool IsLibRawRuntimeErrorMessage(const std::string& message) {
    return message == "RAW support is unavailable because libraw.dll is missing or could not be loaded. Restore the DLL next to Stack and relaunch." ||
        message == "RAW support is unavailable because Stack\\App\\Runtime\\libraw.dll is missing or could not be loaded." ||
        message == "RAW support is unavailable because the managed LibRaw runtime could not be initialized." ||
        message == "RAW support is unavailable in this build because LibRaw support is disabled.";
}

void RefreshDeserializedRawRuntimeErrors(EditorNodeGraph::Graph& graph) {
    const Raw::LibRawRuntimeStatus& runtimeStatus = Raw::GetLibRawRuntimeStatus();
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawSource) {
            continue;
        }

        if (node.rawSource.metadata.sourcePath.empty()) {
            node.rawSource.metadata.sourcePath = node.rawSource.sourcePath;
        }

        const bool hasRawSourcePath =
            !node.rawSource.sourcePath.empty() ||
            !node.rawSource.metadata.sourcePath.empty();
        if (!hasRawSourcePath) {
            continue;
        }

        if (!runtimeStatus.runtimeAvailable) {
            node.rawSource.metadata.error = runtimeStatus.message;
        } else if (IsLibRawRuntimeErrorMessage(node.rawSource.metadata.error)) {
            node.rawSource.metadata.error.clear();
        }
    }
}

std::vector<unsigned char> BuildThumbnailBytes(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    constexpr int kThumbnailSize = 320;
    const float scale = std::min(
        static_cast<float>(kThumbnailSize) / static_cast<float>(std::max(1, width)),
        static_cast<float>(kThumbnailSize) / static_cast<float>(std::max(1, height)));
    const int thumbW = std::max(1, static_cast<int>(std::round(width * scale)));
    const int thumbH = std::max(1, static_cast<int>(std::round(height * scale)));
    std::vector<unsigned char> thumbPixels(static_cast<size_t>(thumbW) * static_cast<size_t>(thumbH) * 4ull, 0u);

    for (int y = 0; y < thumbH; ++y) {
        for (int x = 0; x < thumbW; ++x) {
            const int srcX = std::clamp(static_cast<int>(std::floor((static_cast<float>(x) / thumbW) * width)), 0, width - 1);
            const int srcY = std::clamp(static_cast<int>(std::floor((static_cast<float>(y) / thumbH) * height)), 0, height - 1);
            const size_t srcIndex = static_cast<size_t>((srcY * width + srcX) * 4);
            const size_t dstIndex = static_cast<size_t>((y * thumbW + x) * 4);
            std::copy_n(pixels.data() + srcIndex, 4, thumbPixels.data() + dstIndex);
        }
    }

    return EncodePngBytes(thumbPixels, thumbW, thumbH, 4);
}

} // namespace

nlohmann::json EditorModule::SerializePipeline() {
    if (!CommitRawLayerMaskGraph()) throw std::runtime_error(m_RawLayerStatus);
    json serialized = json::array();
    for (auto& layer : m_Project->layers) {
        serialized.push_back(layer->Serialize());
    }
    nlohmann::json payload = EditorNodeGraph::SerializeGraphPayload(serialized, m_Project->graph);
    if (IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) {
        const int sourceId = ResolveRawWorkspaceStageOutputNodeId();
        if (const auto* source = m_Project->graph.FindNode(sourceId)) payload["rawLayerSourceNodeUuid"] = source->instanceUuid;
        for (auto& node : payload["nodeGraph"]["nodes"])
            if (node.value("id", 0) == sourceId && node.contains("rawRecipe"))
                node["rawRecipe"] = Stack::RawRecipe::SerializeWorkspaceSourceRecipe(m_Project->rawRecipe);
    }
    payload["editorComposite"] = SerializeCompositePersistence();
    payload["editorTimeline"] = SerializeTimelinePersistence();
    payload["rawLayerStack"] = Stack::Project::SerializeRawLayerStack(m_Project->rawLayers.State());
    return payload;
}

void EditorModule::ResetForPipelineDeserialization() {
    ResetProjectInteractionState();
    ResetRenderSubmissionState();
    ClearAutoGainMaskPreview();
    m_CompositeSelectedOutputNodeId = -1;
    ClearRawWorkspaceLivePreviewState();
    m_Pipeline.Clear();
    m_Pipeline.ClearOutput();
    m_CompositePreviewPipeline.Clear();
    m_CompositePreviewPipeline.ClearOutput();
    m_Project->graph.Clear();
    m_Project->layers.clear();
    m_Project->rawLayers.Reset();
    m_SelectedLayerIndex = -1;
    ClearCompositeRuntimeState();
    m_NodeDirtyGenerations.clear();
    m_DevelopAutoSolveTriggerHashes.clear();
    m_DevelopAutoRawSolveTriggerHashes.clear();
    m_DevelopAutoRawCalibrationHashes.clear();
    m_DevelopAutoGuidanceDrafts.clear();
    m_RawDevelopExposureDrafts.clear();
    m_LastRawDevelopInteractionTime = -1.0;
    m_RawDevelopInteractionSerialCounter = 1;
    m_RawDevelopInteractionTimes.clear();
    m_RawDevelopInteractionSerials.clear();
    m_DeferredDevelopCandidateFeedbackTimes.clear();
    m_PreviewDisplayedRevisions.clear();
    m_PreviewPixelCache.clear();
    m_PreviewRequestedGenerations.clear();
    m_PreviewCompletedGenerations.clear();
    m_ScopeDisplayedRevisions.clear();
    m_Project->timeline = {};
    m_TimelineUi.selectedOutputNodeId = -1;
    m_TimelineUi.selectedParameterTarget = {};
    m_TimelineUi.currentFrame = 0;
    m_TimelineUi.durationFrames = 120;
    m_TimelineUi.framesPerSecond = 30;
    m_TimelineUi.playing = false;
    m_TimelineUi.loopPlayback = true;
    m_TimelineUi.settingsPopupOpen = false;
    m_TimelineUi.playbackFrameAccumulator = 0.0;
    ClearTimelineLiveEditPreview();
}

bool EditorModule::DeserializeSinglePipelineLayer(const nlohmann::json& layerData) {
    std::string type = layerData.value("type", "");
    std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayerFromTypeId(type);
    if (!newLayer) {
        return true;
    }

    newLayer->InitializeGL();
    newLayer->Deserialize(layerData);
    m_Project->layers.push_back(newLayer);
    return true;
}

bool EditorModule::FinalizeDeserializedPipeline(const nlohmann::json& serialized, bool restoreSourceFromGraphState) {
    Stack::Project::RawLayerStackState rawLayers;
    std::string rawLayerError;
    if (!Stack::Project::ReadRawLayersFromPipeline(serialized, rawLayers, rawLayerError) ||
        !m_Project->rawLayers.Load(Stack::Project::SerializeRawLayerStack(rawLayers), rawLayerError)) {
        std::cerr << rawLayerError << '\n';
        return false;
    }
    m_SelectedLayerIndex = m_Project->layers.empty() ? -1 : 0;

    int sourceWidth = 0;
    int sourceHeight = 0;
    const std::vector<unsigned char>& sourcePixels = m_Pipeline.GetSourcePixelsRaw();
    if (!sourcePixels.empty()) {
        sourceWidth = m_Pipeline.GetCanvasWidth();
        sourceHeight = m_Pipeline.GetCanvasHeight();
    }

    EditorNodeGraph::DeserializeGraphPayload(
        serialized,
        m_Project->graph,
        static_cast<int>(m_Project->layers.size()),
        sourcePixels,
        sourceWidth,
        sourceHeight,
        m_Pipeline.GetSourceChannels());
    RefreshDeserializedRawRuntimeErrors(m_Project->graph);

    DeserializeCompositePersistence(serialized);
    DeserializeTimelinePersistence(serialized);
    RefreshGraphLayerMetadata();

    const int activeImageNodeId = m_Project->graph.GetActiveImageNodeId();
    if (restoreSourceFromGraphState && activeImageNodeId > 0) {
        if (EditorNodeGraph::Node* imageNode = m_Project->graph.FindNode(activeImageNodeId)) {
            if (imageNode->kind == EditorNodeGraph::NodeKind::Image && !imageNode->image.pixels.empty()) {
                LoadSourceFromPixels(
                    imageNode->image.pixels.data(),
                    imageNode->image.width,
                    imageNode->image.height,
                    imageNode->image.channels);
            } else if (imageNode->kind == EditorNodeGraph::NodeKind::RawSource) {
                const int width = Raw::DisplayWidth(imageNode->rawSource.metadata);
                const int height = Raw::DisplayHeight(imageNode->rawSource.metadata);
                std::vector<unsigned char> transparent = BuildTransparentPixels(width, height);
                LoadSourceFromPixels(
                    transparent.empty() ? nullptr : transparent.data(),
                    width,
                    height,
                    4);
            }
        }
    }

    if (activeImageNodeId > 0) {
        ApplyGraphLayerOrder();
    } else {
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
    }
    MarkRenderRefreshDirty();
    return true;
}

void EditorModule::DeserializePipeline(const nlohmann::json& serialized) {
    Stack::Project::RawLayerStackState rawLayers;
    std::string rawLayerError;
    if (!Stack::Project::ReadRawLayersFromPipeline(serialized, rawLayers, rawLayerError))
        throw std::runtime_error(rawLayerError);
    ResetForPipelineDeserialization();
    const nlohmann::json layers = EditorNodeGraph::ExtractLayerArray(serialized);
    if (!layers.is_array()) return;

    for (const auto& layerData : layers) {
        DeserializeSinglePipelineLayer(layerData);
    }
    FinalizeDeserializedPipeline(serialized, true);
}

void EditorModule::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch, bool loadCompositePreview) {
    if (data != nullptr) {
        std::vector<unsigned char> ownedPixels;
        if (!Stack::PixelBuffer::CopyInterleavedPixels(
                data,
                w,
                h,
                ch,
                ownedPixels)) {
            m_Pipeline.Clear();
            if (loadCompositePreview) {
                m_CompositePreviewPipeline.Clear();
            }
            ClearCompositeSceneTextures();
            MarkRenderRefreshDirty();
            return;
        }

        SharedPixelBuffer sharedPixels;
        try {
            sharedPixels = MakeSharedPixelBufferOwned(std::move(ownedPixels));
        } catch (const std::bad_alloc&) {
            m_Pipeline.Clear();
            if (loadCompositePreview) {
                m_CompositePreviewPipeline.Clear();
            }
            ClearCompositeSceneTextures();
            MarkRenderRefreshDirty();
            return;
        }
        m_Pipeline.LoadSourceFromSharedPixels(sharedPixels, w, h, ch);
        if (loadCompositePreview) {
            m_CompositePreviewPipeline.LoadSourceFromSharedPixels(
                sharedPixels,
                w,
                h,
                ch);
        }
    } else {
        m_Pipeline.LoadSourceFromPixels(nullptr, w, h, ch);
        if (loadCompositePreview) {
            m_CompositePreviewPipeline.LoadSourceFromPixels(nullptr, w, h, ch);
        }
    }
    ClearCompositeSceneTextures();
    MarkRenderRefreshDirty();
}

void EditorModule::LoadSourceFromImagePayload(
    const EditorNodeGraph::ImagePayload& payload,
    bool loadCompositePreview,
    bool markDirty) {
    SharedPixelBuffer sharedPixels = EnsureSharedImagePixels(payload);
    if (sharedPixels.empty()) {
        return;
    }

    m_Pipeline.LoadSourceFromSharedPixels(sharedPixels, payload.width, payload.height, payload.channels);
    if (loadCompositePreview) {
        m_CompositePreviewPipeline.LoadSourceFromSharedPixels(sharedPixels, payload.width, payload.height, payload.channels);
    }
    ClearCompositeSceneTextures();
    if (markDirty) {
        MarkRenderDirty();
    }
}

bool EditorModule::ApplyLoadedProject(const LoadedProjectData& projectData) {
    Stack::Project::RawLayerStackState rawLayers;
    std::string rawLayerError;
    if (!Stack::Project::ReadRawLayersFromPipeline(projectData.pipelineData, rawLayers, rawLayerError, projectData.projectKind == StackBinaryFormat::kRawProjectKind)) {
        std::cerr << rawLayerError << '\n';
        return false;
    }
    const bool hasSharedSource = !projectData.sourcePixelsShared.empty();
    const bool lazySource =
        projectData.sourceState == ProjectSourceState::LazyAsset;
    if (!lazySource &&
        ((!hasSharedSource && projectData.sourcePixels.empty()) ||
         projectData.width <= 0 ||
         projectData.height <= 0)) {
        return false;
    }

    ResetBracketingForProjectLoad(projectData.projectFileName);
    CancelMfdExperimentalProcessing({}, true);
    CancelHdrProcessing();
    CancelMultiFrameGraphProcessing();

    std::string rawSessionError;
    if (!ApplyLoadedRawProjectSessionMetadata(projectData, &rawSessionError)) {
        return false;
    }
    m_Project->rawPipelineActive = false;
    ResetForPipelineDeserialization();
    if (lazySource) {
        // RAW and source-set nodes resolve their immutable managed assets from
        // the project store. Do not install a dimensionally false 1x1 image.
        ClearCompositeSceneTextures();
    } else if (hasSharedSource) {
        m_Pipeline.LoadSourceFromSharedPixels(
            projectData.sourcePixelsShared,
            projectData.width,
            projectData.height,
            projectData.channels);
        m_CompositePreviewPipeline.LoadSourceFromSharedPixels(
            projectData.sourcePixelsShared,
            projectData.width,
            projectData.height,
            projectData.channels);
        ClearCompositeSceneTextures();
        MarkRenderRefreshDirty();
    } else {
        LoadSourceFromPixels(
            projectData.sourcePixels.data(),
            projectData.width,
            projectData.height,
            projectData.channels);
    }
    const nlohmann::json layers = EditorNodeGraph::ExtractLayerArray(projectData.pipelineData);
    if (!layers.is_array()) {
        return false;
    }
    for (const auto& layerData : layers) {
        DeserializeSinglePipelineLayer(layerData);
    }
    if (!FinalizeDeserializedPipeline(projectData.pipelineData, false)) return false;
    ResetNodeBrowserThumbnailState();
    std::size_t nextThumbIndex = 0;
    RestorePersistedNodeBrowserThumbnailEntries(
        projectData.nodeBrowserThumbnailEntries,
        0,
        projectData.nodeBrowserThumbnailEntries.size(),
        nextThumbIndex);
    SetCurrentProjectName(projectData.projectName);
    SetCurrentProjectFileName(projectData.projectFileName);
    if (!ApplyLoadedRawProjectSessionMetadata(projectData, &rawSessionError)) {
        return false;
    }
    m_Project->documentId = !projectData.projectId.empty()
        ? projectData.projectId
        : (projectData.rawProjectSnapshot &&
           !projectData.rawProjectSnapshot->projectId.empty()
            ? projectData.rawProjectSnapshot->projectId
            : Stack::Project::GenerateStableUuid());
    m_Project->adoptionSourcePath = projectData.adoptedFrom;
    // Adoption itself is a durable transition even when the compatibility
    // document has not been edited. Keep the session visually clean while
    // reserving revision 1 so an explicit save can never be mistaken for a
    // clean no-op.
    m_Project->editRevision = m_Project->adoptionSourcePath.empty() ? 0u : 1u;
    m_Project->saves.Reset(m_Project->documentId, 0);
    m_ProjectNamingPromptRequested = false;
    m_ProjectNamingPromptShown = true;
    ClearDirty();
    if (projectData.rawProjectSnapshot &&
        !ValidateAndRepairActiveRawProjectGraphBindings(nullptr, &rawSessionError)) {
        return false;
    }
    CloseRawWorkspaceGalleryWorkspace();
    WarmNodeBrowserThumbnailPixelsAsync();
    m_Project->lastEditTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_Project->lastAutosaveTime = -1.0;
    m_Project->lastAutosaveAttemptTime = -1.0;
    return true;
}

bool EditorModule::FinishWorkspaceInteraction() {
    if (!CommitRawLayerMaskGraph()) return false;
    m_Project->rawLayers.EndGesture();
    return !m_Project->rawInteractionDraft.active || ResolveRawWorkspaceInteractionDraft(false);
}

bool EditorModule::EnterRawWorkspaceRootTab() {
    if (Stack::Workspace::IsPreview()) return false;
    if (m_RawWorkspaceRootTabActive) {
        return true;
    }

    if (!CommitRawLayerMaskGraph()) return false;
    m_Project->rawLayers.EndGesture();
    CancelCanvasTool();
    ClearTrackedToneCurveProbe();
    m_RawWorkspaceRootTabActive = true;
    m_Viewport.FrameTransition().Reset();
    MarkRenderRefreshDirty();
    // RAW, RAW Lab, and Editor are views over one project. A normal Editor
    // project remains live while the RAW presentation is visibly locked; a
    // RAW project/preview keeps the exact same graph and render caches.
    m_RawWorkspaceLockedByEditorProject =
        !IsRawWorkspaceProjectActive() && HasOpenProjectSession();
    const bool explicitOpenPending =
        !m_PendingRawWorkspaceExplicitOpenSourceKey.empty();
    if (explicitOpenPending) {
        const std::string sourceKey =
            std::move(m_PendingRawWorkspaceExplicitOpenSourceKey);
        m_PendingRawWorkspaceExplicitOpenSourceKey.clear();
        // The pending key only comes from an explicit Open/Double-click
        // action. Let it replace a clean/saved Editor project instead of
        // treating it as passive Gallery browsing.
        m_RawWorkspaceLockedByEditorProject = false;
        m_RawWorkspaceExplicitReplacementSourceKey = sourceKey;
        SelectRawWorkspaceSource(sourceKey);
    }
    if (m_PermanentGalleryWorkspace && Stack::RawWorkspace::ShouldRestoreRawGalleryForEmptyWorkspace(
            !m_RawWorkspace.workspaceRoot.empty(),
            m_RawWorkspaceLockedByEditorProject,
            IsRawWorkspaceProjectActive(),
            IsMultiFrameRawProjectActive(),
            explicitOpenPending,
            m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Closed)) {
        m_RawWorkspaceLabUi.galleryHost =
            m_RawWorkspaceLabUi.lastGalleryHost;
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            OpenRawWorkspaceLabNativeGallery();
        }
    }
    return true;
}

bool EditorModule::LeaveRawWorkspaceRootTab(bool enteringEditorTab) {
    if (Stack::Workspace::IsPreview()) return false;
    (void)enteringEditorTab;

    if (m_Project->rawInteractionDraft.active &&
        !ResolveRawWorkspaceInteractionDraft(false)) {
        return false;
    }

    auto closeRawWorkspaceWindows = [this]() {
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            CloseRawWorkspaceLabNativeGallery();
        }
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
        m_RawWorkspaceGalleryWindowOpen = false;
    };
    closeRawWorkspaceWindows();
    m_RawWorkspaceLabFilmstripDrawerState = {};
    m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
    m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
    m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
    m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
    m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = false;
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    m_RawWorkspaceLabFilmstripDragState = {};
    m_RawWorkspaceRootTabActive = false;
    MarkRenderRefreshDirty();
    m_RawWorkspaceLockedByEditorProject = false;
    return true;
}

void EditorModule::RestorePersistedNodeBrowserThumbnailEntries(
    const std::vector<StackBinaryFormat::NodeBrowserThumbnailEntry>& entries,
    std::size_t startIndex,
    std::size_t maxCount,
    std::size_t& outNextIndex) {
    outNextIndex = std::min(startIndex, entries.size());
    const std::size_t endIndex = std::min(entries.size(), startIndex + maxCount);
    for (; outNextIndex < endIndex; ++outNextIndex) {
        const auto& entry = entries[outNextIndex];
        if (entry.previewKey.empty() || entry.pngBytes.empty()) {
            continue;
        }
        NodeBrowserThumbnailRuntimeEntry& runtime = m_NodeBrowserThumbnailEntries[entry.previewKey];
        runtime.previewSeedHash = entry.previewSeedHash;
        runtime.previewRecipeVersion = entry.previewRecipeVersion;
        runtime.pngBytes = entry.pngBytes;
        runtime.decodedPixels.clear();
        runtime.width = 0;
        runtime.height = 0;
        runtime.channels = 4;
        runtime.revision = m_NodeBrowserThumbnailRevisionCounter++;
        runtime.pending = false;
        runtime.fallback = false;
    }
}

void EditorModule::ResetDeferredLoadedProjectApplyState() {
    m_DeferredLoadedProjectApply = {};
}

void EditorModule::FailDeferredLoadedProjectApply(std::string message) {
    const std::shared_ptr<LoadedProjectData> rollbackProject =
        m_DeferredLoadedProjectApply.rollbackProject;
    const bool rollbackDirty = m_DeferredLoadedProjectApply.rollbackDirty;
    const bool rollbackGalleryOpen = m_DeferredLoadedProjectApply.rollbackGalleryOpen;
    auto completion = std::move(m_DeferredLoadedProjectApply.completion);
    m_DeferredLoadedProjectApply.active = false;
    m_DeferredLoadedProjectApply.failed = true;
    m_DeferredLoadedProjectApply.allowRenderSubmission = false;
    m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::Failed;
    m_DeferredLoadedProjectApply.statusText = std::move(message);
    m_DeferredLoadedProjectApply.project.reset();
    m_DeferredLoadedProjectApply.layerArray = nlohmann::json::array();
    m_DeferredLoadedProjectApply.rollbackProject.reset();
    if (rollbackProject) {
        if (ApplyLoadedProject(*rollbackProject)) {
            if (rollbackDirty) {
                MarkDirty();
            }
        } else {
            ResetToBlankProject();
            m_DeferredLoadedProjectApply.statusText +=
                " The previous in-memory project could not be restored.";
        }
    } else {
        ResetToBlankProject();
    }
    if (rollbackGalleryOpen) OpenRawWorkspaceGalleryWorkspace();
    else CloseRawWorkspaceGalleryWorkspace();
    if (completion) {
        try {
            completion(false, m_DeferredLoadedProjectApply.statusText);
        } catch (...) {
        }
    }
}

bool EditorModule::BeginDeferredLoadedProjectApply(
    std::shared_ptr<LoadedProjectData> projectData,
    std::function<void(bool, const std::string&)> onComplete) {
    // The caller handles a rejected start. Keep the running apply and its
    // completion intact until it finishes or the project is closed.
    if (m_DeferredLoadedProjectApply.active) return false;
    ResetDeferredLoadedProjectApplyState();
    m_DeferredLoadedProjectApply.rollbackGalleryOpen = IsRawWorkspaceGalleryWorkspaceOpen();
    const auto rejectCandidate = [this](std::string message) {
        m_DeferredLoadedProjectApply.failed = true;
        m_DeferredLoadedProjectApply.step =
            DeferredLoadedProjectApplyState::Step::Failed;
        m_DeferredLoadedProjectApply.statusText = std::move(message);
        return false;
    };
    const bool lazySource = projectData &&
        projectData->sourceState == ProjectSourceState::LazyAsset;
    if (!projectData ||
        (!lazySource &&
         ((projectData->sourcePixels.empty() && projectData->sourcePixelsShared.empty()) ||
          projectData->width <= 0 ||
          projectData->height <= 0))) {
        return rejectCandidate("Failed to apply the loaded project.");
    }

    const bool isRawProject =
        projectData->projectKind == StackFormat::kRawProjectKind ||
        (projectData->rawWorkspaceData.is_object() &&
         projectData->rawWorkspaceData.value("schema", std::string()) ==
             "stack.rawWorkspace.project");
    if (isRawProject &&
        (!projectData->rawProjectSnapshot || !projectData->projectStore) &&
        !projectData->transientRawPreview) {
        return rejectCandidate(
            "The project does not use the current managed project model.");
    }

    const nlohmann::json layerArray =
        EditorNodeGraph::ExtractLayerArray(projectData->pipelineData);
    if (!layerArray.is_array()) {
        return rejectCandidate("Failed to read the project's editor state.");
    }
    Stack::Project::RawLayerStackState rawLayers;
    std::string rawLayerError;
    if (!Stack::Project::ReadRawLayersFromPipeline(projectData->pipelineData, rawLayers, rawLayerError, projectData->projectKind == StackBinaryFormat::kRawProjectKind))
        return rejectCandidate(rawLayerError);

    if (HasOpenProjectSession()) {
        auto rollbackProject = std::make_shared<LoadedProjectData>();
        const bool canReuseCleanRawSnapshot =
            !IsDirty() &&
            IsRawWorkspaceProjectActive() &&
            m_Project->snapshot &&
            m_Project->store;
        if (canReuseCleanRawSnapshot) {
            // A clean managed project already has an authoritative immutable
            // rollback document. Re-serializing the live graph and gathering
            // every thumbnail here used to stall the UI precisely when a user
            // opened project B over saved project A.
            rollbackProject->sourceState = ProjectSourceState::LazyAsset;
            rollbackProject->pipelineData =
                m_Project->snapshot->pipelineData;
        } else {
            int sourceWidth = 0;
            int sourceHeight = 0;
            int sourceChannels = 4;
            rollbackProject->sourcePixelsShared = m_Pipeline.ShareSourcePixels(
                sourceWidth,
                sourceHeight,
                sourceChannels);
            if (rollbackProject->sourcePixelsShared.empty()) {
                if (IsRawWorkspaceProjectActive()) {
                    rollbackProject->sourceState =
                        ProjectSourceState::LazyAsset;
                    rollbackProject->sourcePixels.clear();
                    sourceWidth = 0;
                    sourceHeight = 0;
                } else {
                    rollbackProject->sourcePixels.assign(4, 0);
                    sourceWidth = 1;
                    sourceHeight = 1;
                    sourceChannels = 4;
                }
            }
            rollbackProject->width = sourceWidth;
            rollbackProject->height = sourceHeight;
            rollbackProject->channels = sourceChannels;
            rollbackProject->pipelineData = SerializePipeline();
        }
        rollbackProject->projectName = m_Project->name;
        rollbackProject->projectFileName = m_Project->fileName;
        rollbackProject->projectId = m_Project->documentId;
        rollbackProject->adoptedFrom = m_Project->adoptionSourcePath;
        if (IsUnifiedProjectStoreActive()) {
            rollbackProject->rawProjectSnapshot =
                std::make_shared<Stack::Project::RawProjectSnapshot>(
                    *m_Project->snapshot);
            rollbackProject->projectStore = m_Project->store;
            rollbackProject->decodedRawSource = m_Project->singleRawSource;
            rollbackProject->rawWorkspaceData = m_Project->snapshot->rawWorkspaceData;
            rollbackProject->projectKind = IsRawWorkspaceProjectActive()
                ? StackFormat::kRawProjectKind : StackFormat::kEditorProjectKind;
            rollbackProject->projectId = m_Project->snapshot->projectId;
        }
        if (IsRawWorkspaceProjectActive()) {
            const Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
            if (!m_Project->snapshot && source &&
                m_Project->rawMode !=
                    Stack::RawWorkspace::RawProjectMode::CustomGraph) {
                rollbackProject->transientRawPreview = true;
                StackFormat::ProjectDocument rollbackDocument;
                Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
                    *source,
                    m_Project->rawRecipe,
                    nlohmann::json::object(),
                    rollbackDocument,
                    m_Project->rawMode,
                    source->project.status !=
                        Stack::RawWorkspace::ProjectStatus::Embedded);
                rollbackProject->rawWorkspaceData =
                    std::move(rollbackDocument.rawWorkspaceData);
                rollbackProject->projectKind = StackFormat::kRawProjectKind;
            }
        }
        m_DeferredLoadedProjectApply.rollbackProject =
            std::move(rollbackProject);
        m_DeferredLoadedProjectApply.rollbackDirty = IsDirty();
    }

    // The candidate is structurally valid and project identity is now about
    // to change. Stop any source-set worker before deserialization begins;
    // generation fencing prevents a stale completion from publishing into
    // the replacement project.
    ResetBracketingForProjectLoad(projectData->projectFileName);
    CancelMfdExperimentalProcessing("The editing project is switching.", true);
    CancelHdrProcessing("The editing project is switching.");
    CancelMultiFrameGraphProcessing("The editing project is switching.");
    m_Project->rawPipelineActive = false;
    m_DeferredLoadedProjectApply.active = true;
    m_DeferredLoadedProjectApply.project = std::move(projectData);
    m_DeferredLoadedProjectApply.layerArray = layerArray;
    m_DeferredLoadedProjectApply.completion = std::move(onComplete);

    m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::ResetRuntime;
    m_DeferredLoadedProjectApply.statusText = "Applying editor state...";
    return true;
}

bool EditorModule::IsDeferredLoadedProjectApplyActive() const {
    return m_DeferredLoadedProjectApply.active;
}

bool EditorModule::HasDeferredLoadedProjectApplyFailed() const {
    return m_DeferredLoadedProjectApply.failed;
}

bool EditorModule::HasDeferredLoadedProjectApplyCoreFinished() const {
    switch (m_DeferredLoadedProjectApply.step) {
    case DeferredLoadedProjectApplyState::Step::WaitForFirstRender:
    case DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails:
    case DeferredLoadedProjectApplyState::Step::Complete:
        return true;
    default:
        return false;
    }
}

bool EditorModule::HasDeferredLoadedProjectFirstRenderReady() const {
    switch (m_DeferredLoadedProjectApply.step) {
    case DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails:
    case DeferredLoadedProjectApplyState::Step::Complete:
        return true;
    default:
        return false;
    }
}

bool EditorModule::IsDeferredLoadedProjectReadyForReveal() const {
    return !m_DeferredLoadedProjectApply.active &&
        !m_DeferredLoadedProjectApply.failed &&
        m_DeferredLoadedProjectApply.step == DeferredLoadedProjectApplyState::Step::Complete;
}

const std::string& EditorModule::GetDeferredLoadedProjectStatusText() const {
    return m_DeferredLoadedProjectApply.statusText;
}

const char* EditorModule::GetDeferredLoadedProjectPhaseLabel() const {
    switch (m_DeferredLoadedProjectApply.step) {
    case DeferredLoadedProjectApplyState::Step::ResetRuntime: return "ResetRuntime";
    case DeferredLoadedProjectApplyState::Step::InstallSource: return "InstallSource";
    case DeferredLoadedProjectApplyState::Step::DeserializeLayers: return "DeserializeLayers";
    case DeferredLoadedProjectApplyState::Step::FinalizePipeline: return "FinalizePipeline";
    case DeferredLoadedProjectApplyState::Step::RestorePersistedThumbnails: return "RestorePersistedThumbnails";
    case DeferredLoadedProjectApplyState::Step::FinalizeBookkeeping: return "FinalizeBookkeeping";
    case DeferredLoadedProjectApplyState::Step::PrepareNodeBrowserThumbnails: return "PrepareNodeBrowserThumbnails";
    case DeferredLoadedProjectApplyState::Step::WaitForFirstRender: return "WaitForFirstRender";
    case DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails: return "WaitForNodeBrowserThumbnails";
    case DeferredLoadedProjectApplyState::Step::Complete: return "Complete";
    case DeferredLoadedProjectApplyState::Step::Failed: return "Failed";
    case DeferredLoadedProjectApplyState::Step::None:
    default:
        return "None";
    }
}

std::size_t EditorModule::GetPendingNodeBrowserThumbnailWarmCount() const {
    return m_NodeBrowserThumbnailWarmPendingEntries;
}

std::size_t EditorModule::GetPendingNodeBrowserThumbnailGenerationCount() const {
    return m_NodeBrowserThumbnailPendingEntries;
}

void EditorModule::TickDeferredLoadedProjectApply(double projectApplyBudgetMs) {
    if (!m_DeferredLoadedProjectApply.active || !m_DeferredLoadedProjectApply.project) {
        return;
    }

    using Clock = std::chrono::steady_clock;
    const auto startTime = Clock::now();
    auto elapsedMs = [&]() {
        return std::chrono::duration<double, std::milli>(Clock::now() - startTime).count();
    };

    bool processedWorkThisFrame = false;
    const double clampedBudgetMs = std::max(0.0, projectApplyBudgetMs);

    while (m_DeferredLoadedProjectApply.active && !m_DeferredLoadedProjectApply.failed) {
        const auto step = m_DeferredLoadedProjectApply.step;
        if (processedWorkThisFrame && elapsedMs() >= clampedBudgetMs &&
            step != DeferredLoadedProjectApplyState::Step::WaitForFirstRender &&
            step != DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails) {
            break;
        }

        switch (step) {
        case DeferredLoadedProjectApplyState::Step::ResetRuntime:
            m_DeferredLoadedProjectApply.statusText = "Applying editor state...";
            ResetForPipelineDeserialization();
            m_DeferredLoadedProjectApply.allowRenderSubmission = false;
            m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::InstallSource;
            processedWorkThisFrame = true;
            break;

        case DeferredLoadedProjectApplyState::Step::InstallSource: {
            const auto& project = *m_DeferredLoadedProjectApply.project;
            m_DeferredLoadedProjectApply.statusText = "Applying editor state...";
            if (project.sourceState == ProjectSourceState::LazyAsset) {
                ClearCompositeSceneTextures();
            } else if (!project.sourcePixelsShared.empty()) {
                m_Pipeline.LoadSourceFromSharedPixels(
                    project.sourcePixelsShared,
                    project.width,
                    project.height,
                    project.channels);
                ClearCompositeSceneTextures();
                MarkRenderRefreshDirty();
            } else {
                LoadSourceFromPixels(
                    project.sourcePixels.data(),
                    project.width,
                    project.height,
                    project.channels,
                    false);
            }
            m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::DeserializeLayers;
            processedWorkThisFrame = true;
            break;
        }

        case DeferredLoadedProjectApplyState::Step::DeserializeLayers: {
            m_DeferredLoadedProjectApply.statusText = "Applying editor state...";
            const auto& layers = m_DeferredLoadedProjectApply.layerArray;
            bool processedLayer = false;
            while (m_DeferredLoadedProjectApply.nextLayerIndex < layers.size()) {
                DeserializeSinglePipelineLayer(layers[m_DeferredLoadedProjectApply.nextLayerIndex]);
                ++m_DeferredLoadedProjectApply.nextLayerIndex;
                processedLayer = true;
                processedWorkThisFrame = true;
                if (elapsedMs() >= clampedBudgetMs) {
                    break;
                }
            }

            if (m_DeferredLoadedProjectApply.nextLayerIndex >= layers.size()) {
                m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::FinalizePipeline;
            } else if (!processedLayer) {
                processedWorkThisFrame = true;
            }
            if (elapsedMs() >= clampedBudgetMs) {
                return;
            }
            break;
        }

        case DeferredLoadedProjectApplyState::Step::FinalizePipeline:
            m_DeferredLoadedProjectApply.statusText = "Applying editor state...";
            if (!FinalizeDeserializedPipeline(m_DeferredLoadedProjectApply.project->pipelineData, false)) {
                FailDeferredLoadedProjectApply("Failed to restore RAW layer state.");
                return;
            }
            // Keep rendering fenced until FinalizeBookkeeping installs the
            // candidate's project/session/source identity. Rendering here can
            // otherwise evaluate project B with project A's RAW metadata and
            // incorrectly count a blank or stale result as B's first frame.
            m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::RestorePersistedThumbnails;
            processedWorkThisFrame = true;
            break;

        case DeferredLoadedProjectApplyState::Step::RestorePersistedThumbnails: {
            m_DeferredLoadedProjectApply.statusText = "Restoring saved node previews...";
            if (m_DeferredLoadedProjectApply.nextThumbnailIndex == 0) {
                ResetNodeBrowserThumbnailState();
            }

            const auto& entries = m_DeferredLoadedProjectApply.project->nodeBrowserThumbnailEntries;
            if (m_DeferredLoadedProjectApply.nextThumbnailIndex >= entries.size()) {
                m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::FinalizeBookkeeping;
                processedWorkThisFrame = true;
                break;
            }

            const std::size_t restoreBatchCount = 8;
            RestorePersistedNodeBrowserThumbnailEntries(
                entries,
                m_DeferredLoadedProjectApply.nextThumbnailIndex,
                restoreBatchCount,
                m_DeferredLoadedProjectApply.nextThumbnailIndex);
            processedWorkThisFrame = true;
            if (m_DeferredLoadedProjectApply.nextThumbnailIndex >= entries.size()) {
                m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::FinalizeBookkeeping;
            }
            if (elapsedMs() >= clampedBudgetMs) {
                return;
            }
            break;
        }

        case DeferredLoadedProjectApplyState::Step::FinalizeBookkeeping: {
            auto& project = *m_DeferredLoadedProjectApply.project;
            m_DeferredLoadedProjectApply.statusText = "Applying editor state...";
            SetCurrentProjectName(project.projectName);
            SetCurrentProjectFileName(project.projectFileName);
            std::string rawSessionError;
            if (!ApplyLoadedRawProjectSessionMetadata(project, &rawSessionError)) {
                FailDeferredLoadedProjectApply(rawSessionError.empty()
                    ? "Failed to activate the loaded RAW project."
                    : rawSessionError);
                return;
            }
            m_Project->documentId = !project.projectId.empty()
                ? project.projectId
                : (project.rawProjectSnapshot &&
                   !project.rawProjectSnapshot->projectId.empty()
                    ? project.rawProjectSnapshot->projectId
                    : Stack::Project::GenerateStableUuid());
            m_Project->adoptionSourcePath = project.adoptedFrom;
            m_Project->editRevision = m_Project->adoptionSourcePath.empty()
                ? 0u
                : 1u;
            m_Project->saves.Reset(m_Project->documentId, 0);
            m_ProjectNamingPromptRequested = false;
            m_ProjectNamingPromptShown = true;
            ClearDirty();
            if (project.rawProjectSnapshot &&
                !ValidateAndRepairActiveRawProjectGraphBindings(
                    nullptr, &rawSessionError)) {
                FailDeferredLoadedProjectApply(rawSessionError.empty()
                    ? "Failed to validate multi-frame graph bindings."
                    : rawSessionError);
                return;
            }
            m_Project->lastEditTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
            m_Project->lastAutosaveTime = -1.0;
            m_Project->lastAutosaveAttemptTime = -1.0;
            if (IsRawWorkspaceProjectActive() &&
                !IsMultiFrameRawProjectActive()) {
                // Project opening is a presentation boundary: publish and
                // adopt a display-sized frame first, then immediately start
                // the cancelable native refinement.
                NoteRawWorkspaceProjectOpenPreview();
            }
            MarkRenderRefreshDirty();
            m_DeferredLoadedProjectApply.targetRenderRevision =
                m_RenderRevision;
            m_DeferredLoadedProjectApply.acceptedRenderGenerationAtStart =
                m_LastCompletedRenderGeneration;
            m_DeferredLoadedProjectApply.allowRenderSubmission = true;
            const bool typedMultiFrameProject =
                project.rawProjectSnapshot &&
                Stack::Project::IsMultiFrameProjectDocument(
                    *project.rawProjectSnapshot);
            // A single-source RAW project is not visually ready until its
            // first fenced presentation has actually been adopted. Native
            // refinement is now prohibited from superseding that initial fit
            // render, so this gate no longer risks the former endless-loading
            // race and it prevents the loader from revealing a blank canvas.
            m_DeferredLoadedProjectApply.step =
                m_Project->graph.IsOutputConnected() &&
                    !typedMultiFrameProject
                ? DeferredLoadedProjectApplyState::Step::WaitForFirstRender
                : DeferredLoadedProjectApplyState::Step::PrepareNodeBrowserThumbnails;
            processedWorkThisFrame = true;
            break;
        }

        case DeferredLoadedProjectApplyState::Step::PrepareNodeBrowserThumbnails: {
            m_DeferredLoadedProjectApply.statusText = "Preparing node browser thumbnails...";
            WarmNodeBrowserThumbnailPixelsAsync();
            m_DeferredLoadedProjectApply.step =
                Stack::EditorRenderScheduling::
                    ShouldBlockProjectReadyForOptionalThumbnails(
                        m_NodeBrowserThumbnailWarmPendingEntries,
                        m_NodeBrowserThumbnailPendingEntries)
                ? DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails
                : DeferredLoadedProjectApplyState::Step::Complete;
            processedWorkThisFrame = true;
            break;
        }

        case DeferredLoadedProjectApplyState::Step::WaitForFirstRender:
            m_DeferredLoadedProjectApply.statusText = "Rendering first frame...";
            {
            const bool awaitingRawProject =
                m_DeferredLoadedProjectApply.project &&
                (m_DeferredLoadedProjectApply.project->projectKind ==
                    StackBinaryFormat::kRawProjectKind ||
                 (m_DeferredLoadedProjectApply.project->rawWorkspaceData.is_object() &&
                  m_DeferredLoadedProjectApply.project->rawWorkspaceData.value(
                      "schema", std::string()) ==
                      "stack.rawWorkspace.project"));
            const bool presentationReady = !awaitingRawProject ||
                IsMultiFrameRawProjectActive() ||
                HasActiveRawWorkspacePresentationForValidation();
            if (Stack::EditorRenderScheduling::
                    ShouldAdvanceProjectLoadAfterFirstPresentation(
                        m_Project->graph.IsOutputConnected(),
                        m_DeferredLoadedProjectApply.targetRenderRevision,
                        m_LastSubmittedRenderRevision,
                        m_DeferredLoadedProjectApply
                            .acceptedRenderGenerationAtStart,
                        m_LastCompletedRenderGeneration,
                        presentationReady)) {
                // Rebuildable preview work is deliberately lower priority
                // than presenting the newly opened project viewport.
                m_DeferredLoadedProjectApply.step =
                    DeferredLoadedProjectApplyState::Step::PrepareNodeBrowserThumbnails;
                processedWorkThisFrame = true;
                continue;
            }
            return;
            }

        case DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails:
            m_DeferredLoadedProjectApply.statusText = "Preparing node browser thumbnails...";
            if (!Stack::EditorRenderScheduling::
                    ShouldBlockProjectReadyForOptionalThumbnails(
                        m_NodeBrowserThumbnailWarmPendingEntries,
                        m_NodeBrowserThumbnailPendingEntries)) {
                m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::Complete;
                processedWorkThisFrame = true;
                continue;
            }
            return;

        case DeferredLoadedProjectApplyState::Step::Complete:
            {
            auto completion =
                std::move(m_DeferredLoadedProjectApply.completion);
            m_DeferredLoadedProjectApply.active = false;
            m_DeferredLoadedProjectApply.failed = false;
            m_DeferredLoadedProjectApply.allowRenderSubmission = false;
            m_DeferredLoadedProjectApply.project.reset();
            m_DeferredLoadedProjectApply.rollbackProject.reset();
            m_DeferredLoadedProjectApply.layerArray = nlohmann::json::array();
            m_DeferredLoadedProjectApply.statusText = "Project ready.";
            CloseRawWorkspaceGalleryWorkspace();
            FinalizeDeferredRawWorkspaceProjectLoadIfNeeded();
            if (completion) {
                try {
                    completion(true, m_DeferredLoadedProjectApply.statusText);
                } catch (...) {
                }
            }
            processedWorkThisFrame = true;
            return;
            }

        case DeferredLoadedProjectApplyState::Step::Failed:
        case DeferredLoadedProjectApplyState::Step::None:
        default:
            return;
        }
    }
}

void EditorModule::PumpNonRenderingWork(double projectApplyBudgetMs, bool foreground) {
    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (m_LastNonRenderingPumpFrame == frame) {
            return;
        }
        m_LastNonRenderingPumpFrame = frame;
    }

    const bool deferGraphPanWork = foreground &&
        m_ActiveSubWindow == EditorSubWindow::NodeGraph &&
        m_Sidebar.GetNodeGraphUI().IsGraphMiddlePanActive();
    if (!deferGraphPanWork) {
        ConsumeRenderWorkerResults();
        ConsumeNodeBrowserThumbnailWorkerResults();
        if (foreground) m_Sidebar.GetNodeGraphUI().WarmPresetPreviewCache(this, 0.35);
    }

    if (m_Project->lifecycle.Phase() ==
            Stack::Project::ProjectLifecyclePhase::Saving &&
        !m_Project->saves.IsBusy() &&
        m_Project->lifecycle.RecoverOrphanedSave()) {
        m_Project->dirty = true;
        PostNotification(
            UiNotificationSeverity::Error,
            "The previous project save stopped without finishing. Your edits remain open and can be saved again.",
            "project-save-orphan-recovered");
    }

    // Autosave belongs to the project document, not to whichever view happens
    // to be visible. Keep this in the shared per-frame pump so a graph edit
    // still becomes durable after the user moves to RAW, Bracket, Queue, or
    // Library. Periodic saves wait for a genuine ten-second editing pause and
    // run at most once per minute; explicit Save, switching, and shutdown
    // retain their immediate flush paths. Only MarkDirty() advances the edit
    // clock, so ordinary browsing cannot manufacture another save.
    if (m_DocumentPersistenceEnabled && ImGui::GetCurrentContext()) {
        const double now = ImGui::GetTime();
        const ImGuiIO& io = ImGui::GetIO();
        bool anyPointerButtonDown = false;
        for (bool buttonDown : io.MouseDown) {
            anyPointerButtonDown = anyPointerButtonDown || buttonDown;
        }
        const bool editInteractionActive = foreground &&
            Stack::Project::ShouldDeferProjectAutosaveForForegroundInput(
                m_RawWorkspaceLocalRangeTargetDragging,
                ImGui::IsAnyItemActive(),
                anyPointerButtonDown,
                io.MouseWheel,
                io.MouseWheelH);
        const bool autosaveWorkBusy =
            m_Project->saves.IsBusy() ||
            editInteractionActive ||
            IsAnyRenderBackendBusy();
        const ProjectFileCommandContext saveContext =
            GetProjectFileCommandContext();
        const bool autosaveAttemptReady =
            m_Project->lastAutosaveAttemptTime < 0.0 ||
            now - m_Project->lastAutosaveAttemptTime >= 60.0;
        if (autosaveAttemptReady &&
            Stack::Project::IsProjectAutosaveDue(
                IsDirty(),
                !saveContext.projectPath.empty() ||
                    IsUnifiedProjectStoreActive(),
                autosaveWorkBusy,
                now,
                m_Project->lastEditTime,
                m_Project->lastAutosaveTime)) {
            if (EnqueueCurrentProjectSave(
                    Stack::Project::ProjectSaveReason::Autosave,
                    m_Project->name)) {
                m_Project->lastAutosaveAttemptTime = now;
            }
        }
    }

    if (foreground) RenderRawWorkspaceLifecyclePopups();
    TickRawWorkspacePersistence();
    TickRawWorkspacePreviewStaging();
    TickDeferredLoadedProjectApply(projectApplyBudgetMs);
    if (m_PendingRawWorkspaceDeferredProjectFinalize &&
        m_DeferredLoadedProjectApply.failed) {
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText =
            m_DeferredLoadedProjectApply.statusText.empty()
                ? "Failed to apply the RAW project. The previous project was restored."
                : m_DeferredLoadedProjectApply.statusText;
        if (IsRawWorkspaceProjectActive()) {
            m_RawWorkspace.selectedSourceKey = m_Project->rawSourceKey;
        }
        PostNotification(
            UiNotificationSeverity::Error,
            m_RawWorkspaceProjectLoadStatusText,
            "raw-workspace-project-load");
    }
    if (!deferGraphPanWork &&
        (!m_DeferredLoadedProjectApply.active || m_DeferredLoadedProjectApply.allowRenderSubmission)) {
        SubmitRenderIfReady();
    }
    if (!deferGraphPanWork) {
        ConsumeRenderWorkerResults();
        ConsumeNodeBrowserThumbnailWorkerResults();
    }
}

void EditorModule::RequestLoadSourceImage(const std::string& path) {
    if (path.empty()) {
        return;
    }

    if (Raw::RawLoader::IsRawPath(path)) {
        const Raw::LibRawRuntimeStatus& runtimeStatus = Raw::GetLibRawRuntimeStatus();
        if (!runtimeStatus.runtimeAvailable) {
            m_SourceLoadTaskState = Async::TaskState::Failed;
            m_SourceLoadStatusText = runtimeStatus.message;
            PostNotification(UiNotificationSeverity::Error, runtimeStatus.message, "editor-raw-runtime");
            return;
        }

        ++m_SourceLoadGeneration;
        const std::uint64_t generation = m_SourceLoadGeneration;
        m_SourceLoadTaskState = Async::TaskState::Queued;
        m_SourceLoadStatusText = "Decoding RAW source in the background...";
        bool submitted = false;
        try {
            submitted = ProjectTasks().SubmitHighPriority("Loading image",
                [this, generation, path]() mutable {
                    Raw::RawImageData rawData;
                    const bool loaded =
                        Raw::RawLoader::LoadFile(path, rawData) &&
                        rawData.metadata.error.empty();
                    std::string error = rawData.metadata.error;
                    Raw::RawMetadata metadata = std::move(rawData.metadata);
                    ProjectTasks().PostToMain(
                        [this,
                         generation,
                         path,
                         loaded,
                         error = std::move(error),
                         metadata = std::move(metadata)]() mutable {
                            if (generation != m_SourceLoadGeneration) return;
                            if (!loaded || !AddGraphRawChainFromMetadata(
                                    path,
                                    std::move(metadata),
                                    EditorNodeGraph::Vec2{ 20.0f, 120.0f })) {
                                m_SourceLoadTaskState = Async::TaskState::Failed;
                                m_SourceLoadStatusText = error.empty()
                                    ? "Failed to load RAW manual chain."
                                    : error;
                                PostNotification(
                                    UiNotificationSeverity::Error,
                                    m_SourceLoadStatusText,
                                    "editor-source-load");
                                return;
                            }
                            m_SourceLoadTaskState = Async::TaskState::Idle;
                            m_SourceLoadStatusText = "RAW manual chain loaded.";
                            SetCurrentProjectName("");
                            SetCurrentProjectFileName("");
                            MarkNodeBrowserThumbnailSourceChanged();
                            PostNotification(
                                UiNotificationSeverity::Success,
                                "RAW manual chain loaded.",
                                "editor-source-load");
                        });
                });
        } catch (...) {
            submitted = false;
        }
        if (!submitted && generation == m_SourceLoadGeneration) {
            m_SourceLoadTaskState = Async::TaskState::Failed;
            m_SourceLoadStatusText = "Could not queue the RAW source load.";
            PostNotification(
                UiNotificationSeverity::Error,
                m_SourceLoadStatusText,
                "editor-source-load");
        }
        return;
    }

    ++m_SourceLoadGeneration;
    const std::uint64_t generation = m_SourceLoadGeneration;
    m_SourceLoadTaskState = Async::TaskState::Queued;
    m_SourceLoadStatusText = "Loading source image in the background...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().SubmitHighPriority("Preparing image", [this, generation, path]() {
            auto postLoadFailure = [this, generation]() {
                return ProjectTasks().PostToMain([this, generation]() {
                    if (generation != m_SourceLoadGeneration) {
                        return;
                    }
                    m_SourceLoadTaskState = Async::TaskState::Failed;
                    m_SourceLoadStatusText = "Failed to load the selected source image.";
                    PostNotification(
                        UiNotificationSeverity::Error,
                        m_SourceLoadStatusText,
                        "editor-source-load");
                });
            };
            auto postEmbeddingFailure = [this, generation]() {
                return ProjectTasks().PostToMain([this, generation]() {
                    if (generation != m_SourceLoadGeneration) {
                        return;
                    }
                    EditorNodeGraph::Node* imageNode =
                        m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId());
                    if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image ||
                        !imageNode->image.isEmbedding ||
                        imageNode->image.embeddingRequestId != generation) {
                        return;
                    }
                    imageNode->image.isEmbedding = false;
                    imageNode->image.embeddingRequestId = 0;
                    m_SourceLoadStatusText =
                        "Source image loaded, but portable storage could not be prepared.";
                    PostNotification(
                        UiNotificationSeverity::Error,
                        "The source image loaded, but Stack could not embed it for project storage.",
                        "editor-source-load-embed");
                });
            };
            bool payloadQueued = false;
            try {
            DecodedImageData decoded;
            const bool success = DecodeImageFromFile(path, decoded);

            EditorNodeGraph::ImagePayload payload;
            std::vector<unsigned char> storagePixels;
            int width = 0;
            int height = 0;
            int channels = 4;
            if (success && !decoded.pixels.empty()) {
                payload.label = FileNameFromPath(path);
                payload.sourcePath = path;
                payload.width = decoded.width;
                payload.height = decoded.height;
                payload.channels = decoded.channels;
                payload.originalChannels = decoded.originalChannels;
                payload.sourceColorMetadata = EditorNodeGraph::InspectImageFileColorMetadata(
                    path, decoded.width, decoded.height, decoded.originalChannels);
                EditorNodeGraph::BuildImagePayloadPreview(
                    decoded.pixels,
                    decoded.width,
                    decoded.height,
                    decoded.channels,
                    payload.previewPixels,
                    payload.previewWidth,
                    payload.previewHeight,
                    payload.previewChannels);
                payload.pixels = std::move(decoded.pixels);
                payload.isEmbedding = true;
                payload.importRequestId = generation;
                payload.embeddingRequestId = generation;
                storagePixels = payload.pixels;
                width = payload.width;
                height = payload.height;
                channels = payload.channels;
            }

            payloadQueued = ProjectTasks().PostToMain([this, generation, path, payload = std::move(payload), success]() mutable {
                if (generation != m_SourceLoadGeneration) {
                    return;
                }

                if (!success || payload.pixels.empty()) {
                    m_SourceLoadTaskState = Async::TaskState::Failed;
                    m_SourceLoadStatusText = "Failed to load the selected source image.";
                    PostNotification(UiNotificationSeverity::Error, "Failed to load the selected source image.", "editor-source-load");
                    return;
                }

                m_SourceLoadTaskState = Async::TaskState::Applying;
                m_SourceLoadStatusText = "Applying source image to the editor...";

                LoadSourceFromPixels(payload.pixels.data(), payload.width, payload.height, payload.channels);

                EditorNodeGraph::Node* imageNode = m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId());
                if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image) {
                    m_Project->graph.ResetFromLayers(static_cast<int>(m_Project->layers.size()), true);
                    imageNode = m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId());
                } else if (!m_Project->graph.IsOutputConnected()) {
                    m_Project->graph.RebuildLinks();
                }

                if (imageNode) {
                    imageNode->title = payload.label.empty() ? "Image" : payload.label;
                    imageNode->image = std::move(payload);
                    m_Project->graph.SetActiveImageNodeId(imageNode->id);
                }
                SetCurrentProjectName("");
                SetCurrentProjectFileName("");
                MarkNodeBrowserThumbnailSourceChanged();
                MarkDirty();

                m_SourceLoadTaskState = Async::TaskState::Idle;
                m_SourceLoadStatusText = "Source image loaded; preparing portable storage...";
                PostNotification(UiNotificationSeverity::Success, "Source image loaded.", "editor-source-load");
            });
            if (!payloadQueued) {
                return;
            }

            if (!success || storagePixels.empty()) {
                return;
            }

            std::vector<unsigned char> pngBytes =
                EncodePngBytesForImageStorageOwned(std::move(storagePixels), width, height, channels);
            ProjectTasks().PostToMain([
                this,
                generation,
                pngBytes = std::move(pngBytes)
            ]() mutable {
                if (generation != m_SourceLoadGeneration) {
                    return;
                }
                EditorNodeGraph::Node* imageNode = m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId());
                if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image ||
                    !imageNode->image.isEmbedding || imageNode->image.embeddingRequestId != generation) {
                    return;
                }

                if (pngBytes.empty()) {
                    imageNode->image.isEmbedding = false;
                    imageNode->image.embeddingRequestId = 0;
                    m_SourceLoadStatusText = "Source image loaded, but portable storage could not be prepared.";
                    PostNotification(
                        UiNotificationSeverity::Error,
                        "The source image loaded, but Stack could not embed it for project storage.",
                        "editor-source-load-embed");
                    return;
                }

                imageNode->image.pngBytes = std::move(pngBytes);
                imageNode->image.isEmbedding = false;
                imageNode->image.embeddingRequestId = 0;
                m_SourceLoadStatusText = "Source image loaded.";
            });
            } catch (...) {
                if (payloadQueued) {
                    postEmbeddingFailure();
                } else {
                    postLoadFailure();
                }
            }
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted && generation == m_SourceLoadGeneration) {
        m_SourceLoadTaskState = Async::TaskState::Failed;
        m_SourceLoadStatusText = "Could not queue the source image load.";
        PostNotification(
            UiNotificationSeverity::Error,
            m_SourceLoadStatusText,
            "editor-source-load");
    }
}

bool EditorModule::ExportImage(const std::string& path) {
    return RequestExportImage(path);
}

bool EditorModule::RequestQueueExportImage(const std::string& path) {
    const bool previous = m_SuppressExportProjectCheckpoint;
    m_SuppressExportProjectCheckpoint = true;
    const bool requested = RequestExportImage(path);
    m_SuppressExportProjectCheckpoint = previous;
    return requested;
}

void EditorModule::BeginPngExportWrite(
    std::string path,
    std::vector<unsigned char> pixels,
    int width,
    int height,
    Stack::NodeMath::PngColorMetadataChunks colorChunks,
    std::uint64_t generation) {
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing PNG export in the background...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Exporting",[
            this,
            generation,
            path = std::move(path),
            width,
            height,
            colorChunks = std::move(colorChunks),
            pixels = std::move(pixels)
        ]() mutable {
            bool success = false;
            std::string errorMsg;

            try {
                const std::filesystem::path destination =
                    std::filesystem::u8path(path);
                const std::vector<unsigned char> basePng =
                    EncodePngBytes(pixels, width, height, 4);
                std::vector<Stack::NodeMath::ContractIssue> metadataIssues;
                const std::vector<unsigned char> outputPng =
                    Stack::NodeMath::InsertPngColorMetadataChunks(
                        basePng, colorChunks, metadataIssues);
                if (outputPng.empty()) {
                    errorMsg = metadataIssues.empty()
                        ? "PNG encoding produced no data."
                        : metadataIssues.front().message;
                } else {
                    success = WriteExportFileAtomically(
                        destination, outputPng, errorMsg);
                }
            } catch (const std::exception& e) {
                errorMsg = std::string("Exception: ") + e.what();
            } catch (...) {
                errorMsg = "Unknown exception during export.";
            }

            if (!success) {
                std::cerr
                    << "[EditorModule] Export failed for path: " << path
                    << ". Reason: " << errorMsg << std::endl;
            }

            ProjectTasks().PostToMain([
                this,
                generation,
                success,
                path = std::move(path),
                errorMsg = std::move(errorMsg)
            ]() {
                if (generation != m_ExportGeneration) {
                    return;
                }
                if (success) {
                    m_ExportTaskState = Async::TaskState::Idle;
                    m_ExportStatusText = "Rendered image exported.";
                    PostNotification(
                        UiNotificationSeverity::Success,
                        "Rendered image exported.",
                        "editor-export-image");
                } else {
                    m_ExportTaskState = Async::TaskState::Failed;
                    m_ExportStatusText = "Could not export \"" +
                        FileNameFromPath(path) + "\": " +
                        (errorMsg.empty()
                            ? std::string("The PNG could not be written.")
                            : errorMsg);
                    PostNotification(
                        UiNotificationSeverity::Error,
                        m_ExportStatusText,
                        "editor-export-image");
                }
            });
        });
    } catch (...) {
        submitted = false;
    }

    if (!submitted && generation == m_ExportGeneration) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Could not queue the image export.";
        PostNotification(
            UiNotificationSeverity::Error,
            m_ExportStatusText,
            "editor-export-image");
    }
}

void EditorModule::CompleteRawWorkspaceExportRender(
    EditorRenderWorker::Result& result,
    bool sourceMatchesActive) {
    if (!m_RawWorkspaceExportRenderRequested ||
        result.rawRenderPurpose != RawRenderPurpose::ExplicitExport ||
        result.generation != m_RawWorkspaceExportRenderGeneration) {
        return;
    }

    m_RawWorkspaceExportRenderRequested = false;
    m_RawWorkspaceExportRenderGeneration = 0;
    const bool rasterValid = result.success && sourceMatchesActive &&
        Stack::PixelBuffer::HasCompletePixelBuffer(
            result.pixels.size(), result.width, result.height, 4);
    if (!rasterValid) {
        m_RawWorkspaceExportPath.clear();
        m_RawWorkspaceExportColorChunks = {};
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = result.error.empty()
            ? "The authoritative RAW export render failed."
            : result.error;
        PostNotification(
            UiNotificationSeverity::Error,
            m_ExportStatusText,
            "editor-export-image");
        return;
    }

    std::string path = std::move(m_RawWorkspaceExportPath);
    m_RawWorkspaceExportPath.clear();
    Stack::NodeMath::PngColorMetadataChunks colorChunks =
        std::move(m_RawWorkspaceExportColorChunks);
    m_RawWorkspaceExportColorChunks = {};
    BeginPngExportWrite(
        std::move(path),
        std::move(result.pixels),
        result.width,
        result.height,
        std::move(colorChunks),
        m_ExportGeneration);
}

bool EditorModule::RequestExportImage(const std::string& path) {
    if(HasPendingBracketingDraft()) {
        m_ExportTaskState=Async::TaskState::Failed;
        m_ExportStatusText="Process or discard pending Bracketing input changes before export.";
        PostNotification(UiNotificationSeverity::Error,m_ExportStatusText,"bracketing-draft-export");
        return false;
    }
    if(IsBracketingActive()&&(!m_HdrAdoptedRawResult||
        m_HdrAdoptedRawResult->inputRevision!=m_Project->snapshot->hdrInputRevision)) {
        m_ExportTaskState=Async::TaskState::Failed;
        m_ExportStatusText="Wait for the current bracket result, or retry processing, before export.";
        PostNotification(UiNotificationSeverity::Error,m_ExportStatusText,"bracketing-result-export");
        return false;
    }
    if (path.empty()) {
        return false;
    }

    if (Async::IsBusy(m_ExportTaskState)) {
        return false;
    }
    if (m_Project->rawInteractionDraft.active &&
        !ResolveRawWorkspaceInteractionDraft(false)) {
        return false;
    }
    if (IsRawWorkspaceProjectActive() &&
        !Stack::RawRecipe::IsViewTransformEnabled(
            m_Project->rawRecipe)) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "View is bypassed. The current 8-bit PNG encoder clips scene-linear negatives and highlight headroom.",
            "raw-export-view-bypassed-clipping");
    }

    Stack::NodeMath::PngColorMetadataChunks colorChunks;
    if (!IsCompositeViewportMode()) {
        const RenderGraphSnapshot semanticSnapshot = BuildGraphSnapshot();
        if (m_Project->graph.IsOutputChannelInspection(
                semanticSnapshot.outputNodeId)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText =
                "A Channel can be inspected in Output, but PNG export "
                "requires an Image. Connect the Channel to Image Combine "
                "first.";
            PostNotification(
                UiNotificationSeverity::Error,
                m_ExportStatusText,
                "editor-export-channel-unsupported");
            return false;
        }
        const Stack::NodeMath::DirectOutputPolicy outputPolicy =
            Stack::NodeMath::EvaluateDirectPngOutputPolicy(semanticSnapshot.outputDescriptor);
        if (!outputPolicy.executable) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText = outputPolicy.diagnostics.empty()
                ? "The declared graph output cannot be written as PNG."
                : outputPolicy.diagnostics.front().message;
            PostNotification(
                UiNotificationSeverity::Error,
                m_ExportStatusText,
                "editor-export-semantic-policy");
            return false;
        }
        colorChunks.writeSrgb = outputPolicy.writeSrgbChunk;
        colorChunks.writeDisplayP3Cicp = outputPolicy.writeDisplayP3Cicp;
        if (outputPolicy.writeRetainedIccProfile) {
            for (const EditorNodeGraph::Node& node : m_Project->graph.GetNodes()) {
                if (node.kind != EditorNodeGraph::NodeKind::Image ||
                    node.image.sourceColorMetadata.dependencyIdentity !=
                        outputPolicy.retainedProfileDependency ||
                    node.image.sourceColorMetadata.payloadKind !=
                        Stack::NodeMath::EmbeddedColorPayloadKind::PngIccpChunk) {
                    continue;
                }
                colorChunks.retainedIccpPayload =
                    node.image.sourceColorMetadata.retainedPayload;
                break;
            }
            if (colorChunks.retainedIccpPayload.empty()) {
                PostNotification(
                    UiNotificationSeverity::Warning,
                    "The graph declares an ICC-based output, but reusable PNG profile bytes are unavailable. The direct result will be exported untagged.",
                    "editor-export-profile-unavailable");
            }
        }
        if (!colorChunks.writeSrgb && !colorChunks.writeDisplayP3Cicp &&
            colorChunks.retainedIccpPayload.empty()) {
            PostNotification(
                UiNotificationSeverity::Warning,
                "The graph output color state is Unknown or has no writable profile. Stack will export the direct result untagged without guessing.",
                "editor-export-untagged");
        }
    }

    if (IsRawWorkspaceProjectActive() && !IsCompositeViewportMode()) {
        if (m_RawRenderClientId == 0) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText =
                "The RAW render service is unavailable for authoritative export.";
            PostNotification(
                UiNotificationSeverity::Error,
                m_ExportStatusText,
                "editor-export-image");
            return false;
        }
        if (!m_SuppressExportProjectCheckpoint &&
            !m_Project->name.empty() &&
            !RequestSaveCurrentProject(m_Project->name)) {
            PostNotification(
                UiNotificationSeverity::Warning,
                "The project checkpoint could not be started. The current RAW recipe will still be exported.",
                "editor-export-save-warning");
        }
        ++m_ExportGeneration;
        m_RawWorkspaceExportRenderRequested = true;
        m_RawWorkspaceExportRenderGeneration = 0;
        m_RawWorkspaceExportPath = path;
        m_RawWorkspaceExportColorChunks = std::move(colorChunks);
        m_ExportTaskState = Async::TaskState::Running;
        m_ExportStatusText = "Rendering the authoritative RAW export...";
        MarkRenderRefreshDirty();
        return true;
    }

    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = IsCompositeViewportMode()
        ? "Capturing the composite export..."
        : "Capturing the rendered image...";

    if (IsCompositeViewportMode()) {
        if (!BuildCompositeExportRaster(pixels, width, height)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText = "Failed to capture the composite export.";
            PostNotification(UiNotificationSeverity::Error, "Failed to capture the composite export.", "editor-export-image");
            return false;
        }
    } else {
        if (!BuildSingleOutputExportRaster(pixels, width, height)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText = "Failed to capture the rendered export.";
            PostNotification(UiNotificationSeverity::Error, "Failed to capture the rendered export.", "editor-export-image");
            return false;
        }
    }

    if (pixels.empty()) {
        std::cerr << "[EditorModule] RequestExportImage: Failed to capture pixels (empty output). Width="
                  << width << ", Height=" << height << std::endl;
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to capture the rendered image.";
        PostNotification(UiNotificationSeverity::Error, "Failed to capture the rendered image.", "editor-export-image");
        return false;
    }

    if (!m_SuppressExportProjectCheckpoint &&
        !m_Project->name.empty() &&
        !RequestSaveCurrentProject(m_Project->name)) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "The project checkpoint could not be started. The captured image will still be exported.",
            "editor-export-save-warning");
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    BeginPngExportWrite(
        path,
        std::move(pixels),
        width,
        height,
        std::move(colorChunks),
        generation);
    return Async::IsBusy(m_ExportTaskState);
}

bool EditorModule::BuildProjectDocumentForSave(
    const std::string& displayName,
    StackFormat::ProjectDocument& outDocument) {
    const std::string trimmedName = displayName.empty() ? "Untitled Project" : displayName;
    const StackFormat::json pipeline = SerializePipeline();

    if (m_Project->snapshot && m_Project->store) {
        outDocument = {};
        outDocument.metadata.projectKind = StackFormat::kRawProjectKind;
        outDocument.metadata.projectName = trimmedName;
        outDocument.metadata.timestamp = BuildTimestampString();
        outDocument.metadata.sourceWidth = 1;
        outDocument.metadata.sourceHeight = 1;
        outDocument.thumbnailBytes = m_Project->snapshot->coverThumbnailBytes;
        outDocument.pipelineData = pipeline;
        outDocument.rawWorkspaceData = m_Project->snapshot->rawWorkspaceData;
        if (!outDocument.rawWorkspaceData.is_object()) {
            outDocument.rawWorkspaceData = StackFormat::json::object();
        }
        outDocument.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
        outDocument.rawWorkspaceData["schemaVersion"] =
            Stack::Project::kRawWorkspaceProjectSchemaVersion;
        outDocument.rawWorkspaceData["rawProjectModel"] =
            Stack::Project::kRawProjectModelSourceSets;
        outDocument.projectStore = m_Project->store;
        outDocument.rawProjectSnapshot =
            std::make_shared<Stack::Project::RawProjectSnapshot>(
                *m_Project->snapshot);
        outDocument.rawProjectSnapshot->projectName = trimmedName;
        outDocument.rawProjectSnapshot->pipelineData = pipeline;
        outDocument.rawProjectSnapshot->rawWorkspaceData =
            outDocument.rawWorkspaceData;
        return true;
    }

    int renderedW = 0;
    int renderedH = 0;
    std::vector<unsigned char> renderedPixels;
    const bool compositeProject = GetViewportMode() == ViewportMode::CompositeCanvas;
    if (compositeProject) {
        BuildCompositeExportRaster(renderedPixels, renderedW, renderedH);
    } else {
        renderedPixels = m_Pipeline.GetOutputPixels(renderedW, renderedH);
        if ((renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) && m_Project->graph.IsOutputConnected()) {
            BuildSingleOutputExportRaster(renderedPixels, renderedW, renderedH);
        }
    }
    if (renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) {
        // A graph-only Editor project is still a valid project. Persist its
        // topology with a neutral placeholder raster until it has a rendered
        // output, rather than making project safety depend on graph wiring.
        renderedW = 1;
        renderedH = 1;
        renderedPixels = BuildTransparentPixels(renderedW, renderedH);
    }

    int sourceW = 0;
    int sourceH = 0;
    std::vector<unsigned char> sourcePixels;
    std::vector<unsigned char> sourcePngBytes;
    if (compositeProject) {
        sourceW = renderedW;
        sourceH = renderedH;
        sourcePixels = BuildTransparentPixels(sourceW, sourceH);
    } else {
        if (const EditorNodeGraph::Node* activeImageNode = m_Project->graph.FindNode(m_Project->graph.GetActiveImageNodeId());
            activeImageNode &&
            activeImageNode->kind == EditorNodeGraph::NodeKind::Image &&
            !activeImageNode->image.pngBytes.empty() &&
            activeImageNode->image.width > 0 &&
            activeImageNode->image.height > 0) {
            sourceW = activeImageNode->image.width;
            sourceH = activeImageNode->image.height;
            sourcePngBytes = activeImageNode->image.pngBytes;
        }
        if (sourcePngBytes.empty()) {
            sourcePixels = m_Pipeline.GetSourcePixels(sourceW, sourceH);
        }
        if (sourcePixels.empty() && sourcePngBytes.empty()) {
            sourceW = renderedW;
            sourceH = renderedH;
            sourcePixels = BuildTransparentPixels(sourceW, sourceH);
        }
    }

    outDocument = {};
    outDocument.metadata.projectKind = StackFormat::kEditorProjectKind;
    outDocument.metadata.projectName = trimmedName;
    outDocument.metadata.timestamp = BuildTimestampString();
    outDocument.metadata.sourceWidth = sourceW;
    outDocument.metadata.sourceHeight = sourceH;
    outDocument.thumbnailBytes = BuildThumbnailBytes(renderedPixels, renderedW, renderedH);
    outDocument.sourceImageBytes = !sourcePngBytes.empty()
        ? std::move(sourcePngBytes)
        : EncodePngBytes(sourcePixels, sourceW, sourceH, 4);
    outDocument.pipelineData = pipeline;
    outDocument.nodeBrowserThumbnailEntries = GetPersistedNodeBrowserThumbnails();
    if (IsRawWorkspaceProjectActive()) {
        if (const Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(m_Project->rawSourceKey)) {
            Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
                *source,
                m_Project->rawRecipe,
                pipeline,
                outDocument,
                m_Project->rawMode,
                source->project.status != Stack::RawWorkspace::ProjectStatus::Embedded);
            ApplyActiveRawWorkspaceModeDataToDocument(outDocument);
        }
    }
    return !outDocument.sourceImageBytes.empty();
}

bool EditorModule::RequestExportProject(const std::string& path) {
    if (path.empty() || Async::IsBusy(m_ExportTaskState)) {
        return false;
    }
    if (HasPendingGraphImageImports()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finishing imported slices before exporting the project.",
            "editor-graph-image-export-wait");
        return false;
    }

    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = "Packaging project file...";

    const std::string exportName = m_Project->name.empty() ? "Untitled Project" : m_Project->name;
    StackFormat::ProjectDocument document;
    if (!BuildProjectDocumentForSave(exportName, document)) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to package the current project.";
        PostNotification(UiNotificationSeverity::Error, "Failed to package the current project.", "editor-export-project");
        return false;
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing project file...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Saving",
            [this, generation, path, document = std::move(document)]() mutable {
                bool success = false;
                try {
                    const std::filesystem::path destination(path);
                    if (destination.has_parent_path()) {
                        std::filesystem::create_directories(destination.parent_path());
                    }
                    success = StackFormat::WriteProjectFile(destination, document);
                } catch (...) {
                    success = false;
                }

                ProjectTasks().PostToMain([this, generation, path, success]() {
                    if (generation != m_ExportGeneration) {
                        return;
                    }

                    if (success) {
                        m_ExportTaskState = Async::TaskState::Idle;
                        m_ExportStatusText = "Project exported.";
                        PostNotification(UiNotificationSeverity::Success, "Project exported.", "editor-export-project");
                        if (m_Project->name.empty()) {
                            const std::string stem = std::filesystem::path(path).stem().string();
                            m_Project->name = stem.empty() ? std::string("Untitled Project") : stem;
                        }
                    } else {
                        m_ExportTaskState = Async::TaskState::Failed;
                        m_ExportStatusText = "Failed to write the project file.";
                        PostNotification(UiNotificationSeverity::Error, "Failed to write the project file.", "editor-export-project");
                    }
                });
            });
    } catch (...) {
        submitted = false;
    }

    if (submitted) {
        return true;
    }
    if (generation == m_ExportGeneration) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Could not queue the project export.";
        PostNotification(
            UiNotificationSeverity::Error,
            m_ExportStatusText,
            "editor-export-project");
    }
    return false;
}
