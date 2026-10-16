#pragma once
#include "../Processor.h"
#include "Renderer/GLLoader.h"
#include <array>
#include <vector>
#include <unordered_map>

namespace Raw::Bracketing::Depth {
// Request-owned resources. Every access, including release, uses the host's
// blocking context handoff. No context or mutable shader state is global.
class GpuRuntime {
public:
    explicit GpuRuntime(const ProcessingRequest& request):request_(request){}
    ~GpuRuntime();
    bool Run(const Raw::OpenGlTask&,std::string&);
    GLuint Program(const char*,std::string&);
    GLuint Buffer(std::size_t bytes,const void* data=nullptr);
    void Bind(unsigned binding,GLuint buffer);
    void Use(GLuint program);
    void I(const char*,int);void I2(const char*,int,int);
    void F(const char*,float);void F2(const char*,float,float);
    void F4(const char*,float,float,float,float);
    void Dispatch(unsigned x,unsigned y=1,unsigned z=1);
    void Read(GLuint,std::size_t bytes,void* data);
    void Upload(GLuint,std::size_t offset,std::size_t bytes,const void* data);
    bool Check(std::string&);
    const ProcessingRequest& Request()const{return request_;}
private:
    const ProcessingRequest& request_;
    std::vector<GLuint> buffers_,programs_;
    std::unordered_map<std::string,GLuint> compiled_;
    GLuint current_=0;
};
struct GpuGuide {GLuint buffer=0;unsigned width=0,height=0,stride=4;std::array<unsigned,5> offsets{};};
struct GpuTracks {GLuint buffer=0;unsigned columns=0,rows=0;};
bool BuildGuide(GpuRuntime&,const PreparedDataset&,std::size_t,GpuGuide&,std::string&);
bool TrackFeatures(GpuRuntime&,const GpuGuide&,const GpuGuide&,GpuTracks&,bool dense,std::string&,const GpuTracks* seed=nullptr);
}
