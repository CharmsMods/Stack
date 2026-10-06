#include "Renderer/Internal/RawZoneAreaRenderer.h"
#include "Renderer/Internal/RawZoneAreaShaders.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_DYNAMIC_READ
#define GL_DYNAMIC_READ 0x88E9
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_FRAMEBUFFER_BARRIER_BIT
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400
#endif

namespace Stack::Renderer {
namespace {
void Hash(std::size_t& h, std::size_t v) { h ^= v + 0x9e3779b9u + (h << 6u) + (h >> 2u); }
void Uniform(unsigned int p, const char* name, int v) { glUniform1i(glGetUniformLocation(p,name),v); }
void Uniform(unsigned int p, const char* name, float v) { glUniform1f(glGetUniformLocation(p,name),v); }
float Unordered(std::uint32_t v) {
    v ^= (v & 0x80000000u) ? 0x80000000u : 0xffffffffu;
    float f; std::memcpy(&f,&v,sizeof(f)); return f;
}

std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> ReadCoveragePreview(
    unsigned int texture,int width,int height,const RawRecipe::RawZoneArea& area) {
    const double scale=std::min(1.0,768.0/std::max(width,height));
    const int w=std::max(1,int(std::lround(width*scale))),h=std::max(1,int(std::lround(height*scale)));
    const GLState::FramebufferState framebuffer;
    const GLState::PixelPackState pack;
    GLint binding=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    const bool scissor=glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE;
    struct Restore {
        const GLState::FramebufferState& framebuffer; const GLState::PixelPackState& pack;
        GLint binding; bool scissor; unsigned int buffers[2]{};
        ~Restore() {
            framebuffer.Restore(); pack.Restore(); glBindTexture(GL_TEXTURE_2D,binding);
            glDeleteFramebuffers(2,buffers); if(scissor) glEnable(GL_SCISSOR_TEST);
        }
    } restore{framebuffer,pack,binding,scissor};
    glGenFramebuffers(2,restore.buffers);
    pack.ConfigureTightCpuReadback(); glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,restore.buffers[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Graph area coverage readback is unavailable.");
    ScopedGLTexture reduced;
    if(w!=width || h!=height) {
        reduced.Reset(GLHelpers::CreateStorageTexture(w,h,GL_R32F));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,restore.buffers[1]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,reduced.Get(),0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Graph area coverage preview allocation failed.");
        glBlitFramebuffer(0,0,width,height,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,restore.buffers[1]); glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    std::vector<float> coverage(std::size_t(w)*h);
    glReadPixels(0,0,w,h,GL_RED,GL_FLOAT,coverage.data());
    return RawRecipe::MakeZoneAreaMaskPreview(coverage,w,h,RawRecipe::ZoneAreaMaskFingerprint(area),768,&area);
}
}
RawZoneAreaRenderer::~RawZoneAreaRenderer() { Clear(); }
void RawZoneAreaRenderer::Clear() {
    for (auto& [key,m] : m_Masks) if(m.texture) glDeleteTextures(1,&m.texture);
    m_Masks.clear(); m_MaskBytes=0;
    m_Rasterizer.Clear();
    m_Guide.reset(); m_PreviewGuide.reset(); m_GuideKey=m_PreviewGuideKey=0;
    if(m_GainProgram) glDeleteProgram(m_GainProgram);
    if(m_ApplyProgram) glDeleteProgram(m_ApplyProgram);
    m_GainProgram=m_ApplyProgram=0;
}
unsigned int RawZoneAreaRenderer::MaskTexture(const RawRecipe::RawZoneArea& area,
    int width,int height,const RawRecipe::RawCropRotationRecipe& transform,const std::function<bool()>& cancelled,
    const RawRecipe::ImageGuide* guide,std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview>& preview) {
    std::size_t key=RawRecipe::ZoneAreaMaskFingerprint(area);
    Hash(key,width); Hash(key,height); Hash(key,transform.rotationDegrees);
    Hash(key,transform.flipHorizontally); Hash(key,transform.flipVertically);
    if (RawRecipe::ZoneAreaUsesGuidance(area)) {
        if (!guide) return 0;
        Hash(key,guide->revision);
    }
    auto found=m_Masks.find(key);
    if(found!=m_Masks.end()) { found->second.used=++m_Use; preview=found->second.preview; return found->second.texture; }
    const auto* raster=m_Rasterizer.Evaluate(area,width,height,transform,guide,cancelled);
    if(!raster) return 0;
    const auto& pixels=*raster;
    preview=RawRecipe::MakeZoneAreaMaskPreview(pixels,width,height,RawRecipe::ZoneAreaMaskFingerprint(area),768,&area);
    const std::size_t bytes=pixels.size()*sizeof(float);
    constexpr std::size_t budget=256u*1024u*1024u;
    while(!m_Masks.empty() && m_MaskBytes+bytes>budget) {
        auto oldest=std::min_element(m_Masks.begin(),m_Masks.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        m_MaskBytes-=oldest->second.bytes; glDeleteTextures(1,&oldest->second.texture); m_Masks.erase(oldest);
    }
    const GLState::PixelUnpackState unpack;
    unpack.ConfigureTightCpuUpload();
    const unsigned int texture=GLHelpers::CreateStorageTexture(width,height,GL_R32F);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RED,GL_FLOAT,pixels.data());
    unpack.Restore();
    if(!texture) throw std::runtime_error("Could not allocate Zones mask");
    m_Masks.emplace(key,Mask{texture,bytes,++m_Use,preview}); m_MaskBytes+=bytes; ++m_MaskBuildCount;
    if(m_Rasterizer.Bytes()>256u*1024u*1024u) m_Rasterizer.Clear();
    return texture;
}

unsigned int RawZoneAreaRenderer::Coverage(const RawRecipe::RawZoneArea& area, unsigned int reference,
    int width, int height, Raw::RawWorkingSpace space, std::size_t revision, const std::function<bool()>& cancelled) {
    GLint binding = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    struct Restore { GLint binding; ~Restore() { glBindTexture(GL_TEXTURE_2D, binding); } } restore{binding};
    auto recipe = RawRecipe::MakeDefaultRecipe({}); recipe.technical.workingSpace = space;
    const bool guided = RawRecipe::ZoneAreaUsesGuidance(area);
    if (guided && (!reference || !PrepareGuide(reference, width, height, recipe, revision, true, cancelled))) return 0;
    std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> preview;
    return MaskTexture(area, width, height, {}, cancelled, guided ? m_Guide.get() : nullptr, preview);
}

unsigned int RawZoneAreaRenderer::Render(unsigned int input,unsigned int reference,int width,int height,
    const RawRecipe::RawDevelopmentRecipe& recipe,bool measure,bool fullResolution,
    std::vector<RawRecipe::RawZoneAreaStatistics>& statistics,const std::function<bool()>& cancelled,
    std::size_t referenceRevision, const std::function<unsigned int(const std::string&)>& coverage) {
    statistics.clear();
    struct Publication {
        std::vector<RawRecipe::RawZoneAreaStatistics>& statistics;
        bool complete=false;
        ~Publication() {if (!complete) statistics.clear();}
    } publication{statistics};
    if(!input || !reference || width<=0 || height<=0 || recipe.localRange.areas.empty()) return 0;
    if(!m_GainProgram) m_GainProgram=GLHelpers::CreateComputeProgram(ZoneAreaShaders::Gain);
    if(!m_ApplyProgram) m_ApplyProgram=GLHelpers::CreateComputeProgram(ZoneAreaShaders::Apply);
    if(!m_GainProgram || !m_ApplyProgram) throw std::runtime_error("Zones area shader compilation failed");
    // Compute dispatches share the GL context with the other RAW stages.
    struct State {
        GLint program=0, active=0, buffer=0, indexedBuffer=0, activeBinding=0;
        GLint textures[3]{}, images[2][6]{};
        State() {
            glGetIntegerv(GL_CURRENT_PROGRAM,&program); glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&activeBinding);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&buffer);
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&indexedBuffer);
            const GLenum fields[]={GL_IMAGE_BINDING_NAME,GL_IMAGE_BINDING_LEVEL,GL_IMAGE_BINDING_LAYERED,
                GL_IMAGE_BINDING_LAYER,GL_IMAGE_BINDING_ACCESS,GL_IMAGE_BINDING_FORMAT};
            for (int i=0;i<3;++i) {
                glActiveTexture(GL_TEXTURE0+i); glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
                if (i < 2) for (int j=0;j<6;++j) glGetIntegeri_v(fields[j],i,&images[i][j]);
            }
            glActiveTexture(active);
        }
        ~State() {
            for(int i=0;i<3;++i) {
                glActiveTexture(GL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,textures[i]);
                if (i < 2) glBindImageTexture(i,images[i][0],images[i][1],images[i][2],images[i][3],images[i][4],images[i][5]);
            }
            glActiveTexture(active); glBindTexture(GL_TEXTURE_2D,activeBinding); glUseProgram(program);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,indexedBuffer);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
        }
    } state;
    const bool guided=std::any_of(recipe.localRange.areas.begin(),recipe.localRange.areas.end(),RawRecipe::ZoneAreaUsesGuidance);
    if (((guided && !coverage) || measure) && !PrepareGuide(reference,width,height,recipe,referenceRevision,guided,cancelled)) return 0;
    ScopedGLTexture gain(GLHelpers::CreateStorageTexture(width,height,GL_R32F));
    ScopedGLTexture curve(GLHelpers::CreateStorageTexture(4097,1,GL_R32F));
    ScopedGLTexture output(GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F));
    if(!gain || !curve || !output) throw std::runtime_error("Could not allocate Zones gain textures");
    unsigned int buffer=0; glGenBuffers(1,&buffer);
    struct BufferOwner { unsigned int id; ~BufferOwner(){glDeleteBuffers(1,&id);} } bufferOwner{buffer};
    bool first=true;
    for(const auto& area:recipe.localRange.areas) {
        if(cancelled && cancelled()) return 0;
        std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> maskPreview;
        const unsigned int mask=coverage ? coverage(area.id) : MaskTexture(area,width,height,recipe.cropRotation,cancelled,
            guided ? m_Guide.get() : nullptr,maskPreview);
        if(!mask && !coverage) return 0;
        // A graph may invert, combine or filter the authored generator. Read
        // the evaluated coverage, bounded to the existing 768-pixel preview.
        if (measure && coverage && mask) maskPreview=ReadCoveragePreview(mask,width,height,area);
        std::array<float,4097> samples;
        for(std::size_t i=0;i<samples.size();++i) samples[i]=RawRecipe::ZoneAreaCurveGain(area,-32.0f+float(i)/64.0f);
        const GLState::PixelUnpackState unpack;
        unpack.ConfigureTightCpuUpload();
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,curve.Get());
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4097,1,GL_RED,GL_FLOAT,samples.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        unpack.Restore();
        std::array<std::uint32_t,260> values{}; values[0]=0xffffffffu;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(values),values.data(),GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer);
        glUseProgram(m_GainProgram);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,reference);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, mask);
        Uniform(m_GainProgram, "hasMask", mask ? 1 : 0);
        glBindImageTexture(1,gain.Get(),0,GL_FALSE,0,GL_READ_WRITE,GL_R32F);
        Uniform(m_GainProgram,"firstArea",first?1:0); Uniform(m_GainProgram,"measure",measure?1:0);
        Uniform(m_GainProgram,"enabled",area.enabled && recipe.localRange.enabled?1:0);
        Uniform(m_GainProgram,"rec2020",recipe.technical.workingSpace==Raw::RawWorkingSpace::LinearRec2020D65?1:0);
        Uniform(m_GainProgram,"offsetEv",area.offsetEv); Uniform(m_GainProgram,"strength",recipe.localRange.strength);
        Uniform(m_GainProgram,"histogramStride",std::max(1,(std::max(width,height)+191)/192));
        glDispatchCompute((width+15)/16,(height+15)/16,1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
        first=false;
        if(measure) {
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(values),values.data());
            RawRecipe::RawZoneAreaStatistics stats;
            stats.areaId=area.id; stats.maskFingerprint=RawRecipe::ZoneAreaMaskFingerprint(area);
            stats.maskPreview=std::move(maskPreview);
            stats.valid=values[3]>0; stats.fullResolution=fullResolution; stats.hasBlack=values[2]!=0;
            if(stats.valid) {stats.minimumEv=Unordered(values[0]);stats.maximumEv=Unordered(values[1]);}
            for(std::size_t i=0;i<256;++i) stats.histogram[i]=float(values[i+4])/65535.0f;
            statistics.push_back(std::move(stats));
        }
    }
    glUseProgram(m_ApplyProgram);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input);
    glBindImageTexture(0,gain.Get(),0,GL_FALSE,0,GL_READ_ONLY,GL_R32F);
    glBindImageTexture(1,output.Get(),0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
    glDispatchCompute((width+15)/16,(height+15)/16,1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_FRAMEBUFFER_BARRIER_BIT);
    publication.complete=true;
    return output.Release();
}
}
