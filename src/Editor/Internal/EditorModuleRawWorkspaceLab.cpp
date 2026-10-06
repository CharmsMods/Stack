#include "Editor/AutoBracket/AutoBracketEditingScope.h"
#include "Editor/Bracketing/BracketingSession.h"
#include "App/WorkspacePresentation.h"
#include "App/WorkspaceInputScope.h"
#include "Utils/UiBusyState.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/Internal/RawLab/RawLabToolPresentation.h"
#include "Editor/Internal/RawLab/RawLabModeSwitcher.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Project/RawLayerSourceTransactions.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;

namespace {

constexpr float kRawLabColumnHorizontalInset = 4.0f;
constexpr float kRawLabColumnVerticalInset = 4.0f;

float SmoothRawLabDrawerProgress(float progress) {
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

ImVec4 MixRawLabDrawerColor(
    const ImVec4& collapsed,
    const ImVec4& expanded,
    float progress) {
    const float amount = std::clamp(progress, 0.0f, 1.0f);
    return ImVec4(
        collapsed.x + (expanded.x - collapsed.x) * amount,
        collapsed.y + (expanded.y - collapsed.y) * amount,
        collapsed.z + (expanded.z - collapsed.z) * amount,
        collapsed.w + (expanded.w - collapsed.w) * amount);
}

int NormalizeRawLabRotationDegrees(int rotationDegrees) {
    int normalized = rotationDegrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}
} // namespace

bool EditorModule::BeginRawWorkspaceEditContext(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    RawWorkspaceEditContext& context) const {
    context = RawWorkspaceEditContext{};
    context.source = selectedSource;
    if (IsMultiFrameRawProjectActive() && m_Project->snapshot) {
        Stack::Project::RawProjectEditRecipeBinding binding;
        if (!Stack::Project::ResolveRawProjectEditRecipe(
                *m_Project->snapshot,
                m_Project->snapshot->activeSourceSetId,
                binding,
                &context.error)) {
            return false;
        }
        context.recipe = std::move(binding.recipe);
        // A merged source can change while its authored RAW recipe stays the
        // same. Key stage caches and retained detail to the actual pixels.
        context.recipe.source.sourcePath = GetActiveRawWorkspacePreviewIdentity();
        context.recipe.source.relativePathKey = context.recipe.source.sourcePath;
        context.recipe.source.fingerprint =
            std::to_string(GetActiveRawWorkspacePreviewSourceHash());
        context.resolvedMode =
            Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
        context.multiFrameSourceSetId = std::move(binding.sourceSetId);
        context.multiFrameResult = true;
        const Stack::Project::ProjectLifecyclePhase phase =
            m_Project->lifecycle.Phase();
        context.canEdit =
            phase != Stack::Project::ProjectLifecyclePhase::Loading &&
            phase != Stack::Project::ProjectLifecyclePhase::Importing &&
            phase != Stack::Project::ProjectLifecyclePhase::Conflict &&
            phase != Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery;
        if (!context.canEdit) {
            context.error =
                phase == Stack::Project::ProjectLifecyclePhase::Conflict
                ? "Resolve the project storage conflict before editing."
                : phase == Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery
                    ? "Save a repaired copy before editing this recovered project."
                    : "The multi-frame project is busy.";
        }
        BindRawOperationContext(context);
        return true;
    }
    if (selectedSource == nullptr) {
        context.error = "Select a RAW image to begin editing.";
        return false;
    }

    context.panelState = Stack::RawWorkspace::BuildRawPanelState(selectedSource);
    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_Project->rawSourceKey == selectedSource->relativePathKey;
    const bool selectedPreviewStageQueued =
        m_RawWorkspacePreviewStageQueued &&
        m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey;
    const bool selectedProjectLoading =
        selectedPreviewStageQueued ||
        (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
         m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey);
    const bool selectedProjectLoadFailed =
        m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey;
    const bool selectedStoredProject =
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded;

    bool resolved = false;
    if (selectedStoredProject && !selectedProjectActive) {
        context.recipe = BuildRawWorkspaceDefaultRecipe(*selectedSource);
        context.error = (selectedProjectLoading || selectedProjectLoadFailed)
            ? m_RawWorkspaceProjectLoadStatusText
            : "Double-click this image in Gallery to open its RAW project.";
    } else {
        resolved = ResolveRawWorkspaceRecipeForSource(
            *selectedSource,
            context.recipe,
            &context.resolvedMode,
            &context.error);
        if (selectedPreviewStageQueued && context.error.empty()) {
            context.error = m_RawWorkspaceProjectLoadStatusText.empty()
                ? "Preparing RAW preview..."
                : m_RawWorkspaceProjectLoadStatusText;
        }
    }
    // A failed replacement may restore the same source's previous live
    // project. The failed attempt must not disable that restored document.
    context.canEdit =
        context.panelState.recipeControlsEditable &&
        resolved &&
        selectedProjectActive &&
        !selectedProjectLoading;
    context.persistedRecipe = selectedProjectActive
        ? &m_Project->rawRecipe
        : &context.recipe;
    if (m_Project->rawInteractionDraft.active &&
        m_Project->rawInteractionDraft.sourceKey == selectedSource->relativePathKey) {
        context.recipe = m_Project->rawInteractionDraft.recipe;
    }
    BindRawOperationContext(context);
    return resolved;
}

void EditorModule::CommitRawWorkspaceEditContext(
    RawWorkspaceEditContext& context,
    bool changed,
    bool interactionActive) {
    if (Stack::Workspace::IsPreview() || !context.canEdit) {
        return;
    }
    if (!context.rawAdjustmentLayerId.empty()) {
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (m_Project->rawLayers.CancelGesture()) {
                MarkDirty(); MarkRenderRefreshDirty();
                NoteRawWorkspaceRecipePreviewEdit(false);
            }
            auto& areas = m_RawWorkspaceLabUi.zoneAreas;
            areas.history.Cancel();areas.active=false;
            for(auto& graph:areas.graphs) graph.second.interaction={};
            m_RawWorkspaceLabUi.sceneToneDraggingPoint=-1;
            ImGui::ClearActiveID();
            return;
        }
        auto& areaUi = m_RawWorkspaceLabUi.zoneAreas;
        if (areaUi.sourceKey == GetActiveRawWorkspacePreviewIdentity() && (changed || !interactionActive))
            areaUi.history.Observe(context.recipe.localRange.areas, interactionActive);
        if (changed) {
            if (interactionActive) m_Project->rawLayers.BeginGesture();
            auto candidate = m_Project->rawLayers.State();
            if (auto* layer = Stack::Project::FindRawAdjustmentLayer(candidate, context.rawAdjustmentLayerId)) {
                std::string error;
                if (Stack::Project::WriteRawLayerOperation(*layer, context.rawOperationUuid, context.recipe, error))
                    ApplyRawLayerStackEdit(std::move(candidate));
                else m_RawLayerStatus = error;
            }
        }
        if (!interactionActive) {
            m_Project->rawLayers.EndGesture();
            if (m_RawWorkspaceAdaptiveGestureActive) {
                NoteRawWorkspaceRecipePreviewEdit(false);
                MarkRenderRefreshDirty();
            }
        }
        return;
    }
    auto& areaUi = m_RawWorkspaceLabUi.zoneAreas;
    if (areaUi.sourceKey == GetActiveRawWorkspacePreviewIdentity()) {
        const bool cancelling = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (cancelling) {
            auto restored = areaUi.history.Cancel();
            changed |= !Stack::RawRecipe::EqualZoneAreas(restored, context.recipe.localRange.areas);
            context.recipe.localRange.areas = std::move(restored);
            areaUi.active = false;
            interactionActive = false;
            for (auto& entry : areaUi.graphs) entry.second.interaction = {};
            ImGui::ClearActiveID();
        }
        else if (changed || !interactionActive) areaUi.history.Observe(context.recipe.localRange.areas, interactionActive);
    }
    if (context.multiFrameResult) {
        const bool cancelling = !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if (cancelling && m_Project->rawLayers.GestureActive()) {
            ApplyMultiFramePostRecipeEdit(context.multiFrameSourceSetId,context.recipe,false,true);
            ImGui::ClearActiveID();
        } else if (changed) {
            ApplyMultiFramePostRecipeEdit(
                context.multiFrameSourceSetId,
                context.recipe,
                interactionActive);
        } else if (!interactionActive && m_RawWorkspaceAdaptiveGestureActive) {
            // A release often changes no value. End the gesture once so the
            // merged result receives the same final refinement as single RAW.
            NoteRawWorkspaceRecipePreviewEdit(false);
            MarkRenderRefreshDirty();
        }
        if (!interactionActive) m_Project->rawLayers.EndGesture();
        return;
    }

