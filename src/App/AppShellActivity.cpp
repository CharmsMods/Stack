#include "AppShell.h"
#include "AppHeaderStyle.h"
#include "GlassNavigationFeedback.h"
#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include <algorithm>
#include <limits>
#include <unordered_set>

Stack::UiActivity::Snapshot AppShell::CollectActivity() const {
    Stack::UiActivity::Snapshot snapshot;
    const auto collectWorkspace = [&](const ProjectWorkspace& workspace) {
        const auto& owner = workspace.editor->GetNotifier().GetOwner();
        const auto name = workspace.editor->GetCurrentProjectName();
        snapshot.SetOwner(owner.id, workspace.id == m_GalleryWorkspaceId ? "Gallery" : name.empty() ? "Untitled" : name);
        workspace.editor->CollectActivity(snapshot);
        const bool loadRepresented = std::any_of(snapshot.entries.begin(),snapshot.entries.end(),[&](const auto& entry) {
            return entry.ownerId == owner.id && entry.label == "Loading project";
        });
        snapshot.AddOperation(!loadRepresented && workspace.loadTransition.phase != LibraryToEditorProjectLoadPhase::None,
            "project-reveal", "Loading project");
    };
    if (const auto* current = FindProjectWorkspace(m_ActiveProjectWorkspace)) collectWorkspace(*current);
    for (const auto& workspace : m_ProjectWorkspaces)
        if (workspace->id != m_ActiveProjectWorkspace) collectWorkspace(*workspace);
    const auto& queue = m_QueueRenderer.GetNotifier().GetOwner();
    snapshot.SetOwner(queue.id, queue.label);
    snapshot.AddOperation(m_QueueRenderer.IsBusy(), "queue-export", "Exporting", 0, m_QueueRenderer.ExportOperationId());
    const auto& inspection = m_QueueRenderer.GetInspectionNotifier().GetOwner();
    snapshot.SetOwner(inspection.id, inspection.label);
    snapshot.AddOperation(m_QueueRenderer.IsInspectionBusy(), "inspection", "Full resolution", 0, m_QueueRenderer.InspectionOperationId());
    const auto& library = LibraryManager::Get().GetNotifier().GetOwner();
    snapshot.SetOwner(library.id, library.label);
    LibraryManager::Get().CollectActivity(snapshot);
    const auto& app = m_AppNotifier.GetOwner();
    snapshot.SetOwner(app.id, app.label, true);
    snapshot.AddOperation(m_BackgroundImageDecodeState == BackgroundImageDecodeState::Queued ||
        m_BackgroundImageDecodeState == BackgroundImageDecodeState::Decoding ||
        m_BackgroundImageDecodeState == BackgroundImageDecodeState::Ready, "background-image", "Loading background");
    if (m_UpdateManager) {
        const auto state = m_UpdateManager->GetSnapshot().state;
        snapshot.AddOperation(state == AppUpdate::UpdateState::Checking || state == AppUpdate::UpdateState::Downloading ||
            state == AppUpdate::UpdateState::Verifying || state == AppUpdate::UpdateState::Installing, "app-update",
            state == AppUpdate::UpdateState::Checking ? "Checking updates" : state == AppUpdate::UpdateState::Downloading ?
            "Downloading update" : state == AppUpdate::UpdateState::Verifying ? "Verifying update" : "Installing update");
    }
    for (const auto& job : Async::TaskSystem::Get().Activities()) {
        const auto ownerId = job.ownerId ? job.ownerId : app.id;
        if (snapshot.IsWorkerLabelSuppressed(job.label, ownerId)) continue;
        // Owner collectors already describe the same stage. Worker tickets cover
        // queued and main-thread handoff work without collapsing other projects.
        if (std::any_of(snapshot.entries.begin(), snapshot.entries.end(), [&](const auto& entry) {
            return entry.ownerId == ownerId && entry.operationId == job.operationId;
        })) continue;
        const auto collected = std::find_if(snapshot.entries.begin(), snapshot.entries.end(), [&](const auto& entry) {
            return entry.ownerId == ownerId && entry.operationId == 0 && entry.label == job.label;
        });
        if (collected != snapshot.entries.end()) {
            collected->operationId = job.operationId;
            collected->id = job.id;
            collected->maintenance = collected->maintenance || job.maintenance;
            continue;
        }
        const auto workspace = FindProjectWorkspace(ownerId);
        if (job.ownerId && !workspace && ownerId != library.id && ownerId != queue.id && ownerId != app.id) continue;
        const auto ownerLabel = workspace ? workspace->editor->GetCurrentProjectName() :
            ownerId == library.id ? library.label : ownerId == queue.id ? queue.label : app.label;
        snapshot.SetOwner(ownerId, ownerLabel.empty() ? "Untitled" : ownerLabel, job.maintenance || !job.ownerId);
        snapshot.AddOperation(true, "worker-" + std::to_string(job.operationId), job.label, job.id, job.operationId);
    }
    if (!snapshot.Busy()) {
        snapshot.SetOwner(app.id, app.label, true);
        snapshot.Add(Async::TaskSystem::Get().HasPendingWork(), "Processing");
    }
    std::stable_sort(snapshot.entries.begin(),snapshot.entries.end(),[&](const auto& a,const auto& b) {
        const auto priority = [&](const auto& entry) { return entry.maintenance ? 2 : entry.ownerId == m_ActiveProjectWorkspace ? 0 : 1; };
        return priority(a) < priority(b);
    });
    if (!snapshot.entries.empty()) {
        snapshot.primaryLabel = snapshot.entries.front().label;
        snapshot.primaryOwnerId = snapshot.entries.front().ownerId;
    }
    return snapshot;
}

float AppShell::RenderActivityIndicator(float width, float height, bool openDetails) {
    const auto palette = Stack::Header::ResolvePalette(m_Appearance ? m_Appearance->GetClearColor()
        : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.pressed);
    auto context=NotificationContext();
    const bool glass=m_Appearance && m_Appearance->GetGlassChromeEnabled();
    if (!glass) m_NavigationRail.activityMotion.Reset();
    bool adaptiveInk=false;
    if (glass) {
        context.customIndicatorFocus=true;
        context.beginIndicator=[&](ImDrawList* draw,ImVec2 min,ImVec2 max,bool selected) {
            const auto glassState=Stack::Navigation::UpdateGlassControlState(m_NavigationRail.activityMotion,selected);
            Stack::Navigation::GlassObjectBacking(draw,min,max,std::min(max.x-min.x,max.y-min.y)*.5f,palette);
            m_RailGlass.AddObject(draw,min,max,palette.caption,std::min(max.x-min.x,max.y-min.y)*.5f,glassState.Elevation(),
                Stack::Renderer::GlassChrome::MaterialRole::NavigationControl);
            adaptiveInk=m_RailGlass.BeginInk(draw,min,max,ImGui::GetItemID());
            Stack::Navigation::DrawGlassControlFeedback(ImRect(min,max),glassState,m_NavigationRail.scale,palette);
            Stack::Navigation::GlassKeyboardFocus(ImRect(min,max),m_NavigationRail.scale,palette);
        };
        context.endIndicator=[&](ImDrawList* draw) { if (adaptiveInk) m_RailGlass.EndInk(draw); };
    }
    const float result = m_NotificationPresenter.RenderIndicator(*m_NotificationStore, CollectActivity(),
        m_ActivityPresentation, width, height, openDetails && !m_NotificationPresenter.BlocksInput(), context);
    ImGui::PopStyleColor(2);
    return result;
}
