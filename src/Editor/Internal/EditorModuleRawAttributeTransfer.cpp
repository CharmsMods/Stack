#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Notifications/AsyncActivity.h"
#include "Editor/RawAttributeTargetProject.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectAttributeTransfer.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Project/ProjectPath.h"

#include <algorithm>
#include <imgui_internal.h>
#include <unordered_set>

namespace {

std::filesystem::path NormalizedAbsolutePath(
    const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

bool SameProjectPath(
    const std::filesystem::path& a,
    const std::filesystem::path& b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return NormalizedAbsolutePath(a) == NormalizedAbsolutePath(b);
}

std::vector<std::string> OrderedSelectedAttributeKeys(
    const std::unordered_set<std::string>& selected) {
    std::vector<std::string> keys;
    for (const Stack::RawRecipe::RawEditAttributeDescriptor& descriptor :
         Stack::RawRecipe::RawEditAttributeDescriptors()) {
        if (selected.find(descriptor.key) != selected.end()) {
            keys.emplace_back(descriptor.key);
        }
    }
    return keys;
}

int FindManagedGraphViewLayerIndex(
    const Stack::Project::RawProjectSnapshot& snapshot,
    const std::string& sourceSetId,
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers) {
    const Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (sourceSet == nullptr) {
        return -1;
    }
    const std::string nodeUuid = sourceSet->settings.value(
        "graphViewTransformNodeUuid", std::string());
    if (nodeUuid.empty()) {
        return -1;
    }
    const auto node = std::find_if(
        graph.GetNodes().begin(),
        graph.GetNodes().end(),
        [&](const EditorNodeGraph::Node& candidate) {
            return candidate.instanceUuid == nodeUuid;
        });
    if (node == graph.GetNodes().end() ||
        node->kind != EditorNodeGraph::NodeKind::Layer ||
        node->layerType != LayerType::ViewTransform ||
        node->layerIndex < 0 ||
        node->layerIndex >= static_cast<int>(layers.size()) ||
        !layers[static_cast<std::size_t>(node->layerIndex)]) {
        return -1;
    }
    return node->layerIndex;
}

} // namespace

bool EditorModule::CopyRawEditAttributesFromGallerySource(
    const Stack::RawWorkspace::SourceRecord& source,
    std::string* errorMessage) {
    Stack::RawRecipe::RawEditAttributeBundle bundle;
    const auto* activeSource = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    if (!IsMultiFrameRawProjectActive() &&
        IsRawWorkspaceProjectActive() &&
        activeSource && Stack::Project::SameProjectPath(
            source.absolutePath, activeSource->absolutePath)) {
        const std::string placement =
            Stack::RawRecipe::IsViewTransformEnabled(
                m_Project->rawRecipe)
                ? "internal"
                : "graph";
        bundle = Stack::RawRecipe::CaptureRawEditAttributeBundle(
            m_Project->rawRecipe,
            source.fileName,
            "single-raw",
            placement);
    } else if (
        (source.project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
         source.project.status == Stack::RawWorkspace::ProjectStatus::Embedded) &&
        !source.project.absolutePath.empty()) {
        std::string captureError;
        if (!Stack::Project::CaptureRawEditAttributesFromProject(
                source.project.absolutePath,
                {},
                bundle,
                &captureError)) {
            if (errorMessage) *errorMessage = std::move(captureError);
            return false;
        }
    } else {
        if (errorMessage) {
            *errorMessage = source.sourceSetProjectMemberships.empty()
                ? "This image has no saved edit to copy yet."
                : "This image has no individual edit. Copy from its multi-frame project card instead.";
        }
        return false;
    }

    m_RawEditAttributeClipboard = std::move(bundle);
    m_RawEditAttributeClipboardObservedText.clear();
    if (ImGui::GetCurrentContext()) {
        const std::string clipboardText =
            Stack::RawRecipe::SerializeRawEditAttributeBundle(
                *m_RawEditAttributeClipboard).dump();
        ImGui::SetClipboardText(clipboardText.c_str());
    }
    PostNotification(
        UiNotificationSeverity::Success,
        "RAW edits copied from " +
            m_RawEditAttributeClipboard->sourceLabel + ".",
        "raw-edit-attributes-copy");
    if (errorMessage) errorMessage->clear();
    return true;
}

bool EditorModule::CopyRawEditAttributesFromProjectPath(
    const std::filesystem::path& projectPath,
    const std::string& sourceSetId,
    std::string* errorMessage) {
    Stack::RawRecipe::RawEditAttributeBundle bundle;
    if (SameProjectPath(projectPath, m_Project->storePath) &&
        m_Project->snapshot) {
        Stack::Project::RawProjectEditRecipeBinding binding;
        std::string resolveError;
        if (!Stack::Project::ResolveRawProjectEditRecipe(
                *m_Project->snapshot,
                sourceSetId,
                binding,
                &resolveError)) {
            if (errorMessage) *errorMessage = std::move(resolveError);
            return false;
        }
        if (binding.multiFrameResult &&
            binding.viewTransformPlacement == "graph") {
            const int layerIndex = FindManagedGraphViewLayerIndex(
                *m_Project->snapshot,
                binding.sourceSetId,
                m_Project->graph,
                m_Project->layers);
            if (layerIndex >= 0) {
                binding.recipe.viewTransform.layerJson =
                    m_Project->layers[static_cast<std::size_t>(layerIndex)]->Serialize();
                binding.recipe.viewTransform.layerJson["enabled"] = false;
            }
        }
        bundle = Stack::RawRecipe::CaptureRawEditAttributeBundle(
            binding.recipe,
            binding.sourceLabel,
            binding.sourceKind,
            binding.viewTransformPlacement);
    } else {
        std::string captureError;
        if (!Stack::Project::CaptureRawEditAttributesFromProject(
                projectPath,
                sourceSetId,
                bundle,
                &captureError)) {
            if (errorMessage) *errorMessage = std::move(captureError);
            return false;
        }
    }
    m_RawEditAttributeClipboard = std::move(bundle);
    m_RawEditAttributeClipboardObservedText.clear();
    if (ImGui::GetCurrentContext()) {
        const std::string clipboardText =
            Stack::RawRecipe::SerializeRawEditAttributeBundle(
                *m_RawEditAttributeClipboard).dump();
        ImGui::SetClipboardText(clipboardText.c_str());
    }
    PostNotification(
        UiNotificationSeverity::Success,
        "RAW edits copied from " +
            m_RawEditAttributeClipboard->sourceLabel + ".",
        "raw-edit-attributes-copy");
    if (errorMessage) errorMessage->clear();
    return true;
}

void EditorModule::RefreshRawEditAttributeClipboardFromSystem() {
    // Targets remain owned by the paste dialog through its pending, open,
    // working and result states, until Cancel, Close or automatic reveal.
    // A Gallery redraw must not replace the edit captured for that session.
    if (m_RawEditAttributePastePopupRequested ||
        !m_RawEditAttributePasteTargets.empty() ||
        Async::IsBusy(m_RawEditAttributePasteTaskState)) {
        return;
    }
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    const char* text = ImGui::GetClipboardText();
    const char* observedText = text ? text : "";
    if (m_RawEditAttributeClipboardObservedText == observedText) {
        return;
    }
    m_RawEditAttributeClipboardObservedText = observedText;
    if (m_RawEditAttributeClipboardObservedText.empty()) {
        return;
    }
    const nlohmann::json value = nlohmann::json::parse(
        m_RawEditAttributeClipboardObservedText, nullptr, false);
    if (value.is_discarded()) {
        return;
    }
    Stack::RawRecipe::RawEditAttributeBundle bundle;
    if (Stack::RawRecipe::DeserializeRawEditAttributeBundle(
            value, bundle, nullptr)) {
        m_RawEditAttributeClipboard = std::move(bundle);
    }
}

void EditorModule::RequestPasteRawEditAttributesForGallerySelection(
    bool includeSelectedProjects) {
    std::vector<Stack::RawWorkspace::SourceRecord> sources;
    sources.reserve(m_RawWorkspace.selectedSourceKeys.size());
    for (const auto& sourceKey : m_RawWorkspace.selectedSourceKeys) {
        const auto source = std::find_if(
            m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
            [&](const auto& candidate) {
                return candidate.relativePathKey == sourceKey;
            });
        if (source != m_RawWorkspace.sources.end()) {
            sources.push_back(*source);
        }
    }
    std::vector<Stack::RawWorkspace::SourceSetProjectCatalogEntry> projects;
    if (includeSelectedProjects) {
        projects.reserve(m_RawWorkspaceLabSelectedProjectPaths.size());
        for (const auto& selectedPath : m_RawWorkspaceLabSelectedProjectPaths) {
            const auto project = std::find_if(
                m_RawWorkspace.sourceSetProjects.begin(),
                m_RawWorkspace.sourceSetProjects.end(),
                [&](const auto& candidate) {
                    return SameProjectPath(candidate.absolutePath, selectedPath);
                });
            if (project != m_RawWorkspace.sourceSetProjects.end()) {
                projects.push_back(*project);
            }
        }
    }
    RequestPasteRawEditAttributesForGalleryTargets(
        std::move(sources), std::move(projects));
}

void EditorModule::RequestPasteRawEditAttributesForGalleryTargets(
    std::vector<Stack::RawWorkspace::SourceRecord> sources,
    std::vector<Stack::RawWorkspace::SourceSetProjectCatalogEntry> projects) {
    RefreshRawEditAttributeClipboardFromSystem();
    if (!m_RawEditAttributeClipboard) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Copy edits from a saved RAW image or multi-frame project first.",
            "raw-edit-attributes-empty-clipboard");
        return;
    }
    std::vector<RawEditAttributePasteTarget> targets;
    targets.reserve(sources.size() + projects.size());
    std::unordered_set<std::string> seen;
    for (const auto& source : sources) {
        if (!seen.insert(source.relativePathKey).second) {
            continue;
        }
        RawEditAttributePasteTarget target;
        target.source = source;
        target.displayName = source.fileName.empty()
            ? source.relativePathKey
            : source.fileName;
        const bool existing =
            (source.project.status ==
                 Stack::RawWorkspace::ProjectStatus::Existing ||
             source.project.status ==
                 Stack::RawWorkspace::ProjectStatus::Embedded) &&
            !source.project.absolutePath.empty();
        target.createProject = !existing;
        target.projectPath = existing
            ? source.project.absolutePath
            : std::filesystem::path();
        if (existing && std::any_of(
                targets.begin(),
                targets.end(),
                [&](const RawEditAttributePasteTarget& candidate) {
                    return SameProjectPath(
                        candidate.projectPath, target.projectPath);
                })) {
            continue;
        }
        targets.push_back(std::move(target));
    }
    for (const auto& project : projects) {
        const bool alreadyAdded = std::any_of(
            targets.begin(),
            targets.end(),
            [&](const RawEditAttributePasteTarget& candidate) {
                return SameProjectPath(
                    candidate.projectPath, project.absolutePath);
            });
        if (alreadyAdded) {
            continue;
        }
        RawEditAttributePasteTarget target;
        target.projectPath = project.absolutePath;
        target.displayName = project.projectName.empty()
            ? project.absolutePath.filename().string()
            : project.projectName;
        if (!project.referenceSourceKey.empty()) {
            const auto source = std::find_if(
                m_RawWorkspace.sources.begin(), m_RawWorkspace.sources.end(),
                [&](const auto& candidate) {
                    return candidate.relativePathKey == project.referenceSourceKey;
                });
            if (source != m_RawWorkspace.sources.end()) {
                target.source = *source;
            }
        }
        targets.push_back(std::move(target));
    }
    if (targets.empty()) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Select one or more RAW images before pasting attributes.",
            "raw-edit-attributes-no-targets");
        return;
    }
    // Automatic bracketing may defer this request. Keep the chosen targets
    // and copied edit; never resolve a later Gallery selection on completion.
    auto preparePaste = [
        this,
        root = m_RawWorkspace.workspaceRoot,
        document = GetProjectDocumentId(),
        targets = std::move(targets),
        bundle = *m_RawEditAttributeClipboard
    ]() mutable {
        const bool sameRoot = root == m_RawWorkspace.workspaceRoot ||
            Stack::Project::SameProjectPath(root, m_RawWorkspace.workspaceRoot);
        if (!sameRoot || GetProjectDocumentId() != document) {
            PostNotification(
                UiNotificationSeverity::Warning,
                "The project or photo folder changed. Select targets again before pasting edits.",
                "raw-edit-attributes-paste-folder-changed");
            return;
        }
        m_RawEditAttributeClipboard = std::move(bundle);
        m_RawEditAttributeClipboardObservedText.clear();
        m_RawEditAttributePasteTargets = std::move(targets);
        m_RawEditAttributePasteSelection.clear();
        for (std::string key :
             Stack::RawRecipe::DefaultRawEditAttributeSelection()) {
            m_RawEditAttributePasteSelection.insert(std::move(key));
        }
        m_RawEditAttributePasteStatusText.clear();
        m_RawEditAttributePasteAutoOpenProjectPath.clear();
        m_RawEditAttributePastePopupRequested = true;
    };
    if (!RequestAutoBracketForeground("paste these edits", preparePaste)) {
        preparePaste();
    }
}