    const bool cancelDraft =
        m_Project->rawInteractionDraft.active &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (cancelDraft) {
        ResolveRawWorkspaceInteractionDraft(true);
        return;
    }
    if (interactionActive) {
        if (changed) {
            UpdateRawWorkspaceInteractionDraft(
                context.persistedRecipe != nullptr
                    ? *context.persistedRecipe
                    : context.recipe,
                context.recipe);
        }
        return;
    }
    if (m_Project->rawInteractionDraft.active) {
        if (changed) {
            m_Project->rawInteractionDraft.recipe = context.recipe;
        }
        ResolveRawWorkspaceInteractionDraft(false);
    } else if (changed) {
        ApplyRawWorkspaceRecipeEditForSelectedSource(
            context.recipe,
            false);
    }
}

bool EditorModule::ApplyMultiFramePostRecipeEdit(
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool interactionActive, bool cancelGesture) {
    if (!IsMultiFrameRawProjectActive() ||
        !m_Project->snapshot ||
        sourceSetId.empty()) {
        return false;
    }
    // Save jobs take their own immutable value snapshot before entering the
    // writer. Keep the active UI snapshot copy-on-write so a burst/HDR slider
    // does not duplicate all assets, cover bytes, and graph JSON every frame.
    if (m_Project->snapshot.use_count() != 1) {
        m_Project->snapshot =
            std::make_shared<Stack::Project::RawProjectSnapshot>(
                *m_Project->snapshot);
    }
    Stack::Project::RawProjectSnapshot& snapshot =
        *m_Project->snapshot;
    if (interactionActive && !cancelGesture) m_Project->rawLayers.BeginGesture();
    const auto update = cancelGesture
        ? Stack::Project::RestoreRawLayerHistory(m_Project->rawLayers,Stack::Project::RawLayerHistoryAction::CancelGesture,
            m_Project->rawRecipe,&snapshot,&m_Project->graph,ResolveRawWorkspaceStageOutputNodeId())
        : Stack::Project::ApplyMergedRawLayerSourceEdit(m_Project->rawLayers,snapshot,sourceSetId,recipe,
            &m_Project->graph,ResolveRawWorkspaceStageOutputNodeId());
    if (!interactionActive) m_Project->rawLayers.EndGesture();
    if (!update.success) {
        m_RawWorkspaceLabUi.multiFrameStatusText = update.errorMessage;
        return false;
    }
    if (!update.changed) {
        return true;
    }
    if (update.preMergeChanged) {
        m_MfdAdoptedRawResult.reset();
        for (EditorNodeGraph::Node& node : m_Project->graph.EditNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
                node.multiFrameDenoise.sourceSetId == update.sourceSetId) {
                node.multiFrameDenoise.resultState = "unavailable";
                node.multiFrameDenoise.presentationStatus =
                    "Shared CFA/Transform settings changed; process the burst again.";
            }
        }
    }
    MarkRenderDirty();
    NoteRawWorkspaceRecipePreviewEdit(interactionActive);
    return true;
}

bool EditorModule::RenderRawWorkspaceLabCommandStrip() {
    const bool galleryOpen = IsRawWorkspaceGalleryWorkspaceOpen();
    using GalleryAction = Stack::RawGalleryActions::Action;
    const auto galleryContext = galleryOpen
        ? CaptureRawGalleryActionContext() : Stack::RawGalleryActions::Context{};
    const bool bracketProject = IsBracketingActive() ||
        (m_Bracketing && m_Bracketing->newProject &&
            !m_Bracketing->sources.empty());
    const bool otherMultiFrameProject =
        IsMultiFrameRawProjectActive() && !bracketProject;
    const bool showBracket = galleryOpen
        ? galleryContext.bracketSources.size() >= 2u
        : bracketProject || otherMultiFrameProject;
    const bool showEdit = galleryOpen
        ? !galleryContext.items.empty()
        : bracketProject || otherMultiFrameProject;
    const int requestedMode = !showBracket && !showEdit ? -1 : RenderRawLabModeSwitcher(
        galleryOpen ? -1 : IsRawBracketModeActive() ? 0 : 1,
        galleryOpen ? Stack::RawGalleryActions::GetAvailability(galleryContext, GalleryAction::Open).enabled
            : !Stack::Workspace::IsPreview() &&
                !GetProjectFileCommandContext().busy && !IsBracketingPresentationActive(),
        showBracket, showEdit,
        galleryOpen ? static_cast<int>(galleryContext.items.size()) : 0,
        otherMultiFrameProject && !galleryOpen ? "Sources" : "Bracket", true);
    if (requestedMode != -1) {
        if (galleryOpen) {
            ExecuteRawGalleryAction(requestedMode == 0 ? GalleryAction::CreateCaptureSet
                : GalleryAction::Open, galleryContext);
        } else {
            RequestRawBracketMode(requestedMode == 0);
        }
    }

    UpdateRawWorkspaceNotificationDecisions();
    return false;
}


