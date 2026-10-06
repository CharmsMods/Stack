#include "App/WorkspacePresentation.h"
#include "Editor/EditorModule.h"

#include "App/AppPaths.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Library/LibraryManager.h"
#include "Raw/RawLoader.h"
#include "Restormer/RestormerClient.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr double kRawWorkspacePersistDebounceSeconds = 0.35;
constexpr std::size_t kRawWorkspaceThumbnailApplyBatchSize = 8;
constexpr float kRawWorkspaceControlsDefaultWidth = 420.0f;
constexpr float kRawWorkspaceControlsMinWidth = 340.0f;
constexpr float kRawWorkspaceControlsMaxWidth = 1040.0f;

struct RawWorkspaceThumbnailUpdate {
    std::size_t sourceIndex = 0;
    std::string sourceKey;
    Stack::RawWorkspace::ThumbnailInfo thumbnail;
    bool transientPreview = false;
};

struct RawWorkspaceProgressivePreviewSession {
    std::mutex mutex;
    std::condition_variable completed;
    std::size_t activeBatches = 0;
};

struct RawWorkspaceCachedSourceIdentity {
    std::uintmax_t fileSizeBytes = 0;
    std::int64_t modifiedTimeTicks = 0;
    std::string fingerprint;
    int algorithmVersion = 0;
    std::int64_t captureTimestamp = 0;
    bool captureMetadataChecked = false;
};

void SetRawWorkspaceStatusNoThrow(
    std::string& target,
    const char* message) noexcept {
    try {
        target = message ? message : "";
    } catch (...) {
        target.clear();
    }
}

double RawWorkspaceClockSeconds() {
    return ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
}

bool RawWorkspaceDebounceElapsed(double dirtyTime) {
    if (!ImGui::GetCurrentContext() || dirtyTime < 0.0) {
        return true;
    }
    return (ImGui::GetTime() - dirtyTime) >= kRawWorkspacePersistDebounceSeconds;
}

float NormalizeRawWorkspaceControlsPanelWidth(float width) {
    if (!std::isfinite(width) || width <= 0.0f) {
        width = kRawWorkspaceControlsDefaultWidth;
    }
    return std::clamp(
        width,
        kRawWorkspaceControlsMinWidth,
        kRawWorkspaceControlsMaxWidth);
}

int FindDirectDownstreamRawDecode(
    const EditorNodeGraph::Graph& graph,
    int rawNodeId) {
    if (rawNodeId <= 0) {
        return -1;
    }

    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (link.fromNodeId != rawNodeId ||
            link.fromSocketId != EditorNodeGraph::kRawOutputSocketId ||
            link.toSocketId != EditorNodeGraph::kRawInputSocketId) {
            continue;
        }

        const EditorNodeGraph::Node* downstream = graph.FindNode(link.toNodeId);
        if (downstream &&
            downstream->kind == EditorNodeGraph::NodeKind::RawDecode) {
            return downstream->id;
        }
    }

    return -1;
}

int FindUpstreamRawDecode(const EditorNodeGraph::Graph& graph, int nodeId) {
    std::unordered_set<int> visited;
    int currentNodeId = nodeId;

    for (int depth = 0; depth < 64 && currentNodeId > 0; ++depth) {
        if (!visited.insert(currentNodeId).second) {
            break;
        }

        const EditorNodeGraph::Node* node = graph.FindNode(currentNodeId);
        if (!node) {
            break;
        }

        if (node->kind == EditorNodeGraph::NodeKind::RawDecode) {
            return node->id;
        }

        if (node->kind == EditorNodeGraph::NodeKind::RawSource ||
            node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            const int downstreamRawDecode =
                FindDirectDownstreamRawDecode(graph, node->id);
            return downstreamRawDecode > 0 ? downstreamRawDecode : -1;
        }

        const EditorNodeGraph::Link* input = graph.FindInputLink(
            node->id,
            node->kind == EditorNodeGraph::NodeKind::RawDevelop
                ? EditorNodeGraph::kRawInputSocketId
                : EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            break;
        }
        currentNodeId = input->fromNodeId;
    }

    return -1;
}

int FindFirstNodeOfKind(
    const EditorNodeGraph::Graph& graph,
    EditorNodeGraph::NodeKind kind) {
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == kind) {
            return node.id;
        }
    }
    return -1;
}

} // namespace

bool EditorModule::FocusRawWorkspace() {
    SwitchToSubWindow(EditorSubWindow::NodeGraph);

    const int selectedNodeId = m_Project->graph.GetSelectedNodeId();
    const int selectedRawDecodeId = FindUpstreamRawDecode(m_Project->graph, selectedNodeId);
    if (selectedRawDecodeId > 0) {
        SelectGraphNode(selectedRawDecodeId);
        return true;
    }

    const int firstRawDecodeId = FindFirstNodeOfKind(m_Project->graph, EditorNodeGraph::NodeKind::RawDecode);
    if (firstRawDecodeId > 0) {
        SelectGraphNode(firstRawDecodeId);
        return true;
    }

    const int firstRawSourceId = FindFirstNodeOfKind(m_Project->graph, EditorNodeGraph::NodeKind::RawSource);
    if (firstRawSourceId > 0) {
        SelectGraphNode(firstRawSourceId);
        return true;
    }

    return false;
}

std::filesystem::path EditorModule::GetRawWorkspaceAppStatePath() const {
    return AppPaths::GetSettingsDirectory() / "RawWorkspaceState.json";
}

Stack::RawWorkspace::AppState
EditorModule::BuildRawWorkspaceAppStateSnapshot() const {
    Stack::RawWorkspace::AppState appState;
    appState.lastWorkspaceRoot = m_RawWorkspace.workspaceRoot;
    appState.recentWorkspaceRoots = m_RawWorkspace.recentWorkspaceRoots;
    appState.controlsPanelWidth = NormalizeRawWorkspaceControlsPanelWidth(
        m_RawWorkspaceLayoutUi.controlsPanelWidth);
    appState.rawLabToolRailWidth = m_RawWorkspaceLabUi.toolRailWidth;
    appState.rawLabLowerShelfHeight = m_RawWorkspaceLabUi.lowerShelfHeight;
    appState.rawLabLowerShelfOpen = m_RawWorkspaceLabUi.lowerShelfOpen;
    appState.rawLabToolRailOnRight = m_RawWorkspaceLabUi.toolRailOnRight;
    appState.rawLabFilmstripHeight = m_RawWorkspaceLabUi.filmstripHeight;
    appState.rawLabGalleryThumbnailScale =
        m_RawWorkspaceLabUi.galleryThumbnailScale;
    appState.rawLabActiveTool =
        static_cast<int>(m_RawWorkspaceLabUi.activeTool);
    appState.rawLabActivePointCurve =
        std::clamp(m_RawWorkspaceLabUi.activePointCurve, 0, 3);
    appState.rawLabColorWarpLiveCloud =
        m_RawWorkspaceLabUi.colorWarpLiveCloud;
    appState.rawLabLastGalleryHost =
        static_cast<int>(m_RawWorkspaceLabUi.lastGalleryHost);
    appState.rawLabGalleryDisplayMode =
        m_RawWorkspaceGalleryDisplayMode ==
                Stack::RawWorkspace::GalleryDisplayMode::List
            ? 1
            : 0;
    appState.rawLabGalleryContentMode = static_cast<int>(m_RawWorkspaceGalleryContentMode);
    return appState;
}

EditorModule::RawWorkspaceScanSnapshot EditorModule::GetRawWorkspaceScanSnapshot() const {
    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
    return m_RawWorkspaceScanSnapshot;
}

EditorModule::RawWorkspaceThumbnailSnapshot EditorModule::GetRawWorkspaceThumbnailSnapshot() const {
    std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
    return m_RawWorkspaceThumbnailSnapshot;
}

void EditorModule::EnsureRawWorkspaceLoaded() {
    if (!m_RawWorkspaceAppStateLoaded) {
        LoadRawWorkspaceAppState();
    }
}

void EditorModule::OpenRawWorkspaceFolderDialog() {
    EnsureRawWorkspaceLoaded();
    const std::string path = FileDialogs::OpenFolderDialog("Open RAW Folder");
    if (!path.empty()) {
        RequestOpenRawWorkspace(path);
    }
}

void EditorModule::RescanRawWorkspace() {
    if(RequestAutoBracketForeground("rescan the folder",[this]{RescanRawWorkspace();}))return;
    EnsureRawWorkspaceLoaded();
    RequestRawWorkspaceScan();
}

void EditorModule::ClearRawWorkspaceForUser() {
    if(RequestAutoBracketForeground("clear the folder",[this]{ClearRawWorkspaceForUser();}))return;
    EnsureRawWorkspaceLoaded();
    const auto notifier = GetNotifier();
    const auto activity = notifier.BeginActivity("Clearing gallery");
    ClearRawWorkspace();
    notifier.CompleteActivity(activity, "Gallery cleared.");
}

void EditorModule::SelectRawWorkspaceSourceForPreview(const std::string& sourceKey) {
    EnsureRawWorkspaceLoaded();
    // This compatibility entry point comes from the graph's explicit
    // "Edit In RAW Tab" action. Browsing a Gallery source is selection-only;
    // entering its editing session must remain an explicit open.
    RequestOpenRawWorkspaceSourceForEditing(sourceKey);
}

void EditorModule::SelectRawWorkspaceSourceForGallery(
    const std::string& sourceKey,
    bool toggle,
    bool extendRange,
    bool openForEditing) {
    if(!openForEditing && IsBracketingToolActive() && !m_RawWorkspaceLabUi.galleryWorkspaceOpen) {
        SelectBracketingSources({sourceKey},toggle,extendRange);
        return;
    }
    EnsureRawWorkspaceLoaded();
    const auto target = std::find_if(
        m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == sourceKey;
        });
    if (target == m_RawWorkspace.sources.end()) return;

    if (extendRange && !m_RawWorkspace.selectedSourceKey.empty()) {
        const auto anchor = std::find_if(
            m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
            [&](const Stack::RawWorkspace::SourceRecord& source) {
                return source.relativePathKey == m_RawWorkspace.selectedSourceKey;
            });
        if (anchor != m_RawWorkspace.sources.end()) {
            const std::size_t first = static_cast<std::size_t>(std::distance(
                m_RawWorkspace.sources.begin(), std::min(anchor, target)));
            const std::size_t last = static_cast<std::size_t>(std::distance(
                m_RawWorkspace.sources.begin(), std::max(anchor, target)));
            m_RawWorkspace.selectedSourceKeys.clear();
            for (std::size_t index = first; index <= last; ++index) {
                m_RawWorkspace.selectedSourceKeys.push_back(
                    m_RawWorkspace.sources[index].relativePathKey);
            }
        }
    } else if (toggle) {
        const auto selected = std::find(
            m_RawWorkspace.selectedSourceKeys.begin(),
            m_RawWorkspace.selectedSourceKeys.end(),
            sourceKey);
        if (selected == m_RawWorkspace.selectedSourceKeys.end()) {
            m_RawWorkspace.selectedSourceKeys.push_back(sourceKey);
        } else {
            m_RawWorkspace.selectedSourceKeys.erase(selected);
        }
        m_RawWorkspace.selectedSourceKey = sourceKey;
    } else {
        m_RawWorkspace.selectedSourceKeys.assign(1u, sourceKey);
        m_RawWorkspace.selectedSourceKey = sourceKey;
    }
    InvalidateRawWorkspaceGalleryPresentation();
    PersistRawWorkspaceCatalog();
    SaveRawWorkspaceAppState();
    if (openForEditing) {
        RequestOpenRawWorkspaceSourceForEditing(sourceKey);
    }
}

