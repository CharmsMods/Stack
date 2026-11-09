#include "GalleryFilesystemBrowserState.h"
#include "Library/LibraryControls.h"
#include "App/AppHeaderStyle.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
namespace {
bool NavigationButton(const char* id,int direction,float unit) {
    const bool pressed=ImGui::Button(id,{28*unit,28*unit});
    const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
    const ImVec2 center{(low.x+high.x)*.5f,(low.y+high.y)*.5f};
    auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    if(direction==0) {
        draw->AddLine({center.x-4*unit,center.y+2*unit},{center.x,center.y-2*unit},color,1.5f*unit);
        draw->AddLine({center.x,center.y-2*unit},{center.x+4*unit,center.y+2*unit},color,1.5f*unit);
    } else {
        draw->AddLine({center.x-2*direction*unit,center.y-4*unit},{center.x+2*direction*unit,center.y},color,1.5f*unit);
        draw->AddLine({center.x+2*direction*unit,center.y},{center.x-2*direction*unit,center.y+4*unit},color,1.5f*unit);
    }
    return pressed;
}
// Semantic glyphs stay paired with text; branch tint is only a wayfinding aid.
template<class Kind>
void LocationIcon(Kind kind, bool open, ImVec2 pos, float size, ImU32 color) {
    auto* d=ImGui::GetWindowDrawList();const float u=size/16.f;
    const auto p=[&](float x,float y){return ImVec2(pos.x+x*u,pos.y+y*u);};
    if(kind==Kind::Folder) {
        LibraryUi::DrawIcon(open?LibraryUi::Icon::OpenFolder:LibraryUi::Icon::Folder,pos,size,color);return;
    }
    if(kind==Kind::Pictures) {
        d->AddRect(p(1,2),p(15,14),color,2*u,0,1.25f*u);
        d->AddCircleFilled(p(11,6),1.5f*u,color);
        d->AddPolyline(std::array<ImVec2,3>{p(2,12),p(6,7),p(11,13)}.data(),3,color,0,1.25f*u);
    } else if(kind==Kind::Desktop) {
        d->AddRect(p(1,1),p(15,11),color,u,0,1.25f*u);
        d->AddLine(p(8,11),p(8,15),color,1.25f*u);d->AddLine(p(4,15),p(12,15),color,1.25f*u);
    } else if(kind==Kind::Network) {
        d->AddRect(p(5,1),p(11,6),color,u,0,1.25f*u);
        d->AddLine(p(8,6),p(8,10),color,1.25f*u);d->AddLine(p(2,10),p(14,10),color,1.25f*u);
        for(float x:{2.f,8.f,14.f}){d->AddLine(p(x,10),p(x,13),color,u);d->AddCircleFilled(p(x,14),1.5f*u,color);}
    } else if(kind==Kind::Removable) {
        d->AddRect(p(4,4),p(12,15),color,2*u,0,1.25f*u);
        d->AddRect(p(5,0),p(11,4),color,0,0,1.25f*u);d->AddLine(p(7,1),p(7,3),color,u);
    } else {
        d->AddRect(p(1,3),p(15,13),color,2*u,0,1.25f*u);
        d->AddLine(p(2,9),p(14,9),color,u);d->AddCircleFilled(p(12,11),u,color);
    }
}
void PathTooltip(const char* path, const char* status=nullptr) {
    ImGui::BeginTooltip();
    const float unit=ImGui::GetFontSize()/14.f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::max(1.f,
        std::min(420*unit,ImGui::GetWindowViewport()->WorkSize.x-32*unit)));
    ImGui::TextUnformatted(path);
    if(status && *status)ImGui::TextUnformatted(status);
    ImGui::PopTextWrapPos();ImGui::EndTooltip();
}
ImVec4 BranchColor(unsigned index) {
    constexpr ImVec4 colors[]={{.64f,.73f,.91f,1},{.64f,.81f,.73f,1},{.84f,.73f,.57f,1},
        {.77f,.69f,.87f,1},{.58f,.79f,.84f,1},{.86f,.69f,.69f,1}};
    return colors[index%6];
}
}
GalleryFilesystemBrowser::GalleryFilesystemBrowser():state_(std::make_shared<State>()) {}
GalleryFilesystemBrowser::~GalleryFilesystemBrowser()=default;
std::optional<std::filesystem::path> GalleryFilesystemBrowser::Render(const std::filesystem::path& current) {
    auto& s=*state_;s.Start(state_);
    if(s.current!=current)s.Navigate(current);
    s.Pump(state_);
    std::optional<std::filesystem::path> selected;
    const float unit=ImGui::GetFontSize()/14.f;
    const bool reduced=LibraryUi::ReducedMotion();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{6*unit,6*unit});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{10*unit,8*unit});
    ImGui::BeginDisabled(s.history.empty() || s.historyIndex==0);
    if(NavigationButton("##Back",-1,unit)){s.requestedHistory=s.historyIndex-1;selected=s.history[*s.requestedHistory];}
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Back");
    ImGui::SameLine();
    ImGui::BeginDisabled(s.history.empty() || s.historyIndex+1>=s.history.size());
    if(NavigationButton("##Forward",1,unit)){s.requestedHistory=s.historyIndex+1;selected=s.history[*s.requestedHistory];}
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Forward");
    ImGui::SameLine();
    ImGui::BeginDisabled(current.empty() || current==current.root_path());
    if(NavigationButton("##Up",0,unit))selected=current.parent_path();
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Up one folder");
    ImGui::SameLine();
    if(ImGui::Button("##FolderTools",{28*unit,28*unit}))ImGui::OpenPopup("FolderTools");
    const auto toolsMin=ImGui::GetItemRectMin();
    LibraryUi::DrawIcon(LibraryUi::Icon::MoreHorizontal,{toolsMin.x+6*unit,toolsMin.y+6*unit},16*unit,ImGui::GetColorU32(ImGuiCol_Text));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Folder navigation options");
    if(ImGui::BeginPopup("FolderTools")) {
        if(ImGui::MenuItem("Show current folder",nullptr,false,!current.empty())) {
            s.filter[0]=0;s.FilterChanged();s.revealRequested=true;s.scrollToCurrent=true;
        }
        if(ImGui::MenuItem("Collapse branches"))s.Collapse();
        if(ImGui::MenuItem("Filter loaded folders",nullptr,&s.showFilter)) {
            if(!s.showFilter){s.filter[0]=0;s.FilterChanged();}
        }
        ImGui::Separator();
        ImGui::MenuItem("Branch colors",nullptr,&s.colors);
        if(ImGui::MenuItem("Refresh locations",nullptr,false,!s.rootsPending)){s.started=false;s.Start(state_);}
        ImGui::EndPopup();
    }
    const float addressWidth=std::max(1.f,ImGui::GetContentRegionAvail().x-34*unit);
    ImGui::SetNextItemWidth(addressWidth);
    if(ImGui::InputTextWithHint("##FolderPath","Paste a folder path",s.address,sizeof(s.address),ImGuiInputTextFlags_EnterReturnsTrue)) {
        const auto path=std::filesystem::u8path(s.address);
        if(path.is_absolute())selected=path.lexically_normal();
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))PathTooltip(s.address);
    ImGui::SameLine();
    if(ImGui::Button("##Ancestors",{28*unit,ImGui::GetFrameHeight()}))ImGui::OpenPopup("Ancestors");
    const auto low=ImGui::GetItemRectMin();
    LibraryUi::DrawIcon(LibraryUi::Icon::ChevronDown,{low.x+8*unit,low.y+(ImGui::GetFrameHeight()-12*unit)*.5f},12*unit,ImGui::GetColorU32(ImGuiCol_Text));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Location and parent folders");
    const float ancestryWidth=std::min(420*unit,ImGui::GetWindowViewport()->WorkSize.x-32*unit);
    ImGui::SetNextWindowSizeConstraints({std::max(1.f,ancestryWidth),0},{std::max(1.f,ancestryWidth),ImGui::GetWindowViewport()->WorkSize.y*.7f});
    if(ImGui::BeginPopup("Ancestors")) {
        if(s.ancestors.empty())ImGui::TextDisabled("Open a folder to see its location");
        for(std::size_t index=0;index<s.ancestors.size();++index) {
            ImGui::PushID(static_cast<int>(index));
            const auto pos=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
            if(ImGui::Selectable("##Location",index+1==s.ancestors.size(),0,{width,28*unit}))selected=s.ancestors[index];
            const float inset=std::min(static_cast<float>(index)*10*unit,50*unit);
            LibraryUi::DrawIcon(LibraryUi::Icon::Folder,{pos.x+6*unit+inset,pos.y+6*unit},14*unit,ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{pos.x+28*unit+inset,pos.y+6*unit},
                {pos.x+width-6*unit,pos.y+28*unit},pos.x+width-6*unit,s.ancestorLabels[index].c_str(),nullptr,nullptr);
            if(ImGui::IsItemHovered())PathTooltip(s.ancestors[index].u8string().c_str());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    if(s.showFilter) {
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputTextWithHint("##FilterFolders","Filter loaded folders",s.filter,sizeof(s.filter)))s.FilterChanged();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Filters folder names already loaded in this tree. Clear to restore your expanded branches.");
    }
    ImGui::TextDisabled(s.filterKey.empty()?"This PC":"Matching loaded folders");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,0});
    const float rowHeight=32*unit;
    if(s.restoreRow){ImGui::SetNextWindowScroll({-1,*s.restoreRow*rowHeight});s.restoreRow.reset();}
    if(ImGui::BeginChild("ComputerFolders",{0,0},false,ImGuiWindowFlags_NoBackground)) {
        s.visibleFirst=static_cast<int>(std::max(0.f,ImGui::GetScrollY()/rowHeight));
        s.visibleLast=static_cast<int>((ImGui::GetScrollY()+ImGui::GetWindowHeight())/rowHeight);
        const auto anchorIndex=static_cast<std::size_t>(s.visibleFirst);
        if(anchorIndex<s.rows.size()) {
            s.scrollAnchor=s.rows[anchorIndex].node;s.anchorMessage=s.rows[anchorIndex].message;
            s.anchorOffset=ImGui::GetScrollY()/rowHeight-static_cast<float>(anchorIndex);
        }
        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(s.rows.size()),rowHeight);
        while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i) {
            auto& row=s.rows[i];auto& node=*row.node;
            ImGui::PushID(&node);
            const auto origin=ImGui::GetCursorScreenPos();
            const float width=ImGui::GetContentRegionAvail().x;
            const float indent=std::min(row.depth*16*unit,std::max(0.f,width-130*unit));
            if(row.message) {
                const char* text=node.pending?"Loading folders...":node.error.empty()?"No subfolders":"Folder unavailable";
                ImGui::Dummy({width,rowHeight});
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{origin.x+indent+8*unit,origin.y+8*unit},
                    {origin.x+width-6*unit,origin.y+rowHeight},origin.x+width-6*unit,text,nullptr,nullptr);
                ImGui::PopStyleColor();
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",node.error.empty()?text:node.error.c_str());
                if(ImGui::BeginPopupContextItem("Retry")) {
                    if(ImGui::MenuItem("Retry listing"))s.Request(state_,row.node);
                    ImGui::EndPopup();
                }
                ImGui::PopID();continue;
            }
            const bool active=s.currentKey==node.key;
            const bool expandable=!node.loaded || !node.children.empty() || !node.error.empty();
            ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,ImVec4(0,0,0,0));
            if(ImGui::Selectable("##Folder",active,ImGuiSelectableFlags_None,{width,rowHeight})) {
                const float mouse=ImGui::GetIO().MousePos.x-origin.x;
                if(expandable && s.filterKey.empty() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouse>=indent && mouse<indent+22*unit) {
                    node.expanded=!node.expanded;s.dirty=true;s.revealRequested=false;s.scrollToCurrent=false;
                    if(node.expanded&&!node.loaded)s.Request(state_,row.node);
                } else selected=node.path;
            }
            ImGui::PopStyleColor(3);
            const bool hovered=ImGui::IsItemHovered();
            const bool focused=ImGui::IsItemFocused();
            if(focused && s.filterKey.empty()) {
                if(expandable && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {node.expanded=true;s.dirty=true;if(!node.loaded)s.Request(state_,row.node);}
                if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {node.expanded=false;s.dirty=true;s.revealRequested=false;s.scrollToCurrent=false;}
            }
            const bool tooltip=ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
            if(ImGui::BeginPopupContextItem("FolderOptions")) {
                if(ImGui::MenuItem("Open folder"))selected=node.path;
                if(ImGui::MenuItem("Refresh folders",nullptr,false,!node.pending))s.Request(state_,row.node);
                ImGui::EndPopup();
            }
            auto* draw=ImGui::GetWindowDrawList();
            const bool expanded=node.expanded || !s.filterKey.empty();
            const float target=expanded?1.f:0.f,hoverTarget=hovered?1.f:0.f;
            if(node.reveal!=target || node.disclosureMotion.Active())node.disclosureMotion.Update(node.reveal,target,.14f,ImGui::GetTime(),ImGui::GetFrameCount(),reduced);
            if(node.hover!=hoverTarget || node.hoverMotion.Active())node.hoverMotion.Update(node.hover,hoverTarget,.12f,ImGui::GetTime(),ImGui::GetFrameCount(),reduced);
            const auto textColor=ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImVec4 backing=textColor;backing.w*=active?.12f:.055f*node.hover;
            if(backing.w>0)draw->AddRectFilled({origin.x+1*unit,origin.y+1*unit},{origin.x+width-1*unit,origin.y+rowHeight-1*unit},ImGui::GetColorU32(backing),4*unit);
            const auto tint=s.colors?Header::Blend(textColor,BranchColor(node.branch),.5f):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            if(active)draw->AddRectFilled({origin.x,origin.y+9*unit},{origin.x+2*unit,origin.y+23*unit},ImGui::GetColorU32(textColor),unit);
            if(row.depth>0) {
                ImVec4 guide=tint;guide.w*=.22f;
                draw->AddLine({origin.x+indent-7*unit,origin.y},{origin.x+indent-7*unit,origin.y+rowHeight},ImGui::GetColorU32(guide),unit);
            }
            const ImVec2 arrow{origin.x+indent+6*unit,origin.y+12*unit};
            if(expandable)draw->AddTriangleFilled({arrow.x,arrow.y+2*node.reveal*unit},
                {arrow.x+8*node.reveal*unit,arrow.y+(8-6*node.reveal)*unit},
                {arrow.x+(5-node.reveal)*unit,arrow.y+(4+3*node.reveal)*unit},ImGui::GetColorU32(ImGuiCol_TextDisabled));
            LocationIcon(node.kind,expanded,{origin.x+indent+24*unit,origin.y+8*unit},16*unit,ImGui::GetColorU32(tint));
            const float textLeft=origin.x+indent+48*unit;
            ImGui::RenderTextEllipsis(draw,{textLeft,origin.y+(rowHeight-ImGui::GetFontSize())*.5f},
                {origin.x+width-6*unit,origin.y+rowHeight},origin.x+width-6*unit,node.label.c_str(),nullptr,nullptr);
            if(tooltip)PathTooltip(node.path.u8string().c_str(),node.pending?"Loading folders...":node.error.c_str());
            ImGui::PopID();
        }
        if(s.rows.empty())ImGui::TextWrapped("%s",!s.filterKey.empty()?"No matching loaded folders.":s.locationsError.empty()?"Loading locations...":s.locationsError.c_str());
    }
    ImGui::EndChild();ImGui::PopStyleVar(3);
    if(selected && s.requestedHistory && *selected!=s.history[*s.requestedHistory])s.requestedHistory.reset();
    return selected;
}
}
