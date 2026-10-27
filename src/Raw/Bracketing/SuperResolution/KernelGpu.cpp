#include "Kernel.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedComputeBindings.h"
#include <algorithm>
#include <cstddef>
#include <limits>

#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif

namespace Raw::Bracketing::Sr {
namespace {
static_assert(sizeof(PixelAccumulator)==208);
static_assert(sizeof(Vec4)==16);
const char* shader=R"GLSL(
#version 430 core
layout(local_size_x=8,local_size_y=8) in;
layout(std430,binding=0) readonly buffer Input { vec4 data[]; };
struct Pixel { vec4 kernel; vec4 narrow[3]; vec4 broad[3]; vec4 narrowUncertainty; vec4 broadUncertainty; vec4 crossNoise; vec4 crossFrameWeight; vec4 crossUncertainty; vec4 detailBroadWeight; };
layout(std430,binding=1) buffer Output { Pixel pixels[]; };
uniform ivec2 outputSize,mapSize;
uniform vec3 target; // reference origin, sensor step
uniform vec3 mapGeometry;
uniform vec4 regionValue, colorValue;
uniform float detailPreference;
uniform vec4 supportGeometry; // reference origin, fixed raw step, categorical mode
uniform ivec2 supportSize;
uniform vec2 supportDomain; // complete Bayer reference domain, not source extent
uniform int depthSupportOffset; // packed per-output veto, -1 when not required
bool supported(vec2 xy,ivec4 region) {
    if(supportGeometry.w==0)return true;
    if(supportGeometry.w!=2||any(isnan(xy))||any(isinf(xy))||
       any(lessThan(xy,vec2(0)))||any(greaterThanEqual(xy,supportDomain)))return false;
    vec2 uv=(xy-supportGeometry.xy)/supportGeometry.z;
    if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(supportSize))))return false;
    ivec2 cell=ivec2(floor(uv));int i=cell.y*supportSize.x+cell.x;
    int offset=mapSize.x*mapSize.y+region.z*region.w;
    return data[offset+i/4][i%4]>0;
}
vec4 mapped(vec2 xy) {
    vec2 uv=clamp((xy-mapGeometry.xy)/mapGeometry.z,vec2(0),vec2(mapSize-1));
    ivec2 p=min(ivec2(uv),mapSize-2);vec2 f=uv-vec2(p);
    int i=p.y*mapSize.x+p.x;
    vec4 p00=data[i],p10=data[i+1],p01=data[i+mapSize.x],p11=data[i+mapSize.x+1];
    vec4 result=mix(mix(p00,p10,f.x),mix(p01,p11,f.x),f.y);
    if((f.x<1&&f.y<1&&p00.z<=0)||(f.x>0&&f.y<1&&p10.z<=0)||
       (f.x<1&&f.y>0&&p01.z<=0)||(f.x>0&&f.y>0&&p11.z<=0))result.z=0;
    return result;
}
void main() {
    ivec4 region=ivec4(regionValue),colors=ivec4(colorValue);
    ivec2 outp=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(outp,outputSize)))return;
    int outi=outp.y*outputSize.x+outp.x;
    if(depthSupportOffset>=0&&data[depthSupportOffset+outi/4][outi%4]<=0)return;
    vec2 reference=target.xy+vec2(outp)*target.z;
    if(!supported(reference,region))return;
    vec4 m=mapped(reference);
    if(m.z<=0||any(isnan(m))||any(isinf(m)))return;
    ivec2 center=ivec2(floor(m.xy+.5));
    float headroom=1;bvec3 usable=bvec3(false);
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
        ivec2 loc=center+ivec2(x,y)-region.xy;
        if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw)))continue;
        float h=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x].w;
        if(h>=0)headroom=min(headroom,h);
        if(h>0) {ivec2 q=center+ivec2(x,y);usable[colors[((q.y&1)<<1)|(q.x&1)]]=true;}
    }
    if(headroom<=0||!all(usable))return;
    vec3 weights=vec3(0),bwts=vec3(0);Pixel p=pixels[outi];
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x) {
        ivec2 q=center+ivec2(x,y),loc=q-region.xy;
        if(any(lessThan(loc,ivec2(0)))||any(greaterThanEqual(loc,region.zw)))continue;
        vec4 s=data[mapSize.x*mapSize.y+loc.y*region.z+loc.x];
        if(s.w<=0)continue;
        int c=colors[((q.y&1)<<1)|(q.x&1)];vec2 d=vec2(q)-m.xy;
        float v=max(1e-10,s.y*max(0.,p.kernel.w)+s.z);
        float base=m.z*headroom*1e-5/(v+max(0.,m.w));
        float w=detailPreference*base*exp(-.5*(p.kernel.x*d.x*d.x+2*p.kernel.y*d.x*d.y+p.kernel.z*d.y*d.y));
        float bw=base*exp(-.5*dot(d,d));
        p.narrow[c].xyz+=vec3(w*s.x,w,w*w*v);weights[c]+=w;
        p.broad[c].xyz+=vec3(bw*s.x,bw,bw*bw*v);bwts[c]+=bw;
        p.detailBroadWeight[c]+=bw*detailPreference;
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
    if(!program_) {
        program_=GLHelpers::CreateComputeProgram(shader);
        if(program_) {
            const char* names[]={"outputSize","mapSize","target","mapGeometry","detailPreference","regionValue","colorValue",
                "supportGeometry","supportSize","supportDomain","depthSupportOffset"};
            for(std::size_t i=0;i<uniforms_.size();++i)uniforms_[i]=glGetUniformLocation(program_,names[i]);
        }
    }
    if(!program_){error="Could not compile super-resolution kernel.";return false;}
    if(!input_)glGenBuffers(1,&input_);
    if(!output_)glGenBuffers(1,&output_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,output_);
    const auto bytes=t.pixels.size()*sizeof(PixelAccumulator);
    if(bytes>outputCapacity_) {
        glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,nullptr,GL_DYNAMIC_DRAW);
        if(!Check(error))return false;
        outputCapacity_=bytes;
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,bytes,t.pixels.data());
    return Check(error);
}
bool GpuKernel::Add(const Tile& t,const FrameRegion& f,std::string& error) {
    if(!Validate(t,f,error)||!program_||!output_)return false;
    Stack::Rendering::ScopedComputeBindings bindings;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,input_);
    // GLSL addresses vec4 records with signed ints. Check every offset and
    // padded allocation before converting byte counts to GL's signed size.
    const auto limit=std::min<std::size_t>(std::numeric_limits<int>::max(),
        std::size_t(std::numeric_limits<GLsizeiptr>::max())/sizeof(Vec4));
    const auto supportRecords=f.support.cells.size()/4+(f.support.cells.size()%4!=0);
    const auto depthRecords=f.depthSupport.values.size()/4+(f.depthSupport.values.size()%4!=0);
    if(f.support.cells.size()>std::size_t(std::numeric_limits<int>::max())||
       f.depthSupport.values.size()>std::size_t(std::numeric_limits<int>::max())||
       f.mapping.size()>limit||f.samples.size()>limit-f.mapping.size()||
       supportRecords>limit-f.mapping.size()-f.samples.size()||
       depthRecords>limit-f.mapping.size()-f.samples.size()-supportRecords) {
        error="Super-resolution input exceeds GPU addressable storage.";return false;
    }
    const auto mapBytes=f.mapping.size()*sizeof(Vec4),sampleBytes=f.samples.size()*sizeof(Vec4);
    const auto supportBytes=supportRecords*sizeof(Vec4);
    const auto depthBytes=depthRecords*sizeof(Vec4),depthOffset=f.mapping.size()+f.samples.size()+supportRecords;
    // Keep orphaning streamed input storage: the preceding capture can still
    // be reading it. In-place reuse here would introduce an implicit GPU wait.
    glBufferData(GL_SHADER_STORAGE_BUFFER,mapBytes+sampleBytes+supportBytes+depthBytes,nullptr,GL_DYNAMIC_DRAW);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,mapBytes,f.mapping.data());
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,mapBytes,sampleBytes,f.samples.data());
    if(supportBytes)glBufferSubData(GL_SHADER_STORAGE_BUFFER,mapBytes+sampleBytes,
        f.support.cells.size()*sizeof(float),f.support.cells.data());
    if(depthBytes)glBufferSubData(GL_SHADER_STORAGE_BUFFER,depthOffset*sizeof(Vec4),
        f.depthSupport.values.size()*sizeof(float),f.depthSupport.values.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,input_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,output_);
    glUseProgram(program_);
    glUniform2i(uniforms_[0],t.width,t.height);
    glUniform2i(uniforms_[1],f.mapWidth,f.mapHeight);
    glUniform3f(uniforms_[2],t.originX,t.originY,t.step);
    glUniform3f(uniforms_[3],f.mapOriginX,f.mapOriginY,f.mapStep);
    glUniform1f(uniforms_[4],f.detailPreference);
    glUniform4f(uniforms_[5],float(f.left),float(f.top),float(f.width),float(f.height));
    glUniform4f(uniforms_[6],float(f.colors[0]),float(f.colors[1]),float(f.colors[2]),float(f.colors[3]));
    glUniform4f(uniforms_[7],float(f.support.originRawX),float(f.support.originRawY),
        float(f.support.cellStepRawPixels),float(f.support.mode));
    glUniform2i(uniforms_[8],f.support.width,f.support.height);
    glUniform2f(uniforms_[9],float(f.support.rawWidth),float(f.support.rawHeight));
    glUniform1i(uniforms_[10],f.depthSupportRequired?int(depthOffset):-1);
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
    outputCapacity_=0;
    uniforms_={};
}
}