bool EditorModule::RequestOpenRawWorkspaceSourceForEditing(
    const std::string& sourceKey) {
    if(RequestAutoBracketForeground("open an image",[this,sourceKey]{RequestOpenRawWorkspaceSourceForEditing(sourceKey);}))return true;
    const Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(sourceKey);
    if (!source) {
        return false;
    }
    if (m_PermanentGalleryWorkspace) {
        if (!m_RawGalleryOpenSelectionHandler) return false;
        RawGalleryOpenSelection selection;
        selection.items.push_back({source->absolutePath, false});
        m_RawGalleryOpenSelectionHandler(std::move(selection), false);
        return true;
    }
    if (IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finish opening the current RAW selection before opening another image.",
            "raw-workspace-selection-load-busy");
        return false;
    }

    auto openAction = [this, sourceKey](std::string* error) {
        if (!FindRawWorkspaceSourceByKey(sourceKey)) {
            if (error) {
                *error = "The selected RAW image is no longer in the current Gallery folder.";
            }
            return false;
        }
        m_RawWorkspaceExplicitReplacementSourceKey = sourceKey;
        CloseRawWorkspaceGalleryWorkspace();
        if (m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame)
            m_RawWorkspaceLabUi.activeTool = RawLabTool::Light;
        if (m_RawWorkspaceRootTabActive) {
            // An explicit open replaces the active editing session. It is not
            // ordinary one-click Gallery browsing, so an Editor-project lock
            // must not swallow the request.
            m_RawWorkspaceLockedByEditorProject = false;
            SelectRawWorkspaceSource(sourceKey);
        } else {
            m_PendingRawWorkspaceExplicitOpenSourceKey = sourceKey;
            RequestOpenRawWorkspaceTab();
        }
        return true;
    };

    const bool opensCurrentSingleRawProject =
        IsRawWorkspaceProjectActive() &&
        !m_Project->rawSourceKey.empty() &&
        sourceKey == m_Project->rawSourceKey;
    if ((NeedsWorkspaceSaveBeforeTransition() || IsProjectFileSaveBusy() || IsRawWorkspaceProjectSaveBusy()) &&
        !opensCurrentSingleRawProject) {
        QueueRawWorkspaceProjectReplacement(
            "open the selected RAW image",
            source->fileName.empty() ? sourceKey : source->fileName,
            std::move(openAction),
            sourceKey);
        RequestOpenRawWorkspaceTab();
        return true;
    }

    std::string error;
    return openAction(&error);
}

bool EditorModule::IsRawWorkspaceScanBusy() const {
    return Async::IsBusy(GetRawWorkspaceScanSnapshot().state);
}

bool EditorModule::IsRawWorkspaceScanBlockingGallery() const {
    const RawWorkspaceScanSnapshot snapshot = GetRawWorkspaceScanSnapshot();
    return Async::IsBusy(snapshot.state) && !snapshot.sourcesPublished;
}

bool EditorModule::IsRawWorkspaceThumbnailBusy() const {
    return Async::IsBusy(GetRawWorkspaceThumbnailSnapshot().state);
}

bool EditorModule::CanEditRawWorkspaceFilmstripOrganization() const {
    const RawWorkspaceScanSnapshot snapshot = GetRawWorkspaceScanSnapshot();
    return snapshot.completedSuccessfully && !Async::IsBusy(snapshot.state);
}

void EditorModule::InvalidateRawWorkspaceGalleryPresentation() {
    ++m_RawWorkspaceGalleryRevision;
}

const Stack::RawWorkspace::GalleryPresentation& EditorModule::GetRawWorkspaceGalleryPresentation() {
    if (m_RawWorkspaceGalleryPresentationRevision != m_RawWorkspaceGalleryRevision) {
        m_RawWorkspaceGalleryPresentationCache =
            Stack::RawWorkspace::BuildGalleryPresentation(m_RawWorkspace);
        m_RawWorkspaceGalleryPresentationRevision = m_RawWorkspaceGalleryRevision;
    }
    return m_RawWorkspaceGalleryPresentationCache;
}

std::string EditorModule::GetRawWorkspaceScanStatusText() const {
    return GetRawWorkspaceScanSnapshot().statusText;
}

std::string EditorModule::GetRawWorkspaceThumbnailStatusText() const {
    return GetRawWorkspaceThumbnailSnapshot().statusText;
}

std::string EditorModule::GetRawWorkspaceProgramBarStatus() const {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        return "RAW Workspace";
    }

    const RawWorkspaceScanSnapshot scanSnapshot = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnailSnapshot = GetRawWorkspaceThumbnailSnapshot();
    std::vector<std::string> parts;

    std::filesystem::path workspaceName = m_RawWorkspace.workspaceRoot.filename();
    if (workspaceName.empty()) {
        workspaceName = m_RawWorkspace.workspaceRoot;
    }
    parts.emplace_back("RAW: " + workspaceName.string());
    parts.emplace_back(std::to_string(m_RawWorkspace.sources.size()) + " files");

    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (selectedSource != nullptr) {
        parts.emplace_back(selectedSource->fileName);
        const char* projectLabel =
            Stack::RawWorkspace::ProjectStatusLabel(selectedSource->project.status);
        if (projectLabel != nullptr && projectLabel[0] != '\0') {
            parts.emplace_back(projectLabel);
        }
    }

    std::string busyText;
    if (Async::IsBusy(scanSnapshot.state)) {
        busyText = scanSnapshot.statusText.empty() ? "Scanning" : scanSnapshot.statusText;
    } else if (Async::IsBusy(thumbnailSnapshot.state)) {
        const Stack::RawWorkspace::ThumbnailProgress& progress = thumbnailSnapshot.progress;
        busyText = "Thumbnails " +
            std::to_string(std::clamp(progress.completed + progress.failed, 0, std::max(1, progress.total))) +
            "/" +
            std::to_string(std::max(1, progress.total));
    } else if (IsRawWorkspaceProjectLoadBusy()) {
        busyText = GetRawWorkspaceProjectLoadStatusText();
        if (busyText.empty()) {
            busyText = "Loading project";
        }
    } else if (IsRawWorkspaceProjectSaveBusy()) {
        busyText = GetRawWorkspaceProjectSaveStatusText();
        if (busyText.empty()) {
            busyText = "Saving project";
        }
    } else if (selectedSource != nullptr &&
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == selectedSource->relativePathKey &&
        (IsEditorRenderBusy() || m_RenderDirty || m_RawWorkspaceFullResolutionPreviewPending)) {
        busyText = Stack::Restormer::Client::Instance().IsInferenceActive()
            ? "AI denoise updating"
            : "Rendering";
    }
    if (!busyText.empty()) {
        parts.emplace_back(busyText);
    } else if (!m_RawWorkspaceStaleRenderStatusText.empty()) {
        parts.emplace_back("Preview stale");
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << " | ";
        }
        out << parts[i];
    }
    return out.str();
}

Stack::RawWorkspace::ScanProgress EditorModule::GetRawWorkspaceScanProgress() const {
    return GetRawWorkspaceScanSnapshot().progress;
}

Stack::RawWorkspace::ThumbnailProgress EditorModule::GetRawWorkspaceThumbnailProgress() const {
    return GetRawWorkspaceThumbnailSnapshot().progress;
}

void EditorModule::LoadRawWorkspaceAppState() {
    if (m_RawWorkspaceAppStateLoaded) {
        return;
    }
    m_RawWorkspaceAppStateLoaded = true;
    (void)GetRawViewportPreferences();

    Stack::RawWorkspace::AppState appState;
    std::string error;
    if (!Stack::RawWorkspace::LoadAppState(GetRawWorkspaceAppStatePath(), appState, &error)) {
        PostNotification(
            UiNotificationSeverity::Error,
            error.empty() ? "RAW Workspace state could not be loaded." : error,
            "raw-workspace-state-load");
    }

    m_RawWorkspace.recentWorkspaceRoots = appState.recentWorkspaceRoots;
    // Gallery selection is session-only. The RAW tab begins without an image
    // loaded or enlarged in the workspace.
    m_RawWorkspace.selectedSourceKey.clear();
    m_RawWorkspaceLayoutUi.controlsPanelWidth =
        NormalizeRawWorkspaceControlsPanelWidth(appState.controlsPanelWidth);
    m_RawWorkspaceLabUi.toolRailWidth = std::clamp(
        appState.rawLabToolRailWidth > 0.0f ? appState.rawLabToolRailWidth : 340.0f,
        240.0f,
        420.0f);
    m_RawWorkspaceLabUi.lowerShelfHeight = std::clamp(
        appState.rawLabLowerShelfHeight > 0.0f ? appState.rawLabLowerShelfHeight : 180.0f,
        80.0f,
        420.0f);
    m_RawWorkspaceLabUi.lowerShelfOpen = appState.rawLabLowerShelfOpen;
    m_RawWorkspaceLabUi.toolRailOnRight = false;
    m_RawWorkspaceLabUi.filmstripHeight = std::clamp(
        appState.rawLabFilmstripHeight > 0.0f ? appState.rawLabFilmstripHeight : 132.0f,
        96.0f,
        280.0f);
    m_RawWorkspaceLabUi.galleryThumbnailScale = std::clamp(
        appState.rawLabGalleryThumbnailScale > 0.0f
            ? appState.rawLabGalleryThumbnailScale
            : 1.0f,
        0.65f,
        1.75f);
    m_RawWorkspaceLabUi.activeTool = appState.rawLabActiveTool == static_cast<int>(RawLabTool::Exposure)
        ? RawLabTool::Exposure : appState.rawLabActiveTool == static_cast<int>(RawLabTool::Detail)
        ? RawLabTool::Detail : appState.rawLabActiveTool == static_cast<int>(RawLabTool::Calibration)
        ? RawLabTool::Calibration : appState.rawLabActiveTool ==
            static_cast<int>(RawLabTool::Color)
        ? RawLabTool::Color
        : static_cast<RawLabTool>(
              std::clamp(appState.rawLabActiveTool, 0, 7));
    m_RawWorkspaceLabUi.lastCurvesTool =
        Stack::EditorModuleTypes::RawLabDrawerTool(m_RawWorkspaceLabUi.activeTool) == RawLabTool::Zones
            ? m_RawWorkspaceLabUi.activeTool : RawLabTool::Zones;
    m_RawWorkspaceLabUi.lastColorTool =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Calibration
            ? RawLabTool::Calibration : RawLabTool::Color;
    m_RawWorkspaceLabUi.activePointCurve =
        std::clamp(appState.rawLabActivePointCurve, 0, 3);
    m_RawWorkspaceLabUi.colorWarpLiveCloud =
        appState.rawLabColorWarpLiveCloud;
    m_RawWorkspaceLabUi.lastGalleryHost =
        appState.rawLabLastGalleryHost == static_cast<int>(RawGalleryHost::NativeWindow)
            ? RawGalleryHost::NativeWindow
            : RawGalleryHost::Filmstrip;
    m_RawWorkspaceLabUi.galleryHost = m_RawWorkspaceLabUi.galleryWorkspaceOpen
        ? RawGalleryHost::Filmstrip : RawGalleryHost::Closed;
    m_RawWorkspaceGalleryDisplayMode =
        appState.rawLabGalleryDisplayMode == 1
            ? Stack::RawWorkspace::GalleryDisplayMode::List
            : Stack::RawWorkspace::GalleryDisplayMode::Grid;
    m_RawWorkspaceGalleryContentMode = static_cast<Stack::RawWorkspace::GalleryContentMode>(
        std::clamp(appState.rawLabGalleryContentMode,0,2));

    if (!appState.lastWorkspaceRoot.empty()) {
        RequestOpenRawWorkspace(appState.lastWorkspaceRoot);
    }
}

