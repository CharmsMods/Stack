#include "ProcessingPresentation.h"
#include "ProcessingSceneGl.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
using namespace Raw::Bracketing;
namespace {
struct Vertex {float x,y,z,w,u,v;};
ImVec2 ProjectScenePoint(const ProcessingPlane& p,float u,float v) {
    const float perspective=1+(u-.5f)*std::sin(p.yaw)*.35f;
    return {p.x+(u-.5f)*p.width*std::cos(p.yaw)/perspective,
        p.y+((v-.5f)*p.height+(u-.5f)*p.width*.065f*std::sin(p.yaw))/perspective};
}
}
ProcessingSceneRenderer::~ProcessingSceneRenderer(){Release();}
void ProcessingSceneRenderer::Release() {
    for(auto& [id,image]:images_)if(image.texture)glDeleteTextures(1,&image.texture);
    for(auto& image:evidenceImages_){if(image.texture)glDeleteTextures(1,&image.texture);image={};}
    images_.clear();poses_.clear();annotations_.clear();previousAnnotations_.clear();previousAnnotationEvidence_.reset();layoutKey_.clear();
    if(output_)glDeleteTextures(1,&output_);if(fbo_)glDeleteFramebuffers(1,&fbo_);
    if(msTexture_)glDeleteTextures(1,&msTexture_);if(msFbo_)glDeleteFramebuffers(1,&msFbo_);
    if(previousOutput_)glDeleteTextures(1,&previousOutput_);if(previousFbo_)glDeleteFramebuffers(1,&previousFbo_);
    if(vbo_)glDeleteBuffers(1,&vbo_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);
    output_=fbo_=previousOutput_=previousFbo_=msTexture_=msFbo_=vbo_=vao_=program_=0;width_=height_=0;lastDraw_=-1;lastEvidence_=nullptr;
    dissolveDuration_=0;
}
std::size_t ProcessingSceneRenderer::TextureBytes() const {
    std::size_t bytes=std::size_t(width_)*height_*4*(1+(previousOutput_?1:0)+(msTexture_?samples_:0));
    for(const auto& [id,image]:images_)bytes+=std::size_t(image.width)*image.height*4*4/3;
    for(unsigned i=0;i<3;++i)if(evidenceImages_[i].source&&i<evidenceImages_[i].source->rasters.size())
        bytes+=evidenceImages_[i].source->rasters[i].rgba.size();
    return bytes;
}
unsigned ProcessingSceneRenderer::Draw(const ProcessingSnapshot& snapshot,const std::vector<ProcessingCard>& cards,
    ImVec2 size,ImVec4,double time,bool reduced,unsigned,float) {
    if(failed_||size.x<1||size.y<1)return 0;
    const bool busy=snapshot.progress.stage==ProcessingStage::LocalAlignment||snapshot.progress.stage==ProcessingStage::Reconstruction;
    const float scale=std::min({1.f,1920.f/size.x,1080.f/size.y});
    const int width=std::max(1,int(size.x*scale)),height=std::max(1,int(size.y*scale));
    std::string layoutKey=std::to_string(int(snapshot.progress.stage))+"/"+snapshot.progress.captureId;
    if(snapshot.evidence) {
        layoutKey+=snapshot.evidence->hasAffine?"/affine":"/fixed";
        for(const auto& raster:snapshot.evidence->rasters)layoutKey+="/"+raster.label;
    }
    const bool sameImages=images_.size()==snapshot.images.size()&&std::all_of(snapshot.images.begin(),snapshot.images.end(),[&](const auto& entry) {
        auto found=images_.find(entry.first);return found!=images_.end()&&found->second.source==entry.second;
    });
    const bool layoutChanged=layoutKey_!=layoutKey||!sameImages;
    if(layoutChanged){layoutKey_=std::move(layoutKey);layoutChangedAt_=time-(reduced?.85:0);}
    // After a transition settles, only measured changes require a new scene.
    // Native labels and counters are still drawn every UI frame.
    if(output_&&width==width_&&height==height_&&!layoutChanged&&sameImages&&lastEvidence_==snapshot.evidence&&
        (reduced||(time-layoutChangedAt_>1.6&&time-dissolveStart_>=dissolveDuration_)))return output_;
    if(output_&&width==width_&&height==height_&&!layoutChanged&&time-lastDraw_<(busy?1./30.:1./60.))return output_;
    const float dt=lastDraw_<0?.016f:float(std::clamp(time-lastDraw_,0.,.1));lastDraw_=time;drawTime_=time;
    if(stage_!=snapshot.progress.stage){stage_=snapshot.progress.stage;stageStart_=time;}
    ProcessingGl::Guard guard;
    const bool resized=width!=width_||height!=height_||!output_;
    const bool changed=layoutChanged||lastEvidence_!=snapshot.evidence;
    if(!reduced&&!resized&&changed&&previousFbo_) {
        // Capture the last displayed mixture, so a new measurement arriving
        // during a dissolve continues from the image actually on screen.
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previousFbo_);
        glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        dissolveStart_=time;dissolveDuration_=layoutChanged?.85:.22;
        previousAnnotations_=annotations_;previousAnnotationEvidence_=lastEvidence_;
    }
    if(reduced||resized){dissolveDuration_=0;previousAnnotations_.clear();previousAnnotationEvidence_.reset();}
    if(!program_) {
        program_=GLHelpers::CreateShaderProgram(ProcessingGl::VertexShader,ProcessingGl::FragmentShader);
        if(!program_){failed_=true;return 0;}
        glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);
    }
    if(width!=width_||height!=height_||!output_) {
        if(output_)glDeleteTextures(1,&output_);if(fbo_)glDeleteFramebuffers(1,&fbo_);
        if(msTexture_)glDeleteTextures(1,&msTexture_);if(msFbo_)glDeleteFramebuffers(1,&msFbo_);
        if(previousOutput_)glDeleteTextures(1,&previousOutput_);if(previousFbo_)glDeleteFramebuffers(1,&previousFbo_);
        previousOutput_=previousFbo_=0;dissolveDuration_=0;
        msTexture_=msFbo_=0;samples_=1;width_=width;height_=height;
        glGenTextures(1,&output_);glBindTexture(GL_TEXTURE_2D,output_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        fbo_=GLHelpers::CreateFBO(output_);glBindFramebuffer(GL_FRAMEBUFFER,fbo_);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){failed_=true;return 0;}
        glGenTextures(1,&previousOutput_);glBindTexture(GL_TEXTURE_2D,previousOutput_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        previousFbo_=GLHelpers::CreateFBO(previousOutput_);
        GLint maximum=0;glGetIntegerv(0x8D57,&maximum);
        // Reserve 25 MiB for 16 mipmapped source textures and three evidence maps.
        const std::size_t pixels=std::size_t(width)*height*4;
        unsigned wanted=maximum>=4?4:maximum>=2?2:1;
        while(wanted>1&&pixels*(wanted+2)+25ull*1024*1024>64ull*1024*1024)wanted/=2;
        if(wanted<=unsigned(maximum)&&wanted>1)if(auto allocate=ProcessingGl::MultisampleFunction()) {
            glGenTextures(1,&msTexture_);glBindTexture(ProcessingGl::TextureMultisample,msTexture_);
            allocate(ProcessingGl::TextureMultisample,wanted,GL_RGBA8,width,height,GL_TRUE);
            glGenFramebuffers(1,&msFbo_);glBindFramebuffer(GL_FRAMEBUFFER,msFbo_);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,ProcessingGl::TextureMultisample,msTexture_,0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE)samples_=wanted;
            else {glDeleteTextures(1,&msTexture_);glDeleteFramebuffers(1,&msFbo_);msTexture_=msFbo_=0;}
        }
    }
    for(auto it=images_.begin();it!=images_.end();) {
        if(!snapshot.images.count(it->first)){glDeleteTextures(1,&it->second.texture);it=images_.erase(it);}else ++it;
    }
    for(const auto& [id,source]:snapshot.images) {
        auto& image=images_[id];if(image.source==source)continue;
        if(image.texture)glDeleteTextures(1,&image.texture);
        image.texture=GLHelpers::CreateTextureFromPixels(source->rgba.data(),source->width,source->height,4,true);
        image.width=source->width;image.height=source->height;image.source=source;
    }
    const auto evidence=snapshot.evidence;
    if(lastEvidence_!=evidence){lastEvidence_=evidence;evidenceStart_=time;}
    for(unsigned i=0;i<3;++i) {
        auto& image=evidenceImages_[i];
        if(image.source==evidence)continue;
        if(image.texture)glDeleteTextures(1,&image.texture);image={};
        if(evidence&&i<evidence->rasters.size()) {
            const auto& r=evidence->rasters[i];
            image.texture=GLHelpers::CreateTextureFromPixels(r.rgba.data(),r.width,r.height,4,false);image.source=evidence;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER,msFbo_?msFbo_:fbo_);glViewport(0,0,width,height);
    glClearColor(.012f,.014f,.018f,1);glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(4*sizeof(float)));
    const auto uniform=[&](const char* name){return glGetUniformLocation(program_,name);};
    glUniform1i(uniform("Image"),0);glUniform1i(uniform("Fullscreen"),0);annotations_.clear();std::set<std::string> alive;
    const auto paint=[&](const std::string& id,ProcessingPlane target,unsigned texture,const std::string& label,
        ImVec4 tint,int kind,int evidenceIndex,bool main,ImVec4 crop=ImVec4(0,0,1,1)) {
        alive.insert(id);auto found=poses_.find(id);
        if(found==poses_.end()) {
            auto start=target;start.x-=reduced?0:.035f;
            if(!reduced&&evidenceIndex>=0&&!main)for(const auto& annotation:annotations_)if(annotation.main) {
                start.x=.43f;start.y=.47f;start.width=.49f;
                start.height=target.height*start.width/std::max(.01f,target.width);start.yaw=-.08f;break;
            }
            found=poses_.emplace(id,PlaneState{start,0}).first;
        }
        auto& state=found->second;const float t=reduced?1.f:1-std::exp(-dt*5);
        const auto approach=[&](float& v,float end){v+=(end-v)*t;};
        approach(state.pose.x,target.x);approach(state.pose.y,target.y);approach(state.pose.width,target.width);
        approach(state.pose.height,target.height);approach(state.pose.yaw,target.yaw);approach(state.opacity,1);
        const auto& p=state.pose;
        const int order[]={0,1,2,0,2,3};Vertex vertices[6];
        // Geometry extends beyond the visible image. Coverage is evaluated in
        // screen pixels so both silhouettes and inner contours receive AA.
        for(unsigned v=0;v<6;++v) {
            const int k=order[v];float u=k==1||k==2?1.015f:-.015f,vy=k>=2?1.015f:-.015f;
            auto point=ProjectScenePoint(p,u,vy);vertices[v]={point.x*2-1,1-point.y*2,0,1,u,vy};
        }
        glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(uniform("Kind"),texture?kind:-1);
        glUniform4f(uniform("Tint"),tint.x,tint.y,tint.z,tint.w*state.opacity);
        glUniform4f(uniform("Crop"),crop.x,crop.y,crop.z,crop.w);
        glUniform3f(uniform("WarpX"),1,0,0);glUniform3f(uniform("WarpY"),0,1,0);
        if(main&&evidence&&evidence->hasAffine) {
            const float blend=reduced?1:float(std::clamp((time-evidenceStart_)/.8,0.,1.));const auto& a=evidence->affine;
            glUniform3f(uniform("WarpX"),1+(a[0]-1)*blend,a[1]*blend,a[2]*blend);
            glUniform3f(uniform("WarpY"),a[3]*blend,1+(a[4]-1)*blend,a[5]*blend);
        }
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,0x88E0);glDrawArrays(GL_TRIANGLES,0,6);
        Annotation annotation;annotation.label=label;annotation.evidence=evidenceIndex;annotation.main=main;
        annotation.corners={ProjectScenePoint(p,0,0),ProjectScenePoint(p,1,0),ProjectScenePoint(p,1,1),ProjectScenePoint(p,0,1)};
        annotations_.push_back(std::move(annotation));
    };
    const auto active=std::find_if(cards.begin(),cards.end(),[&](const auto& c){return c.id==snapshot.progress.captureId;});
    const ProcessingCard* hero=active==cards.end()?(cards.empty()?nullptr:&cards.front()):&*active;
    // Local fields and reliability are defined in the fixed reference domain.
    if(stage_==ProcessingStage::LocalAlignment&&!cards.empty())hero=&cards.front();
    const bool preparing=stage_==ProcessingStage::Preparing||stage_==ProcessingStage::Assets;
    std::vector<std::size_t> order;
    const std::size_t selected=active==cards.end()?0:std::size_t(active-cards.begin());
    if(!cards.empty())order.push_back(selected);
    if(selected!=0&&!cards.empty())order.push_back(0);
    for(std::size_t offset=1;offset<cards.size();++offset) {
        const auto i=(selected+cards.size()-offset)%cards.size();if(i!=0)order.push_back(i);
    }
    unsigned shown=0;
    for(const auto i:order) {
        if(shown>=(preparing?9u:4u))break;
        const auto& card=cards[i];if(!preparing&&hero&&card.id==hero->id)continue;
        auto image=images_.find(card.id);if(image==images_.end()&&!preparing)continue;
        auto pose=ProcessingPlanePose(stage_,shown++,cards.size(),card.group,time,reduced);
        float ratio=image==images_.end()?1.5f:float(image->second.width)/std::max(1u,image->second.height);
        pose.height=pose.width*size.x/size.y/ratio;
        paint(card.id,pose,image==images_.end()?0:image->second.texture,"Group "+std::to_string(card.group+1),{.42f,.46f,.52f,preparing?.9f:.48f},0,-1,false);
    }
    if(!preparing) {
        ProcessingPlane pose;pose.x=.43f;pose.y=.47f;pose.width=.49f;pose.yaw=reduced?0:-.08f;
        unsigned texture=0;float ratio=1.5f;std::string label="Source capture",id=hero?hero->id:"active";int raster=-1;
        if(hero){auto it=images_.find(hero->id);if(it!=images_.end()){texture=it->second.texture;ratio=float(it->second.width)/it->second.height;label=hero->label;}}
        unsigned firstEvidence=0;
        if(evidence&&!evidence->rasters.empty()&&evidence->rasters[0].kind==EvidenceKind::Image&&
           (stage_==ProcessingStage::Groups||stage_==ProcessingStage::Guide||stage_==ProcessingStage::Blend||stage_==ProcessingStage::Reconstruction)) {
            texture=evidenceImages_[0].texture;const auto& r=evidence->rasters[0];ratio=float(r.width)/r.height;
            label=r.label;id=hero?hero->id:"active";raster=0;firstEvidence=1;
        }
        pose.height=std::min(.60f,pose.width*size.x/size.y/ratio);pose.width=pose.height*size.y/size.x*ratio;
        paint(id,pose,texture,label,{.88f,.91f,.96f,1},0,raster,true);
        unsigned supportCount=0;
        if(evidence)for(unsigned i=firstEvidence;i<evidence->rasters.size()&&supportCount<2;++i) {
            const auto& r=evidence->rasters[i];ProcessingPlane support;
            support.x=.82f;support.y=.28f+supportCount*.42f;support.width=.235f;support.yaw=reduced?0:.16f;
            support.height=std::min(.25f,support.width*size.x/size.y*r.height/r.width);
            support.width=support.height*size.y/size.x*r.width/r.height;
            ImVec4 tint=r.kind==EvidenceKind::Rejection?ImVec4(.91f,.61f,.42f,1):ImVec4(.72f,.86f,.91f,1);
            paint("evidence-"+std::to_string(i),support,evidenceImages_[i].texture,r.label,tint,int(r.kind),int(i),false);++supportCount;
        }
        if(evidence&&supportCount==0&&evidence->curve.empty()&&texture) {
            ProcessingPlane detail;detail.x=.82f;detail.y=.44f;detail.width=.22f;detail.height=detail.width*size.x/size.y/ratio;detail.yaw=0;
            paint("detail",detail,texture,"Measured region",{.72f,.86f,.91f,1},0,-1,false,
                {std::clamp(evidence->focusX-.12f,0.f,.76f),std::clamp(evidence->focusY-.12f,0.f,.76f),.24f,.24f});
        }
    }
    for(auto it=poses_.begin();it!=poses_.end();)if(!alive.count(it->first))it=poses_.erase(it);else ++it;
    if(msFbo_) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,msFbo_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo_);
        glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    if(!reduced&&dissolveDuration_>0&&time-dissolveStart_<dissolveDuration_) {
        float t=float(std::clamp((time-dissolveStart_)/dissolveDuration_,0.,1.));t=t*t*(3-2*t);
        const Vertex quad[]={{-1,1,0,1,0,0},{1,1,0,1,1,0},{1,-1,0,1,1,1},
                             {-1,1,0,1,0,0},{1,-1,0,1,1,1},{-1,-1,0,1,0,1}};
        glBindFramebuffer(GL_FRAMEBUFFER,fbo_);glBindTexture(GL_TEXTURE_2D,previousOutput_);
        glUniform1i(uniform("Fullscreen"),1);glUniform4f(uniform("Tint"),1,1,1,1-t);
        glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,0x88E0);glDrawArrays(GL_TRIANGLES,0,6);
    }
    return output_;
}
float ProcessingSceneRenderer::AnnotationOpacity() const {
    const float t=float(std::clamp((drawTime_-layoutChangedAt_)/.85,0.,1.));
    return t*t*(3-2*t);
}
}
