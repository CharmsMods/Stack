#pragma once
#include "Raw/Curves/LightBands.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <stdexcept>

#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
namespace Stack::Renderer {
// Context-owned histogram processor. Only a 4 KiB histogram crosses to CPU;
// every valid source pixel contributes, including small bright populations.
class LightRangeAnalysis {
    unsigned int program=0, buffer=0;
public:
    void Clear() {
        if(program)glDeleteProgram(program);
        if(buffer)glDeleteBuffers(1,&buffer);
        program=buffer=0;
    }
    RawRecipe::LightRange Measure(unsigned int texture,int width,int height,
        bool rec2020,float middleGrey) {
        static constexpr const char* shader=R"(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) buffer Histogram { uint bins[1024]; };
shared uint groupBins[1024];
uniform sampler2D sourceImage;
uniform vec3 luma;
uniform float middleGrey;
void main() {
 uint lane=gl_LocalInvocationIndex;
 for(uint i=lane;i<1024u;i+=256u)groupBins[i]=0u;
 barrier();
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);
 if(all(lessThan(p,textureSize(sourceImage,0)))) {
  vec4 c=texelFetch(sourceImage,p,0);
  if(!any(isnan(c))&&!any(isinf(c))&&c.a>0.0) {
   float y=dot(c.rgb,luma);
   if(y>0.0&&!isnan(y)&&!isinf(y)) {
    int b=int(clamp(floor((log2(y)-log2(middleGrey)+32.0)*16.0),0.0,1023.0));
    atomicAdd(groupBins[b],1u);
   }
  }
 }
 barrier();
 for(uint i=lane;i<1024u;i+=256u)if(groupBins[i]>0u)atomicAdd(bins[i],groupBins[i]);
})";
        if(!program)program=GLHelpers::CreateComputeProgram(shader);
        if(!program)throw std::runtime_error("Light range analysis shader unavailable.");
        GLint previousProgram=0,previousBuffer=0,previousIndexed=0;
        glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&previousBuffer);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&previousIndexed);
        GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
        std::array<std::uint32_t,1024> bins{};
        while(glGetError()!=GL_NO_ERROR) {}
        if(!buffer)glGenBuffers(1,&buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(bins),bins.data(),GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);
        glUseProgram(program);
        GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glBindTexture(GL_TEXTURE_2D,texture);
        glUniform1i(glGetUniformLocation(program,"sourceImage"),active-GL_TEXTURE0);
        glUniform3f(glGetUniformLocation(program,"luma"),rec2020?.2627002f:.2126729f,rec2020?.6779981f:.7151522f,rec2020?.0593017f:.0721750f);
        glUniform1f(glGetUniformLocation(program,"middleGrey"),middleGrey);
        glDispatchCompute((width+15)/16,(height+15)/16,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(bins),bins.data());
        const auto error=glGetError();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,previousIndexed);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,previousBuffer);
        glUseProgram(previousProgram);binding.Restore();
        if(error!=GL_NO_ERROR)throw std::runtime_error("Light range analysis failed.");
        return RawRecipe::FitLightRange(bins);
    }
};
}