void EditorModule::RequestPasteRawEditAttributesForProject(
    const std::filesystem::path& projectPath,
    const std::string& sourceSetId,
    std::string displayName) {
    if(RequestAutoBracketForeground("paste these edits",[this,projectPath,sourceSetId,displayName] {
        RequestPasteRawEditAttributesForProject(projectPath,sourceSetId,displayName);
    }))return;
    RefreshRawEditAttributeClipboardFromSystem();
    if (!m_RawEditAttributeClipboard) {
        PostNotification(
            UiNotificationSeverity::Warning,
            "Copy edits from a saved RAW image or multi-frame project first.",
            "raw-edit-attributes-empty-clipboard");
        return;
    }
    RawEditAttributePasteTarget target;
    target.projectPath = projectPath;
    target.sourceSetId = sourceSetId;
    target.displayName = displayName.empty()
        ? projectPath.filename().string()
        : std::move(displayName);
    target.createProject = false;
    for (const auto& project : m_RawWorkspace.sourceSetProjects) {
        if (!SameProjectPath(project.absolutePath, projectPath)) {
            continue;
        }
        if (!project.referenceSourceKey.empty()) {
            if (const Stack::RawWorkspace::SourceRecord* source =
                    FindRawWorkspaceSourceByKey(project.referenceSourceKey)) {
                target.source = *source;
            }
        }
        break;
    }
    m_RawEditAttributePasteTargets.assign(1u, std::move(target));
    m_RawEditAttributePasteSelection.clear();
    for (std::string key :
         Stack::RawRecipe::DefaultRawEditAttributeSelection()) {
        m_RawEditAttributePasteSelection.insert(std::move(key));
    }
    m_RawEditAttributePasteStatusText.clear();
    m_RawEditAttributePasteAutoOpenProjectPath.clear();
    m_RawEditAttributePastePopupRequested = true;
}

