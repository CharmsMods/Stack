#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"

#include <imgui.h>

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
    m_Pipeline.Clear();
    m_CompositePreviewPipeline.Clear();
    SetCurrentProjectName("");
    SetCurrentProjectFileName("");
    MarkRenderDirty();
    ClearDirty();
    m_LastUserActionTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_LastAutoSaveTime = -1.0;
}

void EditorModule::ResetRenderSubmissionState() {
    ++m_RenderGeneration;
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
    m_HoverFade = 0.0f;
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
            LibraryManager::Get().RequestSaveProject(projectName, this, m_CurrentProjectFileName);
            ResetToBlankProject();
            ImGui::CloseCurrentPopup();
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

