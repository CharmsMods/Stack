#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<unsigned char> MinimalTransparentPngBytes() {
    static constexpr std::array<unsigned char, 70> kPng = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x01, 0x63, 0x60, 0x60, 0x60, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x01, 0x0b, 0x0e, 0x0c,
        0x1a, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
        0x44, 0xae, 0x42, 0x60, 0x82
    };
    return std::vector<unsigned char>(kPng.begin(), kPng.end());
}

std::int64_t RawWorkspaceProjectFileTimeTicks(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, ec);
    return ec
        ? 0
        : std::chrono::duration_cast<std::chrono::microseconds>(
            writeTime.time_since_epoch()).count();
}

std::string RawWorkspaceProjectSaveRevisionKey(
    const std::filesystem::path& workspaceRoot,
    const std::string& sourceKey) {
    return workspaceRoot.lexically_normal().generic_string() + "\n" + sourceKey;
}

void EnsureMinimalProjectDocument(StackBinaryFormat::ProjectDocument& document, const std::string& name) {
    if (document.metadata.projectKind.empty()) {
        document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    }
    if (document.metadata.projectName.empty()) {
        document.metadata.projectName = name.empty() ? "Untitled RAW Project" : name;
    }
    if (document.metadata.sourceWidth <= 0) {
        document.metadata.sourceWidth = 1;
    }
    if (document.metadata.sourceHeight <= 0) {
        document.metadata.sourceHeight = 1;
    }
    if (document.thumbnailBytes.empty()) {
        document.thumbnailBytes = MinimalTransparentPngBytes();
    }
    if (document.sourceImageBytes.empty()) {
        document.sourceImageBytes = MinimalTransparentPngBytes();
    }
}

struct RawWorkspaceProjectLoadResult {
    Stack::RawWorkspace::SourceRecord source;
    EditorLoadedProjectData loadedProject;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::RawProjectMode mode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    Stack::RawWorkspace::ManagedRawSection managedSection;
    bool success = false;
    bool hasRawWorkspaceInfo = false;
    std::string errorMessage;
};

void ApplyRawWorkspaceProjectInfoToSource(
    Stack::RawWorkspace::SourceRecord& source,
    const StackBinaryFormat::ProjectDocument& document,
    const std::filesystem::path& projectPath,
    const std::filesystem::path& projectRelativePath,
    bool autosaved,
    bool dirty,
    std::string associationReason) {
    Stack::RawWorkspace::ProjectInfo info;
    if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(document, info, nullptr)) {
        info = source.project;
        info.status = Stack::RawWorkspace::ProjectStatus::Invalid;
        if (info.errorMessage.empty()) {
            info.errorMessage = "Project does not contain RAW Workspace metadata.";
        }
    }

    info.absolutePath = projectPath.lexically_normal();
    info.relativePath = projectRelativePath.lexically_normal();
    info.projectModifiedTimeTicks = RawWorkspaceProjectFileTimeTicks(info.absolutePath);
    if (info.status != Stack::RawWorkspace::ProjectStatus::Invalid) {
        info.status = info.embeddedRaw
            ? Stack::RawWorkspace::ProjectStatus::Embedded
            : Stack::RawWorkspace::ProjectStatus::Existing;
    }
    info.autosaved = autosaved;
    info.dirty = dirty;
    info.associationReason = std::move(associationReason);
    source.project = std::move(info);
}

} // namespace

void EditorModule::RequestNewProject() {
    if (!HasProjectContent()) {
        ResetToBlankProject();
        return;
    }
    m_ShowNewProjectPrompt = true;
}

bool EditorModule::HasProjectContent() const {
    return !m_CurrentProjectName.empty() ||
        !m_CurrentProjectFileName.empty() ||
        !m_Layers.empty() ||
        !m_NodeGraph.GetNodes().empty() ||
        !m_NodeGraph.GetLinks().empty() ||
        m_Pipeline.HasSourceImage();
}

bool EditorModule::ConsumeUiNotification(UiNotificationEvent& outEvent) {
    if (m_UiNotifications.empty()) {
        return false;
    }
    outEvent = std::move(m_UiNotifications.front());
    m_UiNotifications.pop_front();
    return true;
}

void EditorModule::QueueUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    if (message.empty()) {
        return;
    }
    if (!dedupeKey.empty()) {
        for (UiNotificationEvent& event : m_UiNotifications) {
            if (event.dedupeKey == dedupeKey) {
                event.severity = severity;
                event.message = std::move(message);
                return;
            }
        }
    }
    m_UiNotifications.push_back(UiNotificationEvent{
        severity,
        std::move(message),
        std::move(dedupeKey)
    });
}

void EditorModule::ResetToBlankProject() {
    CancelCanvasTool();
    CancelGraphAutoFocusTracking();
    m_ShowNewProjectPrompt = false;
    m_ShowNewProjectDiscardConfirm = false;
    m_GraphDropImportTaskState = Async::TaskState::Idle;
    m_GraphDropImportStatusText.clear();
    m_PendingGraphDropImports.clear();
    m_SourceLoadTaskState = Async::TaskState::Idle;
    m_SourceLoadStatusText.clear();
    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_NodeGraph.ResetFromLayers(0, false);
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
    m_HdrMergeRequestedGenerations.clear();
    m_HdrMergeCompletedGenerations.clear();
    m_HdrMergeFailureMessages.clear();
    m_HdrMergeRenderingNodeIds.clear();
    m_HdrMergeSubmittedNodesByGeneration.clear();
    m_ScopeDisplayedRevisions.clear();
    ResetNodeBrowserThumbnailState();
    ResetRenderSubmissionState();
    ClearViewportOutputTiles();
    m_Pipeline.Clear();
    m_CompositePreviewPipeline.Clear();
    m_ActiveRawWorkspaceSourceKey.clear();
    m_ActiveRawWorkspaceProjectPath.clear();
    m_ActiveRawWorkspaceRecipe = {};
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    m_ActiveManagedRawSection = {};
    m_ShowRawWorkspaceRelinkPopup = false;
    m_ShowRawWorkspaceEmbedPopup = false;
    SetCurrentProjectName("");
    SetCurrentProjectFileName("");
    MarkRenderDirty();
    ClearDirty();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_LastAutoSaveTime = -1.0;
}

void EditorModule::ResetRenderSubmissionState() {
    ++m_RenderGeneration;
    if (m_RenderWorkerAvailable) {
        m_RenderWorker.InvalidateSnapshotsBefore(m_RenderGeneration);
    }
    m_RenderPending = false;
    m_RenderDirty = true;
    m_LastCompletedRenderGeneration = 0;
    m_LastSubmittedRenderRevision = 0;
    m_HdrMergeRequestedGenerations.clear();
    m_HdrMergeCompletedGenerations.clear();
    m_HdrMergeFailureMessages.clear();
    m_HdrMergeRenderingNodeIds.clear();
    m_HdrMergeSubmittedNodesByGeneration.clear();
    m_Viewport.ResetSinglePreviewState();
    ClearViewportOutputTiles();
    m_HoverFade = 0.0f;
}

