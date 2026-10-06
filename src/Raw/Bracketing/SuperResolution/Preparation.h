#pragma once
#include "Kernel.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/PreparedTileSampler.h"

namespace Raw::Bracketing::Sr {
Vec4 MapCapture(const PreparedSource&,float x,float y,bool reference);
bool BuildFrameRegion(const BracketingAnalysis&,const PreparedSource&,const Tile&,
    PreparedTileSampler&,FrameRegion&,const std::function<bool()>&,std::string&);
float SamplingDiversity(const PreparedDataset&);
Tile MakeTile(unsigned x,unsigned y,unsigned width,unsigned height,unsigned scale,
    const BracketingAnalysis&,bool diverse);
}
