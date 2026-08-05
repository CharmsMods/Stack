#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

unsigned int RenderPipeline::CreateGraphRenderTargetTexture() const {
    return GLHelpers::CreateEmptyTexture(m_Width, m_Height);
}

bool RenderPipeline::RenderIntoGraphTargetTextureImpl(
    unsigned int texture,
    const void* renderContext,
    void (*renderFn)(const void*, unsigned int)) {
    if (texture == 0 || renderContext == nullptr || renderFn == nullptr) {
        return false;
    }

    unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) {
        return false;
    }

    const Stack::Renderer::GraphExecution::ScopedFramebufferState savedFramebufferState(true);
    GLfloat previousClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLboolean previousColorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    const GLboolean previousScissorState = glIsEnabled(GL_SCISSOR_TEST);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        savedFramebufferState.Restore(true);
        glDeleteFramebuffers(1, &fbo);
        return false;
    }

    glViewport(0, 0, m_Width, m_Height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // Graph targets are image data, not windows. Inheriting AppShell's opaque
    // teal window clear made a skipped/failed fullscreen pass look like a
    // valid texture and, for overlays, completely covered the last good
    // preview. Transparent black is the deterministic neutral initialization.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    while (glGetError() != GL_NO_ERROR) {}
    glClear(GL_COLOR_BUFFER_BIT);

    const auto restoreState = [&]() {
        glClearColor(
            previousClearColor[0],
            previousClearColor[1],
            previousClearColor[2],
            previousClearColor[3]);
        glColorMask(
            previousColorMask[0],
            previousColorMask[1],
            previousColorMask[2],
            previousColorMask[3]);
        if (previousScissorState) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        savedFramebufferState.Restore(true);
        glDeleteFramebuffers(1, &fbo);
    };

    GLenum renderError = GL_NO_ERROR;
    try {
        renderFn(renderContext, fbo);
        renderError = glGetError();
    } catch (const std::exception& error) {
        restoreState();
        std::cerr << "[RenderPipeline] Graph target pass threw an exception: "
                  << error.what() << ".\n";
        return false;
    } catch (...) {
        restoreState();
        std::cerr << "[RenderPipeline] Graph target pass threw an unknown exception.\n";
        return false;
    }

    restoreState();
    if (renderError != GL_NO_ERROR) {
        std::cerr << "[RenderPipeline] Graph target pass failed with OpenGL error 0x"
                  << std::hex << static_cast<unsigned int>(renderError)
                  << std::dec << ".\n";
    }
    return renderError == GL_NO_ERROR;
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
    Stack::Renderer::ScopedGLTexture texture(
        CreateGraphRenderTargetTexture());
    if (!texture) return 0;
    try {
        m_GraphTransientTargets.push_back(GraphTransientTarget{
            texture.Get(),
            m_Width,
            m_Height,
            true,
            ++m_GraphResourceUseSerial });
    } catch (const std::bad_alloc&) {
        return 0;
    } catch (const std::length_error&) {
        return 0;
    }
    ++m_LastGraphExecutionStats.transientTargetAllocations;
    return texture.Release();
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

void RenderPipeline::TrimGraphTransientTargetsToBudget() {
    using namespace Stack::Renderer::GraphExecution;

    std::uint64_t totalBytes = GraphTransientTargetBytes();
    while (m_GraphTransientTargets.size() > kGraphTransientTargetMaximumEntries ||
           totalBytes > kGraphTransientTargetSoftByteBudget) {
        const auto victim = std::min_element(
            m_GraphTransientTargets.begin(),
            m_GraphTransientTargets.end(),
            [](const GraphTransientTarget& left, const GraphTransientTarget& right) {
                if (left.inUse != right.inUse) {
                    return !left.inUse;
                }
                return left.lastUseSerial < right.lastUseSerial;
            });
        if (victim == m_GraphTransientTargets.end() || victim->inUse) {
            break;
        }

        const std::uint64_t victimBytes =
            EstimateRawDevelopStageCacheTextureBytes(victim->width, victim->height);
        if (victim->texture != 0) {
            glDeleteTextures(1, &victim->texture);
        }
        m_GraphTransientTargets.erase(victim);
        totalBytes = victimBytes >= totalBytes ? 0 : totalBytes - victimBytes;
        ++m_LastGraphExecutionStats.transientTargetEvictions;
    }
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
        const std::uint64_t bytes =
            Stack::Renderer::GraphExecution::EstimateRawDevelopStageCacheTextureBytes(
            target.width,
            target.height);
        if (bytes > std::numeric_limits<std::uint64_t>::max() - total) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += bytes;
    }
    return total;
}