const Stack::RawWorkspace::SourceRecord* EditorModule::FindRawWorkspaceSourceByKey(const std::string& sourceKey) const {
    const auto it = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == sourceKey;
        });
    return it == m_RawWorkspace.sources.end() ? nullptr : &(*it);
}

Stack::RawWorkspace::SourceRecord* EditorModule::FindRawWorkspaceSourceByKey(const std::string& sourceKey) {
    auto it = std::find_if(
        m_RawWorkspace.sources.begin(),
        m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == sourceKey;
        });
    return it == m_RawWorkspace.sources.end() ? nullptr : &(*it);
}

std::uint64_t EditorModule::BumpRawWorkspaceProjectSaveRevision(
    const std::filesystem::path& workspaceRoot,
    const std::string& sourceKey) {
    if (workspaceRoot.empty() || sourceKey.empty()) {
        return 0;
    }

    const std::string key = RawWorkspaceProjectSaveRevisionKey(workspaceRoot, sourceKey);
    std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveRevisionMutex);
    return ++m_RawWorkspaceProjectSaveRevisions[key];
}

std::uint64_t EditorModule::GetRawWorkspaceProjectSaveRevision(
    const std::filesystem::path& workspaceRoot,
    const std::string& sourceKey) const {
    if (workspaceRoot.empty() || sourceKey.empty()) {
        return 0;
    }

    const std::string key = RawWorkspaceProjectSaveRevisionKey(workspaceRoot, sourceKey);
    std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveRevisionMutex);
    const auto it = m_RawWorkspaceProjectSaveRevisions.find(key);
    return it == m_RawWorkspaceProjectSaveRevisions.end() ? 0 : it->second;
}

bool EditorModule::IsRawWorkspaceProjectSaveJobCurrent(const RawWorkspaceProjectSaveJob& job) const {
    return job.sourceRevision != 0 &&
        GetRawWorkspaceProjectSaveRevision(job.workspaceRoot, job.sourceKey) == job.sourceRevision;
}

Stack::RawRecipe::RawDevelopmentRecipe EditorModule::BuildRawWorkspaceDefaultRecipe(
    const Stack::RawWorkspace::SourceRecord& source) const {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.source.displayName = source.fileName;
    return recipe;
}

bool EditorModule::BuildRawWorkspaceProjectGraph(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool markEdited,
    std::string* outError) {
    if (outError) {
        outError->clear();
    }
    ResetForPipelineDeserialization();

    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    payload.projectStatus = markEdited ? "Edited" : "Not Edited";
    payload.edited = markEdited;
    payload.autosaved = false;

    EditorNodeGraph::Node* rawNode =
        m_NodeGraph.AddRawDevelopmentNode(std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!rawNode) {
        if (outError) {
            *outError = "Failed to create the RAW Development node.";
        }
        return false;
    }
    const int rawNodeId = rawNode->id;

    EditorNodeGraph::Node* outputNode =
        m_NodeGraph.AddOutputNode(EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true);
    if (!outputNode) {
        if (outError) {
            *outError = "Failed to create the Output node.";
        }
        return false;
    }
    const int outputNodeId = outputNode->id;

    std::string connectError;
    if (!m_NodeGraph.TryConnectSockets(
            rawNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &connectError)) {
        if (outError) {
            *outError = connectError.empty()
                ? "Failed to connect the RAW Development node to the Output node."
                : connectError;
        }
        return false;
    }

    m_NodeGraph.SetOutputNodeId(outputNodeId);
    SelectGraphNode(rawNodeId);
    if (markEdited) {
        MarkRenderDirty(rawNodeId);
    } else {
        MarkRenderRefreshDirty();
    }
    return true;
}

bool EditorModule::RequestLoadRawWorkspaceProjectForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    bool includeNodeBrowserThumbnails) {
    if (source.project.absolutePath.empty()) {
        return false;
    }

    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == source.relativePathKey) {
        return true;
    }

    const std::uint64_t generation =
        m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Queued;
    m_RawWorkspaceProjectLoadSourceKey = source.relativePathKey;
    m_RawWorkspaceProjectLoadStatusText = "Loading RAW project...";

    Async::TaskSystem::Get().Submit([this, generation, source, includeNodeBrowserThumbnails]() mutable {
        auto isLoadCanceled = [this, generation]() {
            return generation != m_RawWorkspaceProjectLoadGeneration.load(std::memory_order_relaxed);
        };
        if (isLoadCanceled()) {
            return;
        }

        RawWorkspaceProjectLoadResult result;
        result.source = source;

        StackBinaryFormat::ProjectDocument document;
        StackBinaryFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = false;
        options.includePipelineData = true;
        options.includeNodeBrowserThumbnails = includeNodeBrowserThumbnails;
        options.includeRawWorkspaceData = true;
        if (!StackBinaryFormat::ReadProjectFile(source.project.absolutePath, document, options)) {
            if (isLoadCanceled()) {
                return;
            }
            result.errorMessage = "Failed to load the RAW project.";
        } else {
            if (isLoadCanceled()) {
                return;
            }
            result.loadedProject.sourcePixels.assign(4, 0);
            result.loadedProject.width = 1;
            result.loadedProject.height = 1;
            result.loadedProject.channels = 4;
            result.loadedProject.pipelineData = document.pipelineData.is_null()
                ? nlohmann::json::array()
                : document.pipelineData;
            result.loadedProject.projectName = document.metadata.projectName.empty()
                ? (source.stem.empty() ? source.fileName : source.stem)
                : document.metadata.projectName;
            result.loadedProject.projectFileName = source.project.absolutePath.string();
            result.loadedProject.nodeBrowserThumbnailEntries = document.nodeBrowserThumbnailEntries;

            Stack::RawWorkspace::ProjectInfo projectInfo;
            if (Stack::RawWorkspace::ReadProjectInfoFromDocument(document, projectInfo, &result.recipe)) {
                result.mode = projectInfo.mode;
                result.managedSection = document.rawWorkspaceData.is_object()
                    ? Stack::RawWorkspace::DeserializeManagedRawSection(
                        document.rawWorkspaceData.value("managedRawSection", nlohmann::json::object()))
                    : Stack::RawWorkspace::ManagedRawSection{};
                result.hasRawWorkspaceInfo = true;
            }
            result.success = true;
        }
        if (isLoadCanceled()) {
            return;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, result = std::move(result)]() mutable {
            if (generation != m_RawWorkspaceProjectLoadGeneration.load(std::memory_order_relaxed)) {
                return;
            }

            if (!result.success) {
                m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
                m_RawWorkspaceProjectLoadStatusText = result.errorMessage.empty()
                    ? "Failed to load the RAW project."
                    : result.errorMessage;
                if (m_PendingRawWorkspaceOpenGraphSourceKey == result.source.relativePathKey) {
                    m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
                    m_PendingRawWorkspaceOpenGraphSourceKey.clear();
                }
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    m_RawWorkspaceProjectLoadStatusText,
                    "raw-workspace-project-load");
                return;
            }

            if (!result.hasRawWorkspaceInfo) {
                result.recipe = BuildRawWorkspaceDefaultRecipe(result.source);
                result.mode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
                result.managedSection = {};
            }

            auto loadedProject = std::make_shared<EditorLoadedProjectData>(std::move(result.loadedProject));
            if (!BeginDeferredLoadedProjectApply(loadedProject)) {
                m_PendingRawWorkspaceDeferredProjectFinalize = false;
                m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
                if (m_PendingRawWorkspaceOpenGraphSourceKey == result.source.relativePathKey) {
                    m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
                    m_PendingRawWorkspaceOpenGraphSourceKey.clear();
                }
                m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
                m_RawWorkspaceProjectLoadStatusText = "Failed to apply the RAW project.";
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    m_RawWorkspaceProjectLoadStatusText,
                    "raw-workspace-project-load");
                return;
            }

            m_ActiveRawWorkspaceRecipe = std::move(result.recipe);
            m_ActiveRawWorkspaceMode = result.mode;
            m_ActiveManagedRawSection = result.managedSection;
            m_ActiveRawWorkspaceSourceKey = result.source.relativePathKey;
            m_ActiveRawWorkspaceProjectPath = result.source.project.absolutePath;
            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Applying;
            m_RawWorkspaceProjectLoadStatusText = "Applying RAW project...";
            m_PendingRawWorkspaceDeferredProjectFinalize = true;
            m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey = result.source.relativePathKey;
        });
    });

    return true;
}