void EditorModule::SaveRawWorkspaceAppState() {
    if (!m_WorkspaceAppStatePersistenceEnabled) return;
    if (Stack::Workspace::IsPreview()) return;
    m_RawWorkspaceAppStatePersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceAppStatePersistDirty = true;
    m_RawWorkspaceAppStatePersistDirtyTime = RawWorkspaceClockSeconds();
    StartRawWorkspaceAppStatePersistIfNeeded();
}

void EditorModule::SaveRawWorkspaceGalleryGroupingState() {
    ++m_RawWorkspaceFilmstripOrganizationRevision;
    if (Stack::Workspace::IsPreview()) return;
    const auto key = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    const auto grouping = m_RawWorkspaceManualGroupings.find(key);
    if (!key.empty() && grouping != m_RawWorkspaceManualGroupings.end()) {
        std::string error;
        if (!Stack::RawWorkspace::SaveGalleryState(
                Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot),
                grouping->second, &error)) {
            PostNotification(UiNotificationSeverity::Error, error, "gallery-state-save");
        }
    }
}

void EditorModule::StartRawWorkspaceAppStatePersistIfNeeded() {
    if (!m_WorkspaceAppStatePersistenceEnabled) return;
    if (!m_RawWorkspaceAppStatePersistDirty ||
        m_RawWorkspaceAppStatePersistInFlight) {
        return;
    }
    if (!RawWorkspaceDebounceElapsed(m_RawWorkspaceAppStatePersistDirtyTime)) {
        m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Queued;
        m_RawWorkspaceAppStatePersistStatusText = "Saving RAW Workspace state...";
        return;
    }

    Stack::RawWorkspace::AppState appState =
        BuildRawWorkspaceAppStateSnapshot();

    const std::filesystem::path appStatePath = GetRawWorkspaceAppStatePath();
    const std::uint64_t generation =
        m_RawWorkspaceAppStatePersistGeneration.load(std::memory_order_relaxed);
    m_RawWorkspaceAppStatePersistDirty = false;
    m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
    m_RawWorkspaceAppStatePersistInFlight = true;
    m_RawWorkspaceAppStatePersistInFlightGeneration = generation;
    m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Queued;
    m_RawWorkspaceAppStatePersistStatusText = "Saving RAW Workspace state...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Saving workspace", [
            this,
            generation,
            appStatePath,
            appState = std::move(appState)
        ]() mutable {
            std::string error;
            bool success = false;
            try {
                success = Stack::RawWorkspace::SaveAppStateIfCurrent(
                    appStatePath,
                    appState,
                    [this, generation]() {
                        return generation ==
                            m_RawWorkspaceAppStatePersistGeneration.load(
                                std::memory_order_relaxed);
                    },
                    &error);
            } catch (...) {
                SetRawWorkspaceStatusNoThrow(
                    error,
                    "RAW Workspace state could not be saved.");
            }

            ProjectTasks().PostToMain([
                this,
                generation,
                success,
                error = std::move(error)
            ]() mutable {
                if (generation != m_RawWorkspaceAppStatePersistInFlightGeneration) {
                    return;
                }

                m_RawWorkspaceAppStatePersistInFlight = false;
                m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
                if (generation != m_RawWorkspaceAppStatePersistGeneration.load(std::memory_order_relaxed)) {
                    m_RawWorkspaceAppStatePersistTaskState = m_RawWorkspaceAppStatePersistDirty
                        ? Async::TaskState::Queued
                        : Async::TaskState::Idle;
                    m_RawWorkspaceAppStatePersistStatusText = m_RawWorkspaceAppStatePersistDirty
                        ? "Saving RAW Workspace state..."
                        : std::string();
                    StartRawWorkspaceAppStatePersistIfNeeded();
                    return;
                }
                if (success) {
                    m_RawWorkspaceAppStatePersistTaskState = m_RawWorkspaceAppStatePersistDirty
                        ? Async::TaskState::Queued
                        : Async::TaskState::Idle;
                    m_RawWorkspaceAppStatePersistStatusText = m_RawWorkspaceAppStatePersistDirty
                        ? "Saving RAW Workspace state..."
                        : std::string();
                } else {
                    m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Failed;
                    m_RawWorkspaceAppStatePersistStatusText = error.empty()
                        ? "RAW Workspace state could not be saved."
                        : error;
                    PostNotification(
                        UiNotificationSeverity::Error,
                        m_RawWorkspaceAppStatePersistStatusText,
                        "raw-workspace-state-save");
                }
                StartRawWorkspaceAppStatePersistIfNeeded();
            });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted &&
        generation == m_RawWorkspaceAppStatePersistInFlightGeneration) {
        m_RawWorkspaceAppStatePersistInFlight = false;
        m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
        m_RawWorkspaceAppStatePersistDirty = true;
        m_RawWorkspaceAppStatePersistDirtyTime = RawWorkspaceClockSeconds();
        m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Failed;
        m_RawWorkspaceAppStatePersistStatusText =
            "RAW Workspace state save could not be queued; it will retry.";
    }
}

void EditorModule::ResetRawWorkspaceAppStatePersistState() {
    m_RawWorkspaceAppStatePersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceAppStatePersistTaskState = Async::TaskState::Idle;
    m_RawWorkspaceAppStatePersistDirty = false;
    m_RawWorkspaceAppStatePersistInFlight = false;
    m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
    m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
    m_RawWorkspaceAppStatePersistStatusText.clear();
}

void EditorModule::FlushRawWorkspacePersistenceForShutdown() {
    m_RawWorkspaceAppStatePersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);

    if (m_RawWorkspaceAppStateLoaded && m_WorkspaceAppStatePersistenceEnabled) {
        Stack::RawWorkspace::AppState appState =
            BuildRawWorkspaceAppStateSnapshot();

        std::string appStateError;
        const bool appStateSaved =
            Stack::RawWorkspace::SaveAppState(GetRawWorkspaceAppStatePath(), appState, &appStateError);
        m_RawWorkspaceAppStatePersistDirty = false;
        m_RawWorkspaceAppStatePersistInFlight = false;
        m_RawWorkspaceAppStatePersistInFlightGeneration = 0;
        m_RawWorkspaceAppStatePersistDirtyTime = -1.0;
        m_RawWorkspaceAppStatePersistTaskState = appStateSaved
            ? Async::TaskState::Idle
            : Async::TaskState::Failed;
        m_RawWorkspaceAppStatePersistStatusText = appStateSaved
            ? std::string()
            : (appStateError.empty() ? "RAW Workspace state could not be saved." : appStateError);
    }

    if (!m_RawWorkspace.workspaceRoot.empty()) {
        const Stack::RawWorkspace::ManagedLayout layout =
            Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
        std::string catalogError;
        const bool catalogSaved = Stack::RawWorkspace::WriteCatalogSkeleton(
            layout,
            m_RawWorkspace.sources,
            m_PinnedRawWorkspaceSource.has_value()
                ? m_RawWorkspaceSelectedSourceBeforePinnedProject
                : m_RawWorkspace.selectedSourceKey,
            &catalogError);
        m_RawWorkspaceCatalogPersistDirty = false;
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
        m_RawWorkspaceCatalogPersistTaskState = catalogSaved
            ? Async::TaskState::Idle
            : Async::TaskState::Failed;
        m_RawWorkspaceCatalogPersistStatusText = catalogSaved
            ? std::string()
            : (catalogError.empty() ? "RAW Workspace catalog could not be saved." : catalogError);
    } else {
        m_RawWorkspaceCatalogPersistDirty = false;
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
        m_RawWorkspaceCatalogPersistStatusText.clear();
    }
}

void EditorModule::RequestOpenRawWorkspace(const std::filesystem::path& workspaceRoot) {
    if(RequestAutoBracketForeground("open another folder",[this,workspaceRoot]{RequestOpenRawWorkspace(workspaceRoot);}))return;
    if (workspaceRoot.empty()) {
        return;
    }
    std::string storageError;
    if (!Stack::RawWorkspace::EnsureManagedFolders(workspaceRoot, &storageError)) {
        PostNotification(UiNotificationSeverity::Error, storageError, "workspace-storage");
        return;
    }
    Stack::RawWorkspace::RawGalleryManualGrouping grouping;
    if (!Stack::RawWorkspace::LoadGalleryState(
            Stack::RawWorkspace::BuildManagedLayout(workspaceRoot), grouping, &storageError)) {
        PostNotification(UiNotificationSeverity::Error, storageError, "gallery-state-load");
        return;
    }
    PreserveActiveRawProjectSourceForLibraryNavigation();

    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::absolute(workspaceRoot, ec).lexically_normal();
    m_RawWorkspace.workspaceRoot = ec ? workspaceRoot.lexically_normal() : normalized;
    m_RawWorkspaceManualGroupings[m_RawWorkspace.workspaceRoot.generic_string()] = std::move(grouping);
    // A newly opened folder contains RAW sources before it has any saved
    // projects. Do not carry the previous folder's Projects-only filter into
    // it, or the completed scan looks like an empty filmstrip.
    m_RawWorkspaceGalleryContentMode =
        Stack::RawWorkspace::GalleryContentMode::Gallery;
    m_RawWorkspace.sources.clear();
    m_RawWorkspace.selectedSourceKey.clear();
    m_RawWorkspace.selectedSourceKeys.clear();
    m_RawWorkspace.sourceSetProjects.clear();
    m_RawWorkspaceLabSelectedProjectPaths.clear();
    m_RawWorkspaceLabFocusedProjectPath.clear();
    m_RawWorkspaceLabFocusedProjectName.clear();
    m_RawWorkspaceLabProjectSelectionAnchor.clear();
    InvalidateRawWorkspaceGalleryPresentation();
    m_RawWorkspacePreviewStageFailureSourceKey.clear();
    m_RawWorkspaceRecipePreviewCache.clear();
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    m_RawWorkspacePreviewStageQueuedFrame = -1;
    ClearRawWorkspaceThumbnailTextures();
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailScheduler.Clear();
        m_RawWorkspaceThumbnailWorkerActive = false;
        m_RawWorkspaceThumbnailDecodeRepairAttempts.clear();
    }
    ResetRawWorkspaceCatalogPersistState();
    Stack::RawWorkspace::AddRecentWorkspace(m_RawWorkspace, m_RawWorkspace.workspaceRoot);
    if (m_RawWorkspaceFolderChangedHandler) m_RawWorkspaceFolderChangedHandler(m_RawWorkspace.workspaceRoot);
    SaveRawWorkspaceAppState();
    RequestRawWorkspaceScan();
}

