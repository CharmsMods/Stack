// SPDX-License-Identifier: GPL-3.0-only
#include "MaterialRenderer.h"
#include "Renderer/Glass/EmbeddedMaterial.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Stack::Renderer::Glass {
namespace {
constexpr const char* Vertex=R"(#version 430 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);})";
// Transfer is confined to captured UI pixels. Alpha is opaque at this boundary.
constexpr const char* Transfer=R"(#version 430 core
layout(binding=0) uniform sampler2D source;
layout(location=0) uniform int encode;
layout(location=1) uniform vec4 region;
in vec2 uv;out vec4 color;
void main(){vec3 c=max(texture(source,region.xy+uv*region.zw).rgb,vec3(0));
if(encode==0)c=mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));
else c=mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-.055,step(vec3(.0031308),c));
color=vec4(c,1);})";
std::atomic<std::uint64_t> nextAllocation{1};
stack::glass::gl::UniformDispatch Dispatch() {
    return {nullptr,
        [](void*,std::uint32_t p,const char* n)->std::int32_t{return glGetUniformLocation(p,n);},
        [](void*,std::uint32_t p){glUseProgram(p);},
        [](void*,std::int32_t l,std::int32_t v){glUniform1i(l,v);},
        [](void*,std::int32_t l,float v){glUniform1f(l,v);},
        [](void*,std::int32_t l,float x,float y){glUniform2f(l,x,y);},
        [](void*,std::int32_t l,float x,float y,float z){glUniform3f(l,x,y,z);},
        [](void*,std::int32_t l,std::int32_t n,const float* v){glUniform4fv(l,n,v);}};
}
}
std::size_t MaterialRenderer::RequiredBytes(int w,int h) {
    if(w<=0||h<=0||double(w)*h>4194304.)return BudgetBytes+1;
    std::size_t bytes=std::size_t(w)*h*12; // encoded RGBA8 + linear working RGBA16F
    do {bytes+=std::size_t(w)*h*8; if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);}while(true);
    return bytes;
}
void MaterialRenderer::BeginFrame(){for(auto& target:targets_){target.leased=false;target.composing=false;}}
bool MaterialRenderer::Initialize(){
    if(!error_.empty())return false;
    if(material_)return true;
    glGetIntegerv(0x8872,&limits_.fragmentTextureUnits); // GL_MAX_TEXTURE_IMAGE_UNITS
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limits_.maximumTextureSize);
    material_=GLHelpers::CreateShaderProgram(Vertex,MaterialSource);
    transfer_=GLHelpers::CreateShaderProgram(Vertex,Transfer);
    glGenVertexArrays(1,&vao_);
    if(!material_||!transfer_||!vao_||!uniforms_.initialize(Dispatch(),material_,limits_,stack::glass::gl::InactiveUniformPolicy::RequireAll)){
        error_="Liquid Glass could not initialize its material shaders.";return false;
    }
    opacityLocation_=glGetUniformLocation(material_,"uLayerOpacity");
    restOpacityLocation_=glGetUniformLocation(material_,"uRestFillOpacity");
    restColorLocation_=glGetUniformLocation(material_,"uRestFillLinear");
    waterLocation_=glGetUniformLocation(material_,"uWaterMode");
    strainLocation_=glGetUniformLocation(material_,"uLogStrain");
    joinedCountLocation_=glGetUniformLocation(material_,"uJoinedCount");
    joinedShapesLocation_=glGetUniformLocation(material_,"uJoinedShapes[0]");
    joinedMetricsLocation_=glGetUniformLocation(material_,"uJoinedMetrics[0]");
    if(std::min({opacityLocation_,restOpacityLocation_,restColorLocation_,waterLocation_,strainLocation_,
        joinedCountLocation_,joinedShapesLocation_,joinedMetricsLocation_})<0) {
        error_="Liquid Glass v0.3 bindings are incomplete.";return false;
    }
    return true;
}
void MaterialRenderer::Release(Target& t){
    const unsigned textures[]={t.source,t.encoded,t.composite},fbos[]={t.sourceFbo,t.encodedFbo,t.compositeFbo};
    glDeleteTextures(3,textures);glDeleteFramebuffers(3,fbos);bytes_-=t.bytes;t={};
}
MaterialRenderer::Target* MaterialRenderer::Acquire(int w,int h){
    if(!Initialize()||w>limits_.maximumTextureSize||h>limits_.maximumTextureSize)return nullptr;
    const auto bytes=RequiredBytes(w,h);
    if(bytes>BudgetBytes)return nullptr;
    // Prefer an exact complete texture. An oversized valid subrect is not a
    // supported source mapping. Unused slots may be recycled after BeginFrame.
    for(auto& target:targets_)if(!target.leased&&target.width==w&&target.height==h){target.leased=true;return &target;}
    Target* candidate=nullptr;
    for(auto& target:targets_)if(!target.leased && (bytes_+bytes>BudgetBytes || targets_.size()>=64)){
        if(!candidate)candidate=&target;
        Release(target);
        if(bytes_+bytes<=BudgetBytes)break;
    }
    if(bytes_+bytes>BudgetBytes)return nullptr;
    if(!candidate){targets_.emplace_back();candidate=&targets_.back();}
    auto& t=*candidate;Release(t);t.leased=true;
    GLState::PixelUnpackState unpack;unpack.ConfigureTightCpuUpload();
    unsigned* textures[]={&t.source,&t.encoded,&t.composite};
    unsigned* fbos[]={&t.sourceFbo,&t.encodedFbo,&t.compositeFbo};
    t.mips=1+int(std::floor(std::log2(std::max(w,h))));
    for(int i=0;i<3;++i){
        glGenTextures(1,textures[i]);glBindTexture(GL_TEXTURE_2D,*textures[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,i==0?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        for(int mip=0,mw=w,mh=h;mip<(i==0?t.mips:1);++mip,mw=std::max(1,mw/2),mh=std::max(1,mh/2))
            glTexImage2D(GL_TEXTURE_2D,mip,i==1?GL_RGBA8:GL_RGBA16F,mw,mh,0,GL_RGBA,i==1?GL_UNSIGNED_BYTE:GL_FLOAT,nullptr);
        glGenFramebuffers(1,fbos[i]);glBindFramebuffer(GL_FRAMEBUFFER,*fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,*textures[i],0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
            unpack.Restore();Release(t);error_="Liquid Glass could not allocate a complete GPU surface.";return nullptr;
        }
    }
    unpack.Restore();t.width=w;t.height=h;t.bytes=bytes;bytes_+=bytes;t.allocation=nextAllocation.fetch_add(3);++counters_.allocations;
    return &t;
}
bool MaterialRenderer::Capture(Target& t,int x,int y,std::uint64_t revision){
    // Acquire may change framebuffer bindings; the caller restores its host
    // target before calling Capture. Cache reuse requires caller-owned revision.
    const GLState::FramebufferState host(true);
    const bool srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB)!=GL_FALSE;
    const bool same=revision&&t.ready&&revision==t.revision&&t.captureX==x&&t.captureY==y&&
        t.hostFramebuffer==host.drawFramebuffer&&t.hostBuffer==host.drawBuffer&&t.hostSrgb==srgb;
    t.captureX=x;t.captureY=y;t.hostFramebuffer=host.drawFramebuffer;t.hostBuffer=host.drawBuffer;t.hostSrgb=srgb;
    if(same){++counters_.cacheHits;return true;}
    t.ready=false;
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,host.drawFramebuffer);glReadBuffer(host.drawBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,t.encodedFbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(x,y,x+t.width,y+t.height,0,0,t.width,t.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,t.sourceFbo);glViewport(0,0,t.width,t.height);
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D,t.encoded);
    glUseProgram(transfer_);glUniform1i(0,0);glUniform4f(1,0,0,1,1);glBindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);
    glBindTexture(GL_TEXTURE_2D,t.source);glGenerateMipmap(GL_TEXTURE_2D);
    ++counters_.captures;++counters_.mipBuilds;t.revision=revision;t.ready=true;
    host.Restore(true);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);
    return true;
}
bool MaterialRenderer::Draw(Target& t,const stack::glass::gl::UniformPacket& packet,int x,int y,int w,int h,MaterialEffects effects){
    if(!BeginComposition(t,x,y,w,h))return false;
    if(!Composite(t,packet,x,y,w,h,effects)){t.composing=false;return false;}
    return EndComposition(t);
}
bool MaterialRenderer::BeginComposition(Target& t,int x,int y,int w,int h){
    const int lx=x-t.captureX,ly=y-t.captureY;
    if(!t.ready || t.composing || w<=0 || h<=0 || lx<0 || ly<0 ||
        w>t.width || h>t.height || lx>t.width-w || ly>t.height-h)return false;
    t.outputX=x;t.outputY=y;t.outputWidth=w;t.outputHeight=h;
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_SCISSOR_TEST);
    // The immutable source already contains the decoded scene and semantic
    // underlay. Copy its linear base once; peers never sample this destination.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,t.sourceFbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,t.compositeFbo);glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(lx,ly,lx+w,ly+h,lx,ly,lx+w,ly+h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,t.hostFramebuffer);glDrawBuffer(t.hostBuffer);
    if(t.hostSrgb)glEnable(GL_FRAMEBUFFER_SRGB);
    t.composing=true;++counters_.compositions;return true;
}
bool MaterialRenderer::Composite(Target& t,const stack::glass::gl::UniformPacket& packet,int x,int y,int w,int h,MaterialEffects effects){
    if(!t.ready || !t.composing || w<=0 || h<=0 || x<t.outputX || y<t.outputY ||
        w>t.outputWidth || h>t.outputHeight || x-t.outputX>t.outputWidth-w || y-t.outputY>t.outputHeight-h ||
        !stack::glass::gl::validate(packet,limits_) ||
        !std::isfinite(effects.opacity) || effects.opacity<0 || effects.opacity>1 ||
        !stack::glass::finite(effects.logStrain) || std::hypot(effects.logStrain.x,effects.logStrain.y)>.30001f ||
        !std::isfinite(effects.restFillOpacity) || effects.restFillOpacity<0 || effects.restFillOpacity>1 ||
        !stack::glass::finite(effects.restFillLinear) || (effects.joined && !effects.joined->Valid()))return false;
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    const int lx=x-t.captureX,ly=y-t.captureY;
    glBindFramebuffer(GL_FRAMEBUFFER,t.compositeFbo);glViewport(lx,ly,w,h);
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D,t.source);glBindVertexArray(vao_);
    const auto uploaded=uniforms_.upload(packet);
    if(uploaded){
        glUniform1f(opacityLocation_,effects.opacity);glUniform1f(restOpacityLocation_,effects.restFillOpacity);
        glUniform3f(restColorLocation_,effects.restFillLinear.x,effects.restFillLinear.y,effects.restFillLinear.z);
        glUniform1f(waterLocation_,effects.clearWater?1.f:0.f);glUniform2f(strainLocation_,effects.logStrain.x,effects.logStrain.y);
        glUniform1i(joinedCountLocation_,effects.joined?effects.joined->count:0);
        if(effects.joined) {
            glUniform4fv(joinedShapesLocation_,effects.joined->count,effects.joined->shapes[0].data());
            glUniform4fv(joinedMetricsLocation_,effects.joined->count,effects.joined->metrics[0].data());
        }
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES,0,3);++counters_.draws;
    }
    glDisable(GL_BLEND);glBindFramebuffer(GL_FRAMEBUFFER,t.hostFramebuffer);glDrawBuffer(t.hostBuffer);
    if(t.hostSrgb)glEnable(GL_FRAMEBUFFER_SRGB);
    return bool(uploaded);
}
bool MaterialRenderer::EndComposition(Target& t){
    if(!t.composing)return false;
    const int x=t.outputX,y=t.outputY,w=t.outputWidth,h=t.outputHeight;
    const int lx=x-t.captureX,ly=y-t.captureY;
    glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER,t.hostFramebuffer);glDrawBuffer(t.hostBuffer);glViewport(x,y,w,h);
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindVertexArray(vao_);
    glBindTexture(GL_TEXTURE_2D,t.composite);glUseProgram(transfer_);glUniform1i(0,1);
    glUniform4f(1,float(lx)/t.width,float(ly)/t.height,float(w)/t.width,float(h)/t.height);glDrawArrays(GL_TRIANGLES,0,3);
    if(t.hostSrgb)glEnable(GL_FRAMEBUFFER_SRGB);
    t.composing=false;++counters_.encodes;return true;
}
void MaterialRenderer::Shutdown(){
    for(auto& t:targets_)Release(t);targets_.clear();used_=0;
    if(material_)glDeleteProgram(material_);if(transfer_)glDeleteProgram(transfer_);if(vao_)glDeleteVertexArrays(1,&vao_);
    material_=transfer_=vao_=0;uniforms_={};limits_={};error_.clear();
}
}