void EditorModule::FinalizeDeferredRawWorkspaceProjectLoadIfNeeded() {
    if (!m_PendingRawWorkspaceDeferredProjectFinalize) {
        return;
    }
    if (m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey != m_ActiveRawWorkspaceSourceKey) {
        const std::string deferredSourceKey = m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey;
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        if (m_PendingRawWorkspaceOpenGraphSourceKey == deferredSourceKey) {
            m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
            m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        }
        return;
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        !ValidateActiveRawWorkspaceManagedGraph(false)) {
        MarkActiveRawWorkspaceProjectAsCustomGraph(Stack::RawWorkspace::kCustomGraphReadOnlyReason);
        QueueUiNotification(
            UiNotificationSeverity::Info,
            Stack::RawWorkspace::kCustomGraphReadOnlyReason,
            "raw-workspace-managed-load-invalid");
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph) {
        MarkDirty();
    } else {
        ClearDirty();
    }

    m_PendingRawWorkspaceDeferredProjectFinalize = false;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
    const Stack::RawWorkspace::SourceRecord* activeSource =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    const bool activeSourceHasStoredProject =
        activeSource &&
        (activeSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         activeSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded);
    m_RawWorkspaceProjectLoadStatusText = activeSourceHasStoredProject
        ? "RAW project loaded."
        : "RAW preview ready.";

    if (m_PendingRawWorkspaceOpenGraphAfterProjectLoad &&
        m_PendingRawWorkspaceOpenGraphSourceKey == m_ActiveRawWorkspaceSourceKey) {
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
        m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        FocusRawWorkspaceDevelopmentNode();
        RequestOpenEditorTab();
    }
}

bool EditorModule::ResolveRawWorkspaceRecipeForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    Stack::RawRecipe::RawDevelopmentRecipe& outRecipe,
    Stack::RawWorkspace::RawProjectMode* outMode,
    std::string* outError) const {
    if (IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == source.relativePathKey) {
        outRecipe = m_ActiveRawWorkspaceRecipe;
        if (outMode) {
            *outMode = m_ActiveRawWorkspaceMode;
        }
        return true;
    }

    if (source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
        const auto cacheIt = m_RawWorkspaceRecipePreviewCache.find(source.relativePathKey);
        if (cacheIt != m_RawWorkspaceRecipePreviewCache.end() &&
            cacheIt->second.projectPath == source.project.absolutePath &&
            cacheIt->second.projectModifiedTimeTicks == source.project.projectModifiedTimeTicks) {
            outRecipe = cacheIt->second.recipe;
            if (outMode) {
                *outMode = cacheIt->second.mode;
            }
            if (outError) {
                *outError = cacheIt->second.errorMessage;
            }
            return cacheIt->second.success;
        }

        RawWorkspaceRecipePreviewCacheEntry cacheEntry;
        cacheEntry.projectPath = source.project.absolutePath;
        cacheEntry.projectModifiedTimeTicks = source.project.projectModifiedTimeTicks;
        cacheEntry.mode = source.project.mode;

        StackBinaryFormat::ProjectDocument document;
        StackBinaryFormat::ProjectLoadOptions options;
        options.includeThumbnail = false;
        options.includeSourceImage = false;
        options.includePipelineData = false;
        options.includeNodeBrowserThumbnails = false;
        options.includeRawWorkspaceData = true;
        if (!StackBinaryFormat::ReadProjectFile(source.project.absolutePath, document, options)) {
            outRecipe = BuildRawWorkspaceDefaultRecipe(source);
            cacheEntry.recipe = outRecipe;
            cacheEntry.success = false;
            cacheEntry.errorMessage = "The selected RAW project could not be read.";
            m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
            if (outError) {
                *outError = cacheEntry.errorMessage;
            }
            if (outMode) {
                *outMode = source.project.mode;
            }
            return false;
        }

        Stack::RawWorkspace::ProjectInfo info;
        if (!Stack::RawWorkspace::ReadProjectInfoFromDocument(document, info, &outRecipe)) {
            outRecipe = BuildRawWorkspaceDefaultRecipe(source);
            cacheEntry.recipe = outRecipe;
            cacheEntry.success = false;
            cacheEntry.errorMessage = info.errorMessage.empty()
                ? "The selected project does not contain RAW Workspace metadata."
                : info.errorMessage;
            m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
            if (outError) {
                *outError = cacheEntry.errorMessage;
            }
            if (outMode) {
                *outMode = source.project.mode;
            }
            return false;
        }
        if (info.status == Stack::RawWorkspace::ProjectStatus::Invalid) {
            outRecipe = BuildRawWorkspaceDefaultRecipe(source);
            cacheEntry.recipe = outRecipe;
            cacheEntry.mode = info.mode;
            cacheEntry.success = false;
            cacheEntry.errorMessage = info.errorMessage.empty()
                ? "The selected project contains invalid RAW Workspace metadata."
                : info.errorMessage;
            m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
            if (outError) {
                *outError = cacheEntry.errorMessage;
            }
            if (outMode) {
                *outMode = info.mode;
            }
            return false;
        }

        cacheEntry.recipe = outRecipe;
        cacheEntry.mode = info.mode;
        cacheEntry.success = true;
        cacheEntry.errorMessage.clear();
        m_RawWorkspaceRecipePreviewCache[source.relativePathKey] = cacheEntry;
        if (outMode) {
            *outMode = info.mode;
        }
        return true;
    }

    outRecipe = BuildRawWorkspaceDefaultRecipe(source);
    if (outMode) {
        *outMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    }
    return true;
}