void EditorModule::RequestRawWorkspaceScan() {
    try {
        RequestRawWorkspaceScanImpl();
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
        const std::uint64_t generation = ++m_RawWorkspaceScanGeneration;
        m_RawWorkspaceScanSnapshot = {};
        m_RawWorkspaceScanSnapshot.generation = generation;
        m_RawWorkspaceScanSnapshot.state = Async::TaskState::Failed;
        SetRawWorkspaceStatusNoThrow(
            m_RawWorkspaceScanSnapshot.errorMessage,
            "Could not prepare the Workspace scan.");
        SetRawWorkspaceStatusNoThrow(
            m_RawWorkspaceScanSnapshot.statusText,
            "Could not prepare the Workspace scan.");
    }
}

void EditorModule::RequestRawWorkspaceScanImpl() {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        return;
    }

    ResetRawWorkspaceSimilarityStacks();

    const std::filesystem::path workspaceRoot = m_RawWorkspace.workspaceRoot;
    const std::string selectedBeforeScan = m_RawWorkspace.selectedSourceKey;
    const bool allowCatalogWarmStart = m_RawWorkspace.sources.empty();
    std::unordered_map<std::string, RawWorkspaceCachedSourceIdentity>
        reusableIdentities;
    reusableIdentities.reserve(m_RawWorkspace.sources.size());
    for (const auto& source : m_RawWorkspace.sources) {
        reusableIdentities.emplace(
            source.relativePathKey,
            RawWorkspaceCachedSourceIdentity {
                source.fileSizeBytes,
                source.modifiedTimeTicks,
                source.fingerprint,
                source.sourceIdentityAlgorithmVersion,
                source.captureTimestamp,
                source.captureMetadataChecked });
    }

    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
        generation = ++m_RawWorkspaceScanGeneration;
        m_RawWorkspaceScanSnapshot = {};
        m_RawWorkspaceScanSnapshot.generation = generation;
        m_RawWorkspaceScanSnapshot.state = Async::TaskState::Queued;
        m_RawWorkspaceScanSnapshot.sourcesPublished =
            !m_RawWorkspace.sources.empty();
        m_RawWorkspaceScanSnapshot.statusText = "Scanning Workspace...";
    }

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Scanning folder", 
            [
                this,
                generation,
                workspaceRoot,
                selectedBeforeScan,
                allowCatalogWarmStart,
                reusableIdentities = std::move(reusableIdentities)
            ]() mutable {
                auto isScanCancelled = [this, generation]() {
                    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                    return generation != m_RawWorkspaceScanGeneration;
                };
                auto markScanFailed = [this, generation](const char* message) noexcept {
                    try {
                        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                        if (generation != m_RawWorkspaceScanGeneration) {
                            return;
                        }
                        m_RawWorkspaceScanSnapshot.state = Async::TaskState::Failed;
                        SetRawWorkspaceStatusNoThrow(
                            m_RawWorkspaceScanSnapshot.errorMessage,
                            message);
                        SetRawWorkspaceStatusNoThrow(
                            m_RawWorkspaceScanSnapshot.statusText,
                            message);
                    } catch (...) {
                    }
                };
                auto updateProgress =
                    [this, generation](
                        const Stack::RawWorkspace::ScanProgress& progress) {
                        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                        if (generation != m_RawWorkspaceScanGeneration) {
                            return;
                        }
                        m_RawWorkspaceScanSnapshot.state =
                            Async::TaskState::Running;
                        m_RawWorkspaceScanSnapshot.progress = progress;
                        m_RawWorkspaceScanSnapshot.statusText =
                            progress.statusText.empty()
                            ? std::string("Scanning Workspace...")
                            : progress.statusText;
                    };
                auto isRawPath = [](const std::filesystem::path& path) {
                    return Raw::RawLoader::IsRawPath(path.string()) ||
                        Stack::RawWorkspace::DefaultRawPathPredicate(path);
                };

                try {
                    if (allowCatalogWarmStart) {
                        const Stack::RawWorkspace::ManagedLayout cachedLayout =
                            Stack::RawWorkspace::BuildManagedLayout(workspaceRoot);
                        std::vector<Stack::RawWorkspace::SourceRecord> cachedSources;
                        std::vector<Stack::RawWorkspace::SourceSetProjectCatalogEntry>
                            cachedProjects;
                        if (Stack::RawWorkspace::LoadCatalogSnapshot(
                                cachedLayout,
                                cachedSources,
                                nullptr,
                                nullptr,
                                &cachedProjects) &&
                            !cachedSources.empty() &&
                            !isScanCancelled()) {
                            for (const auto& source : cachedSources) {
                                reusableIdentities[source.relativePathKey] =
                                    RawWorkspaceCachedSourceIdentity {
                                        source.fileSizeBytes,
                                        source.modifiedTimeTicks,
                                        source.fingerprint,
                                        source.sourceIdentityAlgorithmVersion,
                                        source.captureTimestamp,
                                        source.captureMetadataChecked };
                            }
                            ProjectTasks().PostToMain([
                                this,
                                generation,
                                workspaceRoot = cachedLayout.workspaceRoot,
                                cachedSources = std::move(cachedSources),
                                cachedProjects = std::move(cachedProjects)
                            ]() mutable {
                                {
                                    std::lock_guard<std::mutex> lock(
                                        m_RawWorkspaceScanMutex);
                                    if (generation != m_RawWorkspaceScanGeneration ||
                                        !m_RawWorkspace.sources.empty()) {
                                        return;
                                    }
                                    m_RawWorkspace.workspaceRoot =
                                        std::move(workspaceRoot);
                                    m_RawWorkspace.sources =
                                        std::move(cachedSources);
                                    m_RawWorkspace.sourceSetProjects =
                                        std::move(cachedProjects);
                                    m_RawWorkspaceScanSnapshot.sourcesPublished = true;
                                }
                                NormalizeRawWorkspaceFilmstripOrganization();
                                InvalidateRawWorkspaceGalleryPresentation();
                                RequestRawWorkspaceSimilarityRebuild();
                                });
                        }
                    }

                    auto previewSession = std::make_shared<
                        RawWorkspaceProgressivePreviewSession>();
                    std::vector<Stack::RawWorkspace::SourceRecord>
                        progressiveBatch;
                    progressiveBatch.reserve(16);
                    auto flushProgressiveBatch = [
                        this,
                        generation,
                        &progressiveBatch,
                        previewSession
                    ](const Stack::RawWorkspace::ManagedLayout& layout) {
                        if (progressiveBatch.empty()) return true;
                        std::vector<Stack::RawWorkspace::SourceRecord> batch =
                            std::move(progressiveBatch);
                        progressiveBatch.clear();
                        progressiveBatch.reserve(16);
                        std::vector<Stack::RawWorkspace::SourceRecord>
                            quickSources;
                        quickSources.reserve(batch.size());
                        for (const auto& source : batch) {
                            if (source.thumbnail.status !=
                                Stack::RawWorkspace::ThumbnailStatus::Valid) {
                                quickSources.push_back(source);
                            }
                        }

                        if (!ProjectTasks().PostToMain([
                                this,
                                generation,
                                batch = std::move(batch)
                            ]() mutable {
                                {
                                    std::lock_guard<std::mutex> lock(
                                        m_RawWorkspaceScanMutex);
                                    if (generation !=
                                        m_RawWorkspaceScanGeneration) {
                                        return;
                                    }
                                    m_RawWorkspaceScanSnapshot.
                                        sourcesPublished = true;
                                }
                                for (auto& source : batch) {
                                    auto existing = std::find_if(
                                        m_RawWorkspace.sources.begin(),
                                        m_RawWorkspace.sources.end(),
                                        [&](const auto& candidate) {
                                            return candidate.relativePathKey ==
                                                source.relativePathKey;
                                        });
                                    if (existing ==
                                        m_RawWorkspace.sources.end()) {
                                        m_RawWorkspace.sources.push_back(
                                            std::move(source));
                                    } else if (existing->fingerprint ==
                                               source.fingerprint) {
                                        const auto project = existing->project;
                                        const auto memberships = existing->
                                            sourceSetProjectMemberships;
                                        *existing = std::move(source);
                                        existing->project = project;
                                        existing->sourceSetProjectMemberships =
                                            memberships;
                                    }
                                }
                                NormalizeRawWorkspaceFilmstripOrganization();
                                InvalidateRawWorkspaceGalleryPresentation();
                                RequestRawWorkspaceSimilarityRebuild();
                            })) {
                            return false;
                        }

                        if (quickSources.empty()) return true;
                        {
                            std::lock_guard<std::mutex> lock(
                                previewSession->mutex);
                            ++previewSession->activeBatches;
                        }
                        const bool submittedPreview =
                            ProjectTasks().Submit(
                                "Building previews",
                                [
                                    this,
                                    generation,
                                    layout,
                                    quickSources = std::move(quickSources),
                                    previewSession
                                ]() mutable {
                                    try {
                                    std::vector<RawWorkspaceThumbnailUpdate>
                                        updates;
                                    updates.reserve(quickSources.size());
                                    const auto canceled = [this, generation]() {
                                        std::lock_guard<std::mutex> lock(
                                            m_RawWorkspaceScanMutex);
                                        return generation !=
                                            m_RawWorkspaceScanGeneration;
                                    };
                                    for (const auto& source : quickSources) {
                                        if (canceled()) break;
                                        auto result = Stack::RawWorkspace::
                                            GenerateFastNeutralThumbnail(
                                                layout,
                                                source,
                                                Stack::RawWorkspace::
                                                    kFastNeutralThumbnailMaxDimension,
                                                canceled);
                                        if (result.success) {
                                            updates.push_back({
                                                0,
                                                source.relativePathKey,
                                                std::move(result.thumbnail),
                                                true });
                                        }
                                    }
                                    if (!updates.empty()) {
                                        ProjectTasks().PostToMain([
                                            this,
                                            generation,
                                            updates = std::move(updates)
                                        ]() mutable {
                                            {
                                                std::lock_guard<std::mutex> lock(
                                                    m_RawWorkspaceScanMutex);
                                                if (generation !=
                                                    m_RawWorkspaceScanGeneration) {
                                                    return;
                                                }
                                            }
                                            bool changed = false;
                                            for (auto& update : updates) {
                                                auto found = std::find_if(
                                                    m_RawWorkspace.sources.begin(),
                                                    m_RawWorkspace.sources.end(),
                                                    [&](const auto& source) {
                                                        return source.
                                                            relativePathKey ==
                                                            update.sourceKey;
                                                    });
                                                if (found !=
                                                    m_RawWorkspace.sources.end()) {
                                                    found->transientThumbnail =
                                                        std::move(
                                                            update.thumbnail);
                                                    changed = true;
                                                }
                                            }
                                            if (changed) {
                                                InvalidateRawWorkspaceGalleryPresentation();
                                                RequestRawWorkspaceSimilarityRebuild();
                                            }
                                        });
                                    }
                                    } catch (...) {
                                        // The durable pass will retry this
                                        // source after scan intake closes.
                                    }
                                    {
                                        std::lock_guard<std::mutex> lock(
                                            previewSession->mutex);
                                        --previewSession->activeBatches;
                                    }
                                    previewSession->completed.notify_all();
                                });
                        if (!submittedPreview) {
                            {
                                std::lock_guard<std::mutex> lock(
                                    previewSession->mutex);
                                --previewSession->activeBatches;
                            }
                            previewSession->completed.notify_all();
                        }
                        return submittedPreview;
                    };

                    auto sourceReady = [
                        &updateProgress,
                        &progressiveBatch,
                        &flushProgressiveBatch
                    ](
                        const Stack::RawWorkspace::ManagedLayout& layout,
                        Stack::RawWorkspace::SourceRecord& source,
                        Stack::RawWorkspace::ScanProgress& progress) {
                        progress.stage = Stack::RawWorkspace::ScanProgress::
                            Stage::CheckingSavedPreviews;
                        Stack::RawWorkspace::ClassifyThumbnailMetadata(
                            layout,
                            source,
                            Stack::RawWorkspace::
                                kNeutralThumbnailMaxDimension);
                        ++progress.cachedPreviewsChecked;
                        updateProgress(progress);
                        progressiveBatch.push_back(source);
                        // A one-source batch is below both publication bounds
                        // and avoids holding a confirmed source behind the
                        // hash of the next large file.
                        flushProgressiveBatch(layout);
                    };

                    auto reuseSourceIdentity = [
                        &reusableIdentities
                    ](Stack::RawWorkspace::SourceRecord& source) {
                        const auto cached = reusableIdentities.find(
                            source.relativePathKey);
                        if (cached == reusableIdentities.end() ||
                            cached->second.fileSizeBytes !=
                                source.fileSizeBytes ||
                            cached->second.modifiedTimeTicks !=
                                source.modifiedTimeTicks ||
                            cached->second.fingerprint.empty() ||
                            cached->second.algorithmVersion !=
                                Stack::RawWorkspace::
                                    kSourceIdentityAlgorithmVersion) {
                            return false;
                        }
                        source.fingerprint = cached->second.fingerprint;
                        source.sourceIdentityAlgorithmVersion =
                            cached->second.algorithmVersion;
                        source.captureTimestamp = cached->second.captureTimestamp;
                        source.captureMetadataChecked =
                            cached->second.captureMetadataChecked;
                        return true;
                    };

                    Stack::RawWorkspace::ScanResult result =
                        Stack::RawWorkspace::ScanWorkspace(
                            workspaceRoot,
                            isRawPath,
                            updateProgress,
                            isScanCancelled,
                            sourceReady,
                            reuseSourceIdentity);

                    if (!flushProgressiveBatch(result.layout) &&
                        !isScanCancelled()) {
                        markScanFailed(
                            "Could not queue progressive RAW previews.");
                        return;
                    }
                    if (result.success) {
                        std::unique_lock<std::mutex> previewLock(
                            previewSession->mutex);
                        previewSession->completed.wait(
                            previewLock,
                            [&]() {
                                return previewSession->activeBatches == 0;
                            });
                    }

                    if (result.success) {
                        {
                            std::lock_guard<std::mutex> lock(
                                m_RawWorkspaceScanMutex);
                            if (generation == m_RawWorkspaceScanGeneration) {
                                m_RawWorkspaceScanSnapshot.state =
                                    Async::TaskState::Applying;
                                m_RawWorkspaceScanSnapshot.progress =
                                    result.progress;
                                m_RawWorkspaceScanSnapshot.progress.stage =
                                    Stack::RawWorkspace::ScanProgress::Stage::
                                        DiscoveringProjects;
                                m_RawWorkspaceScanSnapshot.statusText =
                                    "Discovering saved projects...";
                            }
                        }
                        if (!Stack::RawWorkspace::DiscoverProjects(
                                result.layout,
                                result.sources,
                                isScanCancelled)) {
                            if (!isScanCancelled()) {
                                markScanFailed(
                                    "Could not discover saved projects.");
                            }
                            return;
                        }
                        if (!Stack::RawWorkspace::DiscoverSourceSetProjects(
                                result.layout,
                                result.sources,
                                result.sourceSetProjects,
                                isScanCancelled)) {
                            if (!isScanCancelled()) {
                                markScanFailed(
                                    "Could not discover saved projects.");
                            }
                            return;
                        }
                    }

                    if (isScanCancelled()) {
                        return;
                    }

                    const bool published = ProjectTasks().PostToMain([
                        this,
                        generation,
                        selectedBeforeScan,
                        result = std::move(result)
                    ]() mutable {
                        try {
                            {
                                std::lock_guard<std::mutex> lock(
                                    m_RawWorkspaceScanMutex);
                                if (generation != m_RawWorkspaceScanGeneration) {
                                    return;
                                }
                                m_RawWorkspaceScanSnapshot.state =
                                    Async::TaskState::Applying;
                                m_RawWorkspaceScanSnapshot.progress =
                                    result.progress;
                                m_RawWorkspaceScanSnapshot.progress.stage =
                                    Stack::RawWorkspace::ScanProgress::Stage::
                                        ApplyingCatalog;
                                m_RawWorkspaceScanSnapshot.statusText =
                                    "Applying Workspace scan...";
                            }

                            if (!result.success) {
                                std::lock_guard<std::mutex> lock(
                                    m_RawWorkspaceScanMutex);
                                m_RawWorkspaceScanSnapshot.state =
                                    Async::TaskState::Failed;
                                m_RawWorkspaceScanSnapshot.errorMessage =
                                    result.errorMessage.empty()
                                    ? "Failed to scan Workspace."
                                    : result.errorMessage;
                                m_RawWorkspaceScanSnapshot.statusText =
                                    m_RawWorkspaceScanSnapshot.errorMessage;
                                return;
                            }

                            m_RawWorkspace.workspaceRoot =
                                result.layout.workspaceRoot;
                            std::unordered_map<
                                std::string,
                                Stack::RawWorkspace::ThumbnailInfo>
                                publishedThumbnails;
                            std::unordered_map<
                                std::string,
                                Stack::RawWorkspace::ThumbnailInfo>
                                publishedTransientThumbnails;
                            publishedThumbnails.reserve(
                                m_RawWorkspace.sources.size());
                            publishedTransientThumbnails.reserve(
                                m_RawWorkspace.sources.size());
                            for (const Stack::RawWorkspace::SourceRecord& source :
                                 m_RawWorkspace.sources) {
                                publishedThumbnails.emplace(
                                    source.relativePathKey,
                                    source.thumbnail);
                                publishedTransientThumbnails.emplace(
                                    source.relativePathKey,
                                    source.transientThumbnail);
                            }
                            for (Stack::RawWorkspace::SourceRecord& source :
                                 result.sources) {
                                const auto thumbnail =
                                    publishedThumbnails.find(
                                        source.relativePathKey);
                                if (thumbnail != publishedThumbnails.end()) {
                                    const Stack::RawWorkspace::SourceRecord* previous =
                                        FindRawWorkspaceSourceByKey(
                                            source.relativePathKey);
                                    const bool sameSource = previous != nullptr &&
                                        !previous->fingerprint.empty() &&
                                        previous->fingerprint == source.fingerprint;
                                    if (sameSource &&
                                        std::max(
                                            thumbnail->second.width,
                                            thumbnail->second.height) ==
                                            Stack::RawWorkspace::
                                                kNeutralThumbnailMaxDimension) {
                                        source.thumbnail = thumbnail->second;
                                    }
                                    const auto transientThumbnail =
                                        publishedTransientThumbnails.find(
                                            source.relativePathKey);
                                    if (sameSource && transientThumbnail !=
                                        publishedTransientThumbnails.end()) {
                                        source.transientThumbnail =
                                            transientThumbnail->second;
                                    }
                                    if (sameSource && previous != nullptr &&
                                        previous->project.status !=
                                            Stack::RawWorkspace::ProjectStatus::
                                                NoProject &&
                                        source.project.status ==
                                            Stack::RawWorkspace::ProjectStatus::
                                                NoProject) {
                                        source.project = previous->project;
                                    }
                                    if (sameSource && previous != nullptr &&
                                        !previous->sourceSetProjectMemberships.
                                            empty()) {
                                        source.sourceSetProjectMemberships =
                                            previous->
                                                sourceSetProjectMemberships;
                                    }
                                }
                            }
                            m_RawWorkspace.sources = std::move(result.sources);
                            m_RawWorkspace.sourceSetProjects =
                                std::move(result.sourceSetProjects);
                            NormalizeRawWorkspaceFilmstripOrganization();
                            {
                                std::lock_guard<std::mutex> lock(
                                    m_RawWorkspaceScanMutex);
                                m_RawWorkspaceScanSnapshot.sourcesPublished = true;
                            }
                            const auto projectStillExists = [&](const std::filesystem::path& path) {
                                return !path.empty() && std::any_of(
                                    m_RawWorkspace.sourceSetProjects.begin(),
                                    m_RawWorkspace.sourceSetProjects.end(),
                                    [&](const Stack::RawWorkspace::SourceSetProjectCatalogEntry& project) {
                                        return project.absolutePath.lexically_normal() ==
                                            path.lexically_normal();
                                    });
                            };
                            m_RawWorkspaceLabSelectedProjectPaths.erase(
                                std::remove_if(
                                    m_RawWorkspaceLabSelectedProjectPaths.begin(),
                                    m_RawWorkspaceLabSelectedProjectPaths.end(),
                                    [&](const std::filesystem::path& path) {
                                        return !projectStillExists(path);
                                    }),
                                m_RawWorkspaceLabSelectedProjectPaths.end());
                            if (!projectStillExists(
                                    m_RawWorkspaceLabFocusedProjectPath)) {
                                m_RawWorkspaceLabFocusedProjectPath.clear();
                                m_RawWorkspaceLabFocusedProjectName.clear();
                            }
                            if (!projectStillExists(
                                    m_RawWorkspaceLabProjectSelectionAnchor)) {
                                m_RawWorkspaceLabProjectSelectionAnchor.clear();
                            }
                            m_RawWorkspace.selectedSourceKeys.erase(
                                std::remove_if(
                                    m_RawWorkspace.selectedSourceKeys.begin(),
                                    m_RawWorkspace.selectedSourceKeys.end(),
                                    [&](const std::string& selectedKey) {
                                    return std::none_of(
                                        m_RawWorkspace.sources.begin(),
                                        m_RawWorkspace.sources.end(),
                                        [&](const Stack::RawWorkspace::SourceRecord& source) {
                                            return source.relativePathKey == selectedKey;
                                        });
                                    }),
                                m_RawWorkspace.selectedSourceKeys.end());
                            Stack::RawWorkspace::AddRecentWorkspace(
                                m_RawWorkspace,
                                m_RawWorkspace.workspaceRoot);

                            const std::string latestSelection =
                                m_RawWorkspace.selectedSourceKey;
                            bool restoredSelection =
                                !latestSelection.empty() &&
                                Stack::RawWorkspace::SelectSourceByKey(
                                    m_RawWorkspace,
                                    latestSelection);
                            if (!restoredSelection &&
                                selectedBeforeScan != latestSelection &&
                                !selectedBeforeScan.empty()) {
                                restoredSelection =
                                    Stack::RawWorkspace::SelectSourceByKey(
                                        m_RawWorkspace,
                                        selectedBeforeScan);
                            }
                            if (!restoredSelection &&
                                IsRawWorkspaceProjectActive() &&
                                !m_Project->rawSourceKey.empty() &&
                                FindRawWorkspaceSourceByKey(
                                    m_Project->rawSourceKey) != nullptr) {
                                // The active project may now be backed only by
                                // its immutable managed asset. Folder rescans
                                // must not clear the selection that owns the
                                // live editing surface.
                                m_RawWorkspace.selectedSourceKey =
                                    m_Project->rawSourceKey;
                                restoredSelection = true;
                            }
                            if (!restoredSelection) {
                                m_RawWorkspace.selectedSourceKey.clear();
                            }
                            m_RawWorkspacePreviewStageFailureSourceKey.clear();
                            InvalidateRawWorkspaceGalleryPresentation();
                            RequestRawWorkspaceSimilarityRebuild();

                            PersistRawWorkspaceCatalog();
                            SaveRawWorkspaceAppState();
                            RequestRawWorkspaceThumbnailGeneration();

                            std::lock_guard<std::mutex> lock(
                                m_RawWorkspaceScanMutex);
                            m_RawWorkspaceScanSnapshot.state =
                                Async::TaskState::Idle;
                            m_RawWorkspaceScanSnapshot.completedSuccessfully =
                                true;
                            m_RawWorkspaceScanSnapshot.progress =
                                result.progress;
                            m_RawWorkspaceScanSnapshot.statusText =
                                result.progress.statusText.empty()
                                ? "Workspace ready."
                                : result.progress.statusText;
                        } catch (...) {
                            std::lock_guard<std::mutex> lock(
                                m_RawWorkspaceScanMutex);
                            if (generation == m_RawWorkspaceScanGeneration) {
                                m_RawWorkspaceScanSnapshot.state =
                                    Async::TaskState::Failed;
                                SetRawWorkspaceStatusNoThrow(
                                    m_RawWorkspaceScanSnapshot.errorMessage,
                                    "Failed to apply the Workspace scan.");
                                SetRawWorkspaceStatusNoThrow(
                                    m_RawWorkspaceScanSnapshot.statusText,
                                    "Failed to apply the Workspace scan.");
                            }
                        }
                    });
                    if (!published) {
                        markScanFailed(
                            "Could not publish the completed Workspace scan.");
                    }
                } catch (...) {
                    markScanFailed("Failed to scan Workspace.");
                }
            });
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
        if (generation == m_RawWorkspaceScanGeneration) {
            m_RawWorkspaceScanSnapshot.state = Async::TaskState::Failed;
            SetRawWorkspaceStatusNoThrow(
                m_RawWorkspaceScanSnapshot.errorMessage,
                "Could not queue the Workspace scan.");
            SetRawWorkspaceStatusNoThrow(
                m_RawWorkspaceScanSnapshot.statusText,
                "Could not queue the Workspace scan.");
        }
    }
}

