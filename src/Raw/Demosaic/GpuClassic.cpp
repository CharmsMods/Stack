// SPDX-License-Identifier: CDDL-1.0
// GPU orchestration for the LibRaw-derived algorithms. See the accompanying
// legal/third_party/LibRaw-Demosaic-NOTICE.txt and CDDL license.
#include "GpuDemosaicInternal.h"
#include "GpuDemosaicShaders.h"
#include "GpuClassicShaders.h"
#include "ClassicDemosaicTerms.h"

namespace Raw::Demosaic::GpuInternal {
Texture Ppg(Context& c) {
    auto green=c.Pass("ppg-green",Shaders::PpgGreen,GL_R32F);
    return c.Pass("ppg-color",Shaders::PpgColor,GL_RGBA32F,green.id);
}
Texture Vng(Context& c,bool fourColors) {
    auto linear=c.Pass("classic-bilinear",Shaders::ClassicBilinear,GL_RGBA32F,0,0,0,fourColors?1:0);
    std::string source="const int terms[384]=int[384](";
    bool first=true;
    for(const auto& term:Classic::VngTerms) for(int value:term) {
        if(!first) source+=',';first=false;source+=std::to_string(value);
    }
    source+=");\n";source+=Shaders::Vng;
    return c.Pass("vng",source,GL_RGBA32F,linear.id,0,0,fourColors?1:0);
}
Texture Ahd(Context& c) {
    Texture horizontal,vertical;
    {
        auto green=c.Pass("ahd-green",Shaders::AhdGreen,GL_R32F,0,0,0,0);
        horizontal=c.Pass("ppg-color",Shaders::PpgColor,GL_RGBA32F,green.id,0,0,1);
        green=c.Pass("ahd-green",Shaders::AhdGreen,GL_R32F,0,0,0,1);
        vertical=c.Pass("ppg-color",Shaders::PpgColor,GL_RGBA32F,green.id,0,0,1);
    }
    Texture homogeneity;
    {
        auto hLab=c.Pass("ahd-lab",Shaders::AhdLab,GL_RGBA32F,horizontal.id);
        auto vLab=c.Pass("ahd-lab",Shaders::AhdLab,GL_RGBA32F,vertical.id);
        homogeneity=c.Pass("ahd-homogeneity",Shaders::AhdHomogeneity,GL_RG32F,hLab.id,vLab.id);
    }
    return c.Pass("ahd-select",Shaders::AhdSelect,GL_RGBA32F,horizontal.id,vertical.id,homogeneity.id);
}
}
