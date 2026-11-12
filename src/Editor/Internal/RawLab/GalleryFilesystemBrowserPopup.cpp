#include "GalleryFilesystemBrowserState.h"
#include "Library/LibraryControls.h"
#include "App/AppPaths.h"
#include <imgui_internal.h>
#include <algorithm>

namespace Stack::Editor::RawLabInternal {
void GalleryFilesystemBrowser::Open(const std::filesystem::path& current) {
    // A fresh session isolates late worker results from a closed picker.
    state_->closed.store(true);
    state_->motion.Stop();state_->ReleasePreviews();
    auto index=std::move(state_->searchIndex);
    if(!index)index=std::make_shared<Search::FolderIndex>(AppPaths::GetCacheDirectory()/"FolderSearch");
    const float tileSize=state_->tileSize;
    state_=std::make_shared<State>();state_->tileSize=tileSize;
    state_->searchIndex=std::move(index);
    state_->Navigate(current);
    openRequested_=true;
}

GalleryFilesystemBrowser::Action GalleryFilesystemBrowser::RenderPopup() {
    constexpr auto title="##GalleryFolderPicker";
    if(openRequested_) {ImGui::OpenPopup(title);openRequested_=false;}
    if(!ImGui::IsPopupOpen(title)) {
        if(!state_->closed.exchange(true) && state_->searchIndex)state_->searchIndex->Suspend();
        state_->motion.Stop();state_->ReleasePreviews(); // Previous frame has already been submitted.
        return {};
    }
    const auto* viewport=ImGui::GetMainViewport();
    const float unit=ImGui::GetFontSize()/14.f;
    popupSize_={viewport->WorkSize.x*.75f,viewport->WorkSize.y*.75f};
    ImGui::SetNextWindowSize(popupSize_,ImGuiCond_Always);
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(),ImGuiCond_Always,{.5f,.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16*unit,16*unit});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,16*unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,0);
    Action changed;
    if(ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
        auto& s=*state_;
        s.PumpPreviews();
        const auto navigate=Render(s.current);
        if(navigate && State::Key(*navigate)!=s.currentKey) {
            const bool outward=!s.current.empty() && *navigate==s.current.parent_path();
            s.returnFolder=outward?s.currentKey:std::wstring{};s.locateReturn=outward;
            const auto cover=s.selection && s.selection->path==*navigate?s.selection:nullptr;
            s.motion.Start(outward,s.navigationTile,cover?cover->texture:0,cover?cover->previewWidth:0,cover?cover->previewHeight:0);
            s.Navigate(*navigate);
            s.publishNavigation=true;
        }
        // Publish only the latest requested location once its listing is usable.
        if(s.publishNavigation && s.directory && s.directory->loaded && !s.directory->pending && s.directory->error.empty()) {
            changed.folder=s.directory->path;s.publishNavigation=false;s.publishedKey=s.currentKey;
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::SetKeyOwner(ImGuiKey_Escape,ImGui::GetID("PickerEscape"),ImGuiInputFlags_LockUntilRelease);
            s.closeRequested=true;
        }
        const auto* popup=ImGui::GetCurrentWindow();
        const bool outside=!popup->Rect().Contains(ImGui::GetIO().MousePos);
        if(outside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            GImGui->OpenPopupStack.Size==GImGui->BeginPopupStack.Size)s.closeRequested=true;
        if(s.openImage){changed.image=std::move(s.openImage);changed.folder.reset();s.closeRequested=true;}
        if(s.closeRequested){s.motion.Stop();s.closed.store(true);if(s.searchIndex)s.searchIndex->Suspend();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(5);
    return changed;
}

void GalleryFilesystemBrowser::RenderTiles(std::optional<std::filesystem::path>& navigate) {
    auto& s=*state_;
    if(s.directory && !s.directory->error.empty()) {
        ImGui::TextWrapped("%s",s.directory->error.c_str());
        if(ImGui::Button("Retry folder") && !s.directory->pending)s.Request(state_,s.directory);
        return;
    }
    if(s.directory && (!s.directory->loaded || s.directory->pending)) {
        ImGui::TextDisabled("Loading folders...");return;
    }
    const bool searching=!s.searchKey.empty();
    if(searching && (s.searchScheduled || s.searchPending)){ImGui::TextDisabled("Searching...");return;}
    if(searching && s.searchRefreshing)ImGui::TextDisabled("Updating search index...");
    if(searching && !s.searchStatus.empty())ImGui::TextWrapped("%s",s.searchStatus.c_str());
    else if(searching && s.searchLimited)ImGui::TextDisabled("Partial results - narrow your search to see more.");
    const auto& tiles=searching?s.searchResults:s.directory?s.directory->children:s.drives;
    if(!s.directory) {
        const float unit=ImGui::GetFontSize()/14.f;
        for(int group=0;group<2;++group) {
            bool heading=false;
            for(const auto& node:tiles) {
                if((node->kind==State::Kind::Network)!=(group==1))continue;
                if(!heading) {ImGui::TextDisabled(group?"Network locations":"Devices and drives");ImGui::Dummy({0,10*unit});heading=true;}
                ImGui::PushID(node.get());
                const auto pos=ImGui::GetCursorScreenPos();
                const float width=ImGui::GetContentRegionAvail().x;
                if(ImGui::Selectable("##Drive",s.selection==node,ImGuiSelectableFlags_AllowDoubleClick,{width,82*unit})) {
                    s.selection=node;
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){if(node->kind==State::Kind::Image)s.openImage=node->path;else {navigate=node->path;s.navigationTile={pos,{pos.x+width,pos.y+82*unit}};}}
                }
                if(!s.motion.Active() && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)){if(node->kind==State::Kind::Image)s.openImage=node->path;else {navigate=node->path;s.navigationTile={pos,{pos.x+width,pos.y+82*unit}};}}
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip("%s",node->pathLabel.c_str());
                auto* draw=ImGui::GetWindowDrawList();
                const auto ink=ImGui::GetColorU32(ImGuiCol_TextDisabled);
                if(node->kind==State::Kind::Optical) {
                    draw->AddCircle({pos.x+24*unit,pos.y+30*unit},17*unit,ink,24,2*unit);
                    draw->AddCircle({pos.x+24*unit,pos.y+30*unit},4*unit,ink,12,2*unit);
                } else {
                    draw->AddRect({pos.x+5*unit,pos.y+16*unit},{pos.x+43*unit,pos.y+45*unit},ink,4*unit,0,2*unit);
                    draw->AddLine({pos.x+7*unit,pos.y+35*unit},{pos.x+41*unit,pos.y+35*unit},ink,unit);
                    draw->AddCircleFilled({pos.x+36*unit,pos.y+40*unit},2*unit,ink);
                    if(node->systemDrive)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
                        draw->AddRectFilled({pos.x+(12+8*x)*unit,pos.y+(19+7*y)*unit},{pos.x+(18+8*x)*unit,pos.y+(24+7*y)*unit},ink);
                }
                const float left=pos.x+58*unit,right=pos.x+width-14*unit;
                ImGui::RenderTextEllipsis(draw,{left,pos.y+6*unit},{right,pos.y+28*unit},right,node->label.c_str(),nullptr,nullptr);
                if(node->capacityKnown) {
                    draw->AddRectFilled({left,pos.y+34*unit},{right,pos.y+42*unit},ImGui::GetColorU32(ImGuiCol_FrameBg),3*unit);
                    const auto fill=node->usedFraction>=.9f?IM_COL32(205,73,73,255):IM_COL32(93,159,196,255);
                    draw->AddRectFilled({left,pos.y+34*unit},{left+(right-left)*node->usedFraction,pos.y+42*unit},fill,3*unit);
                }
                ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::RenderTextEllipsis(draw,{left,pos.y+50*unit},{right,pos.y+75*unit},right,node->capacityLabel.c_str(),nullptr,nullptr);
                ImGui::PopStyleColor();ImGui::PopID();
            }
        }
        if(tiles.empty())ImGui::TextDisabled(searching?"No matching drives":s.rootsPending?"Loading drives...":"No drives available");
        return;
    }
    if(tiles.empty()) {
        ImGui::TextWrapped("%s",searching?(s.searchRefreshing?"Indexing images and folders...":"No matching images or folders"):s.directory?"No images or subfolders":
            s.rootsPending?"Loading locations...":s.locationsError.empty()?"No locations available.":s.locationsError.c_str());
        return;
    }
    const float unit=ImGui::GetFontSize()/14.f;
    const float width=ImGui::GetContentRegionAvail().x;
    const int columns=std::max(1,static_cast<int>(width/(s.tileSize*unit)));
    const float tileWidth=width/columns, tileHeight=tileWidth*.78f+30*unit;
    if(s.locateReturn) {
        s.locateReturn=false;
        for(std::size_t i=0;i<tiles.size();++i)if(tiles[i]->key==s.returnFolder) {
            const float top=float(i/columns)*tileHeight;
            const float scroll=ImGui::GetScrollY(),height=s.gridBounds.GetHeight();
            if(top<scroll || top+tileHeight>scroll+height)ImGui::SetScrollY(std::max(0.f,top-height*.4f));
            break;
        }
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>((tiles.size()+columns-1)/columns),tileHeight);
    while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row) {
        const auto start=ImGui::GetCursorScreenPos();
        for(int column=0;column<columns;++column) {
            const auto index=static_cast<std::size_t>(row)*columns+column;
            if(index>=tiles.size())break;
            const auto& node=tiles[index];
            const ImVec2 pos{start.x+column*tileWidth,start.y};
            ImGui::SetCursorScreenPos(pos);
            const ImRect tileBounds{pos,{pos.x+tileWidth-6*unit,pos.y+tileHeight-6*unit}};
            if(s.motion.Active() && s.motion.Outward() && node->key==s.returnFolder)s.motion.Anchor(tileBounds);
            if(navigate && *navigate==node->path)s.navigationTile=tileBounds;
            ImGui::PushID(node.get());
            if(ImGui::Selectable("##Tile",s.selection==node,ImGuiSelectableFlags_AllowDoubleClick,{tileWidth-6*unit,tileHeight-6*unit})) {
                s.selection=node;
                if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){if(node->kind==State::Kind::Image)s.openImage=node->path;else {navigate=node->path;s.navigationTile={pos,{pos.x+tileWidth-6*unit,pos.y+tileHeight-6*unit}};}}
            }
            if(!s.motion.Active() && ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)){if(node->kind==State::Kind::Image)s.openImage=node->path;else {navigate=node->path;s.navigationTile={pos,{pos.x+tileWidth-6*unit,pos.y+tileHeight-6*unit}};}}
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))ImGui::SetTooltip("%s",node->path.u8string().c_str());
            node->previewFrame=ImGui::GetFrameCount();
            s.RequestPreview(state_,node);
            if(node->texture) {
                const float scale=std::min((tileWidth-24*unit)/node->previewWidth,(tileHeight-46*unit)/node->previewHeight);
                const ImVec2 size{node->previewWidth*scale,node->previewHeight*scale};
                const ImVec2 imagePos{pos.x+(tileWidth-size.x)*.5f,pos.y+8*unit+(tileHeight-46*unit-size.y)*.5f};
                ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)(intptr_t)node->texture,imagePos,
                    {imagePos.x+size.x,imagePos.y+size.y},{0,0},{1,1},IM_COL32_WHITE,6*unit);
            } else LibraryUi::DrawIcon(node->kind==State::Kind::Image?LibraryUi::Icon::Gallery:LibraryUi::Icon::Folder,{pos.x+(tileWidth-56*unit)*.5f,pos.y+28*unit},56*unit,ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),{pos.x+8*unit,pos.y+tileHeight-28*unit},
                {pos.x+tileWidth-14*unit,pos.y+tileHeight},pos.x+tileWidth-14*unit,node->label.c_str(),nullptr,nullptr);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy({width,tileHeight});
    }
}
}
