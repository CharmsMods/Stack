#pragma once
#include "Editor/UI/GraphFrameTransitionPolicy.h"
#include "Editor/Internal/RawWorkspace/RawViewportFadeRenderer.h"
#include "Raw/RawGpuImageLease.h"

class GraphFrameTransition {
public:
    bool Prepare(const Stack::GraphRendering::FramePresentation& next, double workMilliseconds);
    void Retain(Raw::RawGpuImageLease previous) { m_Previous = std::move(previous); }
    void RetainOwned(unsigned int texture, int width, int height);
    void Clear();
    void Reset() { Clear(); m_Last = {}; }
    void Shutdown() { Reset(); m_DrawnPrevious.Reset(); m_Renderer.Shutdown(); }
    void Validate(const Stack::GraphRendering::RequestTag& identity, double now);
    bool Draw(ImDrawList* list, unsigned int current, ImVec2 minimum, ImVec2 maximum,
        double now, ImVec2 uvMinimum, ImVec2 uvMaximum, ImU32 tint, float rounding);
    bool Active() const { return static_cast<bool>(m_Previous); }
private:
    Stack::GraphRendering::FramePresentation m_Last;
    Raw::RawGpuImageLease m_Previous;
    Raw::RawGpuImageLease m_DrawnPrevious;
    int m_DrawnFrame = -1;
    void ReleaseSubmittedDraw();
    Raw::ViewportFadeRenderer m_Renderer;
    double m_Duration = 0;
};
