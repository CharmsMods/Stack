#include "RenderPipeline.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"

#include <new>
#include <stdexcept>

namespace {

struct ScopedPipelineExecutionState {
    Stack::Renderer::GLState::FramebufferState framebuffer { true };
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);

    ~ScopedPipelineExecutionState() {
        framebuffer.Restore(true);
        SetCapability(GL_SCISSOR_TEST, scissor);
        SetCapability(GL_DEPTH_TEST, depth);
        SetCapability(GL_STENCIL_TEST, stencil);
        SetCapability(GL_BLEND, blend);
    }

private:
    static void SetCapability(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }
};

struct OwnedMaskTextureList {
    std::vector<std::pair<int, unsigned int>> values;

    ~OwnedMaskTextureList() {
        for (auto& [nodeId, texture] : values) {
            (void)nodeId;
            if (texture != 0) {
                glDeleteTextures(1, &texture);
            }
        }
    }
};

} // namespace

void RenderPipeline::Execute(const std::vector<std::shared_ptr<LayerBase>>& layers) {
    m_GraphSourceTexture = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    if (m_BaseCanvasWidth > 0 && m_BaseCanvasHeight > 0) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
    }
    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture; // Nothing to process
        return;
    }

    const ScopedPipelineExecutionState savedExecutionState;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);

    glViewport(0, 0, m_Width, m_Height);

    int activeCount = 0;
    for (auto& layer : layers) {
        if (layer && layer->IsVisible()) activeCount++;
    }

    if (activeCount == 0) {
        // No layers active, output is just the source
        m_OutputTexture = m_SourceTexture;
        return;
    }

    // Sequential ping-pong execution:
    // Layer 1 reads Source → writes Ping
    // Layer 2 reads Ping → writes Pong
    // Layer 3 reads Pong → writes Ping ... etc.
    unsigned int currentInput = m_SourceTexture;
    bool usePing = true;

    for (auto& layer : layers) {
        if (!layer || !layer->IsVisible()) continue;

        unsigned int targetFBO = usePing ? m_PingFBO : m_PongFBO;
        unsigned int targetTex = usePing ? m_PingTexture : m_PongTexture;

        glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
        glClear(GL_COLOR_BUFFER_BIT);

        layer->ExecuteWithSource(currentInput, m_SourceTexture, m_Width, m_Height, m_Quad);

        currentInput = targetTex;
        usePing = !usePing;
    }

    m_OutputTexture = currentInput;
}

void RenderPipeline::HandleGraphExecutionFailure(
    const RenderGraphSnapshot& graph,
    bool allocationFailed,
    const char* message) noexcept {
    m_LastGraphExecutionStats.allocationFailed = allocationFailed;
    m_LastGraphExecutionStats.lastSpecializedFailureNodeId =
        graph.outputNodeId;
    try {
        m_LastGraphExecutionStats.lastSpecializedFailure =
            message != nullptr ? message : "Graph execution failed.";
    } catch (...) {
        m_LastGraphExecutionStats.lastSpecializedFailure.clear();
    }
    // Publishing the untouched source as a successful result silently
    // discards the authored graph. Leave the current Editor presentation
    // alone by reporting no replacement output instead.
    m_OutputTexture = 0;
    m_GraphSourceTexture = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    m_Width = m_BaseCanvasWidth;
    m_Height = m_BaseCanvasHeight;
    for (GraphTransientTarget& target : m_GraphTransientTargets) {
        target.inUse = false;
    }
    try {
        ClearRawDevelopmentStageStatsReadbacks();
        ClearRawDevelopmentLocalRangeOverlay();
        ClearRawDevelopmentLocalRangeTargetSample();
        m_RawDevelopmentLocalSuggestionImage = {};
        m_ToneCurveAutoRewriteFeedback.clear();
        m_PreLocalExposureSummaries.clear();
        InvalidateGraphCaches();
    } catch (...) {
        // The outer execution boundary must remain non-throwing even
        // while abandoning partially published cache state.
    }
}

