#include "Editor/UI/GraphFrameTransition.h"
#include "Renderer/GpuMemoryBudget.h"
#include "Raw/RawGpuMemoryBudget.h"

bool GraphFrameTransition::Prepare(const Stack::GraphRendering::FramePresentation& next,
    double workMilliseconds) {
    ReleaseSubmittedDraw();
    Clear();
    m_Duration = Stack::GraphRendering::FrameBlendDuration(m_Last, next, workMilliseconds);
    m_Last = next;
    if (m_Duration <= 0) return false;
    const auto memory = Stack::Renderer::QueryGpuMemoryBudget();
    const auto bytes = static_cast<std::uint64_t>(next.width) * next.height * 8u;
    if (memory.queryAvailable && Raw::ResolveRawGpuMemoryBudget(memory).workingBudgetBytes < bytes) {
        m_Duration = 0;
        return false;
    }
    return true;
}

void GraphFrameTransition::RetainOwned(unsigned int texture, int width, int height) {
    try {
        m_Previous = Raw::RawGpuImageLease::AdoptOwned(texture, width, height,
            Raw::RawGpuImageFamily::Presentation);
    } catch (...) {
        if (texture) glDeleteTextures(1, &texture);
        Clear(); // Presentation smoothing must never prevent result adoption.
    }
}

void GraphFrameTransition::Clear() {
    m_Previous.Reset();
    m_Duration = 0;
}

void GraphFrameTransition::ReleaseSubmittedDraw() {
    if (!ImGui::GetCurrentContext() || ImGui::GetFrameCount() != m_DrawnFrame)
        m_DrawnPrevious.Reset();
}

void GraphFrameTransition::Validate(const Stack::GraphRendering::RequestTag& identity, double now) {
    ReleaseSubmittedDraw();
    if (!Stack::GraphRendering::SameContext(identity, m_Last.identity)) Reset();
    else if (now - m_Last.completedAt >= m_Duration) Clear();
}

bool GraphFrameTransition::Draw(ImDrawList* list, unsigned int current,
    ImVec2 minimum, ImVec2 maximum, double now, ImVec2 uvMinimum, ImVec2 uvMaximum,
    ImU32 tint, float rounding) {
    ReleaseSubmittedDraw();
    if (!m_Previous || m_Duration <= 0) return false;
    const auto amount = static_cast<float>((now - m_Last.completedAt) / m_Duration);
    if (amount >= 1 || !current) { Clear(); return false; }
    // Keep a queued ImGui draw valid if navigation cancels the transition in
    // another viewport before this frame's draw lists reach OpenGL.
    m_DrawnPrevious = m_Previous;
    m_DrawnFrame = ImGui::GetFrameCount();
    return m_Renderer.Draw(list, m_Previous.Texture(), current, minimum, maximum,
        std::clamp(amount, 0.0f, 1.0f), m_Last.encoded, uvMinimum, uvMaximum, tint, rounding);
}
