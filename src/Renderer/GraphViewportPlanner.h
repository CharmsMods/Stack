#pragma once
#include "Renderer/RenderTiling.h"

namespace RenderTiling {
// Reuse operation capability decoding while comparing resolutions of ONE
// immutable snapshot. This cache never survives an edit or crosses projects.
class GraphViewportPlanner {
public:
    explicit GraphViewportPlanner(const RenderGraphSnapshot& graph) : m_Graph(graph) {}
    RenderGraphViewportPlan Plan(int fullWidth, int fullHeight, int previewMaxDimension,
        const Stack::Renderer::ViewportPreviewRequest& request,
        const std::vector<RenderGraphViewportEndpoint>& completeConsumers = {});
private:
    const RenderGraphSnapshot& m_Graph;
    std::map<RenderGraphViewportEndpoint, bool> m_RegionalCapabilities;
};
}
