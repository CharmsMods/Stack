#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
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
    json serialized = json::array();
    for (auto& layer : m_Layers) {
        serialized.push_back(layer->Serialize());
    }
    nlohmann::json payload = EditorNodeGraph::SerializeGraphPayload(serialized, m_NodeGraph);
    payload["editorComposite"] = SerializeCompositePersistence();
    payload["editorTimeline"] = SerializeTimelinePersistence();
    return payload;
}

void EditorModule::ResetForPipelineDeserialization() {
    ResetRenderSubmissionState();
    ClearAutoGainMaskPreview();
    m_CompositeSelectedOutputNodeId = -1;
    ClearRawWorkspaceLivePreviewState();
    m_Pipeline.Clear();
    m_Pipeline.ClearOutput();
    m_CompositePreviewPipeline.Clear();
    m_CompositePreviewPipeline.ClearOutput();
    m_NodeGraph.Clear();
    m_Layers.clear();
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
    m_TimelineAnimation = {};
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
    m_Layers.push_back(newLayer);
    return true;
}

bool EditorModule::FinalizeDeserializedPipeline(const nlohmann::json& serialized, bool restoreSourceFromGraphState) {
    m_SelectedLayerIndex = m_Layers.empty() ? -1 : 0;

    int sourceWidth = 0;
    int sourceHeight = 0;
    const std::vector<unsigned char>& sourcePixels = m_Pipeline.GetSourcePixelsRaw();
    if (!sourcePixels.empty()) {
        sourceWidth = m_Pipeline.GetCanvasWidth();
        sourceHeight = m_Pipeline.GetCanvasHeight();
    }

    EditorNodeGraph::DeserializeGraphPayload(
        serialized,
        m_NodeGraph,
        static_cast<int>(m_Layers.size()),
        sourcePixels,
        sourceWidth,
        sourceHeight,
        m_Pipeline.GetSourceChannels());
    RefreshDeserializedRawRuntimeErrors(m_NodeGraph);

    DeserializeCompositePersistence(serialized);
    DeserializeTimelinePersistence(serialized);
    RefreshGraphLayerMetadata();

    const int activeImageNodeId = m_NodeGraph.GetActiveImageNodeId();
    if (restoreSourceFromGraphState && activeImageNodeId > 0) {
        if (EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(activeImageNodeId)) {
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
    MarkRenderDirty();
    return true;
}

void EditorModule::DeserializePipeline(const nlohmann::json& serialized) {
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
            MarkRenderDirty();
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
            MarkRenderDirty();
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
    MarkRenderDirty();
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
    const bool hasSharedSource = !projectData.sourcePixelsShared.empty();
    if ((!hasSharedSource && projectData.sourcePixels.empty()) ||
        projectData.width <= 0 ||
        projectData.height <= 0) {
        return false;
    }

    CancelMfdExperimentalProcessing({}, true);

    std::string rawSessionError;
    if (!ApplyLoadedRawProjectSessionMetadata(projectData, &rawSessionError)) {
        return false;
    }
    m_RawWorkspacePipelineActive = false;
    ResetForPipelineDeserialization();
    if (hasSharedSource) {
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
        MarkRenderDirty();
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
    FinalizeDeserializedPipeline(projectData.pipelineData, false);
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
    ClearDirty();
    if (projectData.rawProjectSnapshot &&
        !ValidateAndRepairActiveRawProjectGraphBindings(nullptr, &rawSessionError)) {
        return false;
    }
    WarmNodeBrowserThumbnailPixelsAsync();
    EnsureNodeBrowserThumbnailCatalog();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_LastAutoSaveTime = -1.0;
    return true;
}

bool EditorModule::EnterRawWorkspaceRootTab() {
    if (m_RawWorkspaceRootTabActive) {
        return true;
    }

    m_RawWorkspaceRootTabActive = true;
    // RAW, RAW Lab, and Editor are views over one project. A normal Editor
    // project remains live while the RAW presentation is visibly locked; a
    // RAW project/preview keeps the exact same graph and render caches.
    m_RawWorkspaceLockedByEditorProject =
        !IsRawWorkspaceProjectActive() && HasProjectContent();
    if (!m_PendingRawWorkspaceExplicitOpenSourceKey.empty()) {
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
    return true;
}

bool EditorModule::LeaveRawWorkspaceRootTab(bool enteringEditorTab) {
    (void)enteringEditorTab;

    auto closeRawWorkspaceWindows = [this]() {
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            CloseRawWorkspaceLabNativeGallery();
        }
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
        m_RawWorkspaceGalleryWindowOpen = false;
    };
    closeRawWorkspaceWindows();
    m_RawWorkspaceRootTabActive = false;
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
}

bool EditorModule::BeginDeferredLoadedProjectApply(std::shared_ptr<LoadedProjectData> projectData) {
    ResetDeferredLoadedProjectApplyState();
    if (!projectData ||
        (projectData->sourcePixels.empty() && projectData->sourcePixelsShared.empty()) ||
        projectData->width <= 0 ||
        projectData->height <= 0) {
        FailDeferredLoadedProjectApply("Failed to apply the loaded project.");
        return false;
    }

    const bool isRawProject =
        projectData->projectKind == StackFormat::kRawProjectKind ||
        (projectData->rawWorkspaceData.is_object() &&
         projectData->rawWorkspaceData.value("schema", std::string()) ==
             "stack.rawWorkspace.project");
    if (isRawProject && !projectData->rawProjectSnapshot) {
        StackFormat::ProjectDocument rawDocument;
        rawDocument.rawWorkspaceData = projectData->rawWorkspaceData;
        Stack::RawWorkspace::ProjectInfo rawInfo;
        if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(
                rawDocument,
                rawInfo,
                nullptr)) {
            FailDeferredLoadedProjectApply(rawInfo.errorMessage.empty()
                ? "The project does not contain valid RAW Workspace metadata."
                : rawInfo.errorMessage);
            return false;
        }
        if (rawInfo.mode == Stack::RawWorkspace::RawProjectMode::CustomGraph ||
            rawInfo.mode == Stack::RawWorkspace::RawProjectMode::Unknown) {
            FailDeferredLoadedProjectApply(
                rawInfo.mode == Stack::RawWorkspace::RawProjectMode::CustomGraph
                    ? "This legacy custom RAW graph cannot be converted safely and was not opened."
                    : "This project uses an unsupported RAW Workspace mode and was not opened.");
            return false;
        }
    }

    const nlohmann::json layerArray =
        EditorNodeGraph::ExtractLayerArray(projectData->pipelineData);
    if (!layerArray.is_array()) {
        FailDeferredLoadedProjectApply("Failed to read the project's editor state.");
        return false;
    }

    if (HasProjectContent()) {
        auto rollbackProject = std::make_shared<LoadedProjectData>();
        int sourceWidth = 0;
        int sourceHeight = 0;
        int sourceChannels = 4;
        rollbackProject->sourcePixelsShared = m_Pipeline.ShareSourcePixels(
            sourceWidth,
            sourceHeight,
            sourceChannels);
        if (rollbackProject->sourcePixelsShared.empty()) {
            rollbackProject->sourcePixels.assign(4, 0);
            sourceWidth = 1;
            sourceHeight = 1;
            sourceChannels = 4;
        }
        rollbackProject->width = sourceWidth;
        rollbackProject->height = sourceHeight;
        rollbackProject->channels = sourceChannels;
        rollbackProject->pipelineData = SerializePipeline();
        rollbackProject->nodeBrowserThumbnailEntries =
            GetPersistedNodeBrowserThumbnails();
        rollbackProject->projectName = m_CurrentProjectName;
        rollbackProject->projectFileName = m_CurrentProjectFileName;
        if (IsRawWorkspaceProjectActive()) {
            if (m_ActiveRawProjectSnapshot && m_ActiveRawProjectStore) {
                rollbackProject->rawProjectSnapshot =
                    std::make_shared<Stack::Project::RawProjectSnapshot>(
                        *m_ActiveRawProjectSnapshot);
                rollbackProject->projectStore = m_ActiveRawProjectStore;
                rollbackProject->rawWorkspaceData =
                    m_ActiveRawProjectSnapshot->rawWorkspaceData;
                rollbackProject->projectKind = StackFormat::kRawProjectKind;
            }
            const Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
            if (!m_ActiveRawProjectSnapshot && source &&
                m_ActiveRawWorkspaceMode !=
                    Stack::RawWorkspace::RawProjectMode::CustomGraph) {
                StackFormat::ProjectDocument rollbackDocument;
                Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
                    *source,
                    m_ActiveRawWorkspaceRecipe,
                    nlohmann::json::object(),
                    rollbackDocument,
                    m_ActiveRawWorkspaceMode,
                    source->project.status !=
                        Stack::RawWorkspace::ProjectStatus::Embedded);
                rollbackProject->rawWorkspaceData =
                    std::move(rollbackDocument.rawWorkspaceData);
                rollbackProject->projectKind = StackFormat::kRawProjectKind;
            }
        }
        m_DeferredLoadedProjectApply.rollbackProject =
            std::move(rollbackProject);
        m_DeferredLoadedProjectApply.rollbackDirty = m_Dirty;
    }

    // The candidate is structurally valid and project identity is now about
    // to change. Stop any source-set worker before deserialization begins;
    // generation fencing prevents a stale completion from publishing into
    // the replacement project.
    CancelMfdExperimentalProcessing("The editing project is switching.", true);
    m_RawWorkspacePipelineActive = false;
    m_DeferredLoadedProjectApply.active = true;
    m_DeferredLoadedProjectApply.project = std::move(projectData);
    m_DeferredLoadedProjectApply.layerArray = layerArray;

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
            if (!project.sourcePixelsShared.empty()) {
                m_Pipeline.LoadSourceFromSharedPixels(
                    project.sourcePixelsShared,
                    project.width,
                    project.height,
                    project.channels);
                ClearCompositeSceneTextures();
                MarkRenderDirty();
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
            FinalizeDeserializedPipeline(m_DeferredLoadedProjectApply.project->pipelineData, false);
            if (m_RawWorkspaceRootTabActive &&
                m_PendingRawWorkspaceDeferredProjectFinalize) {
                // The first RAW frame must use RAW presentation/render
                // ownership even though thumbnail warm-up and bookkeeping are
                // still completing.
                m_RawWorkspacePipelineActive = true;
            }
            m_DeferredLoadedProjectApply.targetRenderRevision = m_RenderRevision;
            m_DeferredLoadedProjectApply.allowRenderSubmission = true;
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
            std::string migrationError;
            if (!MigrateLoadedManagedRawProject(project, &migrationError)) {
                FailDeferredLoadedProjectApply(migrationError.empty()
                    ? "The legacy RAW project could not be migrated safely."
                    : migrationError);
                return;
            }
            SetCurrentProjectName(project.projectName);
            SetCurrentProjectFileName(project.projectFileName);
            std::string rawSessionError;
            if (!ApplyLoadedRawProjectSessionMetadata(project, &rawSessionError)) {
                FailDeferredLoadedProjectApply(rawSessionError.empty()
                    ? "Failed to activate the loaded RAW project."
                    : rawSessionError);
                return;
            }
            ClearDirty();
            if (project.rawProjectSnapshot &&
                !ValidateAndRepairActiveRawProjectGraphBindings(
                    nullptr, &rawSessionError)) {
                FailDeferredLoadedProjectApply(rawSessionError.empty()
                    ? "Failed to validate multi-frame graph bindings."
                    : rawSessionError);
                return;
            }
            m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
            m_LastAutoSaveTime = -1.0;
            m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::PrepareNodeBrowserThumbnails;
            processedWorkThisFrame = true;
            break;
        }

        case DeferredLoadedProjectApplyState::Step::PrepareNodeBrowserThumbnails:
            m_DeferredLoadedProjectApply.statusText = "Preparing node browser thumbnails...";
            WarmNodeBrowserThumbnailPixelsAsync();
            EnsureNodeBrowserThumbnailCatalog();
            m_DeferredLoadedProjectApply.step = m_NodeGraph.IsOutputConnected()
                ? DeferredLoadedProjectApplyState::Step::WaitForFirstRender
                : DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails;
            processedWorkThisFrame = true;
            break;

        case DeferredLoadedProjectApplyState::Step::WaitForFirstRender:
            m_DeferredLoadedProjectApply.statusText = "Rendering first frame...";
            if (!m_NodeGraph.IsOutputConnected() ||
                (m_LastSubmittedRenderRevision >= m_DeferredLoadedProjectApply.targetRenderRevision &&
                 !m_RenderDirty &&
                 !m_RenderPending &&
                 !m_RenderWorker.IsBusy())) {
                m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails;
                processedWorkThisFrame = true;
                continue;
            }
            return;

        case DeferredLoadedProjectApplyState::Step::WaitForNodeBrowserThumbnails:
            m_DeferredLoadedProjectApply.statusText = "Preparing node browser thumbnails...";
            if (m_NodeBrowserThumbnailWarmPendingEntries == 0 &&
                m_NodeBrowserThumbnailPendingEntries == 0) {
                m_DeferredLoadedProjectApply.step = DeferredLoadedProjectApplyState::Step::Complete;
                processedWorkThisFrame = true;
                continue;
            }
            return;

        case DeferredLoadedProjectApplyState::Step::Complete:
            m_DeferredLoadedProjectApply.active = false;
            m_DeferredLoadedProjectApply.failed = false;
            m_DeferredLoadedProjectApply.allowRenderSubmission = false;
            m_DeferredLoadedProjectApply.project.reset();
            m_DeferredLoadedProjectApply.rollbackProject.reset();
            m_DeferredLoadedProjectApply.layerArray = nlohmann::json::array();
            m_DeferredLoadedProjectApply.statusText = "Project ready.";
            FinalizeDeferredRawWorkspaceProjectLoadIfNeeded();
            processedWorkThisFrame = true;
            return;

        case DeferredLoadedProjectApplyState::Step::Failed:
        case DeferredLoadedProjectApplyState::Step::None:
        default:
            return;
        }
    }
}

