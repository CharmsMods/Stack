#include "AppShell.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

constexpr int RootTabEditor = 1;
constexpr int RootTabLibrary = 0;
constexpr double kLibraryLoadFadeOutSeconds = 0.48;
constexpr double kLibraryLoadSpinnerFadeInSeconds = 0.26;
constexpr double kLibraryLoadSpinnerMinVisibleSeconds = 0.85;
constexpr double kLibraryLoadSpinnerFadeOutSeconds = 0.32;
constexpr double kLibraryLoadEditorRevealSeconds = 1.90;
constexpr bool kLibraryLoadTransitionDiagnostics = false;

} // namespace

void AppShell::BeginLibraryToEditorProjectLoad(const std::string& projectFileName) {
    if (!m_Editor || projectFileName.empty()) return;
    if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId &&
        !ActivateProjectWorkspace(CreateProjectWorkspace())) return;
    if (auto* workspace = FindProjectWorkspace(m_ActiveProjectWorkspace))
        BeginLibraryToEditorProjectLoad(*workspace, projectFileName);
}

void AppShell::BeginLibraryToEditorProjectLoad(ProjectWorkspace& workspace, const std::string& projectFileName) {
    auto& transition = workspace.loadTransition;
    auto* editor = workspace.editor.get();
    if (editor->RequestAutoBracketForeground("open a project", [this, id = workspace.id, projectFileName] {
            if (auto* owner = FindProjectWorkspace(id)) BeginLibraryToEditorProjectLoad(*owner, projectFileName);
        })) return;
    if (projectFileName.empty() ||
        m_ProjectLoadSavePending ||
        transition.phase != LibraryToEditorProjectLoadPhase::None ||
        Async::IsBusy(editor->GetProjectLoadTaskState())) {
        return;
    }

    if (!editor->FinishWorkspaceInteraction()) return;
    if (!m_ProjectLoadCurrentProjectDispositionApproved &&
        (editor->NeedsWorkspaceSaveBeforeTransition() || editor->IsProjectFileSaveBusy() ||
         editor->IsRawWorkspaceProjectSaveBusy())) {
        m_ProjectLoadSavePending = true;
        editor->RequestSaveWorkspaceBeforeClose([this, projectFileName, id = workspace.id](bool success) {
            m_ProjectLoadSavePending = false;
            auto* owner = FindProjectWorkspace(id);
            if (success && owner) {
                m_ProjectLoadCurrentProjectDispositionApproved = true;
                BeginLibraryToEditorProjectLoad(*owner, projectFileName);
            } else {
                if (owner) ReportSaveFailure(*owner->editor, "The save failed, so the other project was not opened.",
                    [this, id, projectFileName] {
                        if (auto* workspace = FindProjectWorkspace(id)) {
                            m_ProjectLoadCurrentProjectDispositionApproved = true;
                            BeginLibraryToEditorProjectLoad(*workspace,projectFileName);
                        }
                    });
            }
        });
        return;
    }
    m_ProjectLoadCurrentProjectDispositionApproved = false;
    transition.Reset();
    transition.ownerLease = editor->ProjectTasks().Retain();
    if (workspace.id == m_ActiveProjectWorkspace) {
        m_RootTabBodyFadeActive = false;
        m_RootTabBodyFadeStartedAt = 0.0;
        m_RootTabBodyFadeFromTab = -1;
        m_RootTabBodyFadeToTab = -1;
        m_RootTabBodyFadeQueuedTab = -1;
        m_RootTabBodyFadeCommitted = false;
    }
    transition.projectFileName = projectFileName;
    transition.decodeReady = false;
    transition.decodeSucceeded = false;
    transition.applySucceeded = false;
    transition.dismissLibraryPreviewsPending = true;
    transition.loadRequested = false;
    transition.firstRenderReady = false;
    transition.nodeBrowserThumbnailsReady = false;
    transition.startedAt = ImGui::GetTime();
    transition.decodeRequestedAt = 0.0;
    transition.decodeReadyAt = 0.0;
    transition.applyStartedAt = 0.0;
    transition.applyFinishedAt = 0.0;
    transition.firstRenderReadyAt = 0.0;
    transition.thumbnailsReadyAt = 0.0;
    transition.readyToRevealAt = 0.0;
    transition.decodedProject.reset();
    transition.trace.clear();
    TraceLibraryLoadTransition(workspace, "load clicked");
    SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::SpinnerFadeIn);
}