bool EditorModule::FocusRawWorkspaceDevelopmentNode() {
    SwitchToSubWindow(EditorSubWindow::NodeGraph);
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        m_ActiveManagedRawSection.rawDecodeNodeId > 0 &&
        m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawDecodeNodeId)) {
        SelectGraphNode(m_ActiveManagedRawSection.rawDecodeNodeId);
        return true;
    }
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::CustomGraph &&
        m_ActiveManagedRawSection.rawSourceNodeId > 0 &&
        m_NodeGraph.FindNode(m_ActiveManagedRawSection.rawSourceNodeId)) {
        SelectGraphNode(m_ActiveManagedRawSection.rawSourceNodeId);
        return true;
    }
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) {
            continue;
        }
        const std::string& key = node.rawDevelopment.recipe.source.relativePathKey;
        if (m_ActiveRawWorkspaceSourceKey.empty() || key.empty() || key == m_ActiveRawWorkspaceSourceKey) {
            SelectGraphNode(node.id);
            return true;
        }
    }
    return false;
}

bool EditorModule::OpenRawWorkspaceProjectInGraph(const Stack::RawWorkspace::SourceRecord& source) {
    if (source.project.status != Stack::RawWorkspace::ProjectStatus::Existing &&
        source.project.status != Stack::RawWorkspace::ProjectStatus::Embedded) {
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Make an edit to create this RAW project first.",
            "raw-workspace-open-graph-preview-only");
        return false;
    }

    if (!IsRawWorkspaceProjectActive() || m_ActiveRawWorkspaceSourceKey != source.relativePathKey) {
        if (!SaveActiveRawWorkspaceProjectIfDirty()) {
            return false;
        }
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = true;
        m_PendingRawWorkspaceOpenGraphSourceKey = source.relativePathKey;
        if (!RequestLoadRawWorkspaceProjectForSource(source, true)) {
            m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
            m_PendingRawWorkspaceOpenGraphSourceKey.clear();
            return false;
        }
        return true;
    }

    FocusRawWorkspaceDevelopmentNode();
    RequestOpenEditorTab();
    return true;
}

bool EditorModule::StageRawWorkspaceProjectForSourcePreview(
    const Stack::RawWorkspace::SourceRecord& source) {
    if (source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
        return RequestLoadRawWorkspaceProjectForSource(source);
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = BuildRawWorkspaceDefaultRecipe(source);
    EditorNodeGraph::Graph previewGraph;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    payload.projectStatus = "Not Edited";
    payload.edited = false;
    payload.autosaved = false;

    std::string graphError;
    EditorNodeGraph::Node* rawNode =
        previewGraph.AddRawDevelopmentNode(std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!rawNode) {
        graphError = "Failed to create the RAW Development node.";
    }
    const int rawNodeId = rawNode ? rawNode->id : -1;

    EditorNodeGraph::Node* outputNode = graphError.empty()
        ? previewGraph.AddOutputNode(EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true)
        : nullptr;
    if (graphError.empty() && !outputNode) {
        graphError = "Failed to create the Output node.";
    }
    const int outputNodeId = outputNode ? outputNode->id : -1;

    if (graphError.empty() &&
        !previewGraph.TryConnectSockets(
            rawNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &graphError)) {
        if (graphError.empty()) {
            graphError = "Failed to connect the RAW Development node to the Output node.";
        }
    }

    if (!graphError.empty()) {
        std::cerr
            << "[RAW Workspace] Failed to create preview project graph"
            << " sourceKey=" << source.relativePathKey
            << " projectPath=" << Stack::RawWorkspace::BuildExpectedProjectInfo(
                Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot),
                source).absolutePath.string()
            << " error=" << graphError
            << "\n";
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Failed to create the RAW project graph: " + graphError,
            "raw-workspace-project-preview-graph-create");
        return false;
    }
    previewGraph.SetOutputNodeId(outputNodeId);
    previewGraph.SelectNode(rawNodeId);

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    const Stack::RawWorkspace::ProjectInfo expectedProject =
        Stack::RawWorkspace::BuildExpectedProjectInfo(layout, source);
    const std::string projectName = source.stem.empty() ? source.fileName : source.stem;

    auto loadedProject = std::make_shared<EditorLoadedProjectData>();
    loadedProject->sourcePixels.assign(4, 0);
    loadedProject->width = 1;
    loadedProject->height = 1;
    loadedProject->channels = 4;
    loadedProject->pipelineData =
        EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), previewGraph);
    loadedProject->projectName = projectName;
    loadedProject->projectFileName = expectedProject.absolutePath.string();

    if (!BeginDeferredLoadedProjectApply(loadedProject)) {
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Failed to apply the RAW preview.";
        return false;
    }

    m_ActiveRawWorkspaceSourceKey = source.relativePathKey;
    m_ActiveRawWorkspaceProjectPath = expectedProject.absolutePath;
    m_ActiveRawWorkspaceRecipe = std::move(recipe);
    m_ActiveRawWorkspaceMode = Stack::RawWorkspace::RawProjectMode::RecipeBacked;
    m_ActiveManagedRawSection = {};
    m_RawWorkspaceProjectLoadSourceKey = source.relativePathKey;
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Applying;
    m_RawWorkspaceProjectLoadStatusText = "Applying RAW preview...";
    m_PendingRawWorkspaceDeferredProjectFinalize = true;
    m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey = source.relativePathKey;
    return true;
}

