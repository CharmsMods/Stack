#pragma once
#include "Animation/ScalarChannel.h"
#include "Renderer/GalleryTransitionOrigin.h"
#include <imgui_internal.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Stack::Navigation {
// Shell-owned presentation shared with its Gallery and editing surfaces.
// No document, task, editor pointer or processing request is retained here.
class GalleryNavigationTransition {
public:
    using Origin=Renderer::GalleryTransitionOrigin;
    void Tick(double now,int frame,bool reduced);
    std::uint64_t NewIntent() {return ++navigationIntent;}
    std::uint64_t Intent() const {return navigationIntent;}
    void Folders(bool open,ImRect anchor,ImRect destination={},std::string chosen={});
    bool FoldersVisible() const {return foldersRequested || folderAmount.IsActive() || folderAmount.Value()>.0001f;}
    bool FoldersMoving() const {return folderAmount.IsActive();}
    bool FoldersRequested() const {return foldersRequested;}
    bool ChosenFolder(const std::string& key) const {return !foldersRequested && key==chosenFolder;}
    ImRect Departure() const {return departure;}
    float FolderAmount() const {return std::clamp(folderAmount.Value(),0.f,1.f);}
    ImRect FolderPose(const std::string& key,const ImRect& target,std::size_t ordinal);
    void FolderPages(std::vector<Origin> pages) {pagesFromFolder=std::move(pages);}
    bool GalleryPhoto(const std::string& identity,const ImRect& target,ImRect& displayed,ImVec2& uvLow,ImVec2& uvHigh) const;
    void BeginImage(Origin origin,std::uint64_t workspace,ImRect viewport);
    void ReturnImage(Origin destination);
    void CancelImage();
    void Retire(std::uint64_t workspace);
    void Shutdown() {CancelImage();pagesFromFolder.clear();submitted.clear();folderAmount.Finish();sceneAmount.Finish();}
    bool Owns(std::uint64_t workspace) const {return imageWorkspace==workspace && imageOrigin.Valid();}
    bool ImageMoving(std::uint64_t workspace) const {return Owns(workspace) && (imageGeometryActive || returning);}
    bool Waiting(std::uint64_t workspace) const {return Owns(workspace) && !returning && !liveReady;}
    bool HasImage() const {return imageOrigin.Valid();}
    std::uint64_t ImageWorkspace() const {return imageWorkspace;}
    ImRect PresentImage(std::uint64_t workspace,ImRect target);
    void Viewport(std::uint64_t workspace,ImRect bounds) {if(Waiting(workspace))TargetImage(Fit(imageOrigin,bounds));}
    void DrawOverlay(ImDrawList& draw);
    float ImageControls(std::uint64_t workspace) const;
    float SceneWeight() const {return std::clamp(sceneAmount.Value(),0.f,1.f);}
    std::uint64_t Generation() const {return generation;}
    bool Active() const;
private:
    void Scene(float duration);
    void TargetImage(ImRect target,bool first=false);
    static ImRect Fit(const Origin&,ImRect);
    static ImRect Mix(ImRect,ImRect,float);
    std::uint64_t generation=0,navigationIntent=0,imageWorkspace=0;
    double time=0,lastTime=-1,folderDeadline=0,imageDeadline=0,readyAt=-1;
    int frame=-1;
    bool reduced=false,foldersRequested=false,returning=false,liveReady=false,imageGeometryActive=false;
    Animation::ScalarChannel folderAmount,sceneAmount{1.f},imageCrop{1.f};
    std::array<Animation::ScalarChannel,4> imageBounds;
    Origin imageOrigin;
    ImRect browseAnchor{},folderDestination{},imageTarget{},departure{};
    std::string chosenFolder;
    std::vector<Origin> pagesFromFolder;
    std::vector<std::shared_ptr<const Renderer::GalleryThumbnailLease>> submitted;
};
}
