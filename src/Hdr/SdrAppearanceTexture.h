#pragma once
#include "Renderer/GLLoader.h"

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

namespace Stack::Hdr {
// Creates the immutable table on the current processing context. The View
// layer owns/deletes it with its shader, not with UI frames or image pixels.
GLuint CreateSdrAppearanceTexture();
class SdrAppearanceBinding {
public:
    explicit SdrAppearanceBinding(GLuint texture);
    ~SdrAppearanceBinding();
    SdrAppearanceBinding(const SdrAppearanceBinding&)=delete;
    SdrAppearanceBinding& operator=(const SdrAppearanceBinding&)=delete;
private:
    GLint active_=0,texture_=0,sampler_=0;
};
}
