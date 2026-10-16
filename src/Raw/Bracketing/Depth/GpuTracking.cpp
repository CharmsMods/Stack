#include "GpuRuntime.h"
#include "TrackingShaders.h"
namespace Raw::Bracketing::Depth {
bool TrackFeatures(GpuRuntime& gpu,const GpuGuide& a,const GpuGuide& b,GpuTracks& tracks,bool dense,std::string& error,const GpuTracks* seed){
    const auto longest=std::max(a.width,a.height);
    tracks.columns=dense?std::max(1u,unsigned(std::uint64_t(512)*a.width/longest)):std::max(1u,a.width/20);
    tracks.rows=dense?std::max(1u,unsigned(std::uint64_t(512)*a.height/longest)):std::max(1u,a.height/20);
    GLuint program=0;
    if(!gpu.Run([&](std::string& e){program=gpu.Program(Shaders::Track,e);if(!program)return false;
        tracks.buffer=gpu.Buffer(std::size_t(tracks.columns)*tracks.rows*8*sizeof(float));return true;},error))return false;
    // Bounded dispatches release the shared context between batches.
    const auto count=tracks.columns*tracks.rows;
    for(unsigned first=0;first<count;first+=256)if(!gpu.Run([&](std::string&){
        gpu.Use(program);gpu.Bind(0,a.buffer);gpu.Bind(1,b.buffer);gpu.Bind(2,tracks.buffer);
        gpu.Bind(3,seed?seed->buffer:tracks.buffer);gpu.I2("seedGrid",seed?seed->columns:0,seed?seed->rows:0);
        gpu.I2("size",a.width,a.height);gpu.I2("grid",tracks.columns,tracks.rows);
        for(unsigned level=0;level<5;++level)gpu.I(("offsets["+std::to_string(level)+"]").c_str(),a.offsets[level]);
        gpu.I("dense",dense?1:0);gpu.I("first",first);gpu.Dispatch(std::min(4u,(count-first+63)/64));return true;
    },error))return false;
    return true;
}
}
