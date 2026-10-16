#include "GpuGeometry.h"
#include "GeometryMathShader.h"
#include "ModelShaders.h"
#include "PoseShaders.h"

namespace Raw::Bracketing::Depth {
bool FitGeometry(GpuRuntime& gpu,const GpuGuide& guide,const GpuTracks& tracks,GpuPose& pose,std::string& error){
    GLuint models=0,modelProgram=0,poseProgram=0;
    const auto code=[](const char* body){return std::string("#version 430 core\n")+Shaders::GeometryMath+body;};
    if(!gpu.Run([&](std::string& e){modelProgram=gpu.Program(code(Shaders::Model).c_str(),e);
        poseProgram=gpu.Program(code(Shaders::Pose).c_str(),e);if(!modelProgram||!poseProgram)return false;
        models=gpu.Buffer(1537*16*sizeof(float));pose.buffer=gpu.Buffer(36*24*sizeof(float));return true;},error))return false;
    const auto model=[&](int mode,unsigned first,unsigned count){return gpu.Run([&](std::string&){
        gpu.Use(modelProgram);gpu.Bind(0,tracks.buffer);gpu.Bind(1,models);gpu.I("count",tracks.columns*tracks.rows);
        gpu.I("mode",mode);gpu.I("first",first);gpu.F2("size",float(guide.width),float(guide.height));gpu.Dispatch(count);return true;},error);};
    for(unsigned first=0;first<1536;first+=64)if(!model(0,first,64))return false;
    if(!model(1,0,1))return false;
    for(unsigned i=0;i<3;++i)if(!model(2,0,1))return false;
    for(unsigned mode=0;mode<2;++mode)if(!gpu.Run([&](std::string&){
        gpu.Use(poseProgram);gpu.Bind(0,tracks.buffer);gpu.Bind(1,models);gpu.Bind(2,pose.buffer);
        gpu.I("count",tracks.columns*tracks.rows);gpu.I("mode",mode);gpu.F2("size",float(guide.width),float(guide.height));
        gpu.Dispatch(mode==0?35:1);return true;},error))return false;
    std::array<float,1537*16> modelData{};std::array<float,36*24> poseData{};
    if(!gpu.Run([&](std::string&){gpu.Read(models,sizeof(modelData),modelData.data());gpu.Read(pose.buffer,sizeof(poseData),poseData.data());return true;},error))return false;
    std::copy_n(modelData.data()+1536*16+12,4,pose.counts.begin());
    std::copy_n(poseData.data()+35*24+16,4,pose.camera.begin());std::copy_n(poseData.data()+35*24+20,4,pose.status.begin());
    pose.accepted=pose.status[1]>0;return true;
}
}