void EditorModule::UpdateRawWorkspaceNotificationDecisions() {
    namespace N = Stack::Notifications;
    if (m_ProjectConflictReloadPending) return;
    const bool conflicted = IsMultiFrameRawProjectActive() && m_Project->snapshot && m_Project->store &&
        m_Project->lifecycle.Phase() == Stack::Project::ProjectLifecyclePhase::Conflict;
    const bool loading = Async::IsBusy(m_Project->files->load.state);
    if ((!conflicted && !loading) || (!m_ProjectConflictDocument.empty() && m_ProjectConflictDocument != GetProjectDocumentId())) {
        if (m_ProjectConflictNotice) GetNotifier().Resolve(m_ProjectConflictNotice);
        m_ProjectConflictNotice = 0;
        m_ProjectConflictDocument.clear();
        m_ProjectInteractionUi.suppressConflictPrompt = false;
    }
    if (conflicted && !m_ProjectConflictNotice && !m_ProjectInteractionUi.suppressConflictPrompt) {
        const auto document = GetProjectDocumentId();
        const auto path = m_Project->storePath;
        const auto notifier = GetNotifier();
        const auto event = std::make_shared<N::EventId>(0);
        const auto valid = [this, document, path] {
            return GetProjectDocumentId() == document && m_Project->storePath == path && m_Project->snapshot &&
                m_Project->lifecycle.Phase() == Stack::Project::ProjectLifecyclePhase::Conflict;
        };
        N::NoticeSpec notice;
        notice.title = "Project changed outside Stack";
        notice.message = "Reload the changed project or save your edits as a copy.";
        notice.details = "Autosave and in-place save remain blocked until the storage conflict is resolved.";
        notice.route = N::Route::Center;
        notice.foreground = m_NotificationForeground;
        notice.operationId = notifier.NewOperation();
        const auto operation = notice.operationId;
        N::ActionSpec reload;
        reload.label = "Reload";
        reload.destructive = true;
        reload.resolveOnSuccess = false;
        reload.canInvoke = [this, valid] { return valid() && !Async::IsBusy(m_Project->files->load.state) &&
            !IsDeferredLoadedProjectApplyActive(); };
        reload.invoke = [this, path, document, notifier, event, operation] {
            if (AutoBracketWorkActive()) return N::ActionResult::Failure(
                "Finish or cancel automatic bracketing before reloading this project.");
            if (!FinishWorkspaceInteraction()) return N::ActionResult::Failure(
                "Finish the current edit before reloading this project.");
            const std::weak_ptr<Stack::Project::FileOperationState> owner = m_Project->files;
            const auto immediate = std::make_shared<std::optional<N::ActionResult>>();
            const auto invoking = std::make_shared<bool>(true);
            m_ProjectConflictReloadPending = true;
            m_ProjectConflictReloadOperation = operation;
            const auto finish = [this, owner, document, notifier, event, operation, immediate, invoking](bool loaded,
                const std::string& message) {
                const auto files = owner.lock();
                if (!files || m_ProjectConflictReloadOperation != operation || !notifier.IsOperationCurrent(operation)) return;
                m_ProjectConflictReloadPending = false;
                if (!loaded && (files != m_Project->files || GetProjectDocumentId() != document)) {
                    notifier.InvalidateOperation(operation);
                    return;
                }
                if (!loaded) {
                    for (auto& tracked : m_NotificationDecisionOwners) if (tracked.operation == operation)
                        tracked.loadGeneration = files->load.generation;
                    m_NotificationDecisionFiles = m_Project->files;
                    m_NotificationDecisionDocument = GetProjectDocumentId();
                    m_NotificationDecisionLoadGeneration = files->load.generation;
                }
                const auto result = loaded ? N::ActionResult::Success() : N::ActionResult::Failure(
                    message.empty() ? "The project could not be reloaded. Your current edits remain open." : message);
                if (*invoking) *immediate = result;
                else notifier.FinishAction(*event, 0, result);
                if (loaded) notifier.Resolve(*event);
            };
            // Reload explicitly discards this conflicted session. Loading it
            // directly avoids trying to save the conflicted original first.
            LibraryManager::Get().RequestLoadProjectDeferredApply(path.string(), this,
                [this, owner, document, finish](bool loaded, std::shared_ptr<Stack::Project::LoadedProjectData> project) {
                    const auto files = owner.lock();
                    if (!files) return;
                    if (!loaded || !project) { finish(false, files->load.statusText); return; }
                    const auto generation = files->load.generation;
                    const auto targetDocument = project->projectId;
                    if (files != m_Project->files || GetProjectDocumentId() != document) {
                        finish(false, "The project changed before reload could apply."); return;
                    }
                    try {
                        const bool started = BeginDeferredLoadedProjectApply(project,
                            [this, owner, generation, targetDocument, finish](bool applied, const std::string& status) {
                                const auto files = owner.lock();
                                if (!files) return;
                                if (files != m_Project->files || generation != files->load.generation ||
                                    (applied && !targetDocument.empty() && GetProjectDocumentId() != targetDocument)) {
                                    finish(false, "The reload request is no longer current."); return;
                                }
                                LibraryManager::Get().FinishDeferredProjectLoad(this, applied, status);
                                finish(applied, status);
                            });
                        if (started) return;
                        LibraryManager::Get().FinishDeferredProjectLoad(this, false, GetDeferredLoadedProjectStatusText());
                    } catch (const std::exception& exception) {
                        LibraryManager::Get().FinishDeferredProjectLoad(this, false, exception.what());
                    } catch (...) {
                        LibraryManager::Get().FinishDeferredProjectLoad(this, false, "The changed project could not be applied.");
                    }
                    finish(false, files->load.statusText);
                });
            m_ProjectConflictReloadGeneration = m_Project->files->load.generation;
            *invoking = false;
            return *immediate ? **immediate : N::ActionResult::Pending();
        };
        N::ActionSpec copy;
        copy.label = "Save copy";
        copy.resolveOnSuccess = false;
        copy.canInvoke = valid;
        copy.invoke = [this, notifier, event] {
            const auto destination = FileDialogs::SaveProjectFileDialog("Save Conflicted Project Copy",
                (m_Project->snapshot->projectName + "-copy.stack").c_str());
            if (destination.empty()) return N::ActionResult::Failure("No copy was saved. Choose a destination to continue.");
            std::string error;
            if (!SaveActiveMultiFrameRawProjectAs(destination, Stack::Project::ProjectStorageKind::PortableFile, &error))
                return N::ActionResult::Failure(error.empty() ? "The project copy could not be saved." : error);
            notifier.Info("Project copy saved. The original storage conflict still needs a reload.");
            notifier.Dismiss(*event);
            return N::ActionResult::Success();
        };
        N::ActionSpec cancel;
        cancel.label = "Keep editing";
        cancel.safeCancel = true;
        cancel.resolveOnSuccess = false;
        cancel.invoke = [this, document, notifier, event] {
            if (GetProjectDocumentId() == document) m_ProjectInteractionUi.suppressConflictPrompt = true;
            notifier.Dismiss(*event);
            return N::ActionResult::Success();
        };
        notice.actions = {std::move(reload), std::move(copy), std::move(cancel)};
        *event = RequestNotificationDecision(std::move(notice));
        m_ProjectConflictNotice = *event;
        m_ProjectConflictDocument = document;
    }
    if (!m_RawWorkspaceLabUi.clearConfirmationRequested) return;
    m_RawWorkspaceLabUi.clearConfirmationRequested = false;
    const auto root = m_RawWorkspace.workspaceRoot;
    const auto document = GetProjectDocumentId();
    N::NoticeSpec notice;
    notice.title = "Clear RAW workspace?";
    notice.message = "Clear the current RAW folder and selection?";
    notice.details = "Saved projects and original captures remain on disk.";
    notice.route = N::Route::Center;
    notice.foreground = m_NotificationForeground;
    notice.operationId = GetNotifier().NewOperation();
    N::ActionSpec clear;
    clear.label = "Clear";
    clear.destructive = true;
    clear.canInvoke = [this, root, document] {
        return m_RawWorkspace.workspaceRoot == root && GetProjectDocumentId() == document;
    };
    clear.invoke = [this] {
        ClearRawWorkspaceForUser();
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
        return N::ActionResult::Success();
    };
    N::ActionSpec cancel;
    cancel.label = "Cancel";
    cancel.safeCancel = true;
    cancel.invoke = [] { return N::ActionResult::Success(); };
    notice.actions = {std::move(clear), std::move(cancel)};
    RequestNotificationDecision(std::move(notice));
}