void EditorModule::PumpNonRenderingWork(double projectApplyBudgetMs) {
    if (ImGui::GetCurrentContext()) {
        const int frame = ImGui::GetFrameCount();
        if (m_LastNonRenderingPumpFrame == frame) {
            return;
        }
        m_LastNonRenderingPumpFrame = frame;
    }

    const bool deferGraphPanWork =
        m_ActiveSubWindow == EditorSubWindow::NodeGraph &&
        m_Sidebar.GetNodeGraphUI().IsGraphMiddlePanActive();
    if (!deferGraphPanWork) {
        ConsumeRenderWorkerResults();
        ConsumeNodeBrowserThumbnailWorkerResults();
        m_Sidebar.GetNodeGraphUI().WarmPresetPreviewCache(this, 0.35);
    }
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
            m_RawWorkspace.selectedSourceKey = m_ActiveRawWorkspaceSourceKey;
        }
        QueueUiNotification(
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
            QueueUiNotification(UiNotificationSeverity::Error, runtimeStatus.message, "editor-raw-runtime");
            return;
        }

        if (AddGraphRawChainFromFile(path, EditorNodeGraph::Vec2{ 20.0f, 120.0f })) {
            m_SourceLoadTaskState = Async::TaskState::Idle;
            m_SourceLoadStatusText = "RAW manual chain loaded.";
            SetCurrentProjectName("");
            SetCurrentProjectFileName("");
            MarkNodeBrowserThumbnailSourceChanged();
            QueueUiNotification(UiNotificationSeverity::Success, "RAW manual chain loaded.", "editor-source-load");
        } else {
            m_SourceLoadTaskState = Async::TaskState::Failed;
            m_SourceLoadStatusText = "Failed to load RAW manual chain.";
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to load RAW manual chain.", "editor-source-load");
        }
        return;
    }

    ++m_SourceLoadGeneration;
    const std::uint64_t generation = m_SourceLoadGeneration;
    m_SourceLoadTaskState = Async::TaskState::Queued;
    m_SourceLoadStatusText = "Loading source image in the background...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().SubmitHighPriority([this, generation, path]() {
            auto postLoadFailure = [this, generation]() {
                return Async::TaskSystem::Get().PostToMain([this, generation]() {
                    if (generation != m_SourceLoadGeneration) {
                        return;
                    }
                    m_SourceLoadTaskState = Async::TaskState::Failed;
                    m_SourceLoadStatusText = "Failed to load the selected source image.";
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        m_SourceLoadStatusText,
                        "editor-source-load");
                });
            };
            auto postEmbeddingFailure = [this, generation]() {
                return Async::TaskSystem::Get().PostToMain([this, generation]() {
                    if (generation != m_SourceLoadGeneration) {
                        return;
                    }
                    EditorNodeGraph::Node* imageNode =
                        m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
                    if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image ||
                        !imageNode->image.isEmbedding ||
                        imageNode->image.embeddingRequestId != generation) {
                        return;
                    }
                    imageNode->image.isEmbedding = false;
                    imageNode->image.embeddingRequestId = 0;
                    m_SourceLoadStatusText =
                        "Source image loaded, but portable storage could not be prepared.";
                    QueueUiNotification(
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

            payloadQueued = Async::TaskSystem::Get().PostToMain([this, generation, path, payload = std::move(payload), success]() mutable {
                if (generation != m_SourceLoadGeneration) {
                    return;
                }

                if (!success || payload.pixels.empty()) {
                    m_SourceLoadTaskState = Async::TaskState::Failed;
                    m_SourceLoadStatusText = "Failed to load the selected source image.";
                    QueueUiNotification(UiNotificationSeverity::Error, "Failed to load the selected source image.", "editor-source-load");
                    return;
                }

                m_SourceLoadTaskState = Async::TaskState::Applying;
                m_SourceLoadStatusText = "Applying source image to the editor...";

                LoadSourceFromPixels(payload.pixels.data(), payload.width, payload.height, payload.channels);

                EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
                if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image) {
                    m_NodeGraph.ResetFromLayers(static_cast<int>(m_Layers.size()), true);
                    imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
                } else if (!m_NodeGraph.IsOutputConnected()) {
                    m_NodeGraph.RebuildLinks();
                }

                if (imageNode) {
                    imageNode->title = payload.label.empty() ? "Image" : payload.label;
                    imageNode->image = std::move(payload);
                    m_NodeGraph.SetActiveImageNodeId(imageNode->id);
                }
                SetCurrentProjectName("");
                SetCurrentProjectFileName("");
                MarkNodeBrowserThumbnailSourceChanged();

                m_SourceLoadTaskState = Async::TaskState::Idle;
                m_SourceLoadStatusText = "Source image loaded; preparing portable storage...";
                QueueUiNotification(UiNotificationSeverity::Success, "Source image loaded.", "editor-source-load");
            });
            if (!payloadQueued) {
                return;
            }

            if (!success || storagePixels.empty()) {
                return;
            }

            std::vector<unsigned char> pngBytes =
                EncodePngBytesForImageStorageOwned(std::move(storagePixels), width, height, channels);
            Async::TaskSystem::Get().PostToMain([
                this,
                generation,
                pngBytes = std::move(pngBytes)
            ]() mutable {
                if (generation != m_SourceLoadGeneration) {
                    return;
                }
                EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
                if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image ||
                    !imageNode->image.isEmbedding || imageNode->image.embeddingRequestId != generation) {
                    return;
                }

                if (pngBytes.empty()) {
                    imageNode->image.isEmbedding = false;
                    imageNode->image.embeddingRequestId = 0;
                    m_SourceLoadStatusText = "Source image loaded, but portable storage could not be prepared.";
                    QueueUiNotification(
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
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_SourceLoadStatusText,
            "editor-source-load");
    }
}

bool EditorModule::ExportImage(const std::string& path) {
    return RequestExportImage(path);
}

bool EditorModule::RequestExportImage(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (Async::IsBusy(m_ExportTaskState)) {
        return false;
    }

    Stack::NodeMath::PngColorMetadataChunks colorChunks;
    if (!IsCompositeViewportMode()) {
        const RenderGraphSnapshot semanticSnapshot = BuildGraphSnapshot();
        if (m_NodeGraph.IsOutputChannelInspection(
                semanticSnapshot.outputNodeId)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText =
                "A Channel can be inspected in Output, but PNG export "
                "requires an Image. Connect the Channel to Image Combine "
                "first.";
            QueueUiNotification(
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
            QueueUiNotification(
                UiNotificationSeverity::Error,
                m_ExportStatusText,
                "editor-export-semantic-policy");
            return false;
        }
        colorChunks.writeSrgb = outputPolicy.writeSrgbChunk;
        colorChunks.writeDisplayP3Cicp = outputPolicy.writeDisplayP3Cicp;
        if (outputPolicy.writeRetainedIccProfile) {
            for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
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
                QueueUiNotification(
                    UiNotificationSeverity::Info,
                    "The graph declares an ICC-based output, but reusable PNG profile bytes are unavailable. The direct result will be exported untagged.",
                    "editor-export-profile-unavailable");
            }
        }
        if (!colorChunks.writeSrgb && !colorChunks.writeDisplayP3Cicp &&
            colorChunks.retainedIccpPayload.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "The graph output color state is Unknown or has no writable profile. Stack will export the direct result untagged without guessing.",
                "editor-export-untagged");
        }
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
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the composite export.", "editor-export-image");
            return false;
        }
    } else {
        if (!BuildSingleOutputExportRaster(pixels, width, height)) {
            m_ExportTaskState = Async::TaskState::Failed;
            m_ExportStatusText = "Failed to capture the rendered export.";
            QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the rendered export.", "editor-export-image");
            return false;
        }
    }

    if (pixels.empty()) {
        std::cerr << "[EditorModule] RequestExportImage: Failed to capture pixels (empty output). Width="
                  << width << ", Height=" << height << std::endl;
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to capture the rendered image.";
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to capture the rendered image.", "editor-export-image");
        return false;
    }

    if (!m_CurrentProjectName.empty()) {
        RequestSaveCurrentProject(m_CurrentProjectName);
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing PNG export in the background...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit([
            this,
            generation,
            path,
            width,
            height,
            colorChunks = std::move(colorChunks),
            pixels = std::move(pixels)
        ]() mutable {
        bool success = false;
        std::string errorMsg;

        try {
            const std::filesystem::path destination = std::filesystem::u8path(path);
            if (destination.has_parent_path()) {
                std::filesystem::create_directories(destination.parent_path());
            }

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
                std::ofstream output(destination, std::ios::binary | std::ios::trunc);
                output.write(
                    reinterpret_cast<const char*>(outputPng.data()),
                    static_cast<std::streamsize>(outputPng.size()));
                success = output.good();
            }

            if (success) {
                // Verify the file was actually written
                if (!std::filesystem::exists(destination) || std::filesystem::file_size(destination) == 0) {
                    success = false;
                    errorMsg = "File does not exist or is empty after stbi_write_png returned success.";
                }
            }

            if (!success) {
                if (errorMsg.empty()) errorMsg = "PNG write or verification failed.";
                // Try fallback path with sanitized name
                const std::filesystem::path fallbackPath = destination.parent_path() / "fallback_export.png";
                std::cerr << "[EditorModule] stbi_write_png failed for " << path << ". Trying fallback " << fallbackPath << std::endl;
                const std::vector<unsigned char> basePng = EncodePngBytes(pixels, width, height, 4);
                std::vector<Stack::NodeMath::ContractIssue> metadataIssues;
                const std::vector<unsigned char> outputPng =
                    Stack::NodeMath::InsertPngColorMetadataChunks(
                        basePng, colorChunks, metadataIssues);
                if (!outputPng.empty()) {
                    std::ofstream output(fallbackPath, std::ios::binary | std::ios::trunc);
                    output.write(
                        reinterpret_cast<const char*>(outputPng.data()),
                        static_cast<std::streamsize>(outputPng.size()));
                    success = output.good();
                }
            }
        } catch (const std::exception& e) {
            success = false;
            errorMsg = std::string("Exception: ") + e.what();
        } catch (...) {
            success = false;
            errorMsg = "Unknown exception during export.";
        }

        if (!success) {
            std::cerr << "[EditorModule] Export failed for path: " << path << ". Reason: " << errorMsg << std::endl;
        }

            Async::TaskSystem::Get().PostToMain([this, generation, success]() {
                if (generation != m_ExportGeneration) {
                    return;
                }

                if (success) {
                    m_ExportTaskState = Async::TaskState::Idle;
                    m_ExportStatusText = "Rendered image exported.";
                    QueueUiNotification(UiNotificationSeverity::Success, "Rendered image exported.", "editor-export-image");
                } else {
                    m_ExportTaskState = Async::TaskState::Failed;
                    m_ExportStatusText = "Failed to write the exported PNG.";
                    QueueUiNotification(UiNotificationSeverity::Error, "Failed to write the exported PNG.", "editor-export-image");
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
        m_ExportStatusText = "Could not queue the image export.";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_ExportStatusText,
            "editor-export-image");
    }
    return false;
}

bool EditorModule::BuildProjectDocumentForSave(
    const std::string& displayName,
    StackFormat::ProjectDocument& outDocument) {
    const std::string trimmedName = displayName.empty() ? "Untitled Project" : displayName;
    const StackFormat::json pipeline = SerializePipeline();

    if (m_ActiveRawProjectSnapshot && m_ActiveRawProjectStore) {
        outDocument = {};
        outDocument.metadata.projectKind = StackFormat::kRawProjectKind;
        outDocument.metadata.projectName = trimmedName;
        outDocument.metadata.timestamp = BuildTimestampString();
        outDocument.metadata.sourceWidth = 1;
        outDocument.metadata.sourceHeight = 1;
        outDocument.thumbnailBytes = m_ActiveRawProjectSnapshot->coverThumbnailBytes;
        outDocument.pipelineData = pipeline;
        outDocument.rawWorkspaceData = m_ActiveRawProjectSnapshot->rawWorkspaceData;
        if (!outDocument.rawWorkspaceData.is_object()) {
            outDocument.rawWorkspaceData = StackFormat::json::object();
        }
        outDocument.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
        outDocument.rawWorkspaceData["schemaVersion"] = 3;
        outDocument.rawWorkspaceData["rawProjectModel"] =
            Stack::Project::kRawProjectModelSourceSets;
        outDocument.projectStore = m_ActiveRawProjectStore;
        outDocument.rawProjectSnapshot =
            std::make_shared<Stack::Project::RawProjectSnapshot>(
                *m_ActiveRawProjectSnapshot);
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
        if ((renderedPixels.empty() || renderedW <= 0 || renderedH <= 0) && m_NodeGraph.IsOutputConnected()) {
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
        if (const EditorNodeGraph::Node* activeImageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
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
                FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
            Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
                *source,
                m_ActiveRawWorkspaceRecipe,
                pipeline,
                outDocument,
                m_ActiveRawWorkspaceMode,
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
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Finishing imported slices before exporting the project.",
            "editor-graph-image-export-wait");
        return false;
    }

    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = "Packaging project file...";

    const std::string exportName = m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName;
    StackFormat::ProjectDocument document;
    if (!BuildProjectDocumentForSave(exportName, document)) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to package the current project.";
        QueueUiNotification(UiNotificationSeverity::Error, "Failed to package the current project.", "editor-export-project");
        return false;
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing project file...";

    bool submitted = false;
    try {
        submitted = Async::TaskSystem::Get().Submit(
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

                Async::TaskSystem::Get().PostToMain([this, generation, path, success]() {
                    if (generation != m_ExportGeneration) {
                        return;
                    }

                    if (success) {
                        m_ExportTaskState = Async::TaskState::Idle;
                        m_ExportStatusText = "Project exported.";
                        QueueUiNotification(UiNotificationSeverity::Success, "Project exported.", "editor-export-project");
                        if (m_CurrentProjectName.empty()) {
                            const std::string stem = std::filesystem::path(path).stem().string();
                            m_CurrentProjectName = stem.empty() ? std::string("Untitled Project") : stem;
                        }
                    } else {
                        m_ExportTaskState = Async::TaskState::Failed;
                        m_ExportStatusText = "Failed to write the project file.";
                        QueueUiNotification(UiNotificationSeverity::Error, "Failed to write the project file.", "editor-export-project");
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
        QueueUiNotification(
            UiNotificationSeverity::Error,
            m_ExportStatusText,
            "editor-export-project");
    }
    return false;
}
