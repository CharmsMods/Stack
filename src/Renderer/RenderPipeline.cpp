#include "RenderPipeline.h"

void RenderPipeline::Execute(const std::vector<std::shared_ptr<LayerBase>>& layers) {
    m_GraphSourceTexture = 0;
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture; // Nothing to process
        return;
    }

    // Save previous GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    // Save and disable states that might interfere with full-screen quad rendering
    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glViewport(0, 0, m_Width, m_Height);

    int activeCount = 0;
    for (auto& layer : layers) {
        if (layer->IsVisible()) activeCount++;
    }

    if (activeCount == 0) {
        // No layers active, output is just the source
        m_OutputTexture = m_SourceTexture;

        // Restore
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        if (prevScissor) glEnable(GL_SCISSOR_TEST);
        if (prevDepth) glEnable(GL_DEPTH_TEST);
        if (prevStencil) glEnable(GL_STENCIL_TEST);
        if (prevBlend) glEnable(GL_BLEND);
        return;
    }

    // Sequential ping-pong execution:
    // Layer 1 reads Source → writes Ping
    // Layer 2 reads Ping → writes Pong
    // Layer 3 reads Pong → writes Ping ... etc.
    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;

    for (auto& layer : layers) {
        if (!layer->IsVisible()) continue;

        unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
        unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        layer->ExecuteWithSource(currentInput, m_SourceTexture, m_Width, m_Height, m_Quad);

        currentInput = targetTex;
        usePing = !usePing;
    }

    m_OutputTexture = currentInput;

    // Restore previous GL state
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

void RenderPipeline::ExecuteGraph(const RenderGraphSnapshot& graph) {
    ExecuteGraphImpl(graph);
}

void RenderPipeline::ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks) {
    m_GraphSourceTexture = 0;
    bool hasConnectedMask = false;
    for (const RenderLayerStep& step : steps) {
        if (step.maskNodeId > 0) {
            hasConnectedMask = true;
            break;
        }
    }
    if (!hasConnectedMask) {
        std::vector<std::shared_ptr<LayerBase>> layers;
        layers.reserve(steps.size());
        for (const RenderLayerStep& step : steps) {
            if (step.layer) {
                layers.push_back(step.layer);
            }
        }
        Execute(layers);
        return;
    }

    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    std::vector<std::pair<int, unsigned int>> maskTextures;
    for (const RenderMaskSource& mask : masks) {
        unsigned int texture = GenerateMaskTexture(mask);
        if (texture) {
            maskTextures.push_back({ mask.nodeId, texture });
        }
    }

    auto findMaskTexture = [&](int nodeId) -> unsigned int {
        for (const auto& item : maskTextures) {
            if (item.first == nodeId) {
                return item.second;
            }
        }
        return 0;
    };

    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;
    int activeCount = 0;
    for (const RenderLayerStep& step : steps) {
        if (step.layer && step.layer->IsVisible()) {
            ++activeCount;
        }
    }

    if (activeCount == 0) {
        m_OutputTexture = m_SourceTexture;
    } else {
        for (const RenderLayerStep& step : steps) {
            if (!step.layer || !step.layer->IsVisible()) {
                continue;
            }

            const unsigned int originalInput = currentInput;
            unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
            unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;
            usePing = !usePing;

            glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
            glClear(GL_COLOR_BUFFER_BIT);
            step.layer->ExecuteWithSource(originalInput, m_SourceTexture, m_Width, m_Height, m_Quad);

            const unsigned int maskTexture = findMaskTexture(step.maskNodeId);
            if (maskTexture) {
                unsigned int blendFBO = usePing ? m_PingFBO : m_PongFBO;
                unsigned int blendTex = usePing ? m_PingTexture : m_PongTexture;
                usePing = !usePing;
                RenderMaskBlend(originalInput, targetTex, maskTexture, blendFBO);
                currentInput = blendTex;
            } else {
                currentInput = targetTex;
            }
        }
        m_OutputTexture = currentInput;
    }

    for (const auto& item : maskTextures) {
        unsigned int texture = item.second;
        glDeleteTextures(1, &texture);
    }

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    if (prevScissor) glEnable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST);
    if (prevStencil) glEnable(GL_STENCIL_TEST);
    if (prevBlend) glEnable(GL_BLEND);
}

const RenderPipeline::PreLocalExposureSummary* RenderPipeline::GetPreLocalExposureSummary(int nodeId) const {
    const auto it = m_PreLocalExposureSummaries.find(nodeId);
    return it != m_PreLocalExposureSummaries.end() ? &it->second : nullptr;
}