void EditorModule::RequestRawWorkspaceThumbnailGeneration() {
    try {
        RequestRawWorkspaceThumbnailGenerationImpl();
    } catch (...) {
        for (Stack::RawWorkspace::SourceRecord& source : m_RawWorkspace.sources) {
            if (source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Queued ||
                source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Generating) {
                source.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Failed;
            }
        }
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        const std::uint64_t generation = ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailSnapshot.generation = generation;
        m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Failed;
        m_RawWorkspaceThumbnailScheduler.Clear();
        m_RawWorkspaceThumbnailWorkerActive = false;
        SetRawWorkspaceStatusNoThrow(
            m_RawWorkspaceThumbnailSnapshot.statusText,
            "Could not prepare RAW thumbnail generation.");
    }
}

void EditorModule::FailRawWorkspaceThumbnailGeneration(
    std::uint64_t generation,
    const std::vector<std::pair<std::size_t, std::string>>& pendingSources,
    const char* message) {
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        if (generation != m_RawWorkspaceThumbnailGeneration) {
            return;
        }
    }
    for (const auto& [sourceIndex, sourceKey] : pendingSources) {
        (void)sourceIndex;
        auto found = std::find_if(
            m_RawWorkspace.sources.begin(),
            m_RawWorkspace.sources.end(),
            [&](const auto& source) {
                return source.relativePathKey == sourceKey;
            });
        if (found == m_RawWorkspace.sources.end()) continue;
        Stack::RawWorkspace::SourceRecord& source = *found;
        if (
            (source.thumbnail.status !=
                 Stack::RawWorkspace::ThumbnailStatus::Queued &&
             source.thumbnail.status !=
                 Stack::RawWorkspace::ThumbnailStatus::Generating)) {
            continue;
        }
        source.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Failed;
        SetRawWorkspaceStatusNoThrow(source.thumbnail.errorMessage, message);
    }

    Stack::RawWorkspace::ThumbnailProgress failedProgress;
    try {
        failedProgress =
            Stack::RawWorkspace::BuildThumbnailProgress(m_RawWorkspace.sources);
    } catch (...) {
    }
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        if (generation != m_RawWorkspaceThumbnailGeneration) {
            return;
        }
        m_RawWorkspaceThumbnailScheduler.Clear();
        m_RawWorkspaceThumbnailWorkerActive = false;
        m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Failed;
        m_RawWorkspaceThumbnailSnapshot.progress = std::move(failedProgress);
        SetRawWorkspaceStatusNoThrow(
            m_RawWorkspaceThumbnailSnapshot.statusText,
            message);
    }
    InvalidateRawWorkspaceGalleryPresentation();
    try {
        PersistRawWorkspaceCatalog();
    } catch (...) {
    }
}

