#include "ProjectSidebarMenu.h"
#include "Library/LibraryControls.h"
#include "Notifications/NotificationPresenter.h"
#include "imgui_internal.h"
#include <algorithm>

namespace Stack::Navigation {
ProjectMenuAction DrawProjectMenu(const char* name,bool dirty,bool automatic,
    const char* busyReason,float scale) {
    const ImVec2 sourceTop=ImGui::GetItemRectMin();
    ImGui::OpenPopupOnItemClick("ProjectActions");
    if(!ImGui::IsPopupOpen("ProjectActions"))return ProjectMenuAction::None;
    // One popup animation per sidebar host, not an accumulating entry for
    // every project ever opened. Closed menus submit no animation work.
    auto* host=ImGui::GetCurrentWindow();
    auto* storage=ImGui::GetStateStorage();
    const auto identityKey=ImHashStr("ProjectMenuIdentity",0,host->ID);
    const auto frameKey=ImHashStr("ProjectMenuFrame",0,host->ID);
    const auto timeKey=ImHashStr("ProjectMenuTime",0,host->ID);
    const auto identity=ImGui::GetID("ProjectActions");
    const int frame=ImGui::GetFrameCount();
    const float now=static_cast<float>(ImGui::GetTime());
    if(storage->GetInt(identityKey)!=static_cast<int>(identity) || storage->GetInt(frameKey,-2)!=frame-1) {
        storage->SetInt(identityKey,static_cast<int>(identity));storage->SetFloat(timeKey,now);
    }
    storage->SetInt(frameKey,frame);
    const float t=Notifications::SystemReducedMotion()?1.f:std::clamp((now-storage->GetFloat(timeKey))/.11f,0.f,1.f);
    const float alpha=1.f-(1.f-t)*(1.f-t)*(1.f-t);
    scale=std::max(.5f,scale);
    // Keep the popup clear of the project rail instead of opening under the pointer.
    ImGui::SetNextWindowPos({host->Pos.x+host->Size.x+12.f*scale,sourceTop.y},ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints({272.f*scale,0},{320.f*scale,ImGui::GetMainViewport()->WorkSize.y*.85f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{18.f*scale,16.f*scale});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{10.f*scale,6.f*scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{10.f*scale,5.f*scale});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,9.f*scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*alpha);
    ProjectMenuAction action=ProjectMenuAction::None;
    if(ImGui::BeginPopup("ProjectActions")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+244.f*scale);
        ImGui::TextWrapped("%s",name);
        ImGui::TextDisabled("%s",automatic?"Background processing":dirty?"Unsaved changes":"Project workspace");
        if(busyReason && *busyReason)ImGui::TextWrapped("%s",busyReason);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        const auto row=[&](const char* label,ProjectMenuAction command,bool open,bool force) {
            // Explicit geometry keeps icon spacing independent of font space widths.
            const ImVec2 origin=ImGui::GetCursorScreenPos();
            ImGui::PushID(label);
            if(ImGui::Selectable("##action",false,ImGuiSelectableFlags_None,
                {0,ImGui::GetFontSize()+16.f*scale}))action=command;
            ImGui::PopID();
            const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
            const float extent=14.f*scale;
            const ImVec2 at(origin.x+2.f*scale,(low.y+high.y-extent)*.5f);
            auto* draw=ImGui::GetWindowDrawList();
            const auto color=ImGui::GetColorU32(force?ImGuiCol_PlotHistogram:ImGuiCol_Text);
            draw->AddText({origin.x+28.f*scale,(low.y+high.y-ImGui::GetFontSize())*.5f},
                ImGui::GetColorU32(ImGuiCol_Text),label);
            if(open)LibraryUi::DrawIcon(LibraryUi::Icon::OpenFolder,at,extent,color);
            else {
                const float inset=3.f*scale;
                draw->AddLine({at.x+inset,at.y+inset},{at.x+extent-inset,at.y+extent-inset},color,1.5f*scale);
                draw->AddLine({at.x+extent-inset,at.y+inset},{at.x+inset,at.y+extent-inset},color,1.5f*scale);
            }
            if(force && ImGui::IsItemHovered())ImGui::SetTooltip("Discard unsaved changes and cancel this project's work.");
        };
        row("Open project",ProjectMenuAction::Open,true,false);
        row("Close project",ProjectMenuAction::Close,false,false);
        if(!automatic) {
            ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            row("Force close",ProjectMenuAction::ForceClose,false,true);
            ImGui::Indent(28.f*scale);
            ImGui::TextDisabled("Discards unsaved changes");
            ImGui::Unindent(28.f*scale);
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(6);
    return action;
}
}
