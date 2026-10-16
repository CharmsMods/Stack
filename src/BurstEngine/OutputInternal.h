#pragma once
#include "Output.h"
#include "Raw/ViewTransform.h"
#include <array>
#include <stdexcept>

namespace Stack::BurstEngine::Output::Internal {
inline void CheckCanceled(const Request& request) {
    if(request.shouldCancel&&request.shouldCancel())throw std::runtime_error("Output canceled.");
}
inline void Progress(const Request& request,double fraction,const char* stage) {
    CheckCanceled(request);
    if(request.reportProgress)request.reportProgress(fraction,stage);
}
struct Image {
    const Raw::RawMetadata& metadata;
    std::vector<float> mosaic;
    std::array<float,9> cameraToWorking{};
    Raw::ViewTransform::PreparedTransform display{};
    unsigned width=0,height=0;
    unsigned outputWidth=0,outputHeight=0;
    float exposure=1;
    std::array<float,3> Pixel(unsigned x,unsigned y) const;
};
void WriteDng(const std::filesystem::path&,const Image&,const Request&);
void WriteFinished(const std::filesystem::path&,const Image&,const Request&);
}
