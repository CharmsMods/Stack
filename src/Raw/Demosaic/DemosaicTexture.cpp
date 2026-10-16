#include "DemosaicTexture.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <new>

namespace Raw::Demosaic {
namespace {
// The prepare callback uses the caller's existing shader, textures and quad.
// Only the target/viewport and texture state are changed here.
struct PreparationTarget {
    Stack::Renderer::GLState::FramebufferState framebuffer{true};
    Stack::Renderer::GLState::TextureBinding binding{GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D};
    GLuint texture=0,fbo=0;
    ~PreparationTarget() {
        binding.Restore(); framebuffer.Restore(true);
        if(fbo) glDeleteFramebuffers(1,&fbo);
        if(texture) glDeleteTextures(1,&texture);
    }
};
}
DemosaicTexture::~DemosaicTexture() { Clear(); }
void DemosaicTexture::Clear() {
    if(m_Texture) glDeleteTextures(1,&m_Texture);
    m_Texture=0; m_Key=0;
}
unsigned int DemosaicTexture::GetOrRender(std::size_t key,int width,int height,CfaPattern pattern,
    DemosaicMethod method,const std::function<bool()>& prepare,const std::function<bool()>& cancel,std::string& error,
    const std::array<float,9>& cameraToSrgb) {
    if(cancel && cancel()) { error="Demosaicing canceled."; return 0; }
    if(m_Texture && m_Key==key) return m_Texture;
    try {
        GLuint replacement=0;
        {
            PreparationTarget target;
            target.texture=GLHelpers::CreateTextureFromData(nullptr,width,height,GL_R32F,GL_RED,GL_FLOAT,GL_NEAREST);
            if(!target.texture) { error="Could not allocate the demosaicing input texture."; return 0; }
            glGenFramebuffers(1,&target.fbo);
            glBindFramebuffer(GL_FRAMEBUFFER,target.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.texture,0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
                error="Could not create the demosaicing input framebuffer."; return 0;
            }
            glViewport(0,0,width,height);
            if(!prepare()) { error="Demosaicing input preparation canceled or failed."; return 0; }
            replacement=m_Gpu.Reconstruct(target.texture,width,height,pattern,method,cancel,error,cameraToSrgb);
        }
        if(!replacement) return 0;
        if(cancel && cancel()) { glDeleteTextures(1,&replacement);error="Demosaicing canceled."; return 0; }
        // Discard the previous result only when a complete replacement exists.
        Clear(); m_Texture=replacement; m_Key=key;
        return m_Texture;
    } catch(const std::bad_alloc&) {
        error="Not enough memory to prepare this demosaicing method at the source resolution.";
    } catch(const std::exception& e) { error=std::string("Demosaicing preparation failed: ")+e.what(); }
    return 0;
}
} // namespace Raw::Demosaic
