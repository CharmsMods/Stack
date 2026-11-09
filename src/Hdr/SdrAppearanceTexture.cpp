#include "SdrAppearanceTexture.h"
#include "SdrAppearance.h"
#include "Renderer/GLStateGuards.h"
#include <stdexcept>

namespace Stack::Hdr {
GLuint CreateSdrAppearanceTexture() {
    constexpr GLenum rgb16=0x8054, imageHeight=0x806E, skipImages=0x806D;
    Renderer::GLState::TextureBinding binding(GL_TEXTURE_3D,GL_TEXTURE_BINDING_3D);
    Renderer::GLState::PixelUnpackState unpack;
    GLint oldHeight=0,oldSkip=0;
    glGetIntegerv(imageHeight,&oldHeight); glGetIntegerv(skipImages,&oldSkip);
    unpack.ConfigureTightCpuUpload();
    glPixelStorei(imageHeight,0); glPixelStorei(skipImages,0);
    GLuint texture=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_3D,texture);
    glTexStorage3D(GL_TEXTURE_3D,1,rgb16,SdrAppearanceEdge,SdrAppearanceEdge,SdrAppearanceEdge);
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,SdrAppearanceEdge,SdrAppearanceEdge,SdrAppearanceEdge,
        GL_RGB,GL_UNSIGNED_SHORT,SdrAppearanceTable());
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const bool ok=texture && glGetError()==GL_NO_ERROR;
    glPixelStorei(imageHeight,oldHeight); glPixelStorei(skipImages,oldSkip);
    unpack.Restore();binding.Restore();
    if(!ok) {
        if(texture) glDeleteTextures(1,&texture);
        throw std::runtime_error("Could not create the SDR appearance table.");
    }
    return texture;
}
SdrAppearanceBinding::SdrAppearanceBinding(GLuint texture) {
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active_);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_3D,&texture_);
    glGetIntegerv(GL_SAMPLER_BINDING,&sampler_);
    glBindSampler(1,0);glBindTexture(GL_TEXTURE_3D,texture);
    glActiveTexture(GL_TEXTURE0);
}
SdrAppearanceBinding::~SdrAppearanceBinding() {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D,GLuint(texture_));glBindSampler(1,GLuint(sampler_));
    glActiveTexture(GLenum(active_));
}
}
