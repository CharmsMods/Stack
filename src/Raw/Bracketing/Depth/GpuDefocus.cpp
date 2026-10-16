#include "Depth.h"
#include "GpuRuntime.h"
#include "DefocusShader.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing::Depth {
bool RenderDefocusGpu(const std::vector<float>& rgb,unsigned width,unsigned height,
    const ReconstructionDepth& depth,const DepthSettings& settings,double originX,double originY,
    double stepX,double stepY,std::vector<float>& output,const Raw::OpenGlTaskExecutor& executor,
    const std::function<bool()>& cancel,std::string& error){
    if(rgb.size()!=std::size_t(width)*height*3||!width||!height||stepX<=0||stepY<=0){error="Invalid depth-of-field raster.";return false;}
    if(cancel&&cancel()){error="Canceled";return false;}
    output=rgb;if(!settings.enabled||settings.blurRadius<=0||!depth.Available())return true;
    if(!executor){error="Depth-of-field rendering requires an OpenGL compute context.";return false;}
    ProcessingRequest request;request.executeOpenGlTask=executor;request.shouldCancel=cancel;GpuRuntime gpu(request);
    constexpr unsigned side=512;const double radius=settings.blurRadius/std::max(stepX,stepY);
    const int halo=int(std::ceil(1.5*radius))+2;const auto maximum=std::size_t(side+2*halo)*(side+2*halo);
    GLuint program=0,input=0,map=0,work=0,result=0,samples=0;
    if(!gpu.Run([&](std::string& e){program=gpu.Program(Shaders::Defocus,e);if(!program)return false;
        input=gpu.Buffer(maximum*3*sizeof(float));map=gpu.Buffer(depth.inverseDepth.size()*2*sizeof(float));
        gpu.Upload(map,0,depth.inverseDepth.size()*sizeof(float),depth.inverseDepth.data());
        gpu.Upload(map,depth.inverseDepth.size()*sizeof(float),depth.confidence.size()*sizeof(float),depth.confidence.data());
        work=gpu.Buffer(maximum*12*sizeof(float));result=gpu.Buffer(maximum*3*sizeof(float));samples=gpu.Buffer(maximum*2*sizeof(float));return true;},error))return false;
    for(unsigned top=0;top<height;top+=side)for(unsigned left=0;left<width;left+=side){
        const unsigned x0=unsigned(std::max(0,int(left)-halo)),y0=unsigned(std::max(0,int(top)-halo));
        const unsigned x1=std::min(width,left+side+halo),y1=std::min(height,top+side+halo),w=x1-x0,h=y1-y0;
        std::vector<float> pixels(std::size_t(w)*h*3);
        for(unsigned y=0;y<h;++y)std::copy_n(rgb.data()+(std::size_t(y+y0)*width+x0)*3,w*3,pixels.data()+std::size_t(y)*w*3);
        const auto dispatch=[&](int mode,int layer){gpu.Use(program);gpu.Bind(0,input);gpu.Bind(1,map);gpu.Bind(2,work);gpu.Bind(3,result);gpu.Bind(4,samples);
            gpu.I2("size",w,h);gpu.I2("mapSize",depth.width,depth.height);gpu.F2("sensorSize",float(depth.sensorWidth),float(depth.sensorHeight));
            gpu.F4("coordinates",float(originX+x0*stepX),float(originY+y0*stepY),float(stepX),float(stepY));
            gpu.F4("settings",float(settings.focus),float(settings.focusRange),float(radius),float(layer));gpu.I("mode",mode);gpu.Dispatch((w+15)/16,(h+15)/16);};
        if(!gpu.Run([&](std::string&){gpu.Upload(input,0,pixels.size()*sizeof(float),pixels.data());dispatch(0,0);return true;},error))return false;
        for(int layer=0;layer<16;++layer)if(!gpu.Run([&](std::string&){dispatch(1,layer);dispatch(2,layer);dispatch(3,layer);return true;},error))return false;
        if(!gpu.Run([&](std::string&){dispatch(4,0);gpu.Read(result,pixels.size()*sizeof(float),pixels.data());return true;},error))return false;
        for(unsigned y=top;y<std::min(height,top+side);++y)
            std::copy_n(pixels.data()+(std::size_t(y-y0)*w+left-x0)*3,std::min(side,width-left)*3,output.data()+(std::size_t(y)*width+left)*3);
    }
    return true;
}
}
