#include "RawLabGalleryLayoutAnimation.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace Stack::Editor::RawLabInternal {

void GalleryLayoutAnimation::Reset() {
    m_Workspace.clear();
    m_Layout.clear();
    m_From.clear();
    m_Frame = -1;
    m_Progress = 1.0f;
    m_Active = false;
    m_AnchorKey.clear();
    m_AnchorPending = false;
}

void GalleryLayoutAnimation::BeginFrame(
    const std::string& workspace, bool grid, int frame, double time) {
    if (workspace != m_Workspace || m_Frame != frame - 1) {
        Reset();
        m_Workspace = workspace;
        m_Grid = grid;
    }
    if (grid != m_Grid) {
        // A reversal starts from the geometry currently on screen.
        m_From = m_Active ? Sample() : m_Layout;
        m_StartedAt = time;
        m_Progress = 0.0f;
        m_Active = std::any_of(m_From.begin(), m_From.end(), [](const auto& entry) {
            return entry.second.rendered && entry.second.image.Overlaps(entry.second.clip) &&
                entry.second.clip.GetWidth() > 0 && entry.second.clip.GetHeight() > 0;
        });
        m_Layout.clear();
        m_Grid = grid;
    } else if (m_Active) {
        const float elapsed = std::clamp(static_cast<float>((time - m_StartedAt) / .32), 0.0f, 1.0f);
        m_Progress = elapsed * elapsed * (3.0f - 2.0f * elapsed);
        if (elapsed >= 1.0f) {
            m_Active = false;
            m_From.clear();
        }
    }
    m_Frame = frame;
    m_NextOrder = 0;
}

void GalleryLayoutAnimation::EndFrame() {
    for (auto entry = m_Layout.begin(); entry != m_Layout.end();) {
        if (entry->second.frame != m_Frame) entry = m_Layout.erase(entry);
        else ++entry;
    }
}

bool GalleryLayoutAnimation::Observe(
    const std::string& key, const ImRect& image, const ImRect& clip, float opacity) {
    ImRect exposed = clip;
    const auto slot = m_Layout.find(key);
    if (slot != m_Layout.end() && slot->second.frame == m_Frame) exposed.ClipWith(slot->second.clip);
    m_Layout.insert_or_assign(key, Pose{image, exposed, opacity, true, ++m_NextOrder, m_Frame});
    return m_Active;
}

void GalleryLayoutAnimation::ObserveSlot(
    const std::string& key, const ImRect& image, const ImRect& clip) {
    m_Layout.insert_or_assign(key, Pose{image, clip, 1.0f, false, ++m_NextOrder, m_Frame});
}

void GalleryLayoutAnimation::PrepareSwitch(const std::string& preferredKey) {
    const Pose* anchor = nullptr;
    float nearest = std::numeric_limits<float>::max();
    for (const auto& [key, pose] : m_Layout) {
        if (!pose.rendered || !pose.image.Overlaps(pose.clip) ||
            pose.clip.GetWidth() <= 0 || pose.clip.GetHeight() <= 0) continue;
        const ImVec2 center = pose.image.GetCenter();
        const ImVec2 viewportCenter = m_Viewport.GetCenter();
        const float distance = (center.x - viewportCenter.x) * (center.x - viewportCenter.x) +
            (center.y - viewportCenter.y) * (center.y - viewportCenter.y);
        if (key == preferredKey || distance < nearest) {
            anchor = &pose;
            m_AnchorKey = key;
            nearest = distance;
            if (key == preferredKey) break;
        }
    }
    m_AnchorPending = anchor != nullptr;
    if (anchor) {
        const ImVec2 center = anchor->image.GetCenter();
        m_AnchorFraction = ImVec2(
            std::clamp((center.x - m_Viewport.Min.x) / std::max(1.0f, m_Viewport.GetWidth()), .1f, .9f),
            std::clamp((center.y - m_Viewport.Min.y) / std::max(1.0f, m_Viewport.GetHeight()), .1f, .9f));
    }
}