bool EditorModule::EnsureRawWorkspaceProjectForSelectedRecipeEdit(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!selectedSource) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Select a RAW source before editing.",
            "raw-workspace-no-selection");
        return false;
    }

    const bool alreadyActive =
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
    if (!alreadyActive &&
        (m_RawWorkspacePreviewStageQueued &&
         m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey)) {
        return false;
    }
    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey) {
        return false;
    }
    if (m_PendingRawWorkspaceDeferredProjectFinalize &&
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey == selectedSource->relativePathKey &&
        IsDeferredLoadedProjectApplyActive()) {
        return false;
    }
    bool needsDefaultGraph = !alreadyActive;

    if (m_ActiveRawWorkspaceSourceKey != selectedSource->relativePathKey) {
        if (!SaveActiveRawWorkspaceProjectIfDirty()) {
            return false;
        }
        ClearRawWorkspaceLivePreviewState();
        if (selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
            selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded) {
            if (!RequestLoadRawWorkspaceProjectForSource(*selectedSource)) {
                return false;
            }
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "Loading this RAW project before applying edits.",
                "raw-workspace-project-load-before-edit");
            return false;
        }
    }

    Stack::RawRecipe::RawDevelopmentRecipe resolvedRecipe = recipe;
    resolvedRecipe.source.sourcePath = selectedSource->absolutePath.string();
    resolvedRecipe.source.relativePathKey = selectedSource->relativePathKey;
    resolvedRecipe.source.fingerprint = selectedSource->fingerprint;
    resolvedRecipe.source.fileSizeBytes = static_cast<std::uint64_t>(selectedSource->fileSizeBytes);
    resolvedRecipe.source.modifiedTimeTicks = selectedSource->modifiedTimeTicks;
    resolvedRecipe.source.displayName = selectedSource->fileName;

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    const Stack::RawWorkspace::ProjectInfo expectedProject =
        Stack::RawWorkspace::BuildExpectedProjectInfo(layout, *selectedSource);

    const bool selectedHasNoProject =
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::NoProject ||
        selectedSource->project.absolutePath.empty();
    const std::filesystem::path targetProjectPath = selectedHasNoProject
            ? expectedProject.absolutePath
            : selectedSource->project.absolutePath;
    const Stack::RawWorkspace::RawProjectMode targetMode = selectedHasNoProject
        ? Stack::RawWorkspace::RawProjectMode::RecipeBacked
        : m_ActiveRawWorkspaceMode;

    if (needsDefaultGraph) {
        const nlohmann::json previousPipeline = SerializePipeline();
        std::string graphError;
        if (!BuildRawWorkspaceProjectGraph(resolvedRecipe, true, &graphError)) {
            DeserializePipeline(previousPipeline);
            std::cerr
                << "[RAW Workspace] Failed to create edit project graph"
                << " selectedSourceKey=" << selectedSource->relativePathKey
                << " activeSourceKey=" << m_ActiveRawWorkspaceSourceKey
                << " targetProjectPath=" << targetProjectPath.string()
                << " error=" << (graphError.empty() ? std::string("unknown") : graphError)
                << "\n";
            QueueUiNotification(
                UiNotificationSeverity::Error,
                graphError.empty()
                    ? "Failed to create the RAW project graph."
                    : "Failed to create the RAW project graph: " + graphError,
                "raw-workspace-project-graph-create");
            return false;
        }
    }
    m_ActiveRawWorkspaceSourceKey = selectedSource->relativePathKey;
    m_ActiveRawWorkspaceProjectPath = targetProjectPath;
    m_ActiveRawWorkspaceRecipe = resolvedRecipe;
    m_ActiveRawWorkspaceMode = targetMode;
    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::RecipeBacked) {
        m_ActiveManagedRawSection = {};
    }
    SetCurrentProjectName(selectedSource->stem.empty() ? selectedSource->fileName : selectedSource->stem);
    SetCurrentProjectFileName(m_ActiveRawWorkspaceProjectPath.string());
    MarkDirty();
    return true;
}

bool EditorModule::ApplyRawWorkspaceRecipeEditForSelectedSource(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool interactionActive) {
    if (IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed &&
        m_RawWorkspace.selectedSourceKey == m_ActiveRawWorkspaceSourceKey) {
        std::string reason;
        if (!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(recipe, &reason)) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                reason.empty() ? "This RAW edit cannot be represented by the managed graph." : reason,
                "raw-workspace-managed-recipe-blocked");
            return false;
        }
    }

    if (!EnsureRawWorkspaceProjectForSelectedRecipeEdit(recipe)) {
        return false;
    }

    if (Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey)) {
        if (source->project.absolutePath.empty()) {
            const Stack::RawWorkspace::ManagedLayout layout =
                Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
            const Stack::RawWorkspace::ProjectInfo expectedProject =
                Stack::RawWorkspace::BuildExpectedProjectInfo(layout, *source);
            source->project.absolutePath = expectedProject.absolutePath;
            source->project.relativePath = expectedProject.relativePath;
        }
        if (source->project.status == Stack::RawWorkspace::ProjectStatus::Unknown ||
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject) {
            source->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
        }
        source->project.mode = m_ActiveRawWorkspaceMode;
        source->project.autosaved = false;
        source->project.dirty = true;
        InvalidateRawWorkspaceGalleryPresentation();
    }

    if (m_ActiveRawWorkspaceMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        if (!ApplyActiveRawWorkspaceRecipeToManagedGraph()) {
            return false;
        }
    } else {
        for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) {
                continue;
            }
            const std::string key = node.rawDevelopment.recipe.source.relativePathKey;
            if (key.empty() || key == m_ActiveRawWorkspaceSourceKey) {
                node.rawDevelopment.recipe = m_ActiveRawWorkspaceRecipe;
                node.rawDevelopment.projectStatus = "Edited";
                node.rawDevelopment.edited = true;
                node.rawDevelopment.autosaved = false;
                MarkRenderDirty(node.id);
            }
        }
    }
    MarkDirty();
    NoteRawWorkspaceRecipePreviewEdit(interactionActive);
    return true;
}

void EditorModule::EnsureRawWorkspaceProjectSaveWorker() {
    if (m_RawWorkspaceProjectSaveWorker.joinable()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveWorkerStopRequested = false;
        m_RawWorkspaceProjectSaveWorkerBusy = false;
    }
    m_RawWorkspaceProjectSaveWorker = std::thread([this]() {
        RawWorkspaceProjectSaveWorkerLoop();
    });
}

void EditorModule::EnqueueRawWorkspaceProjectSave(RawWorkspaceProjectSaveJob job) {
    EnsureRawWorkspaceProjectSaveWorker();
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveQueue.push_back(std::move(job));
    }
    ++m_RawWorkspaceProjectSaveInFlightCount;
    m_RawWorkspaceProjectSaveTaskState = Async::TaskState::Queued;
    m_RawWorkspaceProjectSaveStatusText = "Saving RAW project...";
    m_RawWorkspaceProjectSaveCv.notify_one();
}

void EditorModule::RequestRawWorkspaceProjectSaveWorkerDrain() {
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveWorkerStopRequested = true;
    }
    m_RawWorkspaceProjectSaveCv.notify_all();
}

bool EditorModule::IsRawWorkspaceProjectSaveWorkerIdle() const {
    std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
    return m_RawWorkspaceProjectSaveQueue.empty() && !m_RawWorkspaceProjectSaveWorkerBusy;
}

void EditorModule::ShutdownRawWorkspaceProjectSaveWorker() {
    RequestRawWorkspaceProjectSaveWorkerDrain();
    if (m_RawWorkspaceProjectSaveWorker.joinable()) {
        m_RawWorkspaceProjectSaveWorker.join();
    }
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
        m_RawWorkspaceProjectSaveQueue.clear();
        m_RawWorkspaceProjectSaveWorkerStopRequested = false;
        m_RawWorkspaceProjectSaveWorkerBusy = false;
    }
    m_RawWorkspaceProjectSaveInFlightCount = 0;
    m_RawWorkspaceProjectSaveTaskState = Async::TaskState::Idle;
    m_RawWorkspaceProjectSaveStatusText.clear();
}