void EditorModule::RequestRawWorkspaceThumbnailGenerationImpl() {
    if (m_RawWorkspace.workspaceRoot.empty() || m_RawWorkspace.sources.empty()) {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailSnapshot.progress = Stack::RawWorkspace::BuildThumbnailProgress(m_RawWorkspace.sources);
        m_RawWorkspaceThumbnailSnapshot.statusText = m_RawWorkspaceThumbnailSnapshot.progress.statusText;
        m_RawWorkspaceThumbnailScheduler.Clear();
        m_RawWorkspaceThumbnailWorkerActive = false;
        InvalidateRawWorkspaceGalleryPresentation();
        return;
    }

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    std::vector<RawWorkspaceThumbnailWorkItem> pending;
    std::vector<RawWorkspaceThumbnailWorkItem> quickPending;
    pending.reserve(m_RawWorkspace.sources.size());
    for (std::size_t sourceIndex = 0; sourceIndex < m_RawWorkspace.sources.size(); ++sourceIndex) {
        Stack::RawWorkspace::SourceRecord& source = m_RawWorkspace.sources[sourceIndex];
        if (source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Missing ||
            source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Stale ||
            source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Queued ||
            source.thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Generating) {
            source.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Queued;
            source.thumbnail.errorMessage.clear();
            RawWorkspaceThumbnailWorkItem workItem{
                sourceIndex,
                source.absolutePath,
                source.relativePath,
                source.relativePathKey,
                source.fileName,
                source.stem,
                source.extension,
                source.parentFolderKey,
                source.fileSizeBytes,
                source.modifiedTimeTicks,
                source.fingerprint,
            };
            pending.push_back(workItem);
            if (source.transientThumbnail.status !=
                Stack::RawWorkspace::ThumbnailStatus::Ready) {
                source.transientThumbnail = {};
                quickPending.push_back(std::move(workItem));
            }
        }
    }
    if (!pending.empty()) {
        InvalidateRawWorkspaceGalleryPresentation();
    }

    Stack::RawWorkspace::ThumbnailProgress initialProgress =
        Stack::RawWorkspace::BuildThumbnailProgress(m_RawWorkspace.sources);
    initialProgress.intakeComplete = true;

    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        generation = ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailSnapshot.generation = generation;
        m_RawWorkspaceThumbnailSnapshot.progress = initialProgress;
        m_RawWorkspaceThumbnailSnapshot.statusText = initialProgress.statusText;
        m_RawWorkspaceThumbnailSnapshot.state = pending.empty()
            ? Async::TaskState::Idle
            : Async::TaskState::Queued;
    }

    if (pending.empty()) {
        Stack::RawWorkspace::RemoveTransientThumbnailCache(layout);
        for (Stack::RawWorkspace::SourceRecord& source : m_RawWorkspace.sources) {
            source.transientThumbnail = {};
        }
        PersistRawWorkspaceCatalog();
        return;
    }

    std::vector<std::pair<std::size_t, std::string>> pendingSources;
    pendingSources.reserve(pending.size());
    for (const RawWorkspaceThumbnailWorkItem& item : pending) {
        pendingSources.emplace_back(item.sourceIndex, item.sourceKey);
    }

    PersistRawWorkspaceCatalog();

    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        m_RawWorkspaceThumbnailScheduler.Reset(std::move(quickPending));
        m_RawWorkspaceThumbnailWorkerActive = true;
    }

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Building previews", 
            [
                this,
                generation,
                layout,
                initialProgress,
                pendingSources,
                finalWorkItems = std::move(pending)
            ]() mutable {
        auto markWorkerFailed = [this, generation](const char* message) noexcept {
            try {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                if (generation != m_RawWorkspaceThumbnailGeneration) {
                    return;
                }
                m_RawWorkspaceThumbnailScheduler.Clear();
                m_RawWorkspaceThumbnailWorkerActive = false;
                m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Failed;
                SetRawWorkspaceStatusNoThrow(
                    m_RawWorkspaceThumbnailSnapshot.statusText,
                    message);
            } catch (...) {
            }
        };
        auto publishFailure = [this, generation, &pendingSources](
                                  const char* message) {
            return ProjectTasks().PostToMain([
                this,
                generation,
                pendingSources,
                message
            ]() {
                FailRawWorkspaceThumbnailGeneration(
                    generation,
                    pendingSources,
                    message);
            });
        };
        try {
        const int pendingTotal = static_cast<int>(pendingSources.size());
        Stack::RawWorkspace::ThumbnailProgress progress = initialProgress;
        progress.completed = 0;
        progress.failed = 0;
        progress.queued = pendingTotal;
        std::vector<RawWorkspaceThumbnailUpdate> thumbnailUpdates;
        thumbnailUpdates.reserve(kRawWorkspaceThumbnailApplyBatchSize);

        auto updateProgress = [&](const std::string& item, bool transientPreview) {
            progress.currentItem = item;
            progress.statusText = item.empty()
                ? (transientPreview
                    ? "Preparing quick RAW previews..."
                    : "Rendering high-quality RAW thumbnails...")
                : (transientPreview
                    ? "Preparing quick preview for " + item
                    : "Rendering high-quality thumbnail for " + item);
            std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
            if (generation != m_RawWorkspaceThumbnailGeneration) {
                return;
            }
            m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Running;
            m_RawWorkspaceThumbnailSnapshot.progress = progress;
            m_RawWorkspaceThumbnailSnapshot.statusText = progress.statusText;
        };
        auto flushThumbnailUpdates = [&]() {
            if (thumbnailUpdates.empty()) {
                return true;
            }
            std::vector<RawWorkspaceThumbnailUpdate> updates = std::move(thumbnailUpdates);
            thumbnailUpdates.clear();
            thumbnailUpdates.reserve(kRawWorkspaceThumbnailApplyBatchSize);

            return ProjectTasks().PostToMain([
                this,
                generation,
                updates = std::move(updates)
            ]() mutable {
                {
                    std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                    if (generation != m_RawWorkspaceThumbnailGeneration) {
                        return;
                    }
                }

                bool anyThumbnailUpdated = false;
                bool anyPersistentThumbnailUpdated = false;
                for (RawWorkspaceThumbnailUpdate& update : updates) {
                    auto source = std::find_if(
                        m_RawWorkspace.sources.begin(),
                        m_RawWorkspace.sources.end(),
                        [&](const auto& candidate) {
                            return candidate.relativePathKey ==
                                update.sourceKey;
                        });
                    if (source != m_RawWorkspace.sources.end()) {
                        if (update.transientPreview) {
                            source->transientThumbnail =
                                std::move(update.thumbnail);
                        } else {
                            source->thumbnail =
                                std::move(update.thumbnail);
                            anyPersistentThumbnailUpdated = true;
                        }
                        anyThumbnailUpdated = true;
                    }
                }
                if (anyThumbnailUpdated) {
                    InvalidateRawWorkspaceGalleryPresentation();
                    RequestRawWorkspaceSimilarityRebuild();
                }
                if (anyPersistentThumbnailUpdated) {
                    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);
                    m_RawWorkspaceCatalogPersistDirty = true;
                    m_RawWorkspaceCatalogPersistDirtyTime = RawWorkspaceClockSeconds();
                }
                });
        };

        auto makeSourceRecord = [](
                                    const RawWorkspaceThumbnailWorkItem& item) {
            Stack::RawWorkspace::SourceRecord source;
            source.absolutePath = item.absolutePath;
            source.relativePath = item.relativePath;
            source.relativePathKey = item.sourceKey;
            source.fileName = item.fileName;
            source.stem = item.stem;
            source.extension = item.extension;
            source.parentFolderKey = item.parentFolderKey;
            source.fileSizeBytes = item.fileSizeBytes;
            source.modifiedTimeTicks = item.modifiedTimeTicks;
            source.fingerprint = item.fingerprint;
            return source;
        };

        bool generationStillCurrent = true;
        auto runThumbnailPass = [&](bool transientPreview) {
            while (true) {
                RawWorkspaceThumbnailWorkItem item;
                {
                    std::lock_guard<std::mutex> lock(
                        m_RawWorkspaceThumbnailMutex);
                    if (generation != m_RawWorkspaceThumbnailGeneration) {
                        generationStillCurrent = false;
                        break;
                    }
                    if (m_RawWorkspaceThumbnailScheduler.Empty() ||
                        !m_RawWorkspaceThumbnailScheduler.TryTakeNext(item)) {
                        break;
                    }
                }

                const Stack::RawWorkspace::SourceRecord source =
                    makeSourceRecord(item);
                updateProgress(source.fileName, transientPreview);
                const auto canceled = [this, generation]() {
                    std::lock_guard<std::mutex> lock(
                        m_RawWorkspaceThumbnailMutex);
                    return generation != m_RawWorkspaceThumbnailGeneration;
                };
                Stack::RawWorkspace::ThumbnailGenerationResult result =
                    transientPreview
                    ? Stack::RawWorkspace::GenerateFastNeutralThumbnail(
                          layout,
                          source,
                          Stack::RawWorkspace::kFastNeutralThumbnailMaxDimension,
                          canceled)
                    : Stack::RawWorkspace::GenerateNeutralThumbnail(
                          layout,
                          source,
                          Stack::RawWorkspace::kNeutralThumbnailMaxDimension,
                          canceled);
                {
                    std::lock_guard<std::mutex> lock(
                        m_RawWorkspaceThumbnailMutex);
                    if (generation != m_RawWorkspaceThumbnailGeneration) {
                        generationStillCurrent = false;
                        break;
                    }
                }
                if (!transientPreview) {
                    if (result.success) {
                        ++progress.completed;
                    } else {
                        ++progress.failed;
                    }
                    progress.queued = std::max(
                        0,
                        pendingTotal - progress.completed - progress.failed);
                }

                thumbnailUpdates.push_back(RawWorkspaceThumbnailUpdate{
                    item.sourceIndex,
                    source.relativePathKey,
                    std::move(result.thumbnail),
                    transientPreview,
                });
                if (thumbnailUpdates.size() >=
                    kRawWorkspaceThumbnailApplyBatchSize) {
                    if (!flushThumbnailUpdates()) {
                        return false;
                    }
                }
            }
            return !generationStillCurrent || flushThumbnailUpdates();
        };

        // Complete the lightweight pass for the folder before beginning the
        // more expensive area-sampled render. This keeps the filmstrip useful
        // while the durable high-quality cache is filled in the background.
        if (!runThumbnailPass(true)) {
            markWorkerFailed("Could not publish quick RAW previews.");
            return;
        }
        if (!generationStillCurrent) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
            if (generation != m_RawWorkspaceThumbnailGeneration) {
                return;
            }
            m_RawWorkspaceThumbnailScheduler.Reset(
                std::move(finalWorkItems));
        }
        if (!runThumbnailPass(false)) {
            markWorkerFailed(
                "Could not publish high-quality RAW thumbnails.");
            return;
        }
        if (!generationStillCurrent) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
            if (generation == m_RawWorkspaceThumbnailGeneration) {
                m_RawWorkspaceThumbnailScheduler.Clear();
                m_RawWorkspaceThumbnailWorkerActive = false;
            }
        }

        progress.currentItem.clear();
        const bool highQualityPassComplete = progress.failed == 0;
        progress.statusText = highQualityPassComplete
            ? "High-quality RAW thumbnails are ready."
            : "RAW thumbnails finished with errors; quick previews were kept.";
        if (!ProjectTasks().PostToMain(
                [
                    this,
                    generation,
                    progress,
                    highQualityPassComplete,
                    layout
                ]() mutable {
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
                if (generation != m_RawWorkspaceThumbnailGeneration) {
                    return;
                }
                m_RawWorkspaceThumbnailSnapshot.state = Async::TaskState::Idle;
                m_RawWorkspaceThumbnailSnapshot.progress = progress;
                m_RawWorkspaceThumbnailSnapshot.statusText = progress.statusText;
            }
            if (highQualityPassComplete &&
                Stack::RawWorkspace::RemoveTransientThumbnailCache(layout)) {
                for (Stack::RawWorkspace::SourceRecord& source :
                     m_RawWorkspace.sources) {
                    source.transientThumbnail = {};
                }
            }
            PersistRawWorkspaceCatalog();
        })) {
            markWorkerFailed(
                "Could not publish RAW thumbnail generation completion.");
        }
        } catch (...) {
            markWorkerFailed("RAW thumbnail generation failed.");
            try {
                publishFailure("RAW thumbnail generation failed.");
            } catch (...) {
            }
        }
            });
    } catch (...) {
        submitted = false;
    }
    if (!submitted) {
        {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
            m_RawWorkspaceThumbnailScheduler.Clear();
            m_RawWorkspaceThumbnailWorkerActive = false;
        }
        FailRawWorkspaceThumbnailGeneration(
            generation,
            pendingSources,
            "Could not queue RAW thumbnail generation.");
    }
}

