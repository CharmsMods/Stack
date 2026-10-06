#pragma once

#include "Raw/RawGradingScope.h"

#include <imgui.h>

namespace Stack::Editor::RawLabInternal {

enum class GradingScopePlot { Vectorscope, Parade };

class RawGradingScopeRenderer {
public:
    RawGradingScopeRenderer() = default;
    RawGradingScopeRenderer(const RawGradingScopeRenderer&) = delete;
    RawGradingScopeRenderer& operator=(const RawGradingScopeRenderer&) = delete;

    bool QueueDraw(ImDrawList& drawList,
        const std::shared_ptr<const RawGradingScopeVisualization>& packet,
        GradingScopePlot plot, ImVec2 min, ImVec2 max);
    // Release while the owning UI GL context is current.
    void Shutdown();

private:
    struct PointBuffer {
        unsigned int vertexArray = 0;
        unsigned int buffer = 0;
        int count = 0;
    };
    struct DrawRequest {
        RawGradingScopeRenderer* renderer = nullptr;
        GradingScopePlot plot = GradingScopePlot::Vectorscope;
        ImVec2 min, size, displayPos, displaySize, framebufferScale;
        float alpha = 1.0f;
    };

    bool Upload(const std::shared_ptr<const RawGradingScopeVisualization>& packet);
    static void DrawCallback(const ImDrawList*, const ImDrawCmd* command);

    unsigned int m_Program = 0;
    PointBuffer m_Vector, m_Parade;
    int m_PlotRectLocation = -1;
    int m_DisplayRectLocation = -1;
    int m_DiameterLocation = -1;
    int m_AlphaLocation = -1;
    std::shared_ptr<const RawGradingScopeVisualization> m_Packet;
};

} // namespace Stack::Editor::RawLabInternal