bool EditorModule::WriteRawWorkspaceProjectSaveJob(
    RawWorkspaceProjectSaveJob& job,
    std::string& error) const {
    try {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        StackBinaryFormat::ProjectDocument existingDocumentForWorker;
        StackBinaryFormat::ProjectLoadOptions existingOptions;
        existingOptions.includeThumbnail = false;
        existingOptions.includeSourceImage = false;
        existingOptions.includePipelineData = false;
        existingOptions.includeNodeBrowserThumbnails = false;
        existingOptions.includeRawWorkspaceData = true;
        const bool loadedExistingRawWorkspaceData =
            std::filesystem::exists(job.projectPath) &&
            StackBinaryFormat::ReadProjectFile(job.projectPath, existingDocumentForWorker, existingOptions);
        const nlohmann::json workerEmbeddedRaw =
            loadedExistingRawWorkspaceData && existingDocumentForWorker.rawWorkspaceData.is_object()
                ? existingDocumentForWorker.rawWorkspaceData.value("embeddedRaw", nlohmann::json::object())
                : nlohmann::json::object();
        if (workerEmbeddedRaw.is_object() && workerEmbeddedRaw.value("present", false)) {
            job.document.rawWorkspaceData["embeddedRaw"] = workerEmbeddedRaw;
            job.document.rawWorkspaceData["rawSourceRef"]["linked"] = false;
            job.document.rawWorkspaceData["rawSourceRef"]["embedded"] = true;
        } else if (job.projectStatus == Stack::RawWorkspace::ProjectStatus::Embedded) {
            error = "Failed to preserve the embedded RAW source while saving.";
            return false;
        }

        if (job.projectPath.has_parent_path()) {
            std::filesystem::create_directories(job.projectPath.parent_path());
        }
        if (!StackBinaryFormat::WriteProjectFile(job.projectPath, job.document)) {
            error = "Failed to save the RAW project.";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    } catch (...) {
        error = "Failed to save the RAW project.";
        return false;
    }
}

void EditorModule::CompleteRawWorkspaceProjectSave(
    RawWorkspaceProjectSaveJob job,
    bool success,
    bool skippedStale,
    std::string errorMessage) {
    if (m_RawWorkspaceProjectSaveInFlightCount > 0) {
        --m_RawWorkspaceProjectSaveInFlightCount;
    }
    m_RawWorkspaceProjectSaveTaskState = m_RawWorkspaceProjectSaveInFlightCount > 0
        ? Async::TaskState::Running
        : Async::TaskState::Idle;
    m_RawWorkspaceProjectSaveStatusText = m_RawWorkspaceProjectSaveInFlightCount > 0
        ? "Saving RAW project..."
        : std::string();

    if (skippedStale) {
        return;
    }

    const bool jobWorkspaceCurrent =
        job.workspaceRoot.lexically_normal() == m_RawWorkspace.workspaceRoot.lexically_normal();
    const bool jobRevisionCurrent = IsRawWorkspaceProjectSaveJobCurrent(job);
    Stack::RawWorkspace::SourceRecord* savedSource = jobWorkspaceCurrent
        ? FindRawWorkspaceSourceByKey(job.sourceKey)
        : nullptr;
    if (success) {
        if (savedSource) {
            savedSource->project.status = job.projectStatus;
            savedSource->project.absolutePath = job.projectPath;
            savedSource->project.relativePath = job.projectRelativePath;
            savedSource->project.mode = job.mode;
            savedSource->project.projectModifiedTimeTicks = RawWorkspaceProjectFileTimeTicks(job.projectPath);
            if (jobRevisionCurrent) {
                savedSource->project.autosaved = true;
                savedSource->project.dirty = false;
            }
            InvalidateRawWorkspaceGalleryPresentation();
        }
        if (jobWorkspaceCurrent &&
            jobRevisionCurrent &&
            m_ActiveRawWorkspaceSourceKey == job.sourceKey) {
            SetCurrentProjectName(job.projectName);
            SetCurrentProjectFileName(job.projectPath.string());
            ClearDirty();
        }
        if (jobWorkspaceCurrent) {
            PersistRawWorkspaceCatalog();
        }
        if (jobRevisionCurrent && jobWorkspaceCurrent) {
            QueueUiNotification(
                UiNotificationSeverity::Success,
                "RAW project autosaved.",
                "raw-workspace-project-autosave");
        }
    } else {
        if (savedSource) {
            if (jobRevisionCurrent) {
                savedSource->project.absolutePath = job.previousAbsolutePath;
                savedSource->project.relativePath = job.previousRelativePath;
                savedSource->project.status = job.previousProjectStatus;
                savedSource->project.mode = job.previousMode;
                savedSource->project.autosaved = job.previousAutosaved;
            }
            savedSource->project.dirty = true;
            InvalidateRawWorkspaceGalleryPresentation();
        }
        if (jobWorkspaceCurrent && m_ActiveRawWorkspaceSourceKey == job.sourceKey) {
            MarkDirty();
        }
        if (jobWorkspaceCurrent) {
            PersistRawWorkspaceCatalog();
        }
        QueueUiNotification(
            UiNotificationSeverity::Error,
            errorMessage.empty() ? "Failed to save the RAW project." : errorMessage,
            "raw-workspace-project-save");
    }
}

void EditorModule::RawWorkspaceProjectSaveWorkerLoop() {
    while (true) {
        RawWorkspaceProjectSaveJob job;
        {
            std::unique_lock<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
            m_RawWorkspaceProjectSaveCv.wait(lock, [this]() {
                return m_RawWorkspaceProjectSaveWorkerStopRequested ||
                    !m_RawWorkspaceProjectSaveQueue.empty();
            });
            if (m_RawWorkspaceProjectSaveQueue.empty()) {
                if (m_RawWorkspaceProjectSaveWorkerStopRequested) {
                    break;
                }
                continue;
            }
            job = std::move(m_RawWorkspaceProjectSaveQueue.front());
            m_RawWorkspaceProjectSaveQueue.pop_front();
            m_RawWorkspaceProjectSaveWorkerBusy = true;
        }

        std::string error;
        bool skippedStale = false;
        bool success = true;
        if (!IsRawWorkspaceProjectSaveJobCurrent(job)) {
            skippedStale = true;
        } else {
            success = WriteRawWorkspaceProjectSaveJob(job, error);
        }

        bool postCompletion = true;
        {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceProjectSaveMutex);
            m_RawWorkspaceProjectSaveWorkerBusy = false;
            postCompletion = !m_RawWorkspaceProjectSaveWorkerStopRequested;
        }
        m_RawWorkspaceProjectSaveCv.notify_all();

        if (postCompletion) {
            Async::TaskSystem::Get().PostToMain([
                this,
                job = std::move(job),
                success,
                skippedStale,
                error = std::move(error)
            ]() mutable {
                CompleteRawWorkspaceProjectSave(
                    std::move(job),
                    success,
                    skippedStale,
                    std::move(error));
            });
        }
    }
}

