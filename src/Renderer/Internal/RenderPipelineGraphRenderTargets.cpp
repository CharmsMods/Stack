#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

#include <algorithm>
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

unsigned int RenderPipeline::AcquireGraphTransientTarget() {
    for (GraphTransientTarget& target : m_GraphTransientTargets) {
        if (!target.inUse && target.texture != 0 &&
            target.width == m_Width && target.height == m_Height) {
            target.inUse = true;
            target.lastUseSerial = ++m_GraphResourceUseSerial;
            ++m_LastGraphExecutionStats.transientTargetReuses;
            return target.texture;
        }
    }
    const unsigned int texture = CreateGraphRenderTargetTexture();
    if (texture == 0) return 0;
    m_GraphTransientTargets.push_back(GraphTransientTarget{
        texture, m_Width, m_Height, true, ++m_GraphResourceUseSerial });
    ++m_LastGraphExecutionStats.transientTargetAllocations;
    return texture;
}

void RenderPipeline::ReleaseGraphTransientTarget(unsigned int texture) {
    const auto found = std::find_if(
        m_GraphTransientTargets.begin(), m_GraphTransientTargets.end(),
        [texture](const GraphTransientTarget& target) { return target.texture == texture; });
    if (found == m_GraphTransientTargets.end()) return;
    found->inUse = false;
    found->lastUseSerial = ++m_GraphResourceUseSerial;
}

bool RenderPipeline::PromoteGraphTransientTarget(unsigned int texture) {
    const auto found = std::find_if(
        m_GraphTransientTargets.begin(), m_GraphTransientTargets.end(),
        [texture](const GraphTransientTarget& target) { return target.texture == texture; });
    if (found == m_GraphTransientTargets.end()) return false;
    m_GraphTransientTargets.erase(found);
    return true;
}

void RenderPipeline::DestroyGraphTransientTargets() {
    for (GraphTransientTarget& target : m_GraphTransientTargets) {
        if (target.texture != 0) glDeleteTextures(1, &target.texture);
    }
    m_GraphTransientTargets.clear();
}

std::uint64_t RenderPipeline::GraphTransientTargetBytes() const {
    std::uint64_t total = 0;
    for (const GraphTransientTarget& target : m_GraphTransientTargets) {
        total += Stack::Renderer::GraphExecution::EstimateRawDevelopStageCacheTextureBytes(
            target.width,
            target.height);
    }
    return total;
}
