#pragma once
#include "Raw/ReconstructionDepth.h"
#include "../Processor.h"

namespace Raw::Bracketing::Depth {
bool Estimate(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
// CPU oracle retained for focused numerical comparisons, never a production
// fallback when a GPU context is unavailable.
bool RenderDefocus(const std::vector<float>& rgb,unsigned width,unsigned height,
    const ReconstructionDepth&,const DepthSettings&,double originX,double originY,
    double stepX,double stepY,std::vector<float>& output,const std::function<bool()>&,std::string&);
bool RenderDefocusGpu(const std::vector<float>& rgb,unsigned width,unsigned height,
    const ReconstructionDepth&,const DepthSettings&,double originX,double originY,
    double stepX,double stepY,std::vector<float>& output,const Raw::OpenGlTaskExecutor&,
    const std::function<bool()>&,std::string&);
bool Finish(const ProcessingRequest&,BracketingResult&,std::string&);
bool DemosaicOutputGpu(const ProcessingRequest&,const PreparedDataset&,const std::vector<float>& mosaic,
    std::vector<float>& rgb,std::string&);
bool ResizeOutputGpu(const ProcessingRequest&,const std::vector<float>& rgb,unsigned width,unsigned height,
    Preview&,std::string&);
Preview ReadResultDetail(const ProcessingRequest&,const BracketingResult&,unsigned x,unsigned y,unsigned size);
}
