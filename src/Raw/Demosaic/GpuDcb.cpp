#include "GpuDemosaicInternal.h"
#include "GpuDcbShaders.h"

namespace Raw::Demosaic::GpuInternal {
Texture Dcb(Context& c,bool refine) {
    using namespace Shaders;
    auto original=c.Pass("dcb-initialize",DcbInitialize,GL_RGBA32F);
    Texture rgb;
    {
        auto horizontal=c.Pass("dcb-candidate",DcbCandidate,GL_RGBA32F,original.id,0,0,0);
        auto vertical=c.Pass("dcb-candidate",DcbCandidate,GL_RGBA32F,original.id,0,0,1);
        rgb=c.Pass("dcb-decide",DcbDecide,GL_RGBA32F,original.id,horizontal.id,vertical.id);
    }
    const auto causal=[](const char* body){return std::string(DcbCausalHelpers)+body;};
    auto map=[&](){rgb=c.Pass("dcb-map",DcbMap,GL_RGBA32F,rgb.id);};
    auto correct=[&](int mode){rgb=c.Pass("dcb-correct",causal(DcbCorrect),GL_RGBA32F,rgb.id,0,0,mode);};
    auto color=[&](){
        rgb=c.Pass("dcb-color",DcbColor,GL_RGBA32F,rgb.id,0,0,0);
        rgb=c.Pass("dcb-color",DcbColor,GL_RGBA32F,rgb.id,0,0,1);
    };
    for(int iteration=0;iteration<3;++iteration) {
        for(int pass=0;pass<3;++pass) rgb=c.Causal("dcb-nyquist",causal(DcbNyquist),rgb.id);
        map();correct(0);
    }
    color();
    rgb=c.Causal("dcb-smooth",causal(DcbSmooth),rgb.id);
    map();correct(1);
    for(int pass=0;pass<3;++pass) {map();correct(0);}
    map();
    rgb=c.Pass("dcb-restore",DcbRestore,GL_RGBA32F,rgb.id,original.id);
    original.Reset();color();
    if(refine) {
        rgb=c.Causal("dcb-refine",causal(DcbRefine),rgb.id);
        auto chroma=c.Pass("dcb-known-chroma",DcbKnownChroma,GL_RG32F,rgb.id);
        chroma=c.Pass("dcb-diagonal-chroma",DcbChromaDiagonal,GL_RG32F,chroma.id);
        chroma=c.Pass("dcb-axial-chroma",DcbChromaAxial,GL_RG32F,chroma.id);
        rgb=c.Causal("dcb-final-color",causal(DcbFinalColor),rgb.id,chroma.id);
    }
    return c.Pass("dcb-finish",DcbFinish,GL_RGBA32F,rgb.id);
}
}
