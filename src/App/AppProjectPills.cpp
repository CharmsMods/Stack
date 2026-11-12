#include "AppShell.h"
#include "ProjectSidebarEntry.h"
#include "ProjectSidebarMenu.h"
#include "AppHeaderStyle.h"
#include "GlassNavigationFeedback.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <string>

void AppShell::CancelProjectPillPreview() {
    auto& rail = m_NavigationRail;
    rail.suppressedPreview = rail.hoveredProject;
    rail.hoveredProject = rail.previewProject = 0;
    rail.previewRootView = -1;
    rail.previewAmount = 0.f;
    rail.hoverStarted = 0;
}

void AppShell::UpdateProjectPillPreview(std::uint64_t hovered) {
    auto& rail = m_NavigationRail;
    // A pill activates on release, on the next shell tick. Keep its preview
    // throughout the press so that activation can crossfade from what was seen.
    if (hovered && hovered == rail.previewProject && CanSwitchProjectWorkspace() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) && !m_SettingsPopupOpen && !rail.menuOpen &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const bool blocked = !CanSwitchProjectWorkspace() || m_SettingsPopupOpen || rail.menuOpen ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
        ImGui::IsAnyMouseDown() || ImGui::IsAnyItemActive() ||
        glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != GLFW_TRUE;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        rail.suppressedPreview = hovered;
        hovered = 0;
    }
    if (hovered != rail.suppressedPreview && hovered != 0) rail.suppressedPreview = 0;
    if (hovered == 0 && !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) rail.suppressedPreview = 0;
    if (blocked || hovered == rail.suppressedPreview || hovered == m_ActiveProjectWorkspace) hovered = 0;
    if (rail.hoveredProject != hovered) {
        rail.hoveredProject = hovered;
        rail.hoverStarted = ImGui::GetTime();
    }
    const auto* workspace = hovered ? FindProjectWorkspace(hovered) : nullptr;
    const bool ready = workspace && ImGui::GetTime() - rail.hoverStarted >= .065;
    if (ready && rail.previewProject != hovered) {
        rail.previewProject = hovered;
        rail.previewRootView = workspace->rootTab;
        if (!workspace->editor->IsAutoBracketWorkspace() &&
            (rail.previewRootView == static_cast<int>(Stack::Workspace::Id::Queue) ||
             rail.previewRootView == static_cast<int>(Stack::Workspace::Id::Library)))
            rail.previewRootView = static_cast<int>(Stack::Workspace::Id::Graph);
    }
    const bool showing = ready && rail.previewProject == hovered;
    const float target = showing ? 1.f : 0.f;
    if (Stack::Notifications::SystemReducedMotion()) rail.previewAmount = target;
    else Stack::Header::Approach(rail.previewAmount, target, 42.f);
    if (blocked) rail.previewAmount = 0.f;
    if (rail.previewAmount <= .001f && !showing) {
        rail.previewProject = 0;
        rail.previewRootView = -1;
    }
}

