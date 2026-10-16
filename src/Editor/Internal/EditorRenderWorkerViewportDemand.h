#pragma once

#include "Renderer/RenderPipeline.h"

namespace Stack::EditorRendering {

// Auxiliary graphs select different output endpoints. Their demand must stay
// complete and their temporary preview dimensions must not leak into main work.
class ScopedCompleteViewportDemand {
public:
    ScopedCompleteViewportDemand(RenderPipeline& pipeline,
        const Stack::Renderer::ViewportPreviewRequest& graphRequest)
        : m_Pipeline(pipeline), m_GraphRequest(graphRequest),
          m_RawRequest(pipeline.m_RawViewportRequest),
          m_Edge(pipeline.GetRawViewportCacheEdge()) {
        m_Pipeline.SetGraphViewportRequest({});
        m_Pipeline.SetRawViewportRequest({});
    }
    ~ScopedCompleteViewportDemand() {
        m_Pipeline.SetGraphViewportRequest(m_GraphRequest);
        m_Pipeline.SetRawViewportRequest(m_RawRequest);
        m_Pipeline.SetPreviewMaxDimension(m_Edge);
    }

private:
    RenderPipeline& m_Pipeline;
    Stack::Renderer::ViewportPreviewRequest m_GraphRequest;
    Raw::ViewportRequest m_RawRequest;
    int m_Edge = 0;
};

} // namespace Stack::EditorRendering
