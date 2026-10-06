#include "Renderer/ViewportTextureCopy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
namespace Stack::Renderer {
unsigned int CopyViewportTexture(unsigned int source,int sourceWidth,int sourceHeight,int width,int height) {
    if (!source || sourceWidth <= 0 || sourceHeight <= 0 || width <= 0 || height <= 0) return 0;
    const GLState::TextureBinding savedTexture(GL_TEXTURE_2D, GL_TEXTURE_BINDING_2D);
    glBindTexture(GL_TEXTURE_2D,source);
    GLint sourceFormat = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&sourceFormat);
    ScopedGLTexture target(sourceFormat == GL_RGBA32F
        ? GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F)
        : GLHelpers::CreateEmptyTexture(width,height));
    savedTexture.Restore();
    if (!target) return 0;
    const GLState::FramebufferState saved;
    GLuint sourceFbo = GLHelpers::CreateFBO(source), targetFbo = GLHelpers::CreateFBO(target.Get());
    bool copied = false;
    if (sourceFbo && targetFbo) {
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,targetFbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(0,0,sourceWidth,sourceHeight,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        copied = glGetError() == GL_NO_ERROR;
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
    saved.Restore();
    if (sourceFbo) glDeleteFramebuffers(1,&sourceFbo);
    if (targetFbo) glDeleteFramebuffers(1,&targetFbo);
    return copied ? target.Release() : 0;
}
}
