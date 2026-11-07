#pragma once
#include "Renderer/RenderTiling.h"

namespace RenderTiling {
// Reuse operation capability decoding while comparing resolutions of ONE
// immutable snapshot. This cache never survives an edit or crosses projects.
class GraphViewportPlanner {
public:
    explicit GraphViewportPlanner(const RenderGraphSnapshot& graph, bool retainedFloat32 = false)
        : m_Graph(graph), m_RetainedFloat32(retainedFloat32) {}
    RenderGraphViewportPlan Plan(int fullWidth, int fullHeight, int previewMaxDimension,
        const Stack::Renderer::ViewportPreviewRequest& request,
        const std::vector<RenderGraphViewportEndpoint>& completeConsumers = {});
private:
    const RenderGraphSnapshot& m_Graph;
    bool m_RetainedFloat32 = false;
    std::map<RenderGraphViewportEndpoint, bool> m_RegionalCapabilities;
};
}
