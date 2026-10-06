#pragma once
#include "Renderer/GLLoader.h"
#include <array>

#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
namespace Stack::Rendering {
// The compute stages using this guard bind whole SSBOs at 0/1 and images at
// 0/1/2. Restore every affected binding before returning the render context.
class ScopedComputeBindings {
public:
    ScopedComputeBindings() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program_);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&texture_);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&buffer_);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack_);
        for(GLuint i=0;i<buffers_.size();++i)
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&buffers_[i]);
        for(GLuint i=0;i<images_.size();++i) {
            auto& v=images_[i];
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME,i,&v.name);
            glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL,i,&v.level);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED,i,&v.layered);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYER,i,&v.layer);
            glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS,i,&v.access);
            glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT,i,&v.format);
        }
    }
    ~ScopedComputeBindings() {
        for(GLuint i=0;i<images_.size();++i) {
            const auto& v=images_[i];
            glBindImageTexture(i,v.name,v.level,v.layered?GL_TRUE:GL_FALSE,
                v.layer,v.access,v.format);
        }
        for(GLuint i=0;i<buffers_.size();++i)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,buffers_[i]);
        glUseProgram(program_);
        glBindTexture(GL_TEXTURE_2D_ARRAY,texture_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer_);
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpack_);
    }
    ScopedComputeBindings(const ScopedComputeBindings&)=delete;
private:
    struct Image {GLint name=0,level=0,layered=0,layer=0,access=GL_READ_ONLY,format=GL_R32F;};
    std::array<Image,3> images_{};
    std::array<GLint,2> buffers_{};
    GLint program_=0,texture_=0,buffer_=0,unpack_=4;
};
}
