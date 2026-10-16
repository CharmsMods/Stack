#pragma once
#include "GpuDemosaic.h"
#include "Renderer/GLLoader.h"
#include <array>
#include <unordered_map>
#include <vector>

#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif

namespace Raw::Demosaic::GpuInternal {
struct Program {
    GLuint id=0;
    GLint extent=-1,pattern=-1,mode=-1,rowOffset=-1,cameraToSrgb=-1;
};
struct TexturePool {
    std::vector<std::pair<GLuint,GLenum>> free;
    ~TexturePool();
};
struct Texture {
    GLuint id=0;
    GLenum format=GL_RGBA32F;
    TexturePool* pool=nullptr;
    Texture()=default;
    Texture(GLuint value,GLenum type,TexturePool* owner):id(value),format(type),pool(owner){}
    Texture(const Texture&)=delete;
    Texture& operator=(const Texture&)=delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    ~Texture();
    void Reset();
    GLuint Release();
};
struct Context {
    GLuint mosaic;
    int width,height;
    CfaPattern pattern;
    std::array<float,9> cameraToSrgb{1,0,0,0,1,0,0,0,1};
    const std::function<bool()>& cancel;
    std::unordered_map<std::string,Program>& programs;
    TexturePool pool;
    GLuint statusBuffer=0;
    Context(GLuint source,int w,int h,CfaPattern cfa,const std::function<bool()>& canceled,
        std::unordered_map<std::string,Program>& cache);
    ~Context();
    void CheckCancel() const;
    Texture Pass(const char* name,const std::string& body,GLenum format,
        GLuint a=0,GLuint b=0,GLuint c=0,int mode=0);
    // Ordered CPU sweeps are evaluated as a causal recurrence on the GPU.
    // Past neighbors read the previous relaxation iterate; future neighbors
    // read the immutable original. Exact float convergence is required.
    Texture Causal(const char* name,const std::string& body,GLuint original,
        GLuint auxiliary=0,int mode=0);
    unsigned int Status();
    void ResetStatus();
};
Texture Directional(Context& context,DemosaicMethod method);
Texture Lmmse(Context& context,bool refine);
Texture Dcb(Context& context,bool refine);
Texture Ppg(Context& context);
Texture Vng(Context& context,bool fourColors);
Texture Ahd(Context& context);
}
