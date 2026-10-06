#include "Kernel.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedComputeBindings.h"
#include <cstddef>
#include <limits>

#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif

namespace Raw::Bracketing::Sr {
namespace {
static_assert(sizeof(PixelAccumulator)==192);
static_assert(sizeof(Vec4)==16);
const char* shader=R"GLSL(
#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer Input { vec4 data[]; };
struct Pixel { vec4 kernel; vec4 narrow[3]; vec4 broad[3]; vec4 narrowUncertainty; vec4 broadUncertainty; vec4 crossNoise; vec4 crossFrameWeight; vec4 crossUncertainty; };
layout(std430,binding=1) buffer Output { Pixel pixels[]; };
uniform ivec2 outputSize,mapSize;
uniform vec3 target; // reference origin, sensor step
uniform vec3 mapGeometry;
uniform vec4 regionValue, colorValue;
vec4 mapped(vec2 xy) {
    vec2 uv=clamp((xy-mapGeometry.xy)/mapGeometry.z,vec2(0),vec2(mapSize-1));
    ivec2 p=min(ivec2(uv),mapSize-2);vec2 f=uv-vec2(p);
    int i=p.y*mapSize.x+p.x;
    return mix(mix(data[i],data[i+1],f.x),mix(data[i+mapSize.x],data[i+mapSize.x+1],f.x),f.y);
}
void main() {
    ivec4 region=ivec4(regionValue),colors=ivec4(colorValue);
    ivec2 outp=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(outp,outputSize)))return;
    int outi=outp.y*outputSize.x+outp.x;
    vec4 m=mapped(target.xy+vec2(outp)*target.z);
    if(m.z<=0||any(isnan(m))||any(isinf(m)))return;
    ivec2 center=ivec2(floor(m.xy+.5));
    float headroom=1;
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
        ivec2 loc=center+ivec2(x,y)-region.xy;
        if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw)))continue;
        headroom=min(headroom,data[mapSize.x*mapSize.y+loc.y*region.z+loc.x].w);
    }
    if(headroom<=0)return;
    vec3 weights=vec3(0),bwts=vec3(0);Pixel p=pixels[outi];
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
        ivec2 q=center+ivec2(x,y),loc=q-region.xy;
        if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw)))continue;
        vec4 s=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x];
        if(s.w<=0)continue;
        int c=colors[((q.y&1)<<1)|(q.x&1)];vec2 d=vec2(q)-m.xy;
        float v=max(1e-10,s.y*max(0.,p.kernel.w)+s.z);
        float base=m.z*headroom*1e-5/(v+max(0.,m.w));
        float w=base*exp(-.5*(p.kernel.x*d.x*d.x+2*p.kernel.y*d.x*d.y+p.kernel.z*d.y*d.y));
        float bw=base*exp(-.5*dot(d,d));
        p.narrow[c].xyz+=vec3(w*s.x,w,w*w*v);weights[c]+=w;
        p.broad[c].xyz+=vec3(bw*s.x,bw,bw*bw*v);bwts[c]+=bw;
        p.crossNoise[c]+=w*bw*v;
    }
    for(int c=0;c<3;++c) {
        p.narrow[c].w+=weights[c]*weights[c];p.broad[c].w+=bwts[c]*bwts[c];
        p.narrowUncertainty[c]+=weights[c]*weights[c]*max(0.,m.w);
        p.broadUncertainty[c]+=bwts[c]*bwts[c]*max(0.,m.w);
        p.crossFrameWeight[c]+=weights[c]*bwts[c];
        p.crossUncertainty[c]+=weights[c]*bwts[c]*max(0.,m.w);
    }
    pixels[outi]=p;
}
)GLSL";
bool Check(std::string& error) {
    const auto code=glGetError();
    if(code==GL_NO_ERROR)return true;
    error="Super-resolution GPU operation failed: "+std::to_string(code);return false;
}
}
bool GpuKernel::Begin(const Tile& t,std::string& error) {
    if(!glDispatchCompute||!glBindBufferBase||!glMemoryBarrier||!glGetBufferSubData) {
        error="OpenGL compute is unavailable.";return false;
    }
    GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    if(major<4||(major==4&&minor<3)){error="Super-resolution needs OpenGL 4.3 for GPU execution.";return false;}
    Stack::Rendering::ScopedComputeBindings bindings;
    if(!program_)program_=GLHelpers::CreateComputeProgram(shader);
    if(!program_){error="Could not compile super-resolution kernel.";return false;}
    if(!input_)glGenBuffers(1,&input_);
    if(!output_)glGenBuffers(1,&output_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,output_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,t.pixels.size()*sizeof(PixelAccumulator),t.pixels.data(),GL_DYNAMIC_DRAW);
    return Check(error);
}
bool GpuKernel::Add(const Tile& t,const FrameRegion& f,std::string& error) {
    if(!Validate(t,f,error)||!program_||!output_)return false;
    Stack::Rendering::ScopedComputeBindings bindings;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,input_);
    const auto mapBytes=f.mapping.size()*sizeof(Vec4),sampleBytes=f.samples.size()*sizeof(Vec4);
    glBufferData(GL_SHADER_STORAGE_BUFFER,mapBytes+sampleBytes,nullptr,GL_DYNAMIC_DRAW);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,mapBytes,f.mapping.data());
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,mapBytes,sampleBytes,f.samples.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,input_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,output_);
    glUseProgram(program_);
    glUniform2i(glGetUniformLocation(program_,"outputSize"),t.width,t.height);
    glUniform2i(glGetUniformLocation(program_,"mapSize"),f.mapWidth,f.mapHeight);
    glUniform3f(glGetUniformLocation(program_,"target"),t.originX,t.originY,t.step);
    glUniform3f(glGetUniformLocation(program_,"mapGeometry"),f.mapOriginX,f.mapOriginY,f.mapStep);
    glUniform4f(glGetUniformLocation(program_,"regionValue"),float(f.left),float(f.top),float(f.width),float(f.height));
    glUniform4f(glGetUniformLocation(program_,"colorValue"),float(f.colors[0]),float(f.colors[1]),float(f.colors[2]),float(f.colors[3]));
    glDispatchCompute((t.width+7)/8,(t.height+7)/8,1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
    return Check(error);
}
bool GpuKernel::Read(Tile& t,std::string& error) {
    Stack::Rendering::ScopedComputeBindings bindings;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,output_);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,t.pixels.size()*sizeof(PixelAccumulator),t.pixels.data());
    return Check(error);
}
void GpuKernel::Release() {
    if(program_)glDeleteProgram(program_);
    if(input_)glDeleteBuffers(1,&input_);
    if(output_)glDeleteBuffers(1,&output_);
    program_=input_=output_=0;
}
}
