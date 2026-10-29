#pragma once
#include "Editor/UI/GraphFrameTransitionPolicy.h"
#include "Editor/Internal/RawWorkspace/RawViewportFadeRenderer.h"
#include "Raw/RawGpuImageLease.h"

class GraphFrameTransition {
public:
    struct BlendDescription {
        unsigned int previousTexture = 0;
        ImVec2 previousOffset{}, previousScale{1, 1};
        ImVec4 imageBounds{}; // Bottom-left normalized image coverage of the blend.
        float amount = 1;
        bool encoded = false;
    };
    bool Prepare(const Stack::GraphRendering::FramePresentation& next, double workMilliseconds,
        int fadeBelowFps = Raw::kDefaultViewportFadeBelowFps, bool smoothUpdates = true,
        int targetFps = Raw::kDefaultViewportTargetFps);
    void Retain(Raw::RawGpuImageLease previous) { if (!m_KeepBase) m_Previous = std::move(previous); }
    void RetainOwned(unsigned int texture, int width, int height);
    void Clear();
    void Reset() { Clear(); m_Last = {}; }
    void Shutdown() { Reset(); m_DrawnPrevious.Reset(); m_Renderer.Shutdown(); }
    // Retire submitted-only references even when this editor is not drawn.
    // The active fade's m_Previous lease remains owned until normal clearing.
    void ReleaseSubmittedDraw();
    void Validate(const Stack::GraphRendering::RequestTag& identity, double now);
    bool DescribeBlend(double now, BlendDescription& description);
    bool Draw(ImDrawList* list, unsigned int current, ImVec2 minimum, ImVec2 maximum,
        double now, ImVec2 uvMinimum, ImVec2 uvMaximum, ImU32 tint, float rounding);
    bool Active() const { return static_cast<bool>(m_Previous); }
private:
    Stack::GraphRendering::FramePresentation m_Last;
    Stack::GraphRendering::FramePresentation m_Base;
    Raw::RawGpuImageLease m_Previous;
    Raw::RawGpuImageLease m_DrawnPrevious;
    int m_DrawnFrame = -1;
    Raw::ViewportFadeRenderer m_Renderer;
    double m_Duration = 0;
    double m_StartedAt = -1.0;
    bool m_KeepBase = false;
};
