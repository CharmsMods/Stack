#ifdef _WIN32
#include "WindowPresenter.h"
#include "ScRgbShader.h"
#include "Renderer/GLHelpers.h"
#include <limits>
#ifndef GL_DYNAMIC_READ
#define GL_DYNAMIC_READ 0x88E9
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 (GL_TEXTURE0+1)
#endif

namespace Stack::Hdr {
namespace {
// The GL compositor can contain custom draw callbacks, changing textures or
// animations with identical ImGui geometry. Compare final half-float pixels
// on the GPU. Only a four-byte completion flag crosses on unchanged frames.
const std::string Compare=std::string("#version 430 core\n")+ScRgbConversion+R"(
layout(local_size_x=16,local_size_y=16) in;
layout(binding=0) uniform sampler2D scene;
layout(binding=0,rgba16f) uniform image2D previous;
layout(binding=1,rgba16f) uniform writeonly image2D outputFrame;
layout(location=0) uniform float whiteScale;
layout(location=1) uniform float headroom;
layout(location=2) uniform bool forceAll;
layout(std430,binding=0) buffer Dirty {uint changed;};
shared uint groupChanged;
void main() {
    if(gl_LocalInvocationIndex==0) groupChanged=0u;
    barrier();
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    ivec2 size=textureSize(scene,0);
    if(all(lessThan(p,size))) {
        vec4 s=texelFetch(scene,p,0);
        if(forceAll || any(notEqual(s,imageLoad(previous,p)))) {
            imageStore(previous,p,s);
            imageStore(outputFrame,ivec2(p.x,size.y-1-p.y),toWindowsColor(s,whiteScale,headroom));
            atomicOr(groupChanged,1u);
        }
    }
    barrier();
    if(gl_LocalInvocationIndex==0 && groupChanged!=0u) atomicOr(changed,1u);
})";
}
bool WindowPresenter::Transfer() {
    const size_t bytes=size_t(width)*height*8;
    if(!pbo) {
        if(bytes>(std::numeric_limits<GLsizeiptr>::max)()) { Fail("HDR transfer exceeds available address space."); return false; }
        for(int i=0;i<2;++i) {
            converted[i]=GLHelpers::CreateStorageTexture(width,height,GL_RGBA16F);
            targets[i]=converted[i] ? GLHelpers::CreateFBO(converted[i]) : 0;
            if(!targets[i]) { Fail("Insufficient memory for the HDR transfer."); return false; }
        }
        if(!compare) compare=GLHelpers::CreateComputeProgram(Compare.c_str());
        if(!compare) { Fail("The HDR transfer shader is unavailable."); return false; }
        glGenBuffers(1,&flag); glBindBuffer(GL_SHADER_STORAGE_BUFFER,flag);
        glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(unsigned),nullptr,GL_DYNAMIC_READ);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
        glGenBuffers(1,&pbo); glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER,bytes,nullptr,GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        try {transferBytes.resize(bytes/2);} catch(const std::bad_alloc&) {Fail("Insufficient memory for HDR transfer.");return false;}
    }
    bool ready=false;
    if(pending) {
        const auto result=glClientWaitSync(pending,0,0);
        if(result==GL_TIMEOUT_EXPIRED) return false;
        if(result==GL_WAIT_FAILED) {Fail("HDR transfer synchronization failed.");return false;}
        glDeleteSync(pending); pending=nullptr;
        if(phase==Phase::Comparing) {
            unsigned changed=0;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,flag);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(changed),&changed);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
            if(changed) {
                phase=Phase::Reading;
            } else phase=Phase::Ready;
        } else if(phase==Phase::Reading) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
            glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,bytes,transferBytes.data());
            glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
            context->UpdateSubresource(back.Get(),0,nullptr,transferBytes.data(),width*8,0);
            phase=Phase::Ready; ready=true;
        }
    }
    if(phase==Phase::Ready) {
        unsigned zero=0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,flag);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(zero),&zero);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,flag);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,scene);
        glBindImageTexture(0,converted[0],0,GL_FALSE,0,GL_READ_WRITE,GL_RGBA16F);
        glBindImageTexture(1,converted[1],0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA16F);
        glUseProgram(compare);
        glUniform1f(0,display.white/80.f); glUniform1f(1,display.peak/display.white);
        glUniform1i(2,!hasPrevious || previousWhite!=display.white || previousPeak!=display.peak);
        glDispatchCompute((width+15)/16,(height+15)/16,1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|0x400 /* framebuffer */);
        glBindImageTexture(0,0,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA16F);
        glBindImageTexture(1,0,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA16F);
        glUseProgram(0); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);
        glBindTexture(GL_TEXTURE_2D,0);
        hasPrevious=true; previousWhite=display.white; previousPeak=display.peak;
        phase=Phase::Comparing;
    }
    if(phase==Phase::Reading) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,targets[1]);
        glReadBuffer(GL_COLOR_ATTACHMENT0); glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
        glPixelStorei(GL_PACK_ROW_LENGTH,0); glPixelStorei(GL_PACK_SKIP_PIXELS,0); glPixelStorei(GL_PACK_SKIP_ROWS,0);
        glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE); glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,width,height,GL_RGBA,0x140B /* GL_HALF_FLOAT */,nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0); glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    }
    pending=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0); glFlush();
    if(!pending) {Fail("HDR transfer could not be scheduled.");return false;}
    return ready;
}
}
#endif
