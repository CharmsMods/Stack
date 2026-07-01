#include "Renderer/RenderPipeline.h"

#include <functional>

unsigned int RenderPipeline::CreateGraphRenderTargetTexture() const {
    return GLHelpers::CreateEmptyTexture(m_Width, m_Height);
}

bool RenderPipeline::RenderIntoGraphTargetTexture(unsigned int texture, const std::function<void(unsigned int)>& renderFn) {
    if (texture == 0) {
        return false;
    }

    unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) {
        return false;
    }

    GLint prevFBO = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, m_Width, m_Height);
    glClear(GL_COLOR_BUFFER_BIT);
    renderFn(fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glDeleteFramebuffers(1, &fbo);
    return true;
}
