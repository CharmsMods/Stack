#include "Depth.h"
#include "GpuRuntime.h"
#include <algorithm>
namespace Raw::Bracketing::Depth {
bool ResizeOutputGpu(const ProcessingRequest& request,const std::vector<float>& rgb,unsigned width,unsigned height,Preview& preview,std::string& error){
    if(!preview.width||!preview.height||preview.width>width||preview.height>height){error="Invalid depth preview extent.";return false;}
    constexpr auto shader=R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=8) in;
layout(std430,binding=0) readonly buffer Input {float source[];};
layout(std430,binding=1) buffer Output {float result[];};
uniform ivec2 inputSize,outputSize;uniform int inputTop,outputTop,rows;
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(p.x>=outputSize.x||p.y>=rows)return;
 ivec2 outp=p+ivec2(0,outputTop),a=outp*inputSize/outputSize,b=(outp+1)*inputSize/outputSize;
 vec3 sum=vec3(0);for(int y=a.y;y<b.y;++y)for(int x=a.x;x<b.x;++x){int i=3*((y-inputTop)*inputSize.x+x);sum+=vec3(source[i],source[i+1],source[i+2]);}
 sum/=float((b.x-a.x)*(b.y-a.y));int i=3*(p.y*outputSize.x+p.x);result[i]=sum.r;result[i+1]=sum.g;result[i+2]=sum.b;
})GLSL";
    GpuRuntime gpu(request);GLuint program=0,input=0,output=0;
    const unsigned maximumRows=std::min(height,unsigned(std::uint64_t(17)*height/preview.height+1));
    if(!gpu.Run([&](std::string& e){program=gpu.Program(shader,e);if(!program)return false;
        input=gpu.Buffer(std::size_t(maximumRows)*width*3*sizeof(float));output=gpu.Buffer(std::size_t(preview.width)*16*3*sizeof(float));return true;},error))return false;
    preview.resultRgb.resize(std::size_t(preview.width)*preview.height*3);
    for(unsigned top=0;top<preview.height;top+=16){const unsigned rows=std::min(16u,preview.height-top);
        const unsigned y0=std::uint64_t(top)*height/preview.height,y1=std::uint64_t(top+rows)*height/preview.height;
        if(!gpu.Run([&](std::string&){gpu.Upload(input,0,std::size_t(y1-y0)*width*3*sizeof(float),rgb.data()+std::size_t(y0)*width*3);
            gpu.Use(program);gpu.Bind(0,input);gpu.Bind(1,output);gpu.I2("inputSize",width,height);gpu.I2("outputSize",preview.width,preview.height);
            gpu.I("inputTop",y0);gpu.I("outputTop",top);gpu.I("rows",rows);gpu.Dispatch((preview.width+15)/16,(rows+7)/8);
            gpu.Read(output,std::size_t(rows)*preview.width*3*sizeof(float),preview.resultRgb.data()+std::size_t(top)*preview.width*3);return true;},error))return false;
    }
    return true;
}
}