void AppShell::RequestDeferredLibraryProjectLoad(ProjectWorkspace& workspace) {
    auto& transition = workspace.loadTransition;
    auto* editor = workspace.editor.get();
    if (!editor || transition.loadRequested || transition.projectFileName.empty()) {
        return;
    }

    transition.loadRequested = true;
    transition.decodeRequestedAt = ImGui::GetTime();
    TraceLibraryLoadTransition(workspace, "decode requested");
    LibraryManager::Get().RequestLoadProjectDeferredApply(
        transition.projectFileName,
        editor,
        [this, id = workspace.id, generation = transition.generation](bool success, std::shared_ptr<EditorLoadedProjectData> decodedProject) {
            auto* workspace = FindProjectWorkspace(id);
            if (!workspace || !workspace->loadTransition.AcceptsCompletion(generation)) return;
            auto& transition = workspace->loadTransition;
            transition.decodeReady = true;
            transition.decodeSucceeded = success;
            transition.decodedProject = std::move(decodedProject);
            transition.decodeReadyAt = ImGui::GetTime();
            TraceLibraryLoadTransition(*workspace, success ? "decode ready" : "decode failed");
        });
}

void AppShell::SetLibraryToEditorProjectLoadPhase(ProjectWorkspace& workspace, LibraryToEditorProjectLoadPhase phase) {
    auto& transition = workspace.loadTransition;
    if (transition.phase == phase && phase != LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    auto phaseName = [](LibraryToEditorProjectLoadPhase value) {
        switch (value) {
        case LibraryToEditorProjectLoadPhase::LibraryFadeOut: return "LibraryFadeOut";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: return "SpinnerFadeIn";
        case LibraryToEditorProjectLoadPhase::WaitForEditorReady: return "WaitForEditorReady";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeOut: return "SpinnerFadeOut";
        case LibraryToEditorProjectLoadPhase::EditorReveal: return "EditorReveal";
        case LibraryToEditorProjectLoadPhase::None: default: return "None";
        }
    };

    transition.phase = phase;
    if (phase == LibraryToEditorProjectLoadPhase::None) {
        transition.ownerLease.reset();
    }
    transition.phaseStartTime = ImGui::GetTime();
    transition.phasePresentedFrames = 0;
    if (phase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn) {
        transition.spinnerStartTime = transition.phaseStartTime;
    }

    std::string event = "phase ";
    event += phaseName(phase);
    TraceLibraryLoadTransition(workspace, event);
}

void AppShell::TickLibraryToEditorProjectLoadTransition() {
    for (auto& workspace : m_ProjectWorkspaces)
        TickLibraryToEditorProjectLoadTransition(*workspace);
}

void AppShell::TickLibraryToEditorProjectLoadTransition(ProjectWorkspace& workspace) {
    auto& transition = workspace.loadTransition;
    auto* editor = workspace.editor.get();
    const bool foreground = workspace.id == m_ActiveProjectWorkspace;
    if (transition.phase == LibraryToEditorProjectLoadPhase::None || !editor) {
        return;
    }

    editor->PumpNonRenderingWork(foreground ? 2.5 : 0.5, foreground);
    if (!foreground && transition.phase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn &&
        !transition.loadRequested) RequestDeferredLibraryProjectLoad(workspace);

    const double now = ImGui::GetTime();
    const double elapsed = now - transition.phaseStartTime;
    auto finishToLibrary = [&]() {
        SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::None);
        transition.projectFileName.clear();
        transition.decodedProject.reset();
        transition.decodeReady = false;
        transition.decodeSucceeded = false;
        transition.applySucceeded = false;
        transition.dismissLibraryPreviewsPending = false;
        transition.loadRequested = false;
        transition.firstRenderReady = false;
        transition.nodeBrowserThumbnailsReady = false;
        workspace.rootTab = RootTabLibrary;
        if (foreground) {
            OnTabChanged(m_CurrentTabId, RootTabLibrary);
            m_CurrentTabId = RootTabLibrary;
            m_LibraryWindowOpen = false;
            m_LibraryWindowFocusRequested = false;
        }
    };

    if (transition.dismissLibraryPreviewsPending &&
        transition.phase != LibraryToEditorProjectLoadPhase::LibraryFadeOut) {
        if (foreground) m_Library.DismissPreviewsForProjectLoad();
        transition.dismissLibraryPreviewsPending = false;
    }

    switch (transition.phase) {
    case LibraryToEditorProjectLoadPhase::LibraryFadeOut:
        if (elapsed >= kLibraryLoadFadeOutSeconds) {
            SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::SpinnerFadeIn);
        }
        break;
    case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: {
        const bool minimumSpinnerShown = transition.spinnerStartTime > 0.0 &&
            (now - transition.spinnerStartTime) >= kLibraryLoadSpinnerMinVisibleSeconds;
        if (transition.decodeReady && !transition.decodeSucceeded && elapsed >= kLibraryLoadSpinnerFadeInSeconds && minimumSpinnerShown) {
            SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
        } else if (transition.decodeReady && elapsed >= kLibraryLoadSpinnerFadeInSeconds && minimumSpinnerShown) {
            transition.applyStartedAt = ImGui::GetTime();
            TraceLibraryLoadTransition(workspace, "apply start");
            if (!editor->BeginDeferredLoadedProjectApply(transition.decodedProject)) {
                transition.applySucceeded = false;
                transition.applyFinishedAt = ImGui::GetTime();
                TraceLibraryLoadTransition(workspace, "apply start failed");
                LibraryManager::Get().FinishDeferredProjectLoad(editor,
                    false,
                    editor->GetDeferredLoadedProjectStatusText().empty()
                        ? "Failed to apply the loaded project."
                        : editor->GetDeferredLoadedProjectStatusText());
                SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
                break;
            }
            LibraryManager::Get().SetProjectLoadApplyingStatus(editor, "Applying editor state...");
            SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::WaitForEditorReady);
        }
        break;
    }
    case LibraryToEditorProjectLoadPhase::WaitForEditorReady: {
        if (transition.applyFinishedAt <= 0.0 && editor->HasDeferredLoadedProjectApplyCoreFinished()) {
            transition.applyFinishedAt = now;
            TraceLibraryLoadTransition(workspace, "apply state finished");
        }

        const bool firstRenderReady = editor->HasDeferredLoadedProjectFirstRenderReady();
        if (!transition.firstRenderReady && firstRenderReady) {
            transition.firstRenderReady = true;
            transition.firstRenderReadyAt = now;
            TraceLibraryLoadTransition(workspace, "first render ready");
        }

        const bool thumbnailsReady =
            editor->HasDeferredLoadedProjectApplyCoreFinished() &&
            editor->GetPendingNodeBrowserThumbnailWarmCount() == 0 &&
            editor->GetPendingNodeBrowserThumbnailGenerationCount() == 0;
        if (!transition.nodeBrowserThumbnailsReady && thumbnailsReady) {
            transition.nodeBrowserThumbnailsReady = true;
            transition.thumbnailsReadyAt = now;
            TraceLibraryLoadTransition(workspace, "node browser thumbnails ready");
        }

        if (editor->HasDeferredLoadedProjectApplyFailed()) {
            transition.applySucceeded = false;
            if (transition.applyFinishedAt <= 0.0) {
                transition.applyFinishedAt = ImGui::GetTime();
            }
            TraceLibraryLoadTransition(workspace, "apply end failed");
            LibraryManager::Get().FinishDeferredProjectLoad(editor,
                false,
                editor->GetDeferredLoadedProjectStatusText().empty()
                    ? "Failed to apply the loaded project."
                    : editor->GetDeferredLoadedProjectStatusText());
            SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
            break;
        }

        const std::string statusText = editor->GetDeferredLoadedProjectStatusText();
        if (!statusText.empty()) {
            LibraryManager::Get().SetProjectLoadApplyingStatus(editor, statusText);
        }

        const bool readyForReveal =
            editor->HasDeferredLoadedProjectApplyCoreFinished() &&
            firstRenderReady;
        if (readyForReveal) {
            transition.applySucceeded = true;
            if (transition.applyFinishedAt <= 0.0) {
                transition.applyFinishedAt = ImGui::GetTime();
            }
            transition.readyToRevealAt = ImGui::GetTime();
            TraceLibraryLoadTransition(workspace, "apply end ok");
            TraceLibraryLoadTransition(workspace, "ready to reveal");
            LibraryManager::Get().FinishDeferredProjectLoad(editor, true, "Project loaded into the editor.");
            SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::SpinnerFadeOut);
        }
        break;
    }
    case LibraryToEditorProjectLoadPhase::SpinnerFadeOut:
        if (elapsed >= kLibraryLoadSpinnerFadeOutSeconds) {
            if (transition.applySucceeded) {
                workspace.rootTab = RootTabEditor;
                if (foreground) {
                    OnTabChanged(m_CurrentTabId, RootTabEditor);
                    m_CurrentTabId = RootTabEditor;
                    editor->BeginLibraryLoadReveal();
                }
                TraceLibraryLoadTransition(workspace, "editor reveal start");
                SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::EditorReveal);
            } else {
                finishToLibrary();
            }
        }
        break;
    case LibraryToEditorProjectLoadPhase::EditorReveal:
        if (elapsed >= kLibraryLoadEditorRevealSeconds) {
            SetLibraryToEditorProjectLoadPhase(workspace, LibraryToEditorProjectLoadPhase::None);
            transition.projectFileName.clear();
            transition.decodeReady = false;
            transition.decodeSucceeded = false;
            transition.applySucceeded = false;
            transition.dismissLibraryPreviewsPending = false;
            transition.loadRequested = false;
            transition.decodedProject.reset();
            transition.firstRenderReady = false;
            transition.nodeBrowserThumbnailsReady = false;
        }
        break;
    case LibraryToEditorProjectLoadPhase::None:
        break;
    }
}

