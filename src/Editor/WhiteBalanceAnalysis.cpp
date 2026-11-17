#include "WhiteBalanceAnalysis.h"
#include "Raw/WhiteBalanceEstimate.h"
#include "Raw/RawLoader.h"
#include "Raw/RawGpuPipeline.h"
#include "Raw/RawColorTransform.h"
#include "Editor/RawRenderService.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLStateGuards.h"
#include <atomic>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace Stack::Editor {
struct WhiteBalanceAnalysis::Job {
    std::atomic<bool> canceled{false},done{false};
    std::array<float,3> gains{1,1,1};
    std::string error;
};
WhiteBalanceAnalysis::~WhiteBalanceAnalysis(){Cancel();}
void WhiteBalanceAnalysis::Cancel(){if(job)job->canceled=true;job.reset();}
bool WhiteBalanceAnalysis::Start(std::shared_ptr<const Raw::RawImageData> raw,
    RawRecipe::RawDevelopmentRecipe recipe,std::optional<std::array<float,2>> sample) {
    if(job)return false;
    job=std::make_shared<Job>();auto task=job;
    if(!Async::TaskSystem::Get().SubmitHighPriority("Measuring white balance",[task,raw,recipe,sample]() mutable {
        try {
            if(!raw) {
                auto loaded=std::make_shared<Raw::RawImageData>();
                if(!Raw::RawLoader::LoadFile(recipe.source.sourcePath,*loaded,[task]{return task->canceled.load();}))
                    throw std::runtime_error("The RAW source could not be read.");
                raw=std::move(loaded);
            }
            if(!task->canceled)EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(
                [task,raw,recipe,sample](std::string& error) {
                    if(task->canceled)return false;
                    auto settings=RawRecipe::ToRawDevelopSettings(recipe);
                    settings.whiteBalanceMode=Raw::WhiteBalanceMode::Neutral;
                    settings.debugView=Raw::RawDebugView::DemosaicedCameraRgb;
                    settings.exposureStops=0;settings.encodeSrgbOutput=false;
                    settings.highlightMode=Raw::HighlightReconstructionMode::Off;
                    const auto matrix=Raw::BuildRawCameraToWorkingTransform(raw->metadata,settings,true);
                    const auto neutral=Raw::ResolveRawHighlightNeutral(matrix,settings);
                    settings.debugBypassCameraTransform=true;
                    settings.applyBaselineExposure=false;settings.toneCurvePoints.clear();
                    Raw::RawGpuPipeline pipeline;
                    const auto texture=pipeline.Render(*raw,settings,sample?2048:512,[task]{return task->canceled.load();});
                    if(!texture){error=pipeline.GetLastError();task->error=error;return false;}
                    const int w=pipeline.GetOutputWidth(),h=pipeline.GetOutputHeight();
                    std::vector<float> pixels(std::size_t(w)*h*4);
                    Renderer::GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
                    GLint old=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&old);
                    glBindTexture(GL_TEXTURE_2D,texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
                    glBindTexture(GL_TEXTURE_2D,old);pack.Restore();
                    return Raw::EstimateWhiteBalance(pixels,w,h,neutral,sample,task->gains,task->error);
                },task->error);
        } catch(const std::exception& e){task->error=e.what();}
        task->done=true;
    })) {job.reset();return false;}
    return true;
}
bool WhiteBalanceAnalysis::Poll(std::array<float,3>& gains,std::string& error) {
    if(!job||!job->done)return false;
    gains=job->gains;error=job->error;job.reset();return true;
}
}