bool EditorModule::RenderRawWorkspaceActiveControls(RawWorkspaceEditContext& context) {
    // This flag describes only the current UI frame. Reset it even when a
    // different tool surface is active so a later interaction cannot inherit
    // stale Exposure state after a tool switch.
    m_RawWorkspaceLabUi.globalExposureInteractionActive = false;
    // Keep the move handle and tool chooser outside the scrolling body.
    const ImVec2 controlsMinimum = ImGui::GetCursorScreenPos();
    RenderRawFloatingSurfaceHandle(LabToolName(m_RawWorkspaceLabUi.activeTool));
    if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize("Choose").x + 12.f)
        ImGui::SameLine();
    if (BareTextButton("Choose", false, !Stack::Workspace::IsPreview()))
        m_RawToolPickerRequested = true;
    LabTooltip("Choose an editing tool. Hold Alt anywhere in RAW.");
    const float headerHeight = ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 8.f));
    // Separate scroll state per tool. Switching from a long tool must not
    // open a short tool at the old tool's scroll offset.
    ImGui::PushID(static_cast<int>(m_RawWorkspaceLabUi.activeTool));
    ImGui::BeginChild("RawLabActiveToolSurface", ImVec2(0.f, 0.f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar);
    if (!context.rawAdjustmentLayerId.empty()) {
        const auto* layer = Stack::Project::FindRawAdjustmentLayer(
            m_Project->rawLayers.State(), context.rawAdjustmentLayerId);
        if (layer) ImGui::TextDisabled("%s", layer->name.c_str());
    } else if (context.source || context.multiFrameResult) {
        ImGui::TextDisabled("Base image");
    }
    // Bracket/Edit is local to the selected capture, not application chrome.
    RenderRawWorkspaceLabCommandStrip();
    ImGui::Spacing();
    const RawLabTool drawerTool =
        Stack::EditorModuleTypes::RawLabDrawerTool(m_RawWorkspaceLabUi.activeTool);
    if (drawerTool == RawLabTool::Zones || drawerTool == RawLabTool::Color) {
        const bool curves = drawerTool == RawLabTool::Zones;
        const RawLabTool first = curves ? RawLabTool::Zones : RawLabTool::Color;
        const RawLabTool second = curves ? RawLabTool::Tone : RawLabTool::Calibration;
        ImGui::PushID(curves ? "RawLabCurvesModes" : "RawLabColorModes");
        if (curves) {
            if (BareTextButton("Exposure", m_RawWorkspaceLabUi.activeTool == RawLabTool::Exposure)) RequestRawLabTool(RawLabTool::Exposure);
            ImGui::SameLine();
        }
        if (BareTextButton(curves ? "Local exposure" : "Color Warp",
                m_RawWorkspaceLabUi.activeTool == first)) {
            RequestRawLabTool(first);
        }
        ImGui::SameLine();
        if (BareTextButton(curves ? "Tone" : "Calibration",
                m_RawWorkspaceLabUi.activeTool == second)) {
            RequestRawLabTool(second);
        }
        if (curves) {
            ImGui::SameLine();
            if (BareTextButton("Detail", m_RawWorkspaceLabUi.activeTool == RawLabTool::Detail)) RequestRawLabTool(RawLabTool::Detail);
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (BareTextButton("Settings", m_RawWorkspaceLabUi.settingsTabActive,
            !Stack::Workspace::IsPreview())) RequestRawSettingsPanel();
    LabTooltip("Open this tool's settings in the side panel.");
    ImGui::Spacing();
    bool settingsOpenedThisFrame = false;

    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Denoise ||
        m_RawWorkspaceLabUi.activeTool == RawLabTool::RgbDenoise) {
        if (ImGui::RadioButton("CFA", m_RawWorkspaceLabUi.activeTool == RawLabTool::Denoise)) {
            m_RawWorkspaceLabUi.activeTool = RawLabTool::Denoise;
            SaveRawWorkspaceAppState();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(IsMultiFrameRawProjectActive() && !context.canEdit);
        if (ImGui::RadioButton("RGB", m_RawWorkspaceLabUi.activeTool == RawLabTool::RgbDenoise)) {
            m_RawWorkspaceLabUi.activeTool = RawLabTool::RgbDenoise;
            SaveRawWorkspaceAppState();
        }
        ImGui::EndDisabled();
    }
    bool changed = false;
    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame) {
        RenderBracketingUI();
        if (m_RawLabMultiFrameAdvancedOpenedThisFrame) {
            settingsOpenedThisFrame = true;
            RequestRawSettingsPanel();
            m_RawLabMultiFrameAdvancedOpenedThisFrame = false;
        }
    } else if (context.source == nullptr && !context.multiFrameResult) {
        ImGui::TextDisabled("%s", context.error.empty()
            ? "Choose an image from Gallery to edit."
            : context.error.c_str());
    } else {
        Stack::UiActivity::BeginDisabledForWork(
        !context.canEdit && IsRawWorkspaceProjectLoadBusy(),
        !context.canEdit && !IsRawWorkspaceProjectLoadBusy());
        switch (m_RawWorkspaceLabUi.activeTool) {
            case RawLabTool::Transform:
                if (ImGui::Button("Rotate left")) {
                    context.recipe.cropRotation.rotationDegrees = NormalizeRawLabRotationDegrees(context.recipe.cropRotation.rotationDegrees - 90);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Rotate right")) {
                    context.recipe.cropRotation.rotationDegrees = NormalizeRawLabRotationDegrees(context.recipe.cropRotation.rotationDegrees + 90);
                    changed = true;
                }
                changed |= ImGui::Checkbox("Flip horizontally", &context.recipe.cropRotation.flipHorizontally);
                changed |= ImGui::Checkbox("Flip vertically", &context.recipe.cropRotation.flipVertically);
                break;
            case RawLabTool::Denoise:
                changed |= RenderRawWorkspaceLabDenoiseSurface(context);
                break;
            case RawLabTool::RgbDenoise:
                changed |= RenderRawWorkspaceLabRgbDenoiseSurface(context);
                break;
            case RawLabTool::Light:
                changed |= RenderRawWorkspaceLabLightSurface(context);
                break;
            case RawLabTool::Exposure:
                ImGui::BeginDisabled(IsRawParameterDriven("ev"));
                changed |= ImGui::SliderFloat("Exposure", &context.recipe.preToneExposureEv, -10.f, 10.f, "%.2f EV");
                ImGui::EndDisabled();
                break;
            case RawLabTool::Zones:
                changed |= RenderRawWorkspaceLabZonesSurface(context, RawLabControlSection::Graph);
                break;
            case RawLabTool::Detail:
                changed |= DrawDetailContrastEditor(context.recipe.detailContrast, m_RawWorkspaceLabUi.detailContrastEditor, [this](const auto& id) { return IsRawParameterDriven(id); }, RawLabControlSection::Graph);
                break;
            case RawLabTool::Tone:
                changed |= RenderRawWorkspaceLabToneSurface(context, RawLabControlSection::Graph);
                break;
            case RawLabTool::Color:
                changed |= RenderRawWorkspaceLabColorSurface(context, RawLabControlSection::Graph);
                break;
            case RawLabTool::Calibration:
                changed |= RenderRawWorkspaceLabCalibrationSurface(context);
                break;
            case RawLabTool::View:
                changed |= RenderRawWorkspaceLabViewSurface(context);
                break;
            default:
                ImGui::TextDisabled("This tool is reserved for a later RAW Lab pass.");
                break;
        }
        ImGui::EndDisabled();
        if (!context.error.empty() && !context.canEdit) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", context.error.c_str());
        }
    }
    // Contact detection and the Alt selector follow the visible controls.
    // This does not request a rectangular image blur behind the tool window.
    const auto* controlsWindow = ImGui::GetCurrentWindow();
    m_RawFloatingSurface.contentHeights[static_cast<int>(m_RawWorkspaceLabUi.activeTool)] =
        std::max(80.f, headerHeight +
            controlsWindow->DC.CursorMaxPos.y - controlsWindow->DC.CursorStartPos.y + 16.f);
    ImRect visibleControls(controlsMinimum, ImVec2(controlsWindow->WorkRect.Max.x,
        std::max(controlsMinimum.y, controlsWindow->DC.CursorMaxPos.y)));
    visibleControls.ClipWithFull(controlsWindow->ClipRect);
    m_RawActiveControlMinimum = visibleControls.Min;
    m_RawActiveControlMaximum = visibleControls.Max;
    m_RawActiveControlFrame = ImGui::GetFrameCount();
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleVar();

    const auto curveInteractionActive = [](const RawCurveGraphUiState& state) {
        return state.draggingExposure || state.draggingPoint >= 0 || state.draggingSegment >= 0;
    };
    const bool graphInteractionActive =
        curveInteractionActive(m_RawWorkspaceLabUi.zonesCurveGraph) ||
        std::any_of(
            m_RawWorkspaceLabUi.toneCurveGraphs.begin(),
            m_RawWorkspaceLabUi.toneCurveGraphs.end(),
            curveInteractionActive);
    const bool interactionActive =
        ImGui::IsAnyItemActive() ||
        graphInteractionActive ||
        m_RawWorkspaceLabUi.zoneAreas.active ||
        std::any_of(m_RawWorkspaceLabUi.zoneAreas.graphs.begin(), m_RawWorkspaceLabUi.zoneAreas.graphs.end(),
            [&](const auto& entry) { return curveInteractionActive(entry.second.interaction); }) ||
        m_RawWorkspaceLabUi.colorWarpInteractionActive ||
        (m_RawWorkspaceLabUi.activeTool == RawLabTool::Light &&
         m_RawWorkspaceLabUi.lightInteractionActive);
    CommitRawWorkspaceEditContext(context, changed, interactionActive);
    m_RawWorkspaceLabUi.colorWarpInteractionActive = false;
    return settingsOpenedThisFrame;
}

bool EditorModule::RenderRawWorkspaceLabSecondaryControls(RawWorkspaceEditContext& context) {
    switch (m_RawWorkspaceLabUi.activeTool) {
        case RawLabTool::MultiFrame:
            RenderMultiFrameRawLabAdvanced();
            return false;
        case RawLabTool::Denoise:
            return RenderRawWorkspaceLabDenoiseSecondaryControls(context);
        case RawLabTool::RgbDenoise:
            return RenderRawWorkspaceLabRgbDenoiseSecondaryControls(context);
        case RawLabTool::Light:
            return RenderRawWorkspaceLabLightSecondaryControls(context);
        case RawLabTool::Zones:
            return RenderRawWorkspaceLabZonesSecondaryControls(context);
        case RawLabTool::Color:
            return RenderRawWorkspaceLabColorSecondaryControls(context);
        case RawLabTool::View:
            return RenderRawWorkspaceLabViewSecondaryControls(context);
        default:
            return false;
    }
}

