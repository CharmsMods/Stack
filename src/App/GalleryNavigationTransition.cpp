#include "App/GalleryNavigationTransition.h"
#include <algorithm>
#include <cmath>

namespace Stack::Navigation {
namespace {
float Smooth(float t) {t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t);}
bool Equal(ImRect a,ImRect b) {return a.Min.x==b.Min.x && a.Min.y==b.Min.y && a.Max.x==b.Max.x && a.Max.y==b.Max.y;}
}
ImRect GalleryNavigationTransition::Mix(ImRect a,ImRect b,float t) {
    return {{a.Min.x+(b.Min.x-a.Min.x)*t,a.Min.y+(b.Min.y-a.Min.y)*t},
        {a.Max.x+(b.Max.x-a.Max.x)*t,a.Max.y+(b.Max.y-a.Max.y)*t}};
}
ImRect GalleryNavigationTransition::Fit(const Origin& origin,ImRect viewport) {
    const float width=origin.thumbnail ? float(origin.thumbnail->width) : origin.maximum.x-origin.minimum.x;
    const float height=origin.thumbnail ? float(origin.thumbnail->height) : origin.maximum.y-origin.minimum.y;
    const float fit=std::min(viewport.GetWidth()/std::max(1.f,width),viewport.GetHeight()/std::max(1.f,height));
    const auto center=viewport.GetCenter();
    return {{center.x-width*fit*.5f,center.y-height*fit*.5f},{center.x+width*fit*.5f,center.y+height*fit*.5f}};
}
void GalleryNavigationTransition::Scene(float duration) {
    ++generation;sceneAmount.Reset(0);
    sceneAmount.Retarget(1,Animation::MotionSpec::Tween(duration));
    if (reduced) sceneAmount.Finish();
}
void GalleryNavigationTransition::Tick(double now,int nextFrame,bool reduce) {
    if(frame==nextFrame)return;
    submitted.clear();frame=nextFrame;reduced=reduce;
    const double dt=lastTime<0 ? 0 : std::max(0.,now-lastTime);
    lastTime=time=now;
    if(folderAmount.IsActive()) {
        folderAmount.Advance(dt,reduced);
        if(now>=folderDeadline)folderAmount.Finish();
        if(!folderAmount.IsActive())pagesFromFolder.clear();
    }
    if(sceneAmount.IsActive())sceneAmount.Advance(dt,reduced);
    if(imageCrop.IsActive())imageCrop.Advance(dt,reduced);
    if(imageGeometryActive) {
        bool moving=false;
        for(auto& channel:imageBounds){channel.Advance(dt,reduced);moving|=channel.IsActive();}
        if(now>=imageDeadline || reduced) {for(auto& channel:imageBounds)channel.Finish();moving=false;}
        imageGeometryActive=moving;
    }
    if(!imageGeometryActive && liveReady && readyAt<0)readyAt=now;
    if(returning && !imageGeometryActive && !sceneAmount.IsActive())CancelImage();
    else if(liveReady && readyAt>=0 && now-readyAt>=.12)imageOrigin.thumbnail.reset();
}
void GalleryNavigationTransition::Folders(bool open,ImRect anchor,ImRect destination,std::string chosen) {
    if(foldersRequested==open && (open || chosenFolder==chosen))return;
    if(anchor.GetArea()>0)browseAnchor=anchor;
    NewIntent();
    foldersRequested=open;chosenFolder=std::move(chosen);folderDestination=destination;
    if(returning)imageGeometryActive=false;
    folderAmount.SpringTo(open?1.f:0.f,folderAmount.Velocity(),Animation::MotionSpec::Spring(5.f,1.f));
    folderDeadline=time+.42;
    if(reduced) {folderAmount.Finish();pagesFromFolder.clear();}
    Scene(.42f);
}
ImRect GalleryNavigationTransition::FolderPose(const std::string& key,const ImRect& target,std::size_t ordinal) {
    if(!FoldersMoving() && foldersRequested)return target;
    ImRect anchor=browseAnchor;
    if(anchor.GetArea()<=0)anchor={target.Min,{target.Min.x+24,target.Min.y+24}};
    const float delay=std::min(.06f,float(ordinal)*.012f)/.42f;
    const float amount=Smooth((FolderAmount()-delay)/std::max(.01f,1.f-delay));
    if(!foldersRequested && key==chosenFolder && folderDestination.GetArea()>0)
        return Mix(target,folderDestination,1.f-amount);
    return Mix(anchor,target,amount);
}
bool GalleryNavigationTransition::GalleryPhoto(const std::string& identity,const ImRect& target,ImRect& displayed,ImVec2& uvLow,ImVec2& uvHigh) const {
    if(foldersRequested || pagesFromFolder.empty() || !FoldersMoving())return false;
    for(const auto& page:pagesFromFolder)if(page.sourceIdentity==identity) {
        const float t=1.f-FolderAmount();
        displayed=Mix({page.minimum,page.maximum},target,t);
        uvLow={page.uvMinimum.x*(1-t),page.uvMinimum.y+(1-page.uvMinimum.y)*t};
        uvHigh={page.uvMaximum.x+(1-page.uvMaximum.x)*t,page.uvMaximum.y*(1-t)};
        return true;
    }
    return false;
}
void GalleryNavigationTransition::TargetImage(ImRect target,bool first) {
    if(!first && Equal(target,imageTarget))return;
    imageTarget=target;
    const float values[]={target.Min.x,target.Min.y,target.Max.x,target.Max.y};
    for(std::size_t i=0;i<4;++i) {
        if(first)imageBounds[i].Reset(i==0?imageOrigin.minimum.x:i==1?imageOrigin.minimum.y:i==2?imageOrigin.maximum.x:imageOrigin.maximum.y);
        imageBounds[i].SpringTo(values[i],imageBounds[i].Velocity(),Animation::MotionSpec::Spring(6.f,1.f));
        if(reduced)imageBounds[i].Finish();
    }
    imageDeadline=time+.36;imageGeometryActive=!reduced;
}
void GalleryNavigationTransition::BeginImage(Origin origin,std::uint64_t workspace,ImRect viewport) {
    const bool redirect=HasImage() && imageGeometryActive;
    imageOrigin=std::move(origin);imageWorkspace=workspace;
    departure={imageOrigin.minimum,imageOrigin.maximum};
    returning=liveReady=false;readyAt=-1;
    imageCrop.Reset(0);imageCrop.Retarget(1,Animation::MotionSpec::Tween(.36f));
    if(reduced)imageCrop.Finish();
    if(!imageOrigin.Valid())return;
    TargetImage(Fit(imageOrigin,viewport),!redirect);Scene(.18f);
}
void GalleryNavigationTransition::ReturnImage(Origin destination) {
    if(!HasImage())return;
    departure={{imageBounds[0].Value(),imageBounds[1].Value()},{imageBounds[2].Value(),imageBounds[3].Value()}};
    if(destination.thumbnail)imageOrigin.thumbnail=destination.thumbnail;
    liveReady=false;readyAt=-1;returning=true;
    imageCrop.SpringTo(0,imageCrop.Velocity(),Animation::MotionSpec::Spring(6.f,1.f));
    if(reduced)imageCrop.Finish();
    if(destination.Valid() && destination.subject==imageOrigin.subject) {
        imageOrigin.uvMinimum=destination.uvMinimum;imageOrigin.uvMaximum=destination.uvMaximum;
        TargetImage({destination.minimum,destination.maximum});
    }
    else {imageGeometryActive=false;readyAt=time;}
    Scene(.18f);
}
void GalleryNavigationTransition::CancelImage() {
    imageOrigin={};imageWorkspace=0;imageGeometryActive=liveReady=returning=false;readyAt=-1;
}
void GalleryNavigationTransition::Retire(std::uint64_t workspace) {if(Owns(workspace))CancelImage();}
ImRect GalleryNavigationTransition::PresentImage(std::uint64_t workspace,ImRect target) {
    if(!Owns(workspace) || returning)return target;
    if(liveReady && !imageGeometryActive) {
        if(Equal(target,imageTarget))return target;
        const float values[]={target.Min.x,target.Min.y,target.Max.x,target.Max.y};
        for(std::size_t i=0;i<4;++i)imageBounds[i].Reset(values[i]);
        imageTarget=target;
        return target;
    }
    TargetImage(target);liveReady=true;
    if(!imageGeometryActive && readyAt<0)readyAt=time;
    return {{imageBounds[0].Value(),imageBounds[1].Value()},{imageBounds[2].Value(),imageBounds[3].Value()}};
}
float GalleryNavigationTransition::ImageControls(std::uint64_t workspace) const {
    if(!Owns(workspace) || returning)return 1;
    if(imageGeometryActive)return Smooth(1.f-float((imageDeadline-time)/.22));
    return 1;
}
bool GalleryNavigationTransition::Active() const {
    return folderAmount.IsActive() || sceneAmount.IsActive() || imageGeometryActive ||
        (HasImage() && (!liveReady || (readyAt>=0 && time-readyAt<.12)));
}
void GalleryNavigationTransition::DrawOverlay(ImDrawList& draw) {
    if(!HasImage() || (reduced && liveReady))return;
    float opacity=liveReady && readyAt>=0 ? 1.f-Smooth(float((time-readyAt)/.12)) : 1.f;
    if(returning && !imageGeometryActive)opacity*=1.f-SceneWeight();
    if(opacity<=0)return;
    const ImVec2 low(imageBounds[0].Value(),imageBounds[1].Value()),high(imageBounds[2].Value(),imageBounds[3].Value());
    const auto ink=ImGui::GetColorU32(ImVec4(1,1,1,opacity));
    if(imageOrigin.thumbnail && imageOrigin.thumbnail->texture) {
        submitted.push_back(imageOrigin.thumbnail);
        const float crop=std::clamp(imageCrop.Value(),0.f,1.f);
        const ImVec2 uvLow(imageOrigin.uvMinimum.x*(1-crop),imageOrigin.uvMinimum.y+(1-imageOrigin.uvMinimum.y)*crop);
        const ImVec2 uvHigh(imageOrigin.uvMaximum.x+(1-imageOrigin.uvMaximum.x)*crop,imageOrigin.uvMaximum.y*(1-crop));
        draw.AddImage(static_cast<ImTextureID>(imageOrigin.thumbnail->texture),low,high,uvLow,uvHigh,ink);
    } else if(!liveReady && !returning)draw.AddRectFilled(low,high,ImGui::GetColorU32(ImVec4(.12f,.12f,.12f,opacity)),3);
}
}