GalleryLayoutAnimation::Pose GalleryLayoutAnimation::Interpolate(
    const Pose& from, const Pose& to, float amount) {
    return {ImRect(ImLerp(from.image.Min, to.image.Min, amount),
                   ImLerp(from.image.Max, to.image.Max, amount)),
            ImRect(ImLerp(from.clip.Min, to.clip.Min, amount),
                   ImLerp(from.clip.Max, to.clip.Max, amount)),
            ImLerp(from.opacity, to.opacity, amount), from.rendered || to.rendered, to.order, to.frame};
}

GalleryLayoutAnimation::Pose GalleryLayoutAnimation::OffscreenEndpoint(const Pose& pose, bool grid) {
    Pose endpoint = pose;
    const ImVec2 shift = grid
        ? ImVec2(0.0f, pose.clip.Max.y - pose.image.Min.y + pose.image.GetHeight())
        : ImVec2(pose.clip.Max.x - pose.image.Min.x + pose.image.GetWidth(), 0.0f);
    endpoint.image.Translate(shift);
    return endpoint;
}

GalleryLayoutAnimation::Layout GalleryLayoutAnimation::Sample() const {
    Layout result;
    result.reserve(m_From.size() + m_Layout.size());
    for (const auto& [key, destination] : m_Layout) {
        const auto from = m_From.find(key);
        const Pose origin = from != m_From.end() ? from->second : OffscreenEndpoint(destination, !m_Grid);
        result.emplace(key, Interpolate(origin, destination, m_Progress));
    }
    for (const auto& [key, origin] : m_From) {
        if (m_Layout.find(key) == m_Layout.end())
            result.emplace(key, Interpolate(origin, OffscreenEndpoint(origin, m_Grid), m_Progress));
    }
    return result;
}

void GalleryLayoutAnimation::Draw(ImDrawList* draw, const ImRect& bounds,
    const std::function<Texture(const std::string&)>& texture) const {
    if (!m_Active) return;
    const Layout poses = Sample();
    std::vector<const Layout::value_type*> ordered;
    ordered.reserve(poses.size());
    for (const auto& entry : poses) {
        if (Participates(entry.first)) ordered.push_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        return a->second.order < b->second.order;
    });
    for (const auto* entry : ordered) {
        const auto& [key, pose] = *entry;
        if (pose.opacity <= .001f) continue;
        ImRect clip = pose.clip;
        clip.ClipWith(bounds);
        if (clip.GetWidth() <= 0 || clip.GetHeight() <= 0 || !pose.image.Overlaps(clip)) continue;
        const Texture image = texture(key);
        if (!image.handle || image.width <= 0 || image.height <= 0) continue;
        const float scale = std::min(pose.image.GetWidth() / image.width, pose.image.GetHeight() / image.height);
        const ImVec2 center = pose.image.GetCenter();
        const ImVec2 half(image.width * scale * .5f, image.height * scale * .5f);
        draw->PushClipRect(clip.Min, clip.Max, true);
        draw->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(image.handle)),
            ImVec2(center.x - half.x, center.y - half.y), ImVec2(center.x + half.x, center.y + half.y),
            ImVec2(0, 1), ImVec2(1, 0),
            ImGui::GetColorU32(ImVec4(1, 1, 1, pose.opacity)));
        draw->PopClipRect();
    }
}

bool GalleryLayoutAnimation::Protects(const std::string& key) const {
    return m_Active && Participates(key);
}

bool GalleryLayoutAnimation::Participates(const std::string& key) const {
    const auto visible = [](const Layout& layout, const std::string& candidate) {
        const auto pose = layout.find(candidate);
        return pose != layout.end() && pose->second.clip.GetWidth() > 0 &&
            pose->second.clip.GetHeight() > 0 && pose->second.image.Overlaps(pose->second.clip);
    };
    return visible(m_From, key) || visible(m_Layout, key);
}

}