bool EditorModule::StartRawEditAttributePaste(std::string* errorMessage) {
    if (!m_RawEditAttributeClipboard) {
        if (errorMessage) *errorMessage = "The RAW attribute clipboard is empty.";
        return false;
    }
    if (m_RawEditAttributePasteTargets.empty()) {
        if (errorMessage) *errorMessage = "No RAW targets are selected.";
        return false;
    }
    const std::vector<std::string> selectedKeys =
        OrderedSelectedAttributeKeys(m_RawEditAttributePasteSelection);
    if (selectedKeys.empty()) {
        if (errorMessage) *errorMessage = "Select at least one attribute to paste.";
        return false;
    }
    if (Async::IsBusy(m_RawEditAttributePasteTaskState)) {
        if (errorMessage) *errorMessage = "A RAW attribute paste is already running.";
        return false;
    }

    struct WorkItem {
        RawEditAttributePasteTarget target;
        Stack::Project::ProjectStoreHandle sourceStore;
        std::shared_ptr<Stack::Project::RawProjectSnapshot> sourceSnapshot;
    };
    std::vector<WorkItem> workItems;
    workItems.reserve(m_RawEditAttributePasteTargets.size());
    for (const RawEditAttributePasteTarget& target :
         m_RawEditAttributePasteTargets) {
        WorkItem item;
        item.target = target;
        if (!target.createProject &&
            SameProjectPath(
                target.projectPath, m_Project->storePath) &&
            m_Project->snapshot &&
            m_Project->store) {
            item.sourceStore = m_Project->store;
            item.sourceSnapshot =
                std::make_shared<Stack::Project::RawProjectSnapshot>(
                    *m_Project->snapshot);
            item.sourceSnapshot->pipelineData = SerializePipeline();
            if (!IsMultiFrameRawProjectActive()) {
                item.sourceSnapshot->rawWorkspaceData["rawRecipe"] =
                    Stack::RawRecipe::SerializeRecipe(
                        m_Project->rawRecipe);
            }
        }
        workItems.push_back(std::move(item));
    }

    auto progress = std::make_shared<RawEditAttributePasteProgressState>();
    progress->total = static_cast<std::uint64_t>(workItems.size());
    m_RawEditAttributePasteProgress = progress;

    const std::uint64_t generation =
        m_RawEditAttributePasteGeneration.fetch_add(
            1, std::memory_order_relaxed) + 1;
    const Stack::RawRecipe::RawEditAttributeBundle bundle =
        *m_RawEditAttributeClipboard;
    const std::filesystem::path workspaceRoot =
        m_RawWorkspace.workspaceRoot;
    m_RawEditAttributePasteTaskState = Async::TaskState::Queued;
    m_RawEditAttributePasteStatusText = "Preparing RAW attribute paste...";
    const auto pasteNotifier = GetNotifier();
    const auto pasteActivity = pasteNotifier.BeginActivity("Applying edits");
    const auto pasteCompletion = Stack::Notifications::RetainAsyncActivity(pasteNotifier, pasteActivity);
    const bool submitted = ProjectTasks().Submit(
        Stack::Notifications::ForAsyncActivity(pasteNotifier, pasteActivity, "Applying edits"),[
        this,
        generation,
        progress,
        pasteNotifier,
        pasteActivity,
        pasteCompletion,
        workItems = std::move(workItems),
        bundle,
        selectedKeys,
        workspaceRoot
    ]() mutable {
        for (const WorkItem& item : workItems) {
            if (progress->cancelRequested.load(std::memory_order_relaxed)) {
                break;
            }
            const RawEditAttributePasteTarget& target = item.target;
            {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->currentItem = target.displayName;
            }
            Stack::EditorRawAttributes::CreatedTargetProjectResult created;
            if (target.createProject) {
                created = Stack::EditorRawAttributes::CreateProjectAndPasteAttributes(
                    workspaceRoot,
                    target.source,
                    bundle,
                    selectedKeys);
            } else {
                created = Stack::EditorRawAttributes::DuplicateProjectAndPasteAttributes(
                    target.projectPath,
                    target.sourceSetId,
                    bundle,
                    selectedKeys,
                    item.sourceStore,
                    item.sourceSnapshot.get());
            }
            const Stack::Project::RawProjectAttributeTransferResult& transfer =
                created.transfer;
            if (transfer.success) {
                if (transfer.skipped) {
                    progress->skipped.fetch_add(1, std::memory_order_relaxed);
                } else {
                    progress->succeeded.fetch_add(1, std::memory_order_relaxed);
                }
                if (!transfer.warnings.empty()) {
                    std::lock_guard<std::mutex> lock(progress->mutex);
                    for (const std::string& warning : transfer.warnings) {
                        progress->warnings.push_back(
                            target.displayName + ": " + warning);
                    }
                }
                if (!created.projectPath.empty()) {
                    std::lock_guard<std::mutex> lock(progress->mutex);
                    progress->createdProjectPaths.push_back(
                        created.projectPath);
                }
            } else {
                progress->failed.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->errors.push_back(
                    target.displayName + ": " +
                    (transfer.errorMessage.empty()
                        ? "Paste failed."
                        : transfer.errorMessage));
            }
            progress->completed.fetch_add(1, std::memory_order_relaxed);
            pasteNotifier.UpdateActivity(pasteActivity, "Applying edits to " + target.displayName,
                Stack::Notifications::Progress{
                    static_cast<double>(progress->completed.load(std::memory_order_relaxed)),
                    static_cast<double>(progress->total), ""});
        }
        ProjectTasks().PostToMain([
            this, generation, progress, pasteNotifier, pasteActivity, pasteCompletion
        ]() {
            if (generation != m_RawEditAttributePasteGeneration.load(
                    std::memory_order_relaxed)) {
                return;
            }
            const std::uint64_t succeeded =
                progress->succeeded.load(std::memory_order_relaxed);
            const std::uint64_t failed =
                progress->failed.load(std::memory_order_relaxed);
            const std::uint64_t skipped =
                progress->skipped.load(std::memory_order_relaxed);
            const bool canceled =
                progress->cancelRequested.load(std::memory_order_relaxed);
            m_RawEditAttributePasteTaskState = failed > 0u
                ? Async::TaskState::Failed
                : Async::TaskState::Ready;
            m_RawEditAttributePasteStatusText = canceled
                ? "Paste stopped after " + std::to_string(succeeded) +
                    " successful project" + (succeeded == 1u ? "." : "s.")
                : "Pasted attributes into " + std::to_string(succeeded) +
                    " project" + (succeeded == 1u ? "." : "s.");
            if (failed > 0u) {
                m_RawEditAttributePasteStatusText += " " +
                    std::to_string(failed) + " failed.";
            }
            if (skipped > 0u) {
                m_RawEditAttributePasteStatusText += " " +
                    std::to_string(skipped) + " target" +
                    (skipped == 1u ? " had" : "s had") +
                    " no compatible selected attributes.";
            }
            RescanRawWorkspace();
            LibraryManager::Get().RequestRefreshLibraryAsync();
            {
                std::lock_guard<std::mutex> lock(progress->mutex);
                if (!canceled && failed == 0u && succeeded == 1u &&
                    progress->createdProjectPaths.size() == 1u) {
                    m_RawEditAttributePasteAutoOpenProjectPath =
                        progress->createdProjectPaths.front();
                }
            }
            std::string details;
            {
                std::lock_guard<std::mutex> lock(progress->mutex);
                for (const auto& error : progress->errors) details += error + "\n";
                for (const auto& warning : progress->warnings) details += warning + "\n";
            }
            const auto outcome = canceled && succeeded == 0u
                ? Stack::Notifications::Outcome::Cancelled
                : (canceled || failed > 0u || skipped > 0u || !details.empty())
                    ? (succeeded > 0u ? Stack::Notifications::Outcome::Partial
                        : Stack::Notifications::Outcome::Failure)
                    : Stack::Notifications::Outcome::Success;
            pasteNotifier.FinishActivity(pasteActivity, outcome,
                m_RawEditAttributePasteStatusText, std::move(details));
        });
    });
    if (!submitted) {
        m_RawEditAttributePasteTaskState = Async::TaskState::Failed;
        m_RawEditAttributePasteStatusText =
            "Stack could not start the RAW attribute paste worker.";
        pasteNotifier.FailActivity(pasteActivity, m_RawEditAttributePasteStatusText);
        if (errorMessage) *errorMessage = m_RawEditAttributePasteStatusText;
        return false;
    }
    m_RawEditAttributePasteTaskState = Async::TaskState::Running;
    if (errorMessage) errorMessage->clear();
    return true;
}

