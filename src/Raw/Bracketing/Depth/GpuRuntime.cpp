#include "GpuRuntime.h"
#include "Renderer/GLHelpers.h"
#include <utility>

#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
namespace Raw::Bracketing::Depth {
namespace {
struct Bindings {
    GLint program=0,buffer=0;std::array<GLint,6> indexed{};
    Bindings(){glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&buffer);
        for(unsigned i=0;i<indexed.size();++i)glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&indexed[i]);}
    ~Bindings(){for(unsigned i=0;i<indexed.size();++i)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,indexed[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);glUseProgram(program);}
};
}
GpuRuntime::~GpuRuntime() {
    if(buffers_.empty()&&programs_.empty())return;
    std::string ignored;
    // When the host context has gone away it owns reclamation. Never issue GL
    // deletes on this CPU worker as an attempted recovery.
    try {
        if(request_.executeOpenGlTask)request_.executeOpenGlTask([&](std::string&){
            Bindings state;for(auto p:programs_)glDeleteProgram(p);
            glDeleteBuffers(GLsizei(buffers_.size()),buffers_.data());return true;},ignored);
    } catch(...) { /* Context teardown owns driver cleanup after host failure. */ }
}
bool GpuRuntime::Run(const Raw::OpenGlTask& task,std::string& error) {
    if(request_.shouldCancel&&request_.shouldCancel()){error="Canceled";return false;}
    if(!request_.executeOpenGlTask){error="Image-derived depth requires an OpenGL 4.3 compute context.";return false;}
    return request_.executeOpenGlTask([&](std::string& e){
        if(!glDispatchCompute||!glGetBufferSubData){e="Depth compute is unavailable.";return false;}
        Bindings state;return task(e)&&Check(e);
    },error);
}
GLuint GpuRuntime::Program(const char* source,std::string& error) {
    if(const auto found=compiled_.find(source);found!=compiled_.end())return found->second;
    auto p=GLHelpers::CreateComputeProgram(source);
    if(!p){error="Could not compile the depth compute shader.";return 0;}
    programs_.push_back(p);compiled_.emplace(source,p);return p;
}
GLuint GpuRuntime::Buffer(std::size_t bytes,const void* data){
    GLuint b=0;glGenBuffers(1,&b);buffers_.push_back(b);glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);
    glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,data,GL_DYNAMIC_DRAW);return b;
}
void GpuRuntime::Bind(unsigned slot,GLuint b){glBindBufferBase(GL_SHADER_STORAGE_BUFFER,slot,b);}
void GpuRuntime::Use(GLuint p){current_=p;glUseProgram(p);}
void GpuRuntime::I(const char* n,int v){glUniform1i(glGetUniformLocation(current_,n),v);}
void GpuRuntime::I2(const char* n,int x,int y){glUniform2i(glGetUniformLocation(current_,n),x,y);}
void GpuRuntime::F(const char* n,float v){glUniform1f(glGetUniformLocation(current_,n),v);}
void GpuRuntime::F2(const char* n,float x,float y){glUniform2f(glGetUniformLocation(current_,n),x,y);}
void GpuRuntime::F4(const char* n,float x,float y,float z,float w){glUniform4f(glGetUniformLocation(current_,n),x,y,z,w);}
void GpuRuntime::Dispatch(unsigned x,unsigned y,unsigned z){glDispatchCompute(x,y,z);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);}
void GpuRuntime::Read(GLuint b,std::size_t bytes,void* data){glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,bytes,data);}
void GpuRuntime::Upload(GLuint b,std::size_t offset,std::size_t bytes,const void* data){glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);glBufferSubData(GL_SHADER_STORAGE_BUFFER,offset,bytes,data);}
bool GpuRuntime::Check(std::string& error){const auto code=glGetError();if(code==GL_NO_ERROR)return true;
    error="Depth GPU operation failed (OpenGL "+std::to_string(code)+").";return false;}
}
