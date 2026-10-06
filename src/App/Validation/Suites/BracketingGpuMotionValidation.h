#pragma once
#include "Raw/MultiFrameDenoise/GpuLocalMotion.h"
#include "Renderer/ScopedComputeBindings.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
// Called with the workflow's hidden render context current. Exercise reuse
// across changed captures and the GL bindings used by foreground rendering.
inline void ValidateBracketingGpuMotion() {
    using namespace Raw::Mfd;
    const auto check=[](bool okay,const std::string& error) {if(!okay) throw std::runtime_error(error);};
    Stack::Rendering::ScopedComputeBindings restore;
    CfaLayout layout;check(CfaLayout::TryCreate(Raw::CfaPattern::RGGB,layout),"GPU fixture CFA layout");
    std::array<CfaPyramidBasePlane,4> base;
    for(unsigned s=0;s<4;++s) {
        auto& plane=base[s];plane.site=static_cast<CfaSite>(s);plane.extent={96,96};
        plane.signal.resize(96*96);plane.variance.assign(96*96,.00001);plane.validMask.assign(96*96,1);
        for(unsigned y=0;y<96;++y) for(unsigned x=0;x<96;++x)
            plane.signal[y*96+x]=.3+.12*std::sin(x*.21+y*.13)+.08*std::cos(x*.07-y*.17)+s*.01;
    }
    RegistrationParameters parameters;parameters.pyramidLevels=2;
    CfaPlanePyramid reference,alternate;std::string error;
    check(BuildCfaPlanePyramid(layout,{192,192},base,parameters,reference,&error),error);
    alternate=reference;
    GLuint texture=0,buffer=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D_ARRAY,texture);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY,1,GL_R32F,8,8,4);
    glBindImageTexture(0,texture,0,GL_TRUE,0,GL_READ_ONLY,GL_R32F);
    glGenBuffers(1,&buffer);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,64,nullptr,GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    const auto bindings=[&] {
        GLint value=0;glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&value);
        check(value==static_cast<GLint>(texture),"GPU motion disturbed the caller's texture binding");
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&value);
        check(value==static_cast<GLint>(buffer),"GPU motion disturbed the caller's SSBO binding");
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&value);
        check(value==static_cast<GLint>(buffer),"GPU motion disturbed the caller's indexed SSBO");
        glGetIntegeri_v(GL_IMAGE_BINDING_NAME,0,&value);
        check(value==static_cast<GLint>(texture),"GPU motion disturbed the caller's image binding");
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&value);check(value==8,"GPU motion disturbed upload alignment");
        check(glGetError()==GL_NO_ERROR,"GPU motion left an OpenGL error");
    };
    {
        GpuLocalMotionDiscreteEvaluator evaluator;evaluator.BeginPair(reference,alternate);
        LocalMotionDirectionRequest direction;direction.reference=&reference;direction.source=&alternate;
        std::vector<LocalMotionDiscreteCandidate> candidates={{{96,96},{0,0},0,16},{{96,96},{2,0},0,16}};
        std::vector<LocalMotionDiscreteScore> scores;
        check(evaluator.Evaluate(direction,candidates,scores,error),error);bindings();
        check(scores[0].valid&&scores[0].robustCost<1e-7&&scores[1].robustCost>.01,
            "GPU candidate scoring lost the identity match");
        const auto uploads=evaluator.Diagnostics().uploadedLayers;
        check(evaluator.Evaluate(direction,candidates,scores,error),error);bindings();
        check(evaluator.Diagnostics().uploadedLayers==uploads,"Unchanged motion pyramids were uploaded twice");
        for(auto& levels:alternate.planes) for(auto& level:levels)
            for(auto& value:level.signal) value+=.03;
        evaluator.BeginPair(reference,alternate);
        check(evaluator.Evaluate(direction,candidates,scores,error),error);bindings();
        check(scores[0].robustCost>.1,"Reused alternate address left stale GPU source data");
        check(evaluator.Diagnostics().uploadedLayers==uploads+4,
            "A new capture reuploaded the fixed reference or omitted alternate layers");
        const auto forwardCost=scores[0].robustCost;
        std::swap(direction.reference,direction.source);
        check(evaluator.Evaluate(direction,candidates,scores,error),error);bindings();
        check(std::abs(scores[0].robustCost-forwardCost)<1e-5&&
            evaluator.Diagnostics().uploadedLayers==uploads+4,"Reverse search changed pair identity or upload ownership");
        direction.shouldCancel=[] {return true;};
        check(!evaluator.Evaluate(direction,candidates,scores,error),"Canceled GPU scoring continued");bindings();
    }
    bindings();glDeleteBuffers(1,&buffer);glDeleteTextures(1,&texture);
    std::cout<<"GPU motion: stable render bindings, persistent reference, changed-capture upload, reverse search and cancellation passed.\n";
}
}
