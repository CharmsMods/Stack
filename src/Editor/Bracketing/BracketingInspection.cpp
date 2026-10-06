#include "BracketingSession.h"
#include "Raw/Bracketing/InspectionImage.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
std::shared_ptr<const Raw::Bracketing::CapturePreview> UpdateBracketingInspection(
    BracketingSession& ui,int view,const std::string& frame) {
    using namespace Raw::Bracketing;
    if(ui.detailMode||!ui.result||(!ui.result->analysis&&!ui.result->panorama)||ui.HasInteractivePreview()||view==2||view==6)return {};
    // Saved draft inputs still inspect the published result, whose preparation
    // and group order belong to its own recipe.
    if(ui.processRequired)return {};
    const auto kind=(view==1||view==3||view==5)?InspectionImageKind::Capture:view==4?InspectionImageKind::Group:InspectionImageKind::Result;
    const auto identity=ui.result->identity+"/"+std::to_string(view)+"/"+(kind==InspectionImageKind::Capture?frame:std::to_string(ui.selectedGroup))+
        "/"+std::to_string(ui.inspectionEv)+"/"+std::to_string(ui.showClipping)+"/"+std::to_string(ui.compareSplit);
    if(identity!=ui.inspectionRequested) {
        ui.inspectionRequested=identity;ui.inspectionChangedAt=ImGui::GetTime();
        if(ui.inspectionJob)ui.inspectionJob->canceled=true;
    }
    if(ui.inspectionJob&&ui.inspectionJob->done) {
        auto job=ui.inspectionJob;ui.inspectionJob.reset();
        if(!job->canceled&&job->identity==identity) {
            ui.inspectionImage=std::move(job->image);ui.inspectionPixels=std::move(job->rgba);
            ui.inspectionIdentity=identity;ui.inspectionError=job->error;ui.textureIdentity.clear();
        }
    }
    if(ui.inspectionIdentity==identity)return ui.inspectionImage;
    if(!ui.inspectionJob&&ImGui::GetTime()-ui.inspectionChangedAt>.12) {
        ui.inspectionImage.reset();ui.inspectionPixels.clear();ui.inspectionError.clear();
        auto job=std::make_shared<BracketingSession::InspectionJob>();job->identity=identity;ui.inspectionJob=job;
        ProcessingRequest request;request.recipe=ui.recipe;request.analysis=ui.result->analysis;
        const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
        request.memoryBudgetBytes=budget.valid?budget.budgetBytes:0;
        request.shouldCancel=[job]{return job->canceled.load();};
        GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
        const auto result=ui.result;const auto group=ui.selectedGroup;
        const auto ev=ui.inspectionEv,split=ui.compareSplit;const bool showClipping=ui.showClipping;
        if(view==5)request.memoryBudgetBytes/=2;
        if(!Async::TaskSystem::Get().SubmitHighPriority([job,request,result,kind,frame,group,maximum,view,ev,split,showClipping] {
            try {
                const auto image=RenderInspectionImage(request,*result,kind,frame,group,unsigned(std::max(1,maximum)));
                const auto comparison=view==5&&!job->canceled?RenderInspectionImage(request,*result,InspectionImageKind::Result,{},0,unsigned(std::max(1,maximum))):nullptr;
                if(image&&!job->canceled) {
                    const auto width=image->width,height=image->height;
                    job->rgba.resize(std::size_t(width)*height*4);
                    const auto& metadata=result->preview.metadata;
                    for(unsigned y=0;y<height&&!job->canceled;++y)for(unsigned x=0;x<width;++x) {
                        const bool left=comparison&&float(x)/width<split;
                        const auto& pixels=left?*comparison:*image;
                        const auto p=(std::size_t(y)*pixels.height/height)*pixels.width+std::size_t(x)*pixels.width/width;
                        const float gain=std::exp2(ev)*float(!left&&kind==InspectionImageKind::Capture&&view!=1?pixels.exposureScale:1.);
                        float camera[3]{};
                        for(unsigned c=0;c<3;++c)camera[c]=pixels.rgb[p*3+c]*gain*metadata.cameraWhiteBalance[c]/std::max(1e-6f,metadata.cameraWhiteBalance[1]);
                        const auto output=(std::size_t(y)*width+x)*4;
                        for(unsigned c=0;c<3;++c) {
                            float value=0;for(unsigned k=0;k<3;++k)value+=metadata.cameraToSrgb[c*3+k]*camera[k];
                            value=std::max(0.f,value);value/=1+value;
                            value=value<=.0031308f?value*12.92f:1.055f*std::pow(value,1/2.4f)-.055f;
                            if(showClipping&&pixels.clipped[p])value=c==1?0.f:1.f;
                            job->rgba[output+c]=std::uint8_t(std::clamp(value,0.f,1.f)*255+.5f);
                        }
                        job->rgba[output+3]=pixels.coverage.empty()?255:std::uint8_t(std::clamp(pixels.coverage[p],0.f,1.f)*255+.5f);
                    }
                    auto dimensions=std::make_shared<CapturePreview>();dimensions->width=width;dimensions->height=height;
                    dimensions->exposureScale=image->exposureScale;job->image=std::move(dimensions);
                }
            }
            catch(const std::exception& e){job->error=e.what();}
            job->done=true;
        })) {job->error="Could not load inspection pixels.";job->done=true;}
    }
    return {};
}
}