void EditorModule::RenderRawEditAttributePasteDialog() {
    if (m_RawEditAttributePastePopupRequested) {
        ImGui::OpenPopup("Copy Settings##RawAttributePaste");
        m_RawEditAttributePastePopupRequested = false;
    }
    ImGui::SetNextWindowSize(
        ImVec2(840.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(760.0f, 400.0f), ImVec2(1600.0f, 1200.0f));
    if (!ImGui::BeginPopupModal(
            "Copy Settings##RawAttributePaste",
            nullptr,
            ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    const bool busy = Async::IsBusy(m_RawEditAttributePasteTaskState);
    const bool finished =
        m_RawEditAttributePasteTaskState == Async::TaskState::Ready ||
        m_RawEditAttributePasteTaskState == Async::TaskState::Failed;
    if (finished && !m_RawEditAttributePasteAutoOpenProjectPath.empty()) {
        const std::filesystem::path resultProject =
            std::move(m_RawEditAttributePasteAutoOpenProjectPath);
        ImGui::CloseCurrentPopup();
        m_RawEditAttributePasteTargets.clear();
        m_RawEditAttributePasteProgress.reset();
        m_RawEditAttributePasteTaskState = Async::TaskState::Idle;
        m_RawEditAttributePasteStatusText.clear();
        ImGui::EndPopup();
        if (RequestOpenRawWorkspaceProject(resultProject)) {
            RequestOpenRawLabTab();
        }
        return;
    }

    const std::string sourceLabel = m_RawEditAttributeClipboard
        ? m_RawEditAttributeClipboard->sourceLabel
        : std::string("No copied edit");
    const std::size_t targetCount = m_RawEditAttributePasteTargets.size();

    // Header bar with source and target info
    ImGui::TextUnformatted("Copy RAW Settings");
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("From: %s", sourceLabel.c_str());

    const std::string targetSummary = std::to_string(targetCount) +
        " Target Image" + (targetCount == 1u ? "" : "s");
    const float targetSummaryWidth = ImGui::CalcTextSize(targetSummary.c_str()).x;
    ImGui::SameLine(
        std::max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - targetSummaryWidth));
    ImGui::TextDisabled("%s", targetSummary.c_str());
    if (ImGui::IsItemHovered() && targetCount > 0u) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Target Destinations:");
        constexpr std::size_t kMaxTooltipTargets = 16u;
        const std::size_t showCount = std::min(targetCount, kMaxTooltipTargets);
        for (std::size_t i = 0; i < showCount; ++i) {
            const auto& target = m_RawEditAttributePasteTargets[i];
            ImGui::BulletText(
                "%s  (%s)",
                target.displayName.c_str(),
                target.createProject
                    ? "new project"
                    : "independent copy");
        }
        if (targetCount > showCount) {
            ImGui::TextDisabled(
                "+%llu more targets...",
                static_cast<unsigned long long>(targetCount - showCount));
        }
        ImGui::EndTooltip();
    }

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // Group descriptors by groupKey
    struct AttributeGroup {
        std::string key;
        std::string label;
        std::vector<Stack::RawRecipe::RawEditAttributeDescriptor> items;
    };
    std::vector<AttributeGroup> allGroups;
    for (const auto& desc : Stack::RawRecipe::RawEditAttributeDescriptors()) {
        auto it = std::find_if(
            allGroups.begin(), allGroups.end(),
            [&](const AttributeGroup& g) { return g.key == desc.groupKey; });
        if (it == allGroups.end()) {
            allGroups.push_back({desc.groupKey, desc.groupLabel, {desc}});
        } else {
            it->items.push_back(desc);
        }
    }

    // Distribute groups across 4 balanced columns
    std::vector<const AttributeGroup*> col0Groups;
    std::vector<const AttributeGroup*> col1Groups;
    std::vector<const AttributeGroup*> col2Groups;
    std::vector<const AttributeGroup*> col3Groups;

    for (const auto& group : allGroups) {
        if (group.key == "light" || group.key == "technical" || group.key == "denoise") {
            col0Groups.push_back(&group);
        } else if (group.key == "tone" || group.key == "geometry") {
            col1Groups.push_back(&group);
        } else if (group.key == "zones") {
            col2Groups.push_back(&group);
        } else {
            col3Groups.push_back(&group);
        }
    }

    const float footerHeight = 44.0f;
    ImGui::BeginChild(
        "RawAttributePasteBody",
        ImVec2(0.0f, -footerHeight),
        false,
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 2.0f));
    Stack::UiActivity::BeginDisabledForWork(busy, finished);

    auto renderGroup = [&](const AttributeGroup& group) {
        bool anySelected = false;
        bool allSelected = true;
        for (const auto& item : group.items) {
            const bool selected =
                m_RawEditAttributePasteSelection.find(item.key) !=
                m_RawEditAttributePasteSelection.end();
            anySelected = anySelected || selected;
            allSelected = allSelected && selected;
        }

        bool groupChecked = allSelected;
        if (anySelected && !allSelected) {
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        }
        ImGui::PushID(group.key.c_str());
        if (ImGui::Checkbox("##GroupCheck", &groupChecked)) {
            for (const auto& item : group.items) {
                if (groupChecked) {
                    m_RawEditAttributePasteSelection.insert(item.key);
                } else {
                    m_RawEditAttributePasteSelection.erase(item.key);
                }
            }
        }
        if (anySelected && !allSelected) {
            ImGui::PopItemFlag();
        }
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(group.label.c_str());

        ImGui::Indent(18.0f);
        for (const auto& item : group.items) {
            bool selected =
                m_RawEditAttributePasteSelection.find(item.key) !=
                m_RawEditAttributePasteSelection.end();
            ImGui::PushID(item.key);
            if (ImGui::Checkbox(item.label, &selected)) {
                if (selected) {
                    m_RawEditAttributePasteSelection.insert(item.key);
                } else {
                    m_RawEditAttributePasteSelection.erase(item.key);
                }
            }
            if (ImGui::IsItemHovered() && item.description && item.description[0] != '\0') {
                ImGui::SetTooltip("%s", item.description);
            }
            if (item.spatiallySpecific) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("*");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Spatially specific setting. Review on each target image.");
                }
            }
            ImGui::PopID();
        }
        ImGui::Unindent(18.0f);
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    };

    if (ImGui::BeginTable(
            "RawAttributeColumns",
            4,
            ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Col0", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Col3", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        for (const auto* group : col0Groups) {
            renderGroup(*group);
        }

        ImGui::TableSetColumnIndex(1);
        for (const auto* group : col1Groups) {
            renderGroup(*group);
        }

        ImGui::TableSetColumnIndex(2);
        for (const auto* group : col2Groups) {
            renderGroup(*group);
        }

        ImGui::TableSetColumnIndex(3);
        for (const auto* group : col3Groups) {
            renderGroup(*group);
        }

        ImGui::EndTable();
    }

    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);

    if (m_RawEditAttributePasteProgress) {
        const std::uint64_t completed =
            m_RawEditAttributePasteProgress->completed.load(
                std::memory_order_relaxed);
        const std::uint64_t total =
            m_RawEditAttributePasteProgress->total;
        const float fraction = total > 0u
            ? static_cast<float>(completed) / static_cast<float>(total)
            : 0.0f;
        ImGui::Spacing();
        ImGui::ProgressBar(
            std::clamp(fraction, 0.0f, 1.0f),
            ImVec2(-1.0f, 0.0f));
        std::string currentItem;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        {
            std::lock_guard<std::mutex> lock(
                m_RawEditAttributePasteProgress->mutex);
            currentItem = m_RawEditAttributePasteProgress->currentItem;
            errors = m_RawEditAttributePasteProgress->errors;
            warnings = m_RawEditAttributePasteProgress->warnings;
        }
        if (busy && !currentItem.empty()) {
            ImGui::TextDisabled(
                "Saving %s (%llu of %llu)",
                currentItem.c_str(),
                static_cast<unsigned long long>(completed),
                static_cast<unsigned long long>(total));
        }
        for (const std::string& error : errors) {
            ImGui::TextWrapped("%s", error.c_str());
        }
        for (const std::string& warning : warnings) {
            ImGui::TextDisabled("%s", warning.c_str());
        }
    }
    if (!m_RawEditAttributePasteStatusText.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_RawEditAttributePasteStatusText.c_str());
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // Bottom Left: Check All / Check None
    Stack::UiActivity::BeginDisabledForWork(busy, finished);
    if (ImGui::Button("Check All")) {
        m_RawEditAttributePasteSelection.clear();
        for (std::string key : Stack::RawRecipe::AllRawEditAttributeKeys()) {
            m_RawEditAttributePasteSelection.insert(std::move(key));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Check None")) {
        m_RawEditAttributePasteSelection.clear();
    }
    ImGui::EndDisabled();

    // Bottom Right: Cancel / Copy action buttons
    const float copyButtonWidth = 96.0f;
    const float cancelButtonWidth = 76.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float rightClusterWidth = copyButtonWidth + cancelButtonWidth + spacing;

    ImGui::SameLine(
        std::max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - rightClusterWidth));

    if (busy) {
        if (ImGui::Button("Stop After Current", ImVec2(rightClusterWidth, 0.0f))) {
            m_RawEditAttributePasteProgress->cancelRequested.store(
                true, std::memory_order_relaxed);
        }
    } else if (finished) {
        if (ImGui::Button("Close", ImVec2(rightClusterWidth, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_RawEditAttributePasteTargets.clear();
            m_RawEditAttributePasteProgress.reset();
            m_RawEditAttributePasteTaskState = Async::TaskState::Idle;
            m_RawEditAttributePasteStatusText.clear();
        }
    } else {
        if (ImGui::Button("Cancel", ImVec2(cancelButtonWidth, 0.0f))) {
            ImGui::CloseCurrentPopup();
            m_RawEditAttributePasteTargets.clear();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_RawEditAttributePasteSelection.empty());
        if (ImGui::Button("Copy", ImVec2(copyButtonWidth, 0.0f))) {
            std::string startError;
            if (!StartRawEditAttributePaste(&startError)) {
                m_RawEditAttributePasteStatusText = std::move(startError);
            }
        }
        ImGui::EndDisabled();
    }

    ImGui::EndPopup();
}
