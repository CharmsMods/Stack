#include "Editor/UI/GraphFrameTransition.h"
#include "Renderer/GpuMemoryBudget.h"
#include "Raw/RawGpuMemoryBudget.h"

namespace {
struct FrameBounds { double left = 0, top = 0, right = 1, bottom = 1; };
FrameBounds NormalizedBounds(const Stack::Renderer::ImageRegion& region) {
    if (!region.Valid()) return {};
    return {double(region.x) / region.fullWidth, double(region.y) / region.fullHeight,
        double(region.x + region.width) / region.fullWidth,
        double(region.y + region.height) / region.fullHeight};
}
}

bool GraphFrameTransition::Prepare(const Stack::GraphRendering::FramePresentation& next,
    double workMilliseconds, int fadeBelowFps, bool smoothUpdates) {
    ReleaseSubmittedDraw();
    const double duration = smoothUpdates
        ? Stack::GraphRendering::FrameBlendDuration(m_Last, next, workMilliseconds, fadeBelowFps) : 0;
    const double progress = m_StartedAt >= 0 && m_Duration > 0
        ? std::clamp((next.completedAt - m_StartedAt) / m_Duration, 0.0, 1.0) : 0.0;
    const bool keepBase = m_Previous && m_Duration > 0 && progress < 1.0 &&
        Stack::GraphRendering::CompatibleFrameGeometry(m_Base, next);
    if (!keepBase) {
        Clear();
        m_Base = m_Last;
    }
    m_Duration = duration;
    m_Last = next;
    if (m_Duration <= 0) { Clear(); return false; }
    const auto memory = Stack::Renderer::QueryGpuMemoryBudget();
    const auto bytes = keepBase ? m_Previous.Bytes() :
        static_cast<std::uint64_t>(std::max(0, m_Base.width)) * std::max(0, m_Base.height) * 16u;
    if (memory.queryAvailable && Raw::ResolveRawGpuMemoryBudget(memory).workingBudgetBytes < bytes) {
        Clear();
        return false;
    }
    m_KeepBase = keepBase;
    m_StartedAt = keepBase && m_StartedAt >= 0 ? next.completedAt - progress * duration : -1.0;
    return true;
}

void GraphFrameTransition::RetainOwned(unsigned int texture, int width, int height) {
    if (m_KeepBase) {
        if (texture) glDeleteTextures(1, &texture);
        return;
    }
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
    m_StartedAt = -1.0;
    m_KeepBase = false;
}

void GraphFrameTransition::ReleaseSubmittedDraw() {
    if (!ImGui::GetCurrentContext() || ImGui::GetFrameCount() != m_DrawnFrame)
        m_DrawnPrevious.Reset();
}

void GraphFrameTransition::Validate(const Stack::GraphRendering::RequestTag& identity, double now) {
    ReleaseSubmittedDraw();
    if (!Stack::GraphRendering::SameContext(identity, m_Last.identity)) Reset();
    else if (m_StartedAt >= 0 && now - m_StartedAt >= m_Duration) Clear();
}

bool GraphFrameTransition::Draw(ImDrawList* list, unsigned int current,
    ImVec2 minimum, ImVec2 maximum, double now, ImVec2 uvMinimum, ImVec2 uvMaximum,
    ImU32 tint, float rounding) {
    if (!list || !current) return false;
    BlendDescription blend;
    if (!DescribeBlend(now, blend)) return false;
    const auto previous = NormalizedBounds(m_Base.region), next = NormalizedBounds(m_Last.region);
    const FrameBounds overlap{std::max(previous.left, next.left), std::max(previous.top, next.top),
        std::min(previous.right, next.right), std::min(previous.bottom, next.bottom)};
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) { Clear(); return false; }
    const double width = next.right - next.left, height = next.bottom - next.top;
    const auto screen = [&](double x, double y) {
        return ImVec2(float(minimum.x + (maximum.x - minimum.x) * (x - next.left) / width),
            float(minimum.y + (maximum.y - minimum.y) * (y - next.top) / height));
    };
    const auto uv = [&](double x, double y) {
        return ImVec2(float(uvMinimum.x + (uvMaximum.x - uvMinimum.x) * (x - next.left) / width),
            float(uvMinimum.y + (uvMaximum.y - uvMinimum.y) * (y - next.top) / height));
    };
    const bool complete = overlap.left == next.left && overlap.top == next.top &&
        overlap.right == next.right && overlap.bottom == next.bottom;
    const bool blended = m_Renderer.Draw(list, blend.previousTexture, current,
        screen(overlap.left, overlap.top), screen(overlap.right, overlap.bottom),
        blend.amount, blend.encoded,
        uv(overlap.left, overlap.top), uv(overlap.right, overlap.bottom), tint, complete ? rounding : 0,
        blend.previousOffset, blend.previousScale);
    if (!blended) return false;
    // Draw uncovered portions exactly once. Drawing the complete current patch
    // beneath a translucent blend would change its alpha and RGB composition.
    const auto drawCurrent = [&](double left, double top, double right, double bottom) {
        if (left >= right || top >= bottom) return;
        list->AddImage((ImTextureID)(intptr_t)current, screen(left, top), screen(right, bottom),
            uv(left, top), uv(right, bottom), tint);
    };
    if (!complete) {
        drawCurrent(next.left, next.top, next.right, overlap.top);
        drawCurrent(next.left, overlap.bottom, next.right, next.bottom);
        drawCurrent(next.left, overlap.top, overlap.left, overlap.bottom);
        drawCurrent(overlap.right, overlap.top, next.right, overlap.bottom);
    }
    return true;
}

bool GraphFrameTransition::DescribeBlend(double now, BlendDescription& description) {
    description = {};
    ReleaseSubmittedDraw();
    if (!m_Previous || m_Duration <= 0) return false;
    if (m_StartedAt < 0) m_StartedAt = now;
    const float amount = float(std::clamp((now - m_StartedAt) / m_Duration, 0.0, 1.0));
    if (amount >= 1) { Clear(); return false; }
    const auto previous = NormalizedBounds(m_Base.region), next = NormalizedBounds(m_Last.region);
    const FrameBounds overlap{std::max(previous.left, next.left), std::max(previous.top, next.top),
        std::min(previous.right, next.right), std::min(previous.bottom, next.bottom)};
    if (overlap.left >= overlap.right || overlap.top >= overlap.bottom) { Clear(); return false; }
    description.previousTexture = m_Previous.Texture();
    description.previousOffset = ImVec2(float((next.left - previous.left) / (previous.right - previous.left)),
        float((previous.bottom - next.bottom) / (previous.bottom - previous.top)));
    description.previousScale = ImVec2(float((next.right - next.left) / (previous.right - previous.left)),
        float((next.bottom - next.top) / (previous.bottom - previous.top)));
    description.imageBounds = ImVec4(float(overlap.left), float(1.0 - overlap.bottom),
        float(overlap.right), float(1.0 - overlap.top));
    description.amount = amount;
    description.encoded = m_Last.encoded;
    // Backdrop composition and ordinary ImGui draws borrow the same old frame.
    // Navigation may clear the active blend later in this frame, so retain the
    // lease until all queued draws have reached the render backend.
    m_DrawnPrevious = m_Previous;
    m_DrawnFrame = ImGui::GetFrameCount();
    return true;
}