bool EditorModule::SaveActiveRawWorkspaceProject(bool explicitSave) {
    if (!IsRawWorkspaceProjectActive()) {
        return false;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_ActiveRawWorkspaceSourceKey);
    if (!source) {
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "The active RAW source is no longer in the Workspace scan.",
            "raw-workspace-save-missing-source");
        return false;
    }

    const std::filesystem::path workspaceRoot = m_RawWorkspace.workspaceRoot;
    const std::filesystem::path projectPath = m_ActiveRawWorkspaceProjectPath;
    const std::string sourceKey = m_ActiveRawWorkspaceSourceKey;
    const std::uint64_t saveRevision =
        BumpRawWorkspaceProjectSaveRevision(workspaceRoot, sourceKey);
    StackBinaryFormat::ProjectDocument document;
    StackBinaryFormat::ProjectDocument existingDocument;
    nlohmann::json existingEmbeddedRaw = nlohmann::json::object();
    bool preserveEmbeddedRaw = false;
    bool loadedExistingRawWorkspaceData = false;
    if (explicitSave) {
        StackBinaryFormat::ProjectLoadOptions existingOptions;
        existingOptions.includeThumbnail = false;
        existingOptions.includeSourceImage = false;
        existingOptions.includePipelineData = false;
        existingOptions.includeNodeBrowserThumbnails = false;
        existingOptions.includeRawWorkspaceData = true;
        {
            std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
            loadedExistingRawWorkspaceData =
                std::filesystem::exists(projectPath) &&
                StackBinaryFormat::ReadProjectFile(projectPath, existingDocument, existingOptions);
        }
        existingEmbeddedRaw = loadedExistingRawWorkspaceData && existingDocument.rawWorkspaceData.is_object()
            ? existingDocument.rawWorkspaceData.value("embeddedRaw", nlohmann::json::object())
            : nlohmann::json::object();
        preserveEmbeddedRaw = existingEmbeddedRaw.is_object() && existingEmbeddedRaw.value("present", false);
    }

    const std::string projectName = m_CurrentProjectName.empty()
        ? (source->stem.empty() ? source->fileName : source->stem)
        : m_CurrentProjectName;
    const nlohmann::json pipeline = SerializePipeline();
    if (!explicitSave) {
        document = {};
        document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
        document.metadata.projectName = projectName;
        document.pipelineData = pipeline;
        EnsureMinimalProjectDocument(document, projectName);
    } else if (!BuildProjectDocumentForSave(projectName, document)) {
        document = {};
        document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
        document.metadata.projectName = projectName;
        document.pipelineData = pipeline;
        document.nodeBrowserThumbnailEntries = GetPersistedNodeBrowserThumbnails();
        EnsureMinimalProjectDocument(document, projectName);
    }
    if (loadedExistingRawWorkspaceData && existingDocument.rawWorkspaceData.is_object()) {
        document.rawWorkspaceData = existingDocument.rawWorkspaceData;
    }

    Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
        *source,
        m_ActiveRawWorkspaceRecipe,
        pipeline,
        document,
        m_ActiveRawWorkspaceMode,
        !preserveEmbeddedRaw);
    ApplyActiveRawWorkspaceModeDataToDocument(document);
    if (preserveEmbeddedRaw) {
        document.rawWorkspaceData["embeddedRaw"] = existingEmbeddedRaw;
        document.rawWorkspaceData["rawSourceRef"]["linked"] = false;
        document.rawWorkspaceData["rawSourceRef"]["embedded"] = true;
    }

    const Stack::RawWorkspace::RawProjectMode savedMode = m_ActiveRawWorkspaceMode;
    const Stack::RawWorkspace::ProjectStatus savedProjectStatus =
        source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded
            ? Stack::RawWorkspace::ProjectStatus::Embedded
            : Stack::RawWorkspace::ProjectStatus::Existing;
    const std::filesystem::path projectRelativePath = source->project.relativePath.empty()
        ? Stack::RawWorkspace::BuildProjectRelativePathForSource(*source)
        : source->project.relativePath;

    if (!explicitSave) {
        RawWorkspaceProjectSaveJob job;
        job.workspaceRoot = workspaceRoot;
        job.projectPath = projectPath;
        job.projectRelativePath = projectRelativePath;
        job.sourceKey = sourceKey;
        job.projectName = projectName;
        job.sourceRevision = saveRevision;
        job.mode = savedMode;
        job.projectStatus = savedProjectStatus;
        job.previousAbsolutePath = source->project.absolutePath;
        job.previousRelativePath = source->project.relativePath;
        job.previousProjectStatus = source->project.status;
        job.previousMode = source->project.mode;
        job.previousAutosaved = source->project.autosaved;
        job.previousDirty = source->project.dirty;
        job.document = std::move(document);
        EnqueueRawWorkspaceProjectSave(std::move(job));

        if (source->project.absolutePath.empty()) {
            source->project.absolutePath = projectPath;
            source->project.relativePath = projectRelativePath;
        }
        if (source->project.status == Stack::RawWorkspace::ProjectStatus::Unknown ||
            source->project.status == Stack::RawWorkspace::ProjectStatus::NoProject) {
            source->project.status = Stack::RawWorkspace::ProjectStatus::Existing;
        }
        source->project.mode = savedMode;
        source->project.autosaved = true;
        source->project.dirty = false;
        InvalidateRawWorkspaceGalleryPresentation();
        SetCurrentProjectName(projectName);
        SetCurrentProjectFileName(projectPath.string());
        ClearDirty();
        m_LastAutoSaveTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        PersistRawWorkspaceCatalog();
        return true;
    }

    std::error_code ec;
    bool wroteProject = false;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        std::filesystem::create_directories(projectPath.parent_path(), ec);
        wroteProject = !ec && StackBinaryFormat::WriteProjectFile(projectPath, document);
    }
    if (!wroteProject) {
        MarkDirty();
        QueueUiNotification(
            UiNotificationSeverity::Error,
            "Failed to save the RAW project.",
            "raw-workspace-project-save");
        return false;
    }

    ApplyRawWorkspaceProjectInfoToSource(
        *source,
        document,
        projectPath,
        projectRelativePath,
        !explicitSave,
        false,
        explicitSave ? "explicit-save" : "autosave");
    InvalidateRawWorkspaceGalleryPresentation();
    for (EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment &&
            node.rawDevelopment.recipe.source.relativePathKey == m_ActiveRawWorkspaceSourceKey) {
            node.rawDevelopment.projectStatus = "Edited";
            node.rawDevelopment.edited = true;
            node.rawDevelopment.autosaved = !explicitSave;
        }
    }

    PersistRawWorkspaceCatalog();
    SetCurrentProjectName(projectName);
    SetCurrentProjectFileName(projectPath.string());
    ClearDirty();
    m_LastAutoSaveTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    QueueUiNotification(
        UiNotificationSeverity::Success,
        explicitSave ? "RAW project saved." : "RAW project autosaved.",
        explicitSave ? "raw-workspace-project-save" : "raw-workspace-project-autosave");
    return true;
}