void RenderPipeline::ExecuteGraph(const RenderGraphSnapshot& graph) {
    try {
        const Stack::Renderer::GraphExecution::GraphTopologyIndex topology =
            Stack::Renderer::GraphExecution::BuildGraphTopologyIndex(graph);
        ExecuteGraphImpl(graph, topology);
    } catch (const std::bad_alloc&) {
        HandleGraphExecutionFailure(
            graph,
            true,
            "Graph execution exhausted host memory.");
    } catch (const std::length_error&) {
        HandleGraphExecutionFailure(
            graph,
            true,
            "Graph execution exceeded a host-memory container limit.");
    } catch (const std::exception& error) {
        HandleGraphExecutionFailure(graph, false, error.what());
    } catch (...) {
        HandleGraphExecutionFailure(
            graph,
            false,
            "Graph execution failed unexpectedly.");
    }
}

void RenderPipeline::ExecuteGraph(
    const RenderGraphSnapshot& graph,
    const Stack::Renderer::GraphExecution::GraphTopologyIndex& topology) {
    try {
        ExecuteGraphImpl(graph, topology);
    } catch (const std::bad_alloc&) {
        HandleGraphExecutionFailure(
            graph,
            true,
            "Graph execution exhausted host memory.");
    } catch (const std::length_error&) {
        HandleGraphExecutionFailure(
            graph,
            true,
            "Graph execution exceeded a host-memory container limit.");
    } catch (const std::exception& error) {
        HandleGraphExecutionFailure(graph, false, error.what());
    } catch (...) {
        HandleGraphExecutionFailure(
            graph,
            false,
            "Graph execution failed unexpectedly.");
    }
}

void RenderPipeline::ExecuteMasked(const std::vector<RenderLayerStep>& steps, const std::vector<RenderMaskSource>& masks) {
    m_GraphSourceTexture = 0;
    m_GraphSourceWidth = 0;
    m_GraphSourceHeight = 0;
    if (m_BaseCanvasWidth > 0 && m_BaseCanvasHeight > 0) {
        m_Width = m_BaseCanvasWidth;
        m_Height = m_BaseCanvasHeight;
    }
    bool hasConnectedMask = false;
    for (const RenderLayerStep& step : steps) {
        if (step.maskNodeId > 0) {
            hasConnectedMask = true;
            break;
        }
    }
    if (!hasConnectedMask) {
        std::vector<std::shared_ptr<LayerBase>> layers;
        try {
            layers.reserve(steps.size());
            for (const RenderLayerStep& step : steps) {
                if (step.layer) {
                    layers.push_back(step.layer);
                }
            }
        } catch (const std::bad_alloc&) {
            m_OutputTexture = m_SourceTexture;
            return;
        }
        Execute(layers);
        return;
    }

    if (!m_SourceTexture || m_Width == 0 || m_Height == 0) {
        m_OutputTexture = m_SourceTexture;
        return;
    }

    const ScopedPipelineExecutionState savedExecutionState;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_Width, m_Height);

    OwnedMaskTextureList maskTextures;
    try {
        maskTextures.values.reserve(masks.size());
    } catch (const std::bad_alloc&) {
        m_OutputTexture = m_SourceTexture;
        return;
    }
    for (const RenderMaskSource& mask : masks) {
        unsigned int texture = GenerateMaskTexture(mask);
        if (texture) {
            maskTextures.values.push_back({ mask.nodeId, texture });
        }
    }

    auto findMaskTexture = [&](int nodeId) -> unsigned int {
        for (const auto& item : maskTextures.values) {
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
                currentInput = RenderMaskBlend(
                    originalInput, targetTex, maskTexture, blendFBO)
                    ? blendTex
                    : originalInput;
            } else {
                currentInput = targetTex;
            }
        }
        m_OutputTexture = currentInput;
    }

}

const RenderPipeline::PreLocalExposureSummary* RenderPipeline::GetPreLocalExposureSummary(int nodeId) const {
    const auto it = m_PreLocalExposureSummaries.find(nodeId);
    return it != m_PreLocalExposureSummaries.end() ? &it->second : nullptr;
}
