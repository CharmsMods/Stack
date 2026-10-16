#pragma once
#include "GpuRuntime.h"
namespace Raw::Bracketing::Depth {
struct GpuPose {GLuint buffer=0;bool accepted=false;std::array<float,4> counts{},camera{},status{};};
bool FitGeometry(GpuRuntime&,const GpuGuide&,const GpuTracks&,GpuPose&,std::string&);
}
