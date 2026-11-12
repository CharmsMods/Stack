#include "GalleryFilesystemBrowserState.h"
#include "GalleryBrowserScroll.h"
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
    if(kind==Kind::Downloads) {
        d->AddLine(p(8,1),p(8,11),color,1.5f*u);
        d->AddPolyline(std::array<ImVec2,3>{p(4,7),p(8,11),p(12,7)}.data(),3,color,0,1.5f*u);
        d->AddLine(p(2,15),p(14,15),color,1.5f*u);
    } else if(kind==Kind::Optical) {
        d->AddCircle(p(8,8),6*u,color,20,1.25f*u);d->AddCircle(p(8,8),1.5f*u,color,12,1.25f*u);
    } else if(kind==Kind::Pictures) {
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
GalleryFilesystemBrowser::~GalleryFilesystemBrowser(){state_->closed.store(true);state_->motion.Stop();if(state_->searchIndex)state_->searchIndex->Suspend();state_->ReleasePreviews();}
std::optional<std::filesystem::path> GalleryFilesystemBrowser::Render(const std::filesystem::path& current) {
    auto& s=*state_;s.Start(state_);
    if(!s.motion.Active())s.navigationTile={};
    if(s.current!=current)s.Navigate(current);
    if(s.directory && !s.directory->loaded && !s.directory->pending && s.directory->error.empty())s.Request(state_,s.directory);
    s.Pump(state_);s.PumpSearch(state_);
    std::optional<std::filesystem::path> selected;
    const float unit=ImGui::GetFontSize()/14.f;
    const bool reduced=LibraryUi::ReducedMotion();
    // GLFW/ImGui button indices 3 and 4 are the standard mouse Back/Forward buttons.
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        if(ImGui::IsMouseClicked(3) && s.historyIndex>0) {s.requestedHistory=s.historyIndex-1;selected=s.history[*s.requestedHistory];}
        else if(ImGui::IsMouseClicked(4) && s.historyIndex+1<s.history.size()) {s.requestedHistory=s.historyIndex+1;selected=s.history[*s.requestedHistory];}
    }
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
    ImGui::BeginDisabled(current.empty());
    if(NavigationButton("##Up",0,unit))selected=current==current.root_path()?std::filesystem::path{}:current.parent_path();
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
        if(ImGui::MenuItem("Refresh current folder",nullptr,false,s.directory && !s.directory->pending)) {
            s.Request(state_,s.directory);if(s.searchIndex)s.searchIndex->Refresh();
        }
        if(ImGui::MenuItem("Refresh locations",nullptr,false,!s.rootsPending)){s.started=false;s.Start(state_);}
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const float searchWidth=std::clamp(ImGui::GetContentRegionAvail().x*.28f,130*unit,250*unit);
    const float addressWidth=std::max(1.f,ImGui::GetContentRegionAvail().x-74*unit-searchWidth);
    ImGui::SetNextItemWidth(addressWidth);
    const bool submittedPath=ImGui::InputTextWithHint("##FolderPath","This PC",s.address,sizeof(s.address),ImGuiInputTextFlags_EnterReturnsTrue);
    if(submittedPath) {
        try {
            const auto path=std::filesystem::u8path(s.address);
            if(path.is_absolute())selected=path.lexically_normal();
            else s.addressError="Enter an absolute folder path.";
        } catch(const std::exception&) {s.addressError="This folder path is invalid.";}
    }
    if(ImGui::IsItemEdited() && !submittedPath)s.addressError="Press Enter to navigate to this path.";
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))PathTooltip(s.address,s.addressError.c_str());
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
    ImGui::SameLine();
    ImGui::SetNextItemWidth(searchWidth);
    if(ImGui::InputTextWithHint("##SearchFolders",current.empty()?"Search drives":"Search images and folders",s.search,sizeof(s.search)))s.SearchChanged();
    if(ImGui::IsItemHovered())ImGui::SetTooltip(current.empty()?"Filter drives by name or letter":"Search names in this folder and all subfolders. Small spelling mistakes are tolerated. Ctrl-scroll resizes thumbnails.");
    ImGui::SameLine();
    if(ImGui::Button("##CloseBrowser",{28*unit,28*unit}))s.closeRequested=true;
    const auto closeMin=ImGui::GetItemRectMin();
    auto* closeDraw=ImGui::GetWindowDrawList();
    const auto closeColor=ImGui::GetColorU32(ImGuiCol_Text);
    closeDraw->AddLine({closeMin.x+9*unit,closeMin.y+9*unit},{closeMin.x+19*unit,closeMin.y+19*unit},closeColor,1.5f*unit);
    closeDraw->AddLine({closeMin.x+19*unit,closeMin.y+9*unit},{closeMin.x+9*unit,closeMin.y+19*unit},closeColor,1.5f*unit);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Close folder browser");
    if(s.showFilter) {
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputTextWithHint("##FilterFolders","Filter loaded folders",s.filter,sizeof(s.filter)))s.FilterChanged();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Filters folder names already loaded in this tree. Clear to restore your expanded branches.");
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,0});
    const float rowHeight=32*unit;
    const float bodyHeight=std::max(1.f,ImGui::GetContentRegionAvail().y);
    const float treeWidth=std::min(260*unit,ImGui::GetContentRegionAvail().x*.30f);
    if(ImGui::BeginChild("BrowserLocations",{treeWidth,bodyHeight},false,ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar)) {
        for(const auto& shortcut:s.shortcuts) {
            ImGui::PushID(shortcut.get());const auto pos=ImGui::GetCursorScreenPos();
            if(ImGui::Selectable("##Shortcut",s.currentKey==shortcut->key,0,{0,rowHeight}))selected=shortcut->path;
            LocationIcon(shortcut->kind,false,{pos.x+8*unit,pos.y+8*unit},16*unit,ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::GetWindowDrawList()->AddText({pos.x+34*unit,pos.y+8*unit},ImGui::GetColorU32(ImGuiCol_Text),shortcut->label.c_str());
            ImGui::PopID();
        }
        ImGui::Dummy({0,12*unit});
        const auto pcPos=ImGui::GetCursorScreenPos();
        if(ImGui::Selectable("##ThisPC",current.empty(),0,{0,rowHeight}))selected=std::filesystem::path{};
        LocationIcon(State::Kind::Desktop,false,{pcPos.x+8*unit,pcPos.y+8*unit},16*unit,ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::GetWindowDrawList()->AddText({pcPos.x+34*unit,pcPos.y+8*unit},ImGui::GetColorU32(ImGuiCol_Text),"This PC");
        if(s.restoreRow){ImGui::SetNextWindowScroll({-1,*s.restoreRow*rowHeight});s.restoreRow.reset();}
    if(ImGui::BeginChild("ComputerFolders",{0,0},false,ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar)) {
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
            const bool expandable=!node.loaded || node.folderChildren!=0 || !node.error.empty();
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
        RenderGalleryBrowserScroll(unit);
        if(s.rows.empty())ImGui::TextWrapped("%s",!s.filterKey.empty()?"No matching loaded folders.":s.locationsError.empty()?"Loading locations...":s.locationsError.c_str());
    }
    ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine(0,10*unit);
    if(selected)s.motion.Stop();
    if(s.resetGridScroll){ImGui::SetNextWindowScroll({0,s.directory?s.directory->gridScroll:0});s.resetGridScroll=false;}
    if(ImGui::BeginChild("FolderTiles",{0,bodyHeight},false,ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar)) {
        if(ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,ImGui::GetCurrentWindow()->ID);
            const float wheel=ImGui::GetIO().MouseWheel;
            if(wheel!=0){s.motion.Stop();s.tileSize=std::clamp(s.tileSize*std::pow(1.12f,wheel),96.f,360.f); }
        }
        s.gridBounds=ImGui::GetCurrentWindow()->InnerClipRect;
        s.motion.Begin(s.gridBounds,!s.locateReturn && ((!s.directory && !s.rootsPending) || (s.directory && s.directory->loaded && !s.directory->pending)),LibraryUi::ReducedMotion());
        if(s.directory && !s.directory->error.empty())s.motion.Stop();
        const int firstVertex=ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1.f);
        ImGui::BeginDisabled(s.motion.Active());
        RenderTiles(selected);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        s.motion.Finish(ImGui::GetWindowDrawList(),firstVertex);
        if(selected && !s.motion.Active() && !s.current.empty() && *selected==s.current.parent_path())
            s.motion.Capture(ImGui::GetWindowDrawList(),s.gridBounds);
        if(s.directory)s.directory->gridScroll=ImGui::GetScrollY();
        if(!s.motion.Active())RenderGalleryBrowserScroll(unit);
    }
    ImGui::EndChild();ImGui::PopStyleVar(3);
    if(s.motion.Active())selected.reset();
    if(selected && s.requestedHistory && *selected!=s.history[*s.requestedHistory])s.requestedHistory.reset();
    return selected;
}
}
