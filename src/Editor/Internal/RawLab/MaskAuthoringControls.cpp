#include "Editor/EditorModule.h"
#include "Editor/MaskToolBindings.h"
#include "Renderer/MaskPickerBlur.h"
#include <imgui_internal.h>

using namespace Stack;
using namespace Stack::MaskAuthoring;
using Phase=Editor::MaskAuthoringSession::Phase;

void EditorModule::RenderMaskToolRail(float scale) {
    for(int i=0;i<8;++i) {
        const Tool tool=Tool(i);ImGui::PushID(i);
        const auto center=ImGui::GetCursorScreenPos();
        const bool clicked=ImGui::InvisibleButton("tool",{36*scale,34*scale});
        const bool hovered=ImGui::IsItemHovered();auto* d=ImGui::GetWindowDrawList();
        if(hovered || (MaskAuthoringActive() && m_MaskSession.tool==tool))
            d->AddRectFilled(center,ImGui::GetItemRectMax(),ImGui::GetColorU32(ImGuiCol_ButtonHovered),6*scale);
        Editor::DrawMaskGlyph(d,tool,{center.x+18*scale,center.y+17*scale},9*scale,ImGui::GetColorU32(ImGuiCol_Text));
        if(hovered)ImGui::SetTooltip("%s (%s) · Right-click to change shortcut",Editor::MaskToolName(tool),ImGui::GetKeyName(Editor::MaskToolKey(tool)));
        if(clicked)StartMaskTool(tool);
        if(ImGui::BeginPopupContextItem("Bind")){
            ImGui::Text("%s shortcut",Editor::MaskToolName(tool));
            ImGui::TextUnformatted("Press a letter");
            for(int k=ImGuiKey_A;k<=ImGuiKey_Z;++k)if(ImGui::IsKeyPressed(ImGuiKey(k),false)){
                if(Editor::BindMaskTool(tool,ImGuiKey(k)))ImGui::CloseCurrentPopup();
                else PostNotification(UiNotificationSeverity::Error,"Could not save mask shortcut.","mask-binding");
                break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

void EditorModule::DrawMaskPickerTarget(const ImRect& bounds,const char* label,const Project::NodeEditTarget& target) {
    if(bounds.GetWidth()<1 || bounds.GetHeight()<1)return;
    auto* draw=ImGui::GetForegroundDrawList();
    const bool hovered=bounds.Contains(ImGui::GetIO().MousePos) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const auto id=ImGui::GetID(label);
    draw->PushClipRect(bounds.Min,bounds.Max,true);
    Renderer::MaskPickerBlur::Draw(draw,id,bounds.Min,bounds.Max,m_MaskSession.generation+m_RenderRevision);
    draw->AddRectFilled(bounds.Min,bounds.Max,hovered?IM_COL32(95,105,117,95):IM_COL32(58,63,72,95));
    if(hovered)draw->AddRect(bounds.Min,bounds.Max,IM_COL32(220,226,239,235),4.f,2.f,ImDrawFlags_None);
    const auto size=ImGui::CalcTextSize(label),center=bounds.GetCenter();
    const ImVec2 pos{center.x-size.x*.5f,center.y-size.y*.5f};
    draw->AddText({pos.x+1,pos.y+1},IM_COL32(0,0,0,220),label);draw->AddText(pos,IM_COL32_WHITE,label);
    draw->PopClipRect();
    if(hovered && ImGui::GetFrameCount()>m_MaskSession.acceptAfter && ImGui::IsMouseClicked(0))ChooseMaskDestination(target);
}

void EditorModule::DrawMaskPalette() {
    auto& s=m_MaskSession;const auto* v=ImGui::GetMainViewport();const float em=ImGui::GetFontSize();
    ImGui::SetNextWindowViewport(v->ID);
    const float width=std::min(39*em,v->Size.x-24);
    ImGui::SetNextWindowSize({width,0});
    ImGui::SetNextWindowSizeConstraints({width,0},{width,std::max(100.f,v->Size.y*.5f)});
    ImGui::SetNextWindowPos({std::clamp((m_ViewToolMinimum.x+m_ViewToolMaximum.x)*.5f,v->Pos.x+width*.5f,v->Pos.x+v->Size.x-width*.5f),v->Pos.y+2.7f*em},ImGuiCond_Always,{.5f,0});
    ImGui::Begin("Mask tools###ViewMaskPalette",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoFocusOnAppearing);
    if(s.phase==Phase::Applying){ImGui::TextUnformatted("Creating full-resolution mask…");ImGui::SameLine();
        if(ImGui::Button("Cancel"))CancelMaskAuthoring();ImGui::End();return;}
    ImGui::TextUnformatted(Editor::MaskToolName(s.tool));ImGui::SameLine();
    if(ImGui::SmallButton("Eyedropper"))s.tool=Tool::Circle;
    ImGui::SameLine();
    if(ImGui::Checkbox("Grayscale",&s.gray))s.textureDirty=true;
    const bool hasCoverage=std::any_of(s.recipe.marks.begin(),s.recipe.marks.end(),[](const Mark& m){
        return m.tool!=Tool::Circle && m.tool!=Tool::Edge && m.tool!=Tool::Erase;
    });
    const bool canApply=hasCoverage && s.field && !s.dragging && (s.maskUuid.empty() || s.edited);
    ImGui::SameLine();ImGui::BeginDisabled(!canApply);
    const bool enter=canApply && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) &&
        ImGui::IsKeyPressed(ImGuiKey_Enter,false);
    if(ImGui::Button("Apply") || enter) {
        s.phase=Phase::Applying;s.pendingSource=true;++s.generation;
        if(s.work)s.work->canceled.store(true);
    }
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Cancel")){CancelMaskAuthoring();ImGui::End();return;}
    if(s.tool==Tool::Values || std::any_of(s.recipe.marks.begin(),s.recipe.marks.end(),[](const Mark& m){return m.tool==Tool::Values;})) {
        if(ImGui::RadioButton("Connected Area",!s.recipe.wholeImage)){s.recipe.wholeImage=false;s.Dirty();}
        ImGui::SameLine();if(ImGui::RadioButton("Whole Image",s.recipe.wholeImage)){s.recipe.wholeImage=true;s.Dirty();}
    }
    if(!s.field){ImGui::TextUnformatted(s.pendingSource?"Reading image colors…":s.status.c_str());
        if(!s.pendingSource && ImGui::SmallButton("Retry")){s.pendingSource=true;++s.generation;}
    } else if(!s.status.empty())ImGui::TextWrapped("%s",s.status.c_str());
    const auto& marks=s.recipe.marks;
    for(int i=0;i<int(marks.size());++i) {
        const auto& m=marks[i];if(m.tool!=Tool::Circle)continue;
        ImGui::PushID(i);const auto p=m.points.front();
        ImVec4 swatch{.5f,.5f,.5f,1};
        if(s.field){const auto& f=*s.field;int x=int(p[0]*(f.width-1)),y=int(p[1]*(f.height-1));
            const auto* c=&f.values[(size_t(y)*f.width+x)*3];
            if(std::isfinite(c[0])){
                RawRecipe::RawColorWarpCoordinate color;color.lightness=std::cbrt(std::max(0.f,c[0]));color.a=c[1];color.b=c[2];
                const auto rgb=RawRecipe::ColorWarpCoordinateToWorkingRgb(color,Raw::RawWorkingSpace::LinearSrgbD65);
                swatch={std::pow(std::clamp(rgb[0],0.f,1.f),1/2.2f),std::pow(std::clamp(rgb[1],0.f,1.f),1/2.2f),std::pow(std::clamp(rgb[2],0.f,1.f),1/2.2f),1};}}
        if(ImGui::ColorButton("Sample",swatch,ImGuiColorEditFlags_NoTooltip,{1.3f*em,1.3f*em})){s.selected=i;ImGui::OpenPopup("Role");}
        if(ImGui::IsItemHovered()){s.selected=i;ImGui::SetTooltip("Sample %d · %s",i+1,m.role==Role::Protect?"Protect":m.role==Role::Include?"Include":"Transition");}
        if(ImGui::BeginPopup("Role")){for(auto role:{Role::Include,Role::Protect,Role::Transition}) {
            if(ImGui::MenuItem(role==Role::Include?"Include":role==Role::Protect?"Protect":"Transition",nullptr,m.role==role)){
                s.recipe.marks[i].role=role;s.Dirty();}
        }ImGui::EndPopup();}
        ImGui::PopID();if(ImGui::GetCursorPosX()+3*em<width)ImGui::SameLine();
    }
    if(!marks.empty()){
        ImGui::NewLine();
        ImGui::SetNextItemWidth(12*em);
        const bool selected=s.selected>=0 && s.selected<int(marks.size());
        if(ImGui::BeginCombo("##Marks",selected?Editor::MaskToolName(marks[s.selected].tool):"Select mark")){
            for(int i=0;i<int(marks.size());++i){ImGui::PushID(i);char label[64];snprintf(label,sizeof(label),"%d  %s",i+1,Editor::MaskToolName(marks[i].tool));
                if(ImGui::Selectable(label,i==s.selected)){s.selected=i;s.tool=marks[i].tool;}ImGui::PopID();}
            ImGui::EndCombo();
        }
        ImGui::SameLine();ImGui::BeginDisabled(!selected || s.dragging);
        if(ImGui::SmallButton("Delete mark")){s.recipe.marks.erase(s.recipe.marks.begin()+s.selected);s.selected=-1;s.Dirty();}
        ImGui::EndDisabled();
    }
    ImGui::End();
}

