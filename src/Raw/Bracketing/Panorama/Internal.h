#pragma once
#include "Panorama.h"
#include "../Processor.h"
#include <opencv2/core.hpp>
#include <opencv2/stitching/detail/matchers.hpp>
#include <filesystem>
#include <stdexcept>

namespace Raw::Bracketing::Panorama {
// Cached pixels are calibrated camera RGB, conservative variance, clipping.
inline constexpr unsigned PixelChannels=5;
struct Capture {
    Source source;
    RawMetadata metadata;
    unsigned width=0,height=0;
    double proxyScale=1;
    bool trustedNoise=false;
    std::filesystem::path pixels;
    cv::Mat proxy,guide,clipping;
};
struct Prepared {
    std::filesystem::path directory;
    bool removeOnRelease=false;
    std::shared_ptr<const Prepared> cacheOwner;
    std::vector<Capture> captures;
    std::vector<cv::detail::ImageFeatures> features;
    std::vector<cv::detail::MatchesInfo> matches;
    Layout layout;
    unsigned reference=0;
    ~Prepared();
};
struct Seams {
    cv::Mat owners;
    double scale=1;
};
inline void CheckCanceled(const ProcessingRequest& request) {
    if(request.shouldCancel&&request.shouldCancel())throw std::runtime_error("Canceled");
}
inline void Progress(const ProcessingRequest& request,ProcessingStage stage,double fraction,
    unsigned done=0,unsigned total=0,const std::string& capture={}) {
    CheckCanceled(request);
    ReportPresentation(request,stage,done,total,capture);
    if(request.reportProgress)request.reportProgress(fraction,ProcessingStageTitle(stage));
}
std::shared_ptr<Prepared> Prepare(const ProcessingRequest&);
void SolveLayout(const ProcessingRequest&,Prepared&);
void RefineLens(const ProcessingRequest&,Prepared&);
void ChooseCanvas(const ProcessingRequest&,Layout&);
cv::Point2d Project(const Camera&,const Layout&,double,double);
bool Unproject(const Camera&,const Layout&,double,double,cv::Point2d&);
Seams MatchBrightnessAndSeams(const ProcessingRequest&,Prepared&);
void Composite(const ProcessingRequest&,const Prepared&,const Seams&,BracketingResult&);
void MakePreview(BracketingResult&);
}