void EditorModule::ClearRawWorkspace() {
    PreserveActiveRawProjectSourceForLibraryNavigation();
    ResetRawWorkspaceSimilarityStacks();
    m_RawWorkspace.workspaceRoot.clear();
    if (m_RawWorkspaceFolderChangedHandler) m_RawWorkspaceFolderChangedHandler({});
    m_RawWorkspace.sources.clear();
    m_RawWorkspace.selectedSourceKey.clear();
    m_RawWorkspace.selectedSourceKeys.clear();
    m_RawWorkspace.sourceSetProjects.clear();
    m_RawWorkspaceLabSelectedProjectPaths.clear();
    m_RawWorkspaceLabFocusedProjectPath.clear();
    m_RawWorkspaceLabFocusedProjectName.clear();
    m_RawWorkspaceLabProjectSelectionAnchor.clear();
    InvalidateRawWorkspaceGalleryPresentation();
    m_RawWorkspacePreviewStageFailureSourceKey.clear();
    m_RawWorkspaceRecipePreviewCache.clear();
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    m_RawWorkspacePreviewStageQueuedFrame = -1;
    if (!IsRawWorkspaceProjectActive()) {
        ResetRawWorkspaceAutoBaseState();
    }
    ClearRawWorkspaceThumbnailTextures();
    ResetRawWorkspaceCatalogPersistState();
    SaveRawWorkspaceAppState();

    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
    ++m_RawWorkspaceScanGeneration;
    m_RawWorkspaceScanSnapshot = {};
    {
        std::lock_guard<std::mutex> thumbnailLock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
    }
}

void EditorModule::SelectRawWorkspaceSource(const std::string& sourceKey) {
    const bool explicitReplacement =
        sourceKey == m_RawWorkspaceExplicitReplacementSourceKey;
    if (!m_RawWorkspaceRootTabActive ||
        (m_RawWorkspaceLockedByEditorProject && !explicitReplacement)) {
        return;
    }
    // Gallery browsing is selection-only while an editing project is active.
    // It must never unload, replace, or implicitly switch the project session.
    if (IsRawWorkspaceProjectActive() &&
        sourceKey != m_Project->rawSourceKey &&
        !explicitReplacement) {
        if (Stack::RawWorkspace::SelectSourceByKey(m_RawWorkspace, sourceKey)) {
            InvalidateRawWorkspaceGalleryPresentation();
            PersistRawWorkspaceCatalog();
            SaveRawWorkspaceAppState();
        }
        return;
    }
    if (IsDeferredLoadedProjectApplyActive()) {
        m_RawWorkspaceExplicitReplacementSourceKey.clear();
        PostNotification(
            UiNotificationSeverity::Warning,
            "Finish loading the current RAW selection before choosing another image.",
            "raw-workspace-selection-load-busy");
        return;
    }
    const bool selectionChanged = sourceKey != m_RawWorkspace.selectedSourceKey;
    if (selectionChanged && !FlushActiveRawWorkspaceProjectIfDirty()) {
        return;
    }
    if (selectionChanged) {
        ClearRawWorkspaceLivePreviewState();
        ResetRawWorkspaceAutoBaseState();
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        m_RawWorkspaceProjectLoadGeneration.fetch_add(1, std::memory_order_relaxed);
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
        m_RawWorkspaceProjectLoadSourceKey.clear();
        m_RawWorkspaceProjectLoadStatusText.clear();
        m_PendingRawWorkspaceDeferredProjectFinalize = false;
        m_PendingRawWorkspaceDeferredProjectFinalizeSourceKey.clear();
        m_PendingRawWorkspaceOpenGraphAfterProjectLoad = false;
        m_PendingRawWorkspaceOpenGraphSourceKey.clear();
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
    } else if (m_RawWorkspacePreviewStageFailureSourceKey == sourceKey) {
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        if (m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
            m_RawWorkspaceProjectLoadSourceKey == sourceKey) {
            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
            m_RawWorkspaceProjectLoadSourceKey.clear();
            m_RawWorkspaceProjectLoadStatusText.clear();
        }
    } else if (m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == sourceKey) {
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
        m_RawWorkspaceProjectLoadSourceKey.clear();
        m_RawWorkspaceProjectLoadStatusText.clear();
    }
    if (!Stack::RawWorkspace::SelectSourceByKey(m_RawWorkspace, sourceKey)) {
        if (explicitReplacement) {
            m_RawWorkspaceExplicitReplacementSourceKey.clear();
        }
        return;
    }
    if (selectionChanged) {
        InvalidateRawWorkspaceGalleryPresentation();
    }
    QueueSelectedRawWorkspaceSourcePreviewStaging();
    m_RawWorkspaceExplicitReplacementSourceKey.clear();
    PersistRawWorkspaceCatalog();
    SaveRawWorkspaceAppState();
}

