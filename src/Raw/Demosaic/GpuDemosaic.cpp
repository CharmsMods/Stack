#include "GpuDemosaicInternal.h"
#include "GpuDemosaicShaders.h"
#include "GpuLmmseShaders.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace Raw::Demosaic {
namespace {
// The RAW shader resumes immediately after this operation. Preserve its
// program, sampling units, image binding and storage-buffer bindings.
struct ComputeState {
    GLint program=0,active=0,buffer=0,indexedBuffer=0;
    GLint textures[4]{},image[6]{};
    ComputeState() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&buffer);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&indexedBuffer);
        for(int i=0;i<4;++i) {
            glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
        }
        const GLenum fields[]={GL_IMAGE_BINDING_NAME,GL_IMAGE_BINDING_LEVEL,GL_IMAGE_BINDING_LAYERED,
            GL_IMAGE_BINDING_LAYER,GL_IMAGE_BINDING_ACCESS,GL_IMAGE_BINDING_FORMAT};
        for(int i=0;i<6;++i) glGetIntegeri_v(fields[i],0,&image[i]);
        glActiveTexture(active);
    }
    ~ComputeState() {
        for(int i=0;i<4;++i) {glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);}
        glActiveTexture(active);glUseProgram(program);
        glBindImageTexture(0,image[0],image[1],image[2],image[3],image[4],image[5]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,indexedBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    }
};
}
namespace GpuInternal {
TexturePool::~TexturePool() { for(const auto& item:free) glDeleteTextures(1,&item.first); }
Texture::Texture(Texture&& other) noexcept :id(other.id),format(other.format),pool(other.pool) {other.id=0;}
Texture& Texture::operator=(Texture&& other) noexcept {
    if(this!=&other) {Reset();id=other.id;format=other.format;pool=other.pool;other.id=0;} return *this;
}
Texture::~Texture(){Reset();}
void Texture::Reset() {
    if (!id) return;
    // Returning an image during exception cleanup must not throw if host
    // memory is exhausted. A failed pool insertion can simply delete it.
    try { pool->free.emplace_back(id,format); }
    catch (const std::bad_alloc&) { glDeleteTextures(1,&id); }
    id=0;
}
GLuint Texture::Release(){return std::exchange(id,0);}
Context::Context(GLuint source,int w,int h,CfaPattern cfa,const std::function<bool()>& canceled,
    std::unordered_map<std::string,Program>& cache):mosaic(source),width(w),height(h),pattern(cfa),cancel(canceled),programs(cache) {
    glGenBuffers(1,&statusBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,statusBuffer);
    const GLuint zero=0;
    glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(zero),&zero,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,statusBuffer);
    if(glGetError()!=GL_NO_ERROR) {glDeleteBuffers(1,&statusBuffer);throw std::runtime_error("Could not allocate demosaicing status buffer.");}
}
Context::~Context(){glDeleteBuffers(1,&statusBuffer);}
void Context::CheckCancel() const {if(cancel && cancel()) throw std::runtime_error("Demosaicing canceled.");}
void Context::ResetStatus() {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    const GLuint zero=0;glBindBuffer(GL_SHADER_STORAGE_BUFFER,statusBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(zero),&zero);
}
unsigned int Context::Status() {
    GLuint value=0;glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,statusBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(value),&value);
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Could not check GPU demosaicing completion.");
    return value;
}
Texture Context::Pass(const char* name,const std::string& body,GLenum format,GLuint a,GLuint b,GLuint c,int mode) {
    CheckCancel();
    auto& program=programs[name];
    if(!program.id) {
        std::string source=Shaders::Common;
        source.insert(source.find('\n',source.find("#version"))+1,std::string("#define OUT_FORMAT ")+(format==GL_R32F?"r32f\n":format==GL_RG32F?"rg32f\n":"rgba32f\n"));
        source+=body;
        // Preserve reference expression order, including adaptive decisions.
        // GLSL's default contraction/reassociation can flip equal gradients.
        {
            std::size_t start=0;
            while((start=source.find("vec4 evaluate(",start))!=std::string::npos) {
                source.insert(start,"precise ");start+=8+14;
            }
        }
        std::string compilerError;
        const GLuint shader=GLHelpers::CompileShader(GL_COMPUTE_SHADER,source.c_str(),&compilerError);
        if(!shader) throw std::runtime_error(std::string("Could not compile demosaicing pass: ")+name+". "+compilerError);
        program.id=glCreateProgram();glAttachShader(program.id,shader);glLinkProgram(program.id);glDeleteShader(shader);
        GLint linked=0;glGetProgramiv(program.id,GL_LINK_STATUS,&linked);
        if(!linked) {
            char log[2048]{};glGetProgramInfoLog(program.id,sizeof(log),nullptr,log);
            glDeleteProgram(program.id);program.id=0;
            throw std::runtime_error(std::string("Could not link demosaicing pass: ")+name+". "+log);
        }
        program.extent=glGetUniformLocation(program.id,"extent");program.pattern=glGetUniformLocation(program.id,"pattern");
        program.mode=glGetUniformLocation(program.id,"mode");program.rowOffset=glGetUniformLocation(program.id,"rowOffset");
        program.cameraToSrgb=glGetUniformLocation(program.id,"cameraToSrgb");
    }
    GLuint texture=0;
    const auto found=std::find_if(pool.free.begin(),pool.free.end(),[&](const auto& item){return item.second==format;});
    if(found!=pool.free.end()) {texture=found->first;pool.free.erase(found);}
    else texture=GLHelpers::CreateTextureFromData(nullptr,width,height,format,
        format==GL_R32F?GL_RED:format==GL_RG32F?GL_RG:GL_RGBA,GL_FLOAT);
    if(!texture) throw std::runtime_error(std::string("Could not allocate GPU demosaicing pass: ")+name);
    Texture output(texture,format,&pool);
    glUseProgram(program.id);glUniform2i(program.extent,width,height);
    glUniform1i(program.pattern,static_cast<int>(pattern));glUniform1i(program.mode,mode);
    if(program.cameraToSrgb>=0) glUniformMatrix3fv(program.cameraToSrgb,1,GL_TRUE,cameraToSrgb.data());
    const GLuint inputs[]={mosaic,a,b,c};
    for(int i=0;i<4;++i) {glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,inputs[i]);}
    glBindImageTexture(0,texture,0,GL_FALSE,0,GL_WRITE_ONLY,format);
    // Bound dispatch sizes allow cancellation between batches without dropping
    // data or reconstructing only the visible viewport.
    for(int row=0;row<height;row+=256) {
        CheckCancel();glUniform1i(program.rowOffset,row);
        glDispatchCompute((width+15)/16,(std::min(256,height-row)+15)/16,1);
    }
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT);
    if(glGetError()!=GL_NO_ERROR) throw std::runtime_error(std::string("GPU demosaicing pass failed: ")+name);
    return output;
}
Texture Context::Causal(const char* name,const std::string& body,GLuint original,GLuint auxiliary,int mode) {
    Texture previous;
    const int maximum=width+2*height+1;
    for(int step=0;step<maximum;++step) {
        ResetStatus();
        auto next=Pass(name,body,GL_RGBA32F,previous.id?previous.id:original,auxiliary,original,mode);
        previous=std::move(next);
        // Only four status bytes cross to the CPU. Image samples stay on GPU.
        if((step&7)==7 || step+1==maximum) {
            const auto status=Status();
            if(status&2u) throw std::runtime_error("DCB produced a non-finite sample.");
            if(!(status&1u)) return previous;
        }
    }
    throw std::runtime_error("Ordered DCB GPU pass did not converge.");
}
Texture Directional(Context& c,DemosaicMethod method) {
    auto green=c.Pass("directional-green",Shaders::DirectionalGreen,GL_R32F,0,0,0,static_cast<int>(method));
    return c.Pass("directional-color",Shaders::DirectionalColor,GL_RGBA32F,green.id,0,0,static_cast<int>(method));
}
Texture Lmmse(Context& c,bool refine) {
    Texture green;
    {
        auto diff=c.Pass("lmmse-difference",Shaders::LmmseDifference,GL_RG32F);
        auto smooth=c.Pass("lmmse-smooth",Shaders::LmmseSmooth,GL_RG32F,diff.id);
        green=c.Pass("lmmse-green",Shaders::LmmseGreen,GL_R32F,diff.id,smooth.id);
    }
    auto chroma=c.Pass("lmmse-chroma",Shaders::LmmseChroma,GL_RG32F,green.id);
    auto rgb=c.Pass("lmmse-color",Shaders::LmmseColor,GL_RGBA32F,green.id,chroma.id);
    green.Reset();chroma.Reset();
    if(refine) for(int pass=0;pass<2;++pass) rgb=c.Pass("refine-chroma",Shaders::RefineChroma,GL_RGBA32F,rgb.id);
    return rgb;
}
}
struct GpuDemosaic::Impl {std::unordered_map<std::string,GpuInternal::Program> programs;};
GpuDemosaic::GpuDemosaic():m_Impl(std::make_unique<Impl>()){}
GpuDemosaic::~GpuDemosaic(){for(const auto& item:m_Impl->programs) if(item.second.id) glDeleteProgram(item.second.id);}
unsigned int GpuDemosaic::Reconstruct(unsigned int mosaic,int width,int height,CfaPattern pattern,
    DemosaicMethod method,const std::function<bool()>& cancel,std::string& error,
    const std::array<float,9>& cameraToSrgb) {
    error.clear();
    if(!mosaic || width<2 || height<2 || pattern==CfaPattern::Unknown) {error="GPU demosaicing requires a Bayer texture of at least 2 by 2 pixels.";return 0;}
    ComputeState state;
    try {
        GpuInternal::Context c(mosaic,width,height,pattern,cancel,m_Impl->programs);
        c.cameraToSrgb=cameraToSrgb;
        GpuInternal::Texture rgb;
        switch(method) {
            case DemosaicMethod::HamiltonAdams:
            case DemosaicMethod::EdgeWeighted:
            case DemosaicMethod::SmoothHue: rgb=GpuInternal::Directional(c,method);break;
            case DemosaicMethod::Lmmse:
            case DemosaicMethod::LmmseRefined: rgb=GpuInternal::Lmmse(c,method==DemosaicMethod::LmmseRefined);break;
            case DemosaicMethod::Dcb:
            case DemosaicMethod::DcbEnhanced: rgb=GpuInternal::Dcb(c,method==DemosaicMethod::DcbEnhanced);break;
            case DemosaicMethod::Ppg: rgb=GpuInternal::Ppg(c);break;
            case DemosaicMethod::Vng: rgb=GpuInternal::Vng(c,false);break;
            case DemosaicMethod::VngFourColor: rgb=GpuInternal::Vng(c,true);break;
            case DemosaicMethod::Ahd: rgb=GpuInternal::Ahd(c);break;
            case DemosaicMethod::MalvarHamilton:
            case DemosaicMethod::HamiltonLmmse:
            case DemosaicMethod::DcbLmmseDetail:
            case DemosaicMethod::DcbLmmseShadows:
            case DemosaicMethod::DcbLmmseChroma:
            case DemosaicMethod::DcbLmmseMalvar: {
                GpuInternal::Texture smooth;
                if(method!=DemosaicMethod::MalvarHamilton) smooth=GpuInternal::Lmmse(c,false);
                auto sharp=(method==DemosaicMethod::MalvarHamilton || method==DemosaicMethod::HamiltonLmmse)?
                    GpuInternal::Directional(c,DemosaicMethod::HamiltonAdams):GpuInternal::Dcb(c,true);
                rgb=c.Pass("combine",std::string(Shaders::Malvar)+Shaders::Combine,GL_RGBA32F,smooth.id,sharp.id,0,static_cast<int>(method));
                break;
            }
            default: throw std::runtime_error("Unsupported GPU demosaicing method.");
        }
        c.CheckCancel();
        if(c.Status()&2u) throw std::runtime_error("GPU demosaicing produced a non-finite sample.");
        return rgb.Release();
    } catch(const std::exception& e) {error=e.what();return 0;}
}
}