void AppShell::OnFramePresented() {
    auto* owner = FindProjectWorkspace(m_ActiveProjectWorkspace);
    if (!owner) return;
    auto& workspace = *owner;
    auto& transition = workspace.loadTransition;
    if (transition.phase == LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    auto phaseName = [](LibraryToEditorProjectLoadPhase value) {
        switch (value) {
        case LibraryToEditorProjectLoadPhase::LibraryFadeOut: return "LibraryFadeOut";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: return "SpinnerFadeIn";
        case LibraryToEditorProjectLoadPhase::WaitForEditorReady: return "WaitForEditorReady";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeOut: return "SpinnerFadeOut";
        case LibraryToEditorProjectLoadPhase::EditorReveal: return "EditorReveal";
        case LibraryToEditorProjectLoadPhase::None: default: return "None";
        }
    };

    ++transition.phasePresentedFrames;
    if (transition.phasePresentedFrames == 1) {
        std::string event = "first presented ";
        event += phaseName(transition.phase);
        TraceLibraryLoadTransition(workspace, event);
    }

    if (transition.phase == LibraryToEditorProjectLoadPhase::SpinnerFadeIn &&
        transition.phasePresentedFrames >= 1 &&
        !transition.loadRequested) {
        RequestDeferredLibraryProjectLoad(workspace);
    }
}

void AppShell::RenderLibraryLoadTransitionDiagnostics() {
    auto* workspace = FindProjectWorkspace(m_ActiveProjectWorkspace);
    if (!workspace) return;
    auto& transition = workspace->loadTransition;
    auto* editor = workspace->editor.get();
    if (!kLibraryLoadTransitionDiagnostics ||
        transition.phase == LibraryToEditorProjectLoadPhase::None) {
        return;
    }

    auto phaseName = [](LibraryToEditorProjectLoadPhase value) {
        switch (value) {
        case LibraryToEditorProjectLoadPhase::LibraryFadeOut: return "LibraryFadeOut";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeIn: return "SpinnerFadeIn";
        case LibraryToEditorProjectLoadPhase::WaitForEditorReady: return "WaitForEditorReady";
        case LibraryToEditorProjectLoadPhase::SpinnerFadeOut: return "SpinnerFadeOut";
        case LibraryToEditorProjectLoadPhase::EditorReveal: return "EditorReveal";
        case LibraryToEditorProjectLoadPhase::None: default: return "None";
        }
    };

    const double now = ImGui::GetTime();
    const double elapsed = transition.startedAt > 0.0 ? now - transition.startedAt : 0.0;
    const double decodeMs = (transition.decodeRequestedAt > 0.0 && transition.decodeReadyAt > 0.0)
        ? (transition.decodeReadyAt - transition.decodeRequestedAt) * 1000.0
        : 0.0;
    const double applyMs = (transition.applyStartedAt > 0.0 && transition.applyFinishedAt > 0.0)
        ? (transition.applyFinishedAt - transition.applyStartedAt) * 1000.0
        : 0.0;
    const double firstRenderMs = (transition.applyStartedAt > 0.0 && transition.firstRenderReadyAt > 0.0)
        ? (transition.firstRenderReadyAt - transition.applyStartedAt) * 1000.0
        : 0.0;
    const double thumbnailReadyMs = (transition.applyStartedAt > 0.0 && transition.thumbnailsReadyAt > 0.0)
        ? (transition.thumbnailsReadyAt - transition.applyStartedAt) * 1000.0
        : 0.0;
    const double revealReadyMs = (transition.startedAt > 0.0 && transition.readyToRevealAt > 0.0)
        ? (transition.readyToRevealAt - transition.startedAt) * 1000.0
        : 0.0;

    const ImVec2 basePos = ImGui::GetWindowPos();
    ImGui::SetCursorScreenPos(ImVec2(basePos.x + 12.0f, basePos.y + 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(8, 28, 34, 210));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(112, 190, 176, 110));
    ImGui::BeginChild("LibraryLoadTransitionDiagnostics", ImVec2(380.0f, 232.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    ImGui::Text("Library -> Editor load");
    ImGui::Text("Phase: %s  frames: %d", phaseName(transition.phase), transition.phasePresentedFrames);
    ImGui::Text("Editor phase: %s", editor->GetDeferredLoadedProjectPhaseLabel());
    ImGui::Text("Elapsed: %.2fs", elapsed);
    ImGui::Text("Decode: requested=%s ready=%s ok=%s %.1fms",
        transition.loadRequested ? "yes" : "no",
        transition.decodeReady ? "yes" : "no",
        transition.decodeSucceeded ? "yes" : "no",
        decodeMs);
    ImGui::Text("Apply: started=%s ok=%s %.1fms",
        transition.applyStartedAt > 0.0 ? "yes" : "no",
        transition.applySucceeded ? "yes" : "no",
        applyMs);
    ImGui::Text("First render: ready=%s %.1fms", transition.firstRenderReady ? "yes" : "no", firstRenderMs);
    ImGui::Text(
        "Thumbs: warm=%zu pending=%zu ready=%s %.1fms",
        editor->GetPendingNodeBrowserThumbnailWarmCount(),
        editor->GetPendingNodeBrowserThumbnailGenerationCount(),
        transition.nodeBrowserThumbnailsReady ? "yes" : "no",
        thumbnailReadyMs);
    ImGui::Text("Ready to reveal: %s %.1fms",
        transition.readyToRevealAt > 0.0 ? "yes" : "no",
        revealReadyMs);
    if (!editor->GetProjectLoadStatusText().empty()) {
        ImGui::TextWrapped("Status: %s", editor->GetProjectLoadStatusText().c_str());
    }
    ImGui::Separator();
    const int firstTrace = std::max(0, static_cast<int>(transition.trace.size()) - 5);
    for (int i = firstTrace; i < static_cast<int>(transition.trace.size()); ++i) {
        ImGui::TextDisabled("%s", transition.trace[static_cast<std::size_t>(i)].c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void AppShell::TraceLibraryLoadTransition(ProjectWorkspace& workspace, const std::string& event) {
    auto& transition = workspace.loadTransition;
    if (!kLibraryLoadTransitionDiagnostics) {
        return;
    }

    const double now = ImGui::GetTime();
    const double elapsed = transition.startedAt > 0.0 ? now - transition.startedAt : 0.0;
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "%.3fs  %s", elapsed, event.c_str());
    transition.trace.emplace_back(buffer);
    if (transition.trace.size() > 12) {
        transition.trace.erase(transition.trace.begin());
    }
}
