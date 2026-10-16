#include "Depth.h"
#include "GpuRuntime.h"
#include "../ProcessingInternal.h"
#include "Raw/Demosaic/GpuDemosaicShaders.h"
#include <algorithm>

namespace Raw::Bracketing::Depth {
bool DemosaicOutputGpu(const ProcessingRequest& request,const PreparedDataset& data,const std::vector<float>& mosaic,
    std::vector<float>& rgb,std::string& error){
    const auto& source=data.sources[data.origin];const unsigned width=data.width,height=data.height;
    if(width<2||height<2||mosaic.size()!=std::size_t(width)*height){error="Invalid mosaic for depth-of-field output.";return false;}
    std::vector<float> gain(std::size_t(width)*height);
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    for(unsigned ty=0;ty<source.frame.tileRows;++ty)for(unsigned tx=0;tx<source.frame.tileColumns;++tx){
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        Mfd::PreparedRawTile tile;if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit)return false;
        for(unsigned y=0;y<tile.extent.height;++y)std::copy_n(tile.comparisonGain.data()+std::size_t(y)*tile.extent.width,
            tile.extent.width,gain.data()+std::size_t(tile.originY+y)*width+tile.originX);
    }
    const auto code=std::string(R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) readonly buffer Input {float inputData[];};
layout(std430,binding=1) buffer Output {float outputData[];};
uniform ivec2 size,coreSize,coreOffset,origin;uniform int pattern;
int reflected(int p,int s){int q=abs(p)%(2*(s-1));return q<s?q:2*(s-1)-q;}
float raw(ivec2 p){p=ivec2(reflected(p.x,size.x),reflected(p.y,size.y));int i=p.y*size.x+p.x;
 return inputData[i]*inputData[size.x*size.y+i];}
int cfa(ivec2 p){p+=origin;int i=((p.y&1)<<1)|(p.x&1);
 if(pattern==1)return int[4](0,1,1,2)[i];if(pattern==2)return int[4](2,1,1,0)[i];
 if(pattern==3)return int[4](1,2,0,1)[i];return int[4](1,0,2,1)[i];}
)GLSL")+Raw::Demosaic::Shaders::Malvar+R"GLSL(
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,coreSize)))return;
 vec3 rgb=mhc(p+coreOffset);int i=3*(p.y*coreSize.x+p.x);outputData[i]=rgb.r;outputData[i+1]=rgb.g;outputData[i+2]=rgb.b;}
)GLSL";
    GpuRuntime gpu(request);GLuint program=0,input=0,output=0;
    constexpr unsigned side=512;
    if(!gpu.Run([&](std::string& e){program=gpu.Program(code.c_str(),e);if(!program)return false;
        input=gpu.Buffer((side+4)*(side+4)*2*sizeof(float));output=gpu.Buffer(side*side*3*sizeof(float));return true;},error))return false;
    rgb.resize(std::size_t(width)*height*3);
    for(unsigned top=0;top<height;top+=side)for(unsigned left=0;left<width;left+=side){
        const unsigned x0=left<2?0:left-2,y0=top<2?0:top-2;
        const unsigned w=std::min(width,left+side+2)-x0,h=std::min(height,top+side+2)-y0;
        const unsigned cw=std::min(side,width-left),ch=std::min(side,height-top),n=w*h;
        std::vector<float> packed(std::size_t(n)*2),pixels(std::size_t(cw)*ch*3);
        for(unsigned y=0;y<h;++y){std::copy_n(mosaic.data()+std::size_t(y0+y)*width+x0,w,packed.data()+std::size_t(y)*w);
            std::copy_n(gain.data()+std::size_t(y0+y)*width+x0,w,packed.data()+n+std::size_t(y)*w);}
        if(!gpu.Run([&](std::string&){gpu.Upload(input,0,packed.size()*sizeof(float),packed.data());gpu.Use(program);
            gpu.Bind(0,input);gpu.Bind(1,output);gpu.I2("size",w,h);gpu.I2("coreSize",cw,ch);gpu.I2("coreOffset",left-x0,top-y0);
            gpu.I2("origin",x0,y0);gpu.I("pattern",int(source.frame.activeCfaPattern));gpu.Dispatch((cw+15)/16,(ch+15)/16);
            gpu.Read(output,pixels.size()*sizeof(float),pixels.data());return true;},error))return false;
        for(unsigned y=0;y<ch;++y)std::copy_n(pixels.data()+std::size_t(y)*cw*3,cw*3,rgb.data()+(std::size_t(top+y)*width+left)*3);
    }
    return true;
}
}