void EditorModule::RenderRawWorkspaceLabUI() {
    m_RawActiveControlFrame = -2;
    m_RawFloatingSurface.frame = -2;
    LoadResourceTextures();
    EnsureRawWorkspaceLoaded();
    m_RawWorkspaceLabUi.galleryWorkspaceOpen = m_PermanentGalleryWorkspace;
    if (m_PermanentGalleryWorkspace) {
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Filmstrip;
    } else {
        // Project surfaces never reserve space for, draw, or react to the
        // Gallery filmstrip, including the tail of an old drawer animation.
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip)
            m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
        m_RawWorkspaceLabAnimatedFilmstripHeight = 0.0f;
        m_RawWorkspaceLabFilmstripAnimationOpenHeight = 0.0f;
        m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
        m_RawWorkspaceLabFilmstripDrawerState = {};
        m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
        m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
        m_RawWorkspaceLabFilmstripHoverFrame = -1;
        m_RawWorkspaceLabFilmstripHoverOpacity = 0.0f;
    }
    PumpNonRenderingWork(2.5);
    PumpRawWorkspaceThumbnailTextureUploads();
    PollRawWorkspaceLabGradingScope();
    if (!m_RawWorkspaceLabDrawerAnimationInitialized) {
        // Ordinary filmstrips animate their first open. The Gallery overrides
        // the drawer height on its first layout so it starts expanded.
        m_RawWorkspaceLabDrawerAnimationInitialized = true;
        m_RawWorkspaceLabAnimatedLowerShelfHeight = 0.0f;
        m_RawWorkspaceLabAnimatedFilmstripHeight = 0.0f;
        m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild(
        "RawWorkspaceLabRoot",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (IsBracketingActive() && m_Bracketing &&
        (m_Bracketing->pending || m_Bracketing->job || m_Bracketing->failed || m_Bracketing->processRequired)) {
        ImGui::TextWrapped("%s %s", m_HdrAdoptedRawResult ? "Showing the previous bracket result." : "Bracket processing is pending.", m_Bracketing->status.c_str());
        if (m_Bracketing->failed && ImGui::Button("Retry bracket")) {
            m_Bracketing->pending = true;
            CommitBracketingDraft(true, nullptr);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Bracketing")) RequestOpenMultiFrameTab();
    }
    const bool galleryBrowser = m_PermanentGalleryWorkspace;
    if (galleryBrowser) {
        if (RenderRawWorkspaceGalleryActionBar()) {
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            return;
        }
        HandleRawGalleryActionShortcuts();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        if (m_RawSectionPanelFrame != ImGui::GetFrameCount()) {
            const float navigationWidth = std::min(
                260.0f * ImGui::GetFontSize() / 13.0f,
                ImGui::GetContentRegionAvail().x * 0.28f);
            if (RenderRawWorkspaceLabGalleryNavigation(
                    ImVec2(navigationWidth, ImGui::GetContentRegionAvail().y))) {
                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                return;
            }
            ImGui::SameLine(0.0f, 0.0f);
        }
        ImGui::BeginChild("RawGalleryBody", ImVec2(0.0f, 0.0f), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            m_RawWorkspaceGalleryLayoutAnimation.Reset();
        const std::string layoutKey = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string() +
            ":" + std::to_string(static_cast<int>(m_RawWorkspaceGalleryContentMode)) +
            ":" + std::to_string(static_cast<int>(m_RawWorkspaceLabUi.galleryNavigationMode)) +
            ":" + m_RawWorkspaceLabUi.galleryProjectId;
        m_RawWorkspaceGalleryLayoutAnimation.BeginFrame(layoutKey,
            m_RawWorkspaceLabUi.galleryWorkspaceGrid, ImGui::GetFrameCount(), ImGui::GetTime());
    } else {
        m_RawWorkspaceGalleryLayoutAnimation.Reset();
    }
    const float rawLabRootLeft = ImGui::GetWindowPos().x;
    const bool colorWorkspaceBackdrop =
        !m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
        m_RawWorkspaceLabUi.activeTool == RawLabTool::Color;
    if (colorWorkspaceBackdrop) {
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    }

    const bool creatingMfd =
        m_RawWorkspaceLabUi.galleryNavigationMode ==
        RawGalleryNavigationMode::MultiFrameCreation;
    if (creatingMfd && !Stack::Workspace::IsPreview()) {
        // Project creation uses the same windowed Gallery as ordinary source
        // browsing. Keep the picker available until the user explicitly
        // creates or cancels the capture set.
        if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow) {
            OpenRawWorkspaceLabNativeGallery();
        }
        m_RawWorkspaceLabAnimatedFilmstripHeight = 0.0f;
        m_RawWorkspaceLabFilmstripAnimationOpenHeight = 0.0f;
        m_RawWorkspaceLabAnimatedLowerShelfHeight = 0.0f;
    }

    const bool multiFrameProject = IsMultiFrameRawProjectActive() &&
        m_Project->snapshot;
    if (multiFrameProject && !creatingMfd) {
        if (m_RawLabPresentedMultiFrameProjectId !=
            m_Project->snapshot->projectId) {
            m_RawLabPresentedMultiFrameProjectId =
                m_Project->snapshot->projectId;
            m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
            m_RawWorkspaceLabUi.galleryNavigationMode =
                IsBracketingActive()?RawGalleryNavigationMode::ProjectRoot:RawGalleryNavigationMode::ProjectFrames;
            m_RawWorkspaceLabUi.galleryProjectId =
                m_Project->snapshot->projectId;
        }
    } else if (!creatingMfd) {
        m_RawLabPresentedMultiFrameProjectId.clear();
    }

    const RawWorkspaceScanSnapshot scanSnapshot = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnailSnapshot = GetRawWorkspaceThumbnailSnapshot();
    const std::string filmstripWorkspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (m_RawWorkspaceLabFilmstripDrawerWorkspaceKey !=
            filmstripWorkspaceKey) {
        m_RawWorkspaceLabFilmstripDrawerWorkspaceKey =
            filmstripWorkspaceKey;
        m_RawWorkspaceLabFilmstripHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
        m_RawWorkspaceLabFilmstripHoverFrame = -1;
        m_RawWorkspaceLabFilmstripHoverSuppressed = false;
        m_RawWorkspaceLabFilmstripHoverOpacity = 0.0f;
        m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
        m_RawWorkspaceLabFilmstripDrawerState = {};
        m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
        m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
        m_RawWorkspaceGalleryDrawerNeedsInitialExpansion = true;
        m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
        m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
        m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
        m_RawWorkspaceLabFilmstripDragState = {};
        m_RawWorkspaceLabFilmstripScrollTargetX = -1.0f;
        m_RawWorkspaceLabFilmstripScrollLastAppliedX = -1.0f;
        m_RawWorkspaceLabFilmstripStackAnimations.clear();
        m_RawWorkspaceLabFilmstripSourceLastSlotX.clear();
    }
    // Filmstrip multi-selection can move the Gallery focus without opening a
    // different image. Keep the editing panel on its active source until an
    // explicit open replaces that editing session.
    const bool useActiveEditingSource =
        !m_Project->rawSourceKey.empty() &&
        (IsBracketingToolActive() || IsRawWorkspaceProjectActive());
    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(useActiveEditingSource
            ? m_Project->rawSourceKey
            : m_RawWorkspace.selectedSourceKey);
    const int currentFrame = ImGui::GetFrameCount();
    const bool filmstripTileHovered =
        m_RawWorkspaceLabFilmstripHoverFrame >= currentFrame - 1;
    const bool filmstripPointerInside =
        m_PermanentGalleryWorkspace &&
        m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip &&
        (m_RawWorkspaceLabFilmstripDrawerPointerInside ||
         filmstripTileHovered);
    if (!filmstripPointerInside) {
        m_RawWorkspaceLabFilmstripHoverSuppressed = false;
    }
    const bool filmstripHoverTarget =
        (galleryBrowser || (filmstripPointerInside &&
            !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift &&
            !m_RawWorkspaceLabFilmstripHoverSuppressed)) &&
        FindRawWorkspaceSourceByKey(
            m_RawWorkspaceLabFilmstripHoverSourceKey) != nullptr;
    m_RawWorkspaceLabFilmstripHoverBlend = std::min(
        1.0f,
        m_RawWorkspaceLabFilmstripHoverBlend +
            std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) / 0.18f);
    const float hoverResponse = 1.0f - std::exp(
        -std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) / 0.14f);
    m_RawWorkspaceLabFilmstripHoverOpacity +=
        ((filmstripHoverTarget ? 1.0f : 0.0f) -
            m_RawWorkspaceLabFilmstripHoverOpacity) * hoverResponse;
    if (!filmstripHoverTarget &&
        m_RawWorkspaceLabFilmstripHoverOpacity < 0.005f) {
        m_RawWorkspaceLabFilmstripHoverOpacity = 0.0f;
        m_RawWorkspaceLabFilmstripHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
        m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
        m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
    }
    const bool filmstripHoverVisible =
        m_RawWorkspaceLabFilmstripHoverOpacity > 0.005f;
    const bool showEditingSurface =
        !m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
        !filmstripHoverVisible && (IsBracketingToolActive() ||
        multiFrameProject ||
        (selectedSource != nullptr && IsRawWorkspaceProjectActive() &&
         m_Project->rawSourceKey == selectedSource->relativePathKey));
    m_RawWorkspaceLabEditingSurfaceReveal +=
        ((showEditingSurface ? 1.0f : 0.0f) -
            m_RawWorkspaceLabEditingSurfaceReveal) * hoverResponse;
    if (m_RawWorkspaceLabEditingSurfaceReveal < 0.005f) {
        m_RawWorkspaceLabEditingSurfaceReveal = 0.0f;
    } else if (m_RawWorkspaceLabEditingSurfaceReveal > 0.995f) {
        m_RawWorkspaceLabEditingSurfaceReveal = 1.0f;
    }
    RawWorkspaceEditContext context;
    BeginRawWorkspaceEditContext(selectedSource, context);
    ApplyRequestedRawLabTool(context);

    UpdateRawWorkspaceNotificationDecisions();
    ImGui::SetCursorPosX(0.0f);
    ImGui::BeginChild("RawModuleWorkspace", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (galleryBrowser && m_RawWorkspaceLabUi.galleryWorkspaceGrid) {
        m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
        m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
        m_RawWorkspaceGalleryPreviewHovered = false;
        RenderRawWorkspaceLabGalleryWorkspace(ImGui::GetContentRegionAvail());
    } else if (m_RawWorkspace.workspaceRoot.empty() && !multiFrameProject && !IsBracketingToolActive() &&
        !m_RawWorkspaceLabUi.galleryWorkspaceOpen) {
        m_RawWorkspaceLabFilmstripDrawerState = {};
        m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
        m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
        m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
        m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
        m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = false;
        m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
        m_RawWorkspaceLabFilmstripDragState = {};
        const float contentStartX = ImGui::GetCursorPosX();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(
            contentStartX + std::max(0.0f, (available.x - 180.0f) * 0.5f),
            std::max(0.0f, (available.y - 34.0f) * 0.42f)));
        if (BareTextButton("Open RAW Folder", false, true, ImVec2(180.0f, 34.0f))) {
            OpenRawWorkspaceFolderDialog();
        }
    } else {
        const float totalHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
        const bool filmstripRequested =
            m_PermanentGalleryWorkspace &&
            m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip;
        const float filmstripTargetHeight = filmstripRequested
            ? std::clamp(
                  m_RawWorkspaceLabUi.filmstripHeight,
                  kRawLabFilmstripMinimumHeight,
                  std::min(
                      kRawLabFilmstripMaximumHeight,
                      std::max(
                          kRawLabFilmstripMinimumHeight,
                          totalHeight * 0.30f)))
            : 0.0f;
        if (filmstripRequested) {
            m_RawWorkspaceLabFilmstripAnimationOpenHeight =
                filmstripTargetHeight;
        }
        m_RawWorkspaceLabAnimatedFilmstripHeight =
            AnimateRawLabDrawerHeight(
                m_RawWorkspaceLabAnimatedFilmstripHeight,
                filmstripTargetHeight,
                ImGui::GetIO().DeltaTime);
        const bool showFilmstrip =
            filmstripRequested ||
            m_RawWorkspaceLabAnimatedFilmstripHeight > 0.25f;
        // Bracketing edits a capture draft before a RAW project exists. Its
        // source list and Process action must be available while browsing.
        // Other tools still require an explicitly opened RAW project.
        const bool gradingScopeHotkeyAvailable =
            showEditingSurface &&
            m_RawWorkspaceLabUi.lowerShelfOpen &&
            CanConsumeEditorCommandKeys() &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            !ImGui::GetIO().KeyCtrl &&
            !ImGui::GetIO().KeyShift &&
            !ImGui::GetIO().KeyAlt;
        if (gradingScopeHotkeyAvailable &&
            ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            m_RawWorkspaceLabUi.gradingScopesShowInput =
                !m_RawWorkspaceLabUi.gradingScopesShowInput;
            MarkRenderRefreshDirty();
        }
        const float filmstripHeight = showFilmstrip
            ? std::min(std::max(0.0f, totalHeight - 1.0f),
                std::max(1.0f, m_RawWorkspaceLabAnimatedFilmstripHeight))
            : 0.0f;
        const bool projectsFilmstripMode =
            m_RawWorkspaceLabUi.galleryNavigationMode ==
                RawGalleryNavigationMode::ProjectRoot &&
            m_RawWorkspaceGalleryContentMode !=
                Stack::RawWorkspace::GalleryContentMode::Gallery;
        bool drawerContentAvailable = true;
        if (projectsFilmstripMode &&
            !m_RawWorkspaceLabUi.galleryWorkspaceOpen) {
            drawerContentAvailable = false;
            const auto& presentation = GetRawWorkspaceGalleryPresentation();
            for (const auto& group : presentation.groups) {
                if (std::any_of(
                        group.sources.begin(),
                        group.sources.end(),
                        [](const auto& source) {
                            return source.savedProjectCount > 1u;
                        })) {
                    drawerContentAvailable = true;
                    break;
                }
            }
        }
        Stack::RawWorkspace::RawGalleryFilmstripDrawerInput drawerInput;
        drawerInput.now = ImGui::GetTime();
        drawerInput.enabled = filmstripRequested && drawerContentAvailable;
        drawerInput.pointerInside =
            m_RawWorkspaceLabFilmstripDrawerPointerInside;
        drawerInput.interactionRetained =
            m_RawWorkspaceLabFilmstripDrawerInteractionRetained;
        drawerInput.keyboardToggle =
            m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested;
        drawerInput.galleryWorkspace =
            m_RawWorkspaceLabUi.galleryWorkspaceOpen;
        drawerInput.previewHovered =
            m_RawWorkspaceGalleryPreviewHovered;
        m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = false;
        const bool drawerWasOpen =
            m_RawWorkspaceLabFilmstripDrawerState.open;
        m_RawWorkspaceLabFilmstripDrawerState =
            Stack::RawWorkspace::UpdateRawGalleryFilmstripDrawerState(
                m_RawWorkspaceLabFilmstripDrawerState,
                drawerInput);
        const bool normalGalleryFilmstrip =
            m_RawWorkspaceLabUi.galleryNavigationMode ==
                RawGalleryNavigationMode::ProjectRoot &&
            m_RawWorkspaceGalleryContentMode ==
                Stack::RawWorkspace::GalleryContentMode::Gallery;
        const auto maximumFilmstripStackSize = [&]() {
            if (!normalGalleryFilmstrip) return std::size_t { 1u };
            ResolveRawWorkspaceFilmstripStacks();
            return m_RawWorkspaceFilmstripMaximumStackSizeCache;
        };
        constexpr float kFilmstripStackGap = 10.0f;
        const float filmstripTileHeight = kRawLabFilmstripTileHeight *
            m_RawWorkspaceLabUi.galleryThumbnailScale;
        const auto drawerHeightForCurrentStacks = [&]() {
            return Stack::RawWorkspace::
                ComputeRawGalleryFilmstripDrawerTargetHeight(
                    filmstripTargetHeight,
                    filmstripTileHeight,
                    kFilmstripStackGap,
                    maximumFilmstripStackSize(),
                    totalHeight);
        };
        if (!drawerWasOpen &&
            m_RawWorkspaceLabFilmstripDrawerState.open) {
            m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
            if (normalGalleryFilmstrip) {
                for (const auto& stack : ResolveRawWorkspaceFilmstripStacks()) {
                    if (stack.sourceKeys.empty()) continue;
                    const std::string cover =
                        Stack::RawWorkspace::ResolveRawGalleryStackCover(
                            stack.sourceKeys,
                            m_RawWorkspace.selectedSourceKey);
                    std::vector<std::string> order =
                        Stack::RawWorkspace::BuildRawGalleryExpansionOrder(
                            stack.sourceKeys,
                            cover);
                    m_RawWorkspaceLabFilmstripDrawerSessionOrders.emplace(
                        stack.sourceKeys.front(),
                        std::move(order));
                }
            }
            m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight =
                drawerHeightForCurrentStacks();
            if (m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
                m_RawWorkspaceGalleryDrawerNeedsInitialExpansion) {
                m_RawWorkspaceLabFilmstripDrawerAnimatedHeight =
                    m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight;
                m_RawWorkspaceGalleryDrawerNeedsInitialExpansion = false;
            }
        }
        if (!filmstripRequested) {
            m_RawWorkspaceLabFilmstripDrawerState = {};
            m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = 0.0f;
            m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
            m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
            m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
            m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
            m_RawWorkspaceLabFilmstripDragState = {};
        } else if (m_RawWorkspaceLabFilmstripDrawerState.open) {
            // Similarity grouping is asynchronous. Do not leave a drawer at
            // its initial singleton height when a larger stack arrives after
            // the open transition has begun.
            m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = std::max(
                m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight,
                drawerHeightForCurrentStacks());
            m_RawWorkspaceLabFilmstripDrawerAnimatedHeight =
                AnimateRawLabDrawerHeight(
                    std::max(
                        filmstripHeight,
                        m_RawWorkspaceLabFilmstripDrawerAnimatedHeight),
                    std::max(
                        filmstripHeight,
                        m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight),
                    ImGui::GetIO().DeltaTime);
        } else if (
            m_RawWorkspaceLabFilmstripDrawerAnimatedHeight >
                filmstripHeight + 0.25f) {
            m_RawWorkspaceLabFilmstripDrawerAnimatedHeight =
                AnimateRawLabDrawerHeight(
                    m_RawWorkspaceLabFilmstripDrawerAnimatedHeight,
                    filmstripHeight,
                    ImGui::GetIO().DeltaTime);
        } else {
            m_RawWorkspaceLabFilmstripDrawerAnimatedHeight = filmstripHeight;
            m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight = 0.0f;
            m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
        }
        // Keep one persistent overlay host for both the collapsed filmstrip
        // and the expanded drawer. Swapping between two ImGui child trees at
        // the end of the animation changes their scroll state and introduces
        // a small but visible navigation/tile position jump.
        const bool showFilmstripDrawerOverlay = filmstripRequested;
        const float drawerExpansionRange =
            m_RawWorkspaceLabFilmstripDrawerFrozenTargetHeight -
            filmstripHeight;
        const float drawerExpansionProgress =
            drawerExpansionRange > 0.25f
            ? SmoothRawLabDrawerProgress(
                  (m_RawWorkspaceLabFilmstripDrawerAnimatedHeight -
                   filmstripHeight) / drawerExpansionRange)
            : (m_RawWorkspaceLabFilmstripDrawerState.open ? 1.0f : 0.0f);
        const float editingHeight = std::max(1.0f, totalHeight - filmstripHeight);
        const float previewDisplayHeight = std::max(
            1.0f,
            editingHeight - std::max(
                0.0f,
                m_RawWorkspaceLabFilmstripDrawerAnimatedHeight -
                    filmstripHeight));
        const float editingWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const ImVec2 editingMinimum = ImGui::GetCursorScreenPos();
        std::unique_ptr<Stack::AutoBracket::EditingScope> autoBracketInput;
        if (!m_RawWorkspaceLabUi.galleryWorkspaceOpen) {
            autoBracketInput = std::make_unique<Stack::AutoBracket::EditingScope>(
                *this, editingMinimum, ImVec2(editingWidth, editingHeight));
        }
        // Tools overlay the image rather than reducing its fit/navigation area.
        const float previewColumnWidth = editingWidth;
        const bool lowerShelfRequested =
            showEditingSurface &&
            m_RawWorkspaceLabUi.lowerShelfOpen;
        const float lowerShelfTargetHeight = lowerShelfRequested
            ? kRawLabFloatingScopesHeight
            : 0.0f;
        m_RawWorkspaceLabAnimatedLowerShelfHeight =
            AnimateRawLabDrawerHeight(
                m_RawWorkspaceLabAnimatedLowerShelfHeight,
                lowerShelfTargetHeight,
                ImGui::GetIO().DeltaTime);
        const auto renderPreviewColumn = [&]() {
            // The image continues beneath the UI through the compositor.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(0.0f, 0.0f));
            ImGui::BeginChild(
                "RawLabPreviewColumn",
                ImVec2(previewColumnWidth, editingHeight),
                false,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(
                    kRawLabColumnHorizontalInset,
                    kRawLabColumnVerticalInset));
            ImGui::BeginChild(
                "RawLabPreviewRegion",
                ImVec2(0.0f, previewDisplayHeight),
                false,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);
            RenderRawWorkspaceLabPreview(selectedSource);
            if (filmstripHoverVisible) {
                RenderRawWorkspaceLabFilmstripPreviewOverlay();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };
        if (m_RawWorkspaceLabUi.galleryWorkspaceOpen) {
            ImGui::BeginChild("RawGalleryPreviewColumn",
                ImVec2(editingWidth, editingHeight), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            RenderRawWorkspaceLabGalleryWorkspace(
                ImVec2(editingWidth, previewDisplayHeight));
            ImGui::EndChild();
        } else {
            if (m_RawWorkspaceLabEditingSurfaceReveal > .005f)
                RenderRawFloatingControls(context, editingMinimum,
                    ImVec2(editingWidth, previewDisplayHeight), showEditingSurface);
            // Canvas tools also poll mouse/key input directly. A tool gesture
            // must not simultaneously pan, zoom, paint or select on the image.
            Stack::Workspace::InputScope toolInput(RawFloatingSurfaceOwnsPointer(), false);
            renderPreviewColumn();
        }
        autoBracketInput.reset();
        if (showFilmstrip) {
            constexpr float kFilmstripNavigationWidth = 136.0f;
            constexpr float kFilmstripNavigationGap = 12.0f;
            constexpr float kFilmstripBottomPadding = 8.0f;
            const auto renderFilmstripScroll = [&](bool expandedDrawer) {
                constexpr float kTimelineHeight = 54.0f;
                const float scrollHeight = std::max(
                    1.0f, ImGui::GetContentRegionAvail().y - kTimelineHeight);
                ImGui::BeginChild(
                    "RawLabFilmstripScroll",
                    ImVec2(0.0f, scrollHeight),
                    false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
                const float tileHeight =
                    kRawLabFilmstripTileHeight *
                    m_RawWorkspaceLabUi.galleryThumbnailScale;
                const float thumbnailOffset = std::max(
                    0.0f,
                    ImGui::GetContentRegionAvail().y -
                        tileHeight - kFilmstripBottomPadding);
                ImGui::SetCursorPosY(
                    ImGui::GetCursorPosY() + thumbnailOffset);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha *
                    m_RawWorkspaceGalleryLayoutAnimation.ContentAlpha());
                const bool layoutAnimating = m_RawWorkspaceGalleryLayoutAnimation.Active();
                if (layoutAnimating) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                RenderRawWorkspaceLabGalleryContent(
                    true,
                    expandedDrawer,
                    expandedDrawer ? drawerExpansionProgress : 0.0f);
                if (layoutAnimating) ImGui::PopItemFlag();
                ImGui::PopStyleVar();
                const float scrollX = ImGui::GetScrollX();
                const float scrollMaxX = ImGui::GetScrollMaxX();
                const float viewportWidth = ImGui::GetWindowSize().x;
                ImGui::EndChild();
                RenderRawWorkspaceLabFilmstripTimeline(
                    scrollX, scrollMaxX, viewportWidth);
            };
            const auto renderFilmstripBody = [&](bool expandedDrawer) {
                const ImVec2 expandedContentMinimum =
                    ImGui::GetCursorScreenPos();
                if ((!expandedDrawer || drawerExpansionProgress <= 0.001f) &&
                    filmstripHeight > 20.0f) {
                    const float filmstripSplitterHeight = std::min(
                        8.0f,
                        std::max(0.0f, ImGui::GetContentRegionAvail().y));
                    if (filmstripSplitterHeight > 0.5f) {
                        ImGui::InvisibleButton(
                            "##RawLabFilmstripHeightSplitter",
                            ImVec2(
                                ImGui::GetContentRegionAvail().x,
                                filmstripSplitterHeight));
                        const bool filmstripSplitterHovered =
                            ImGui::IsItemHovered();
                        const bool filmstripSplitterActive =
                            ImGui::IsItemActive();
                        if (filmstripSplitterHovered ||
                            filmstripSplitterActive) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                        }
                        const bool filmstripAnimationSettled =
                            std::abs(
                                m_RawWorkspaceLabAnimatedFilmstripHeight -
                                filmstripTargetHeight) <= 0.5f;
                        if (filmstripSplitterActive &&
                            filmstripAnimationSettled) {
                            const float previous =
                                m_RawWorkspaceLabUi.filmstripHeight;
                            m_RawWorkspaceLabUi.filmstripHeight = std::clamp(
                                previous - ImGui::GetIO().MouseDelta.y,
                                kRawLabFilmstripMinimumHeight,
                                kRawLabFilmstripMaximumHeight);
                            if (std::abs(
                                    previous -
                                    m_RawWorkspaceLabUi.filmstripHeight) >
                                0.01f) {
                                m_RawWorkspaceLabFilmstripResizeDirty = true;
                            }
                        }
                        if (ImGui::IsItemDeactivated() &&
                            m_RawWorkspaceLabFilmstripResizeDirty) {
                            m_RawWorkspaceLabFilmstripResizeDirty = false;
                            SaveRawWorkspaceAppState();
                        }
                    }
                }
                if (galleryBrowser) {
                    if (expandedDrawer) ImGui::SetCursorScreenPos(expandedContentMinimum);
                    renderFilmstripScroll(expandedDrawer);
                    return false;
                }
                if (!expandedDrawer) {
                    ImGui::BeginChild(
                        "RawLabFilmstripNavigation",
                        ImVec2(kFilmstripNavigationWidth, 0.0f),
                        false,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
                    const bool workspaceChanged =
                        RenderRawWorkspaceLabGalleryHeader(false);
                    ImGui::EndChild();
                    ImGui::SameLine(0.0f, kFilmstripNavigationGap);
                    renderFilmstripScroll(false);
                    return workspaceChanged;
                }

                // The splitter is still available while the persistent host
                // is collapsed, but expanded layout is positioned from the
                // original content origin so it remains pixel-identical
                // throughout the drawer transition.
                ImGui::SetCursorScreenPos(expandedContentMinimum);

                const ImVec2 contentMinimum = ImGui::GetCursorScreenPos();
                const ImVec2 contentSize = ImGui::GetContentRegionAvail();
                const float navigationHeight = std::min(
                    contentSize.y,
                    std::max(
                        1.0f,
                        filmstripHeight - 16.0f -
                            ImGui::GetStyle().ItemSpacing.y));
                ImGui::SetCursorScreenPos(ImVec2(
                    contentMinimum.x,
                    contentMinimum.y + contentSize.y - navigationHeight));
                ImGui::BeginChild(
                    "RawLabFilmstripNavigation",
                    ImVec2(kFilmstripNavigationWidth, navigationHeight),
                    false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
                const bool workspaceChanged =
                    RenderRawWorkspaceLabGalleryHeader(false);
                ImGui::EndChild();

                ImGui::SetCursorScreenPos(ImVec2(
                    contentMinimum.x + kFilmstripNavigationWidth +
                        kFilmstripNavigationGap,
                    contentMinimum.y));
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(0.0f, 0.0f));
                ImGui::BeginChild(
                    "RawLabFilmstripExpandedScrollHost",
                    ImVec2(
                        std::max(
                            1.0f,
                            contentSize.x - kFilmstripNavigationWidth -
                                kFilmstripNavigationGap),
                        contentSize.y),
                    false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::PopStyleVar();
                renderFilmstripScroll(true);
                ImGui::EndChild();
                ImGui::SetCursorScreenPos(ImVec2(
                    contentMinimum.x,
                    contentMinimum.y + contentSize.y));
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
                return workspaceChanged;
            };

            const ImVec2 filmstripMinimum = ImGui::GetCursorScreenPos();
            const ImVec2 filmstripSize(
                std::max(1.0f, ImGui::GetContentRegionAvail().x),
                filmstripHeight);
            bool workspaceChanged = false;
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(16.0f, 4.0f));
            ImGui::BeginChild(
                "RawLabFilmstrip",
                filmstripSize,
                false,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);
            if (!showFilmstripDrawerOverlay) {
                workspaceChanged = renderFilmstripBody(false);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImVec2 activeDrawerMinimum = filmstripMinimum;
            ImVec2 activeDrawerMaximum(
                filmstripMinimum.x + filmstripSize.x,
                filmstripMinimum.y + filmstripSize.y);
            if (showFilmstripDrawerOverlay) {
                const float drawerHeight = std::clamp(
                    m_RawWorkspaceLabFilmstripDrawerAnimatedHeight,
                    filmstripHeight,
                    totalHeight);
                activeDrawerMinimum.y = activeDrawerMaximum.y - drawerHeight;
                const float drawerLeftExtension = std::max(
                    0.0f,
                    activeDrawerMinimum.x - rawLabRootLeft);
                activeDrawerMinimum.x = rawLabRootLeft;
                const ImVec4 drawerSurface = MixRawLabDrawerColor(
                    ImGui::GetStyleColorVec4(ImGuiCol_ChildBg),
                    GetWorkspaceBaseColor(),
                    drawerExpansionProgress);
                const ImGuiViewport* drawerViewport =
                    ImGui::GetWindowViewport();
                ImGui::SetNextWindowViewport(drawerViewport->ID);
                ImGui::SetNextWindowPos(
                    activeDrawerMinimum,
                    ImGuiCond_Always);
                ImGui::SetNextWindowSize(
                    ImVec2(
                        activeDrawerMaximum.x - activeDrawerMinimum.x,
                        drawerHeight),
                    ImGuiCond_Always);
                ImGui::PushStyleColor(
                    ImGuiCol_WindowBg,
                    drawerSurface);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                const ImGuiWindowFlags drawerWindowFlags =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNav |
                    ImGuiWindowFlags_NoDocking;
                ImGui::Begin(
                    "RawLabFilmstripDrawerOverlay",
                    nullptr,
                    drawerWindowFlags);
                // Keep the drawer above the workspace, but stop promoting it
                // once one of its menus is open. Re-promoting this window on
                // every frame places it in front of an existing popup after
                // the popup's appearing frame.
                if (!ImGui::IsPopupOpen(
                        nullptr,
                        ImGuiPopupFlags_AnyPopupId)) {
                    ImGui::BringWindowToDisplayFront(
                        ImGui::GetCurrentWindow());
                }
                ImGui::SetCursorPos(ImVec2(drawerLeftExtension, 0.0f));
                ImGui::PushStyleColor(
                    ImGuiCol_ChildBg,
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(16.0f, 4.0f));
                ImGui::BeginChild(
                    "RawLabFilmstripDrawerContent",
                    ImVec2(filmstripSize.x, drawerHeight),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
                workspaceChanged = renderFilmstripBody(true);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::End();
                ImGui::PopStyleVar(3);
                ImGui::PopStyleColor();
            }

            m_RawWorkspaceLabFilmstripDrawerPointerInside =
                filmstripRequested &&
                ImGui::IsMouseHoveringRect(
                    activeDrawerMinimum,
                    activeDrawerMaximum,
                    false);
            m_RawWorkspaceLabFilmstripDrawerInteractionRetained =
                showFilmstripDrawerOverlay &&
                (ImGui::IsAnyItemActive() ||
                 ImGui::IsPopupOpen(
                     nullptr,
                     ImGuiPopupFlags_AnyPopupId));
            if (workspaceChanged) {
                ImGui::EndChild(); // RawModuleWorkspace
                if (colorWorkspaceBackdrop) {
                    ImGui::PopStyleColor();
                }
                if (galleryBrowser) {
                    ImGui::EndChild(); // RawGalleryBody
                    ImGui::PopStyleVar();
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                return;
            }
        }

    }

    ImGui::EndChild(); // RawModuleWorkspace

    if (galleryBrowser) RenderRawWorkspaceGalleryLayoutTransition();

    // Multi-frame project creation can be requested from the Gallery window,
    // the editing rail, or the empty-workspace prompt. Render its
    // modal once at this stable root scope so every request uses the same
    // ImGui popup ID and the gallery workflow cannot strand the request.
    bool preparedCaptureSetDialogThisFrame = false;
    if (!Stack::Workspace::IsPreview() && m_RequestCreateMultiFrameFromGallerySelection) {
        m_RequestCreateMultiFrameFromGallerySelection = false;
        try {
            preparedCaptureSetDialogThisFrame =
                RequestCreateMfdProjectFromGallerySelection();
        } catch (const std::exception& exception) {
            m_RawWorkspaceLabUi.multiFrameStatusText =
                std::string("Could not prepare the capture-set dialog: ") +
                exception.what();
        } catch (...) {
            m_RawWorkspaceLabUi.multiFrameStatusText =
                "Could not prepare the capture-set dialog.";
        }
    }
    // Opening and populating a modal after the gallery has changed selection,
    // navigation, and tab state in the same ImGui frame can violate popup/ID
    // stack invariants. Cross that boundary on the next frame instead.
    if (!Stack::Workspace::IsPreview() && !preparedCaptureSetDialogThisFrame) {
        RenderMultiFrameRawLabCreationPopup();
    }
    if (!Stack::Workspace::IsPreview()) {
        RenderRawEditAttributePasteDialog();
        RenderRawWorkspaceLifecyclePopups();
    }
    (void)thumbnailSnapshot;
    if (colorWorkspaceBackdrop) {
        ImGui::PopStyleColor();
    }
    if (galleryBrowser) {
        ImGui::EndChild(); // RawGalleryBody
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}