bool EditorModule::SaveActiveRawWorkspaceProjectIfDirty() {
    if (!IsRawWorkspaceProjectActive() || !m_Dirty) {
        return true;
    }
    return SaveActiveRawWorkspaceProject(false);
}

bool EditorModule::FlushActiveRawWorkspaceProjectIfDirty() {
    return SaveActiveRawWorkspaceProjectIfDirty();
}

bool EditorModule::RequestSaveCurrentProject(
    const std::string& fallbackName,
    std::function<void(bool)> onComplete) {
    if (IsRawWorkspaceProjectActive()) {
        const bool success = SaveActiveRawWorkspaceProject(true);
        if (onComplete) {
            onComplete(success);
        }
        return success;
    }

    const std::string projectName = !fallbackName.empty()
        ? fallbackName
        : (m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName);
    LibraryManager::Get().RequestSaveProject(projectName, this, m_CurrentProjectFileName, std::move(onComplete));
    return true;
}

bool EditorModule::RelinkActiveRawWorkspaceProjectToSelectedSource() {
    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source || source->project.absolutePath.empty()) {
        return false;
    }

    BumpRawWorkspaceProjectSaveRevision(m_RawWorkspace.workspaceRoot, source->relativePathKey);
    StackBinaryFormat::ProjectDocument document;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        if (!StackBinaryFormat::ReadProjectFile(source->project.absolutePath, document)) {
            return false;
        }
        std::string error;
        if (!Stack::RawWorkspace::RelinkProjectDocumentToSource(*source, document, &error)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to relink RAW project." : error,
                "raw-workspace-project-relink");
            return false;
        }
        if (!StackBinaryFormat::WriteProjectFile(source->project.absolutePath, document)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Failed to write the relinked RAW project.",
                "raw-workspace-project-relink");
            return false;
        }
    }

    ApplyRawWorkspaceProjectInfoToSource(
        *source,
        document,
        source->project.absolutePath,
        source->project.relativePath.empty()
            ? Stack::RawWorkspace::BuildProjectRelativePathForSource(*source)
            : source->project.relativePath,
        false,
        false,
        "relinked-selected-source");
    PersistRawWorkspaceCatalog();
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "RAW project relinked.",
        "raw-workspace-project-relink");
    return true;
}

bool EditorModule::EmbedActiveRawWorkspaceProject() {
    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source || source->project.absolutePath.empty()) {
        return false;
    }

    BumpRawWorkspaceProjectSaveRevision(m_RawWorkspace.workspaceRoot, source->relativePathKey);
    StackBinaryFormat::ProjectDocument document;
    {
        std::lock_guard<std::mutex> fileLock(m_RawWorkspaceProjectFileWriteMutex);
        if (!StackBinaryFormat::ReadProjectFile(source->project.absolutePath, document)) {
            return false;
        }
        std::string error;
        if (!Stack::RawWorkspace::EmbedRawSourceInProjectDocument(*source, document, &error)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                error.empty() ? "Failed to embed RAW source." : error,
                "raw-workspace-project-embed");
            return false;
        }
        if (!StackBinaryFormat::WriteProjectFile(source->project.absolutePath, document)) {
            QueueUiNotification(
                UiNotificationSeverity::Error,
                "Failed to write the embedded RAW project.",
                "raw-workspace-project-embed");
            return false;
        }
    }

    ApplyRawWorkspaceProjectInfoToSource(
        *source,
        document,
        source->project.absolutePath,
        source->project.relativePath.empty()
            ? Stack::RawWorkspace::BuildProjectRelativePathForSource(*source)
            : source->project.relativePath,
        false,
        false,
        "embedded-selected-source");
    PersistRawWorkspaceCatalog();
    QueueUiNotification(
        UiNotificationSeverity::Success,
        "RAW source embedded in the selected project.",
        "raw-workspace-project-embed");
    return true;
}

void EditorModule::RenderRawWorkspaceLifecyclePopups() {
    if (m_ShowRawWorkspaceRelinkPopup) {
        ImGui::OpenPopup("Relink RAW Project##RawWorkspace");
        m_ShowRawWorkspaceRelinkPopup = false;
    }
    if (m_ShowRawWorkspaceEmbedPopup) {
        ImGui::OpenPopup("Bake / Embed RAW##RawWorkspace");
        m_ShowRawWorkspaceEmbedPopup = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Relink RAW Project##RawWorkspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
        ImGui::TextWrapped(
            "Relink this project to the selected RAW file. Stack will update the linked source metadata in the project file.");
        if (source != nullptr) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", source->relativePathKey.c_str());
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(source == nullptr || source->project.absolutePath.empty());
        if (ImGui::Button("Relink", ImVec2(120.0f, 0.0f))) {
            RelinkActiveRawWorkspaceProjectToSelectedSource();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Bake / Embed RAW##RawWorkspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Stack::RawWorkspace::SourceRecord* source =
            FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
        ImGui::TextWrapped(
            "Linked projects stay smaller and depend on the original RAW file. Embedded projects are larger but keep a copy of this RAW inside the selected project.");
        if (source != nullptr) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", source->relativePathKey.c_str());
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(source == nullptr || source->project.absolutePath.empty());
        if (ImGui::Button("Embed In This Project", ImVec2(180.0f, 0.0f))) {
            EmbedActiveRawWorkspaceProject();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorModule::RenderProjectLifecyclePopups() {
    if (m_ShowNewProjectPrompt) {
        ImGui::OpenPopup("Start New Project##Editor");
        m_ShowNewProjectPrompt = false;
    }
    if (m_ShowNewProjectDiscardConfirm) {
        ImGui::OpenPopup("Confirm Discard Project##Editor");
        m_ShowNewProjectDiscardConfirm = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Start New Project##Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Start a new project? You can save the current project first, continue without saving, or cancel.");
        ImGui::Spacing();

        if (ImGui::Button("Save Project", ImVec2(140.0f, 0.0f))) {
            const std::string projectName = m_CurrentProjectName.empty() ? "Untitled Project" : m_CurrentProjectName;
            if (RequestSaveCurrentProject(projectName, [this](bool success) {
                    if (success) {
                        ResetToBlankProject();
                    }
                })) {
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(140.0f, 0.0f))) {
            m_ShowNewProjectDiscardConfirm = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Discard Project##Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Are you sure you want to discard the current project and start a new blank one?");
        ImGui::Spacing();

        if (ImGui::Button("Yes, Discard", ImVec2(140.0f, 0.0f))) {
            ResetToBlankProject();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