void AppShell::RenderProjectPills(const ImVec2& position, const ImVec2& size) {
    auto& rail = m_NavigationRail;
    rail.projectMenuOpen = false;
    ImGui::SetCursorScreenPos(position);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3.f * rail.scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 3.f * rail.scale);
    const bool visible = ImGui::BeginChild("OpenProjectPills", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground);
    if (!visible) {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        UpdateProjectPillPreview(0);
        return;
    }
    const bool enabled = CanSwitchProjectWorkspace();
    std::uint64_t hovered = 0;
    bool activationQueued = false;
    ImGui::BeginDisabled(!enabled);
    for (const auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_GalleryWorkspaceId) continue;
        const auto id = workspace->id;
        if ((m_WorkspaceTabAction==WorkspaceTabAction::Close && m_WorkspaceTabActionId==id) ||
            m_PendingWorkspaceRetirement==id ||
            (m_PendingFileAction==PendingFileAction::CloseWorkspace && m_ActiveProjectWorkspace==id)) continue;
        ImGui::PushID(static_cast<int>(id));
        const bool selected = id == m_ActiveProjectWorkspace;
        const bool dirty = workspace->editor->NeedsWorkspaceSaveBeforeTransition();
        unsigned texture=0; int width=0,height=0;
        auto& editor=*workspace->editor;
        const bool automatic=editor.IsAutoBracketWorkspace();
        const char* name=automatic ? "Auto bracket" : editor.GetCurrentProjectName().empty()
            ? "Untitled project" : editor.GetCurrentProjectName().c_str();
        if (!automatic && !editor.TryGetViewportPreviewOverview(texture,width,height)) {
            Stack::Renderer::ViewportPreviewGeometry geometry;
            const bool partial=editor.TryGetViewportPreviewGeometry(geometry) && geometry.resident.Partial();
            if (!partial && !editor.TryGetActiveRawWorkspacePresentationTexture(texture,width,height) &&
                editor.GetViewportOutputRenderGeneration() &&
                (!editor.IsBracketingToolActive() || editor.HasCurrentRawWorkspacePresentation())) {
                auto& pipeline=editor.GetPipeline(); texture=pipeline.GetOutputTexture();
                width=pipeline.GetCanvasWidth();height=pipeline.GetCanvasHeight();
            }
        }
        if (!texture || width <= 0 || height <= 0) {
            const auto& cover = workspace->galleryOrigin.thumbnail;
            if (cover) {
                texture = cover->texture;
                width = cover->width;
                height = cover->height;
            } else if (ImGui::IsRectVisible(ImVec2(size.x, size.x * 2.f))) {
                editor.TryGetBracketSidebarTexture(texture, width, height);
            }
        }
        const float rowHeight=Stack::Navigation::ProjectEntryHeight(size.x,rail.scale,
            texture ? width : 0, texture ? height : 0);
        // The workspace scope supplies identity; renaming must not replace the
        // active/navigation item during a press or save completion.
        const bool clicked = ImGui::InvisibleButton("##ProjectPill",
            ImVec2(size.x - 4.f * rail.scale, rowHeight), ImGuiButtonFlags_EnableNav);
        const bool over = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
        // Preview follows pointer hover only. Navigation focus can persist
        // after opening a document, which would otherwise keep its input blocked.
        if (over) hovered = id;
        if (clicked) {
            QueueWorkspaceTabAction(WorkspaceTabAction::Select, id);
            activationQueued = true;
        }
        const bool middleClose = enabled && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) &&
            ImGui::GetTopMostPopupModal() == nullptr &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_NoNavOverride);
        if (middleClose) {
            ImGui::ClosePopupsOverWindow(ImGui::GetCurrentWindow(), false);
            CancelProjectPillPreview(); QueueWorkspaceTabAction(WorkspaceTabAction::Close, id); hovered = 0;
        }
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        auto* draw = ImGui::GetWindowDrawList();
        if (workspace->galleryArrivalPending && ImGui::IsItemVisible()) {
            if (workspace->galleryArrivalStarted < 0) workspace->galleryArrivalStarted=ImGui::GetTime();
            const float elapsed=float((ImGui::GetTime()-workspace->galleryArrivalStarted)/.32);
            const float t=Stack::Notifications::SystemReducedMotion()?1.f:std::clamp(elapsed,0.f,1.f);
            const float ease=t*t*(3.f-2.f*t);
            const auto& origin=workspace->galleryOrigin;
            if (origin.thumbnail && t<1.f) {
                const auto mix=[&](ImVec2 a,ImVec2 b) {return ImVec2(a.x+(b.x-a.x)*ease,a.y+(b.y-a.y)*ease);};
                const float fit=std::min((max.x-min.x-4.f*rail.scale)/origin.thumbnail->width,
                    (max.y-min.y-4.f*rail.scale)/origin.thumbnail->height);
                const ImVec2 extent(origin.thumbnail->width*fit,origin.thumbnail->height*fit);
                const ImVec2 destination((min.x+max.x-extent.x)*.5f,(min.y+max.y-extent.y)*.5f);
                ImGui::GetForegroundDrawList()->AddImageRounded((ImTextureID)(intptr_t)origin.thumbnail->texture,
                    mix(origin.minimum,destination),mix(origin.maximum,{destination.x+extent.x,destination.y+extent.y}),
                    origin.uvMinimum,origin.uvMaximum,IM_COL32_WHITE,3.f*rail.scale);
            }
            if(t>=1.f) workspace->galleryArrivalPending=false;
        }
        if (ImGui::IsItemVisible() && !workspace->galleryArrivalPending && !ImGui::IsItemClicked(ImGuiMouseButton_Middle))
            Stack::Navigation::DrawProjectEntry(draw,min,max,rail.scale,texture,width,height,
                automatic ? "Auto" : name,selected,dirty);
        if (ImGui::IsItemVisible() && !workspace->galleryArrivalPending && editor.IsGalleryBracketProcessing()) {
            const ImVec2 center((min.x+max.x)*.5f,(min.y+max.y)*.5f);
            const float radius=6.f*rail.scale;
            draw->AddCircleFilled(center,radius+3.f*rail.scale,IM_COL32(0,0,0,180),20);
            const float angle=float(std::fmod(ImGui::GetTime()*5.,6.283185307));
            draw->PathArcTo(center,radius,angle,angle+4.5f,20);
            draw->PathStroke(IM_COL32(245,245,245,255),0,1.7f*rail.scale);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s",name);
        // Resolve command status only while this particular menu is open.
        const bool menuOpen=ImGui::IsPopupOpen("ProjectActions");
        const auto fileContext=menuOpen?editor.GetProjectFileCommandContext():EditorModule::ProjectFileCommandContext{};
        switch(Stack::Navigation::DrawProjectMenu(name,dirty,automatic,
            fileContext.busy?fileContext.busyReason.c_str():"",rail.scale)) {
        case Stack::Navigation::ProjectMenuAction::Open: QueueWorkspaceTabAction(WorkspaceTabAction::Select,id);break;
        case Stack::Navigation::ProjectMenuAction::Close: QueueWorkspaceTabAction(WorkspaceTabAction::Close,id);break;
        case Stack::Navigation::ProjectMenuAction::ForceClose: QueueWorkspaceTabAction(WorkspaceTabAction::ForceClose,id);break;
        default:break;
        }
        // Include the opening frame, and release the hold after selection,
        // Escape or an outside click. The reveal update consumes this next frame.
        rail.projectMenuOpen |= ImGui::IsPopupOpen("ProjectActions");
        if ((selected || workspace->galleryArrivalPending) && m_ScrollActiveProjectTabIntoView) ImGui::SetScrollHereY(.5f);
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    m_ScrollActiveProjectTabIntoView = false;
    m_WorkspaceCompositor.KeepChrome(ImGui::GetWindowDrawList());
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    if (!activationQueued) UpdateProjectPillPreview(hovered);
}

void AppShell::RenderProjectPreviewFallback() {
    const auto& rail = m_NavigationRail;
    if (!rail.previewProject || rail.previewAmount <= .001f ||
        m_WorkspaceCompositor.HasProjectPreview(rail.previewProject, rail.previewRootView)) return;
    const auto* workspace = FindProjectWorkspace(rail.previewProject);
    if (!workspace) return;
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetForegroundDrawList(viewport);
    ImVec4 fill = m_Editor ? m_Editor->GetWorkspaceBaseColor() : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    fill.w = rail.previewAmount;
    draw->AddRectFilled(rail.panelPosition,
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), ImGui::GetColorU32(fill));
    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text); ink.w *= rail.previewAmount;
    const std::string name = workspace->editor->GetCurrentProjectName();
    draw->AddText(ImVec2(rail.bodyPosition.x + 28.f, rail.bodyPosition.y + 28.f),
        ImGui::GetColorU32(ink), name.empty() ? "Untitled project" : name.c_str());
    draw->AddText(ImVec2(rail.bodyPosition.x + 28.f, rail.bodyPosition.y + 58.f),
        ImGui::GetColorU32(ink), "No recent view available. Click the pill to open this workspace.");
}
