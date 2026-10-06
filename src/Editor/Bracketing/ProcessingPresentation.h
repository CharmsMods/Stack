#pragma once
#include "Project/BracketingProgress.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <imgui.h>

namespace Stack::Editor {
using Project::ProcessingMilestone;
using Project::ProcessingSnapshot;
using Project::ProcessingMailbox;
class ProcessingDirector {
public:
    ProcessingSnapshot Select(const ProcessingSnapshot&,double now,bool reduced);
    void Ready(const ProcessingSnapshot&,double now,bool reduced);
    void Skip(double now);
    bool Done(double now) const {return readyAt_>=0 && now>=deadline_;}
    float Finish(double now) const;
    double Deadline() const {return deadline_;}
private:
    std::set<std::uint64_t> seen_;
    std::uint64_t liveOccurrence_=0;
    std::string liveEvidence_;
    double entered_=0,readyAt_=-1,deadline_=-1;
    std::vector<ProcessingMilestone> ending_;
    ProcessingMilestone displayed_;
    bool hasDisplayed_=false;
};
struct ProcessingCard {
    std::string id,label;
    unsigned group=0;
    ImVec4 color{.65f,.7f,.75f,1};
};
struct ProcessingPlane {float x=0,y=0,z=0,yaw=0,width=2,height=1.33f;};
ProcessingPlane ProcessingPlanePose(Raw::Bracketing::ProcessingStage stage,std::size_t index,
    std::size_t count,unsigned group,double seconds,bool reduced);

class ProcessingSceneRenderer {
public:
    ~ProcessingSceneRenderer();
    unsigned Draw(const ProcessingSnapshot&,const std::vector<ProcessingCard>&,ImVec2 size,
        ImVec4 background,double time,bool reduced,unsigned finalTexture=0,float finish=0);
    void Release();
    std::size_t TextureBytes() const;
    void Annotate(ImDrawList*,ImVec2 origin,ImVec2 size,const ProcessingSnapshot&,float alpha) const;
    float AnnotationOpacity() const;
private:
    unsigned program_=0,vao_=0,vbo_=0,fbo_=0,output_=0;
    int width_=0,height_=0;
    bool failed_=false;
    double lastDraw_=-1,stageStart_=0;
    Raw::Bracketing::ProcessingStage stage_=Raw::Bracketing::ProcessingStage::Assets;
    Raw::Bracketing::ProcessingStage previousStage_=stage_;
    struct Image {unsigned texture=0,width=0,height=0;std::shared_ptr<const Raw::Bracketing::ProcessingThumbnail> source;};
    std::map<std::string,Image> images_;
    struct EvidenceImage {unsigned texture=0;std::shared_ptr<const Raw::Bracketing::ProcessingEvidence> source;};
    std::array<EvidenceImage,3> evidenceImages_{};
    struct PlaneState {ProcessingPlane pose;float opacity=0;};
    std::map<std::string,PlaneState> poses_;
    struct Annotation {std::string label;std::array<ImVec2,4> corners;int evidence=-1;bool main=false;};
    std::vector<Annotation> annotations_;
    std::vector<Annotation> previousAnnotations_;
    std::shared_ptr<const Raw::Bracketing::ProcessingEvidence> previousAnnotationEvidence_;
    void AnnotateLayer(ImDrawList*,ImVec2,ImVec2,const ProcessingSnapshot&,float,const std::vector<Annotation>&) const;
    unsigned msTexture_=0,msFbo_=0,samples_=1;
    double drawTime_=0;
    std::shared_ptr<const Raw::Bracketing::ProcessingEvidence> lastEvidence_;
    double evidenceStart_=0;
    std::string layoutKey_;
    double layoutChangedAt_=0;
    unsigned previousOutput_=0,previousFbo_=0;
    double dissolveStart_=0,dissolveDuration_=0;
};
struct ProcessingPresentation {
    std::shared_ptr<ProcessingMailbox> mailbox;
    std::vector<ProcessingCard> cards;
    ProcessingSceneRenderer renderer;
    std::string recipeIdentity,projectId;
    std::uint64_t generation=0,expectedHash=0;
    bool active=false,canceling=false,waitingForRaw=false;
    ImGuiID previousFocus=0,focusWindowId=0;
    bool focusCaptured=false;
    double began=0,finishedAt=-1;
    ProcessingDirector director;
    void RestoreFocus();
    ~ProcessingPresentation();
};
}