void EditorModule::QueueSelectedRawWorkspaceSourcePreviewStaging() {
    if (!m_RawWorkspaceRootTabActive || m_RawWorkspaceLockedByEditorProject) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }
    if (m_RawWorkspace.workspaceRoot.empty() || m_RawWorkspace.selectedSourceKey.empty()) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    if (IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == source->relativePathKey) {
        if (m_RawWorkspacePreviewStageSourceKey == source->relativePathKey) {
            m_RawWorkspacePreviewStageQueued = false;
            m_RawWorkspacePreviewStageSourceKey.clear();
            m_RawWorkspacePreviewStageQueuedFrame = -1;
        }
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        return;
    }

    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == source->relativePathKey) {
        return;
    }
    if (m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == source->relativePathKey) {
        return;
    }
    if (m_RawWorkspacePreviewStageFailureSourceKey == source->relativePathKey) {
        return;
    }

    const bool createNewProject =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::ProjectRoot &&
        m_RawWorkspaceGalleryContentMode ==
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    const bool storedProject = !createNewProject &&
        (source->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded);
    m_RawWorkspacePreviewStageQueued = true;
    m_RawWorkspacePreviewStageSourceKey = source->relativePathKey;
    m_RawWorkspacePreviewStageQueuedFrame =
        ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    m_RawWorkspaceProjectLoadStatusText =
        storedProject ? "Loading RAW project..." : "Preparing RAW preview...";
}

void EditorModule::TickRawWorkspacePreviewStaging() {
    if (!m_RawWorkspaceRootTabActive || m_RawWorkspaceLockedByEditorProject) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }
    if (!m_RawWorkspacePreviewStageQueued) {
        return;
    }
    if (ImGui::GetCurrentContext() &&
        m_RawWorkspacePreviewStageQueuedFrame >= 0 &&
        ImGui::GetFrameCount() <= m_RawWorkspacePreviewStageQueuedFrame) {
        return;
    }

    const std::string queuedSourceKey = m_RawWorkspacePreviewStageSourceKey;
    if (queuedSourceKey.empty() ||
        queuedSourceKey != m_RawWorkspace.selectedSourceKey) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(queuedSourceKey);
    if (!source) {
        m_RawWorkspacePreviewStageQueued = false;
        m_RawWorkspacePreviewStageSourceKey.clear();
        m_RawWorkspacePreviewStageQueuedFrame = -1;
        return;
    }

    const bool createNewProject =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::ProjectRoot &&
        m_RawWorkspaceGalleryContentMode ==
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    const bool storedProject = !createNewProject &&
        (source->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded);
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    m_RawWorkspacePreviewStageQueuedFrame = -1;

    const bool staged = EnsureSelectedRawWorkspaceSourcePreviewStaged();
    if (!storedProject && !staged) {
        m_RawWorkspaceProjectLoadSourceKey = queuedSourceKey;
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Failed to prepare the RAW preview.";
    }
}

bool EditorModule::EnsureSelectedRawWorkspaceSourcePreviewStaged() {
    if (!m_RawWorkspaceRootTabActive ||
        m_RawWorkspaceLockedByEditorProject ||
        m_RawWorkspace.workspaceRoot.empty() ||
        m_RawWorkspace.selectedSourceKey.empty()) {
        return false;
    }

    Stack::RawWorkspace::SourceRecord* source =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    if (!source) {
        return false;
    }

    const bool createNewProject =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
            RawGalleryNavigationMode::ProjectRoot &&
        m_RawWorkspaceGalleryContentMode ==
            Stack::RawWorkspace::GalleryContentMode::Gallery;
    if (!createNewProject &&
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == source->relativePathKey) {
        m_RawWorkspacePreviewStageFailureSourceKey.clear();
        return true;
    }
    if (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
        m_RawWorkspaceProjectLoadSourceKey == source->relativePathKey) {
        return true;
    }

    if (m_RawWorkspacePreviewStageFailureSourceKey == source->relativePathKey) {
        return false;
    }

    const bool discardCurrentForReplacement =
        source->relativePathKey == m_RawWorkspaceReplacementSkipSaveSourceKey;
    if (!discardCurrentForReplacement &&
        !FlushActiveRawWorkspaceProjectIfDirty()) {
        m_RawWorkspacePreviewStageFailureSourceKey = source->relativePathKey;
        return false;
    }

    ClearRawWorkspaceLivePreviewState();
    const bool staged = StageRawWorkspaceProjectForSourcePreview(
        *source,
        createNewProject);
    if (discardCurrentForReplacement) {
        m_RawWorkspaceReplacementSkipSaveSourceKey.clear();
    }
    if (!staged) {
        m_RawWorkspacePreviewStageFailureSourceKey = source->relativePathKey;
        return false;
    }

    m_RawWorkspacePreviewStageFailureSourceKey.clear();
    return true;
}

void EditorModule::PersistRawWorkspaceCatalog() {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        return;
    }

    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistDirty = true;
    m_RawWorkspaceCatalogPersistDirtyTime = RawWorkspaceClockSeconds();
    StartRawWorkspaceCatalogPersistIfNeeded();
}

void EditorModule::StartRawWorkspaceCatalogPersistIfNeeded() {
    if (m_RawWorkspace.workspaceRoot.empty()) {
        m_RawWorkspaceCatalogPersistDirty = false;
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
        m_RawWorkspaceCatalogPersistStatusText.clear();
        return;
    }
    if (!m_RawWorkspaceCatalogPersistDirty || m_RawWorkspaceCatalogPersistInFlight) {
        return;
    }
    const RawWorkspaceScanSnapshot scan = GetRawWorkspaceScanSnapshot();
    if (Async::IsBusy(scan.state) || !scan.completedSuccessfully) {
        // Progressive sources are usable before the scan is authoritative,
        // but an incomplete catalog must never replace the last good one.
        return;
    }
    if (!RawWorkspaceDebounceElapsed(m_RawWorkspaceCatalogPersistDirtyTime)) {
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Queued;
        m_RawWorkspaceCatalogPersistStatusText = "Saving RAW Workspace catalog...";
        return;
    }

    const Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(m_RawWorkspace.workspaceRoot);
    std::vector<Stack::RawWorkspace::CatalogSourceRecord> sources =
        Stack::RawWorkspace::BuildCatalogSourceRecords(m_RawWorkspace.sources);
    const std::string selectedSourceKey = m_PinnedRawWorkspaceSource.has_value()
        ? m_RawWorkspaceSelectedSourceBeforePinnedProject
        : m_RawWorkspace.selectedSourceKey;
    const std::uint64_t generation =
        m_RawWorkspaceCatalogPersistGeneration.load(std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistDirty = false;
    m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
    m_RawWorkspaceCatalogPersistInFlight = true;
    m_RawWorkspaceCatalogPersistInFlightGeneration = generation;
    m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Queued;
    m_RawWorkspaceCatalogPersistStatusText = "Saving RAW Workspace catalog...";

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit("Saving workspace", [
            this,
            generation,
            layout,
            sources = std::move(sources),
            selectedSourceKey
        ]() mutable {
            std::string error;
            bool success = false;
            try {
                success = Stack::RawWorkspace::WriteCatalogSkeletonIfCurrent(
                    layout,
                    sources,
                    selectedSourceKey,
                    [this, generation]() {
                        return generation ==
                            m_RawWorkspaceCatalogPersistGeneration.load(
                                std::memory_order_relaxed);
                    },
                    &error);
            } catch (...) {
                SetRawWorkspaceStatusNoThrow(
                    error,
                    "RAW Workspace catalog could not be saved.");
            }

            ProjectTasks().PostToMain([
                this,
                generation,
                success,
                error = std::move(error)
            ]() mutable {
                if (generation != m_RawWorkspaceCatalogPersistInFlightGeneration) {
                    return;
                }

                m_RawWorkspaceCatalogPersistInFlight = false;
                m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
                if (generation != m_RawWorkspaceCatalogPersistGeneration.load(std::memory_order_relaxed)) {
                    m_RawWorkspaceCatalogPersistTaskState = m_RawWorkspaceCatalogPersistDirty
                        ? Async::TaskState::Queued
                        : Async::TaskState::Idle;
                    m_RawWorkspaceCatalogPersistStatusText = m_RawWorkspaceCatalogPersistDirty
                        ? "Saving RAW Workspace catalog..."
                        : std::string();
                    StartRawWorkspaceCatalogPersistIfNeeded();
                    return;
                }
                if (success) {
                    m_RawWorkspaceCatalogPersistTaskState = m_RawWorkspaceCatalogPersistDirty
                        ? Async::TaskState::Queued
                        : Async::TaskState::Idle;
                    m_RawWorkspaceCatalogPersistStatusText = m_RawWorkspaceCatalogPersistDirty
                        ? "Saving RAW Workspace catalog..."
                        : std::string();
                } else {
                    m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Failed;
                    m_RawWorkspaceCatalogPersistStatusText = error.empty()
                        ? "RAW Workspace catalog could not be saved."
                        : error;
                    PostNotification(
                        UiNotificationSeverity::Error,
                        m_RawWorkspaceCatalogPersistStatusText,
                        "raw-workspace-catalog-save");
                }
                StartRawWorkspaceCatalogPersistIfNeeded();
            });
        });
    } catch (...) {
        submitted = false;
    }
    if (!submitted &&
        generation == m_RawWorkspaceCatalogPersistInFlightGeneration) {
        m_RawWorkspaceCatalogPersistInFlight = false;
        m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
        m_RawWorkspaceCatalogPersistDirty = true;
        m_RawWorkspaceCatalogPersistDirtyTime = RawWorkspaceClockSeconds();
        m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Failed;
        m_RawWorkspaceCatalogPersistStatusText =
            "RAW Workspace catalog save could not be queued; it will retry.";
    }
}

void EditorModule::ResetRawWorkspaceCatalogPersistState() {
    m_RawWorkspaceCatalogPersistGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceCatalogPersistTaskState = Async::TaskState::Idle;
    m_RawWorkspaceCatalogPersistDirty = false;
    m_RawWorkspaceCatalogPersistInFlight = false;
    m_RawWorkspaceCatalogPersistInFlightGeneration = 0;
    m_RawWorkspaceCatalogPersistDirtyTime = -1.0;
    m_RawWorkspaceCatalogPersistStatusText.clear();
}

void EditorModule::TickRawWorkspacePersistence() {
    StartRawWorkspaceAppStatePersistIfNeeded();
    StartRawWorkspaceCatalogPersistIfNeeded();
}
