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
    m_Presented.clear();
    m_Frame = -1;
    m_Progress = 1.0f;
    m_Active = false;
    m_AnchorKey.clear();
    m_AnchorPending = false;
    m_Viewport = {};
}

void GalleryLayoutAnimation::BeginFrame(
    const std::string& workspace, bool grid, int frame, double time,
    std::uint64_t orderRevision, bool reducedMotion) {
    if (workspace != m_Workspace || m_Frame != frame - 1) {
        Reset();
        m_Workspace = workspace;
        m_Grid = grid;
        m_OrderRevision = orderRevision;
    }
    if (reducedMotion) {
        m_Active = false;
        m_Progress = 1.0f;
        m_From.clear();
        m_Presented.clear();
    } else if (grid != m_Grid || orderRevision != m_OrderRevision) {
        // A reversal starts from the geometry currently on screen.
        m_From = m_Active ? Sample() : m_Layout;
        m_StartedAt = time;
        m_Duration = grid != m_Grid ? 0.26 : 0.22;
        m_Progress = 0.0f;
        m_Active = std::any_of(m_From.begin(), m_From.end(), [](const auto& entry) {
            return entry.second.rendered && entry.second.image.Overlaps(entry.second.clip) &&
                entry.second.clip.GetWidth() > 0 && entry.second.clip.GetHeight() > 0;
        });
        m_Layout.clear();
        if (!m_Active) m_From.clear();
    } else if (m_Active) {
        const float elapsed = std::clamp(static_cast<float>((time - m_StartedAt) / m_Duration), 0.0f, 1.0f);
        const float remaining = 1.0f - elapsed;
        m_Progress = 1.0f - remaining * remaining * remaining;
        if (elapsed >= 1.0f) {
            m_Active = false;
            m_From.clear();
            m_Presented.clear();
        }
    }
    m_Grid = grid;
    m_OrderRevision = orderRevision;
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
    // A newly discovered image has no previous slot.
    // Show it in its real slot immediately, even while its neighbors move.
    return m_Active && m_From.find(key) != m_From.end();
}

void GalleryLayoutAnimation::ObserveSlot(
    const std::string& key, const ImRect& image, const ImRect& clip) {
    m_Layout.insert_or_assign(key, Pose{image, clip, 1.0f, false, ++m_NextOrder, m_Frame});
}

void GalleryLayoutAnimation::PrepareSwitch(const std::string& preferredKey) {
    const Layout displayed = m_Active ? Sample() : Layout{};
    const auto& layout = m_Active ? displayed : m_Layout;
    const Pose* anchor = nullptr;
    float nearest = std::numeric_limits<float>::max();
    for (const auto& [key, pose] : layout) {
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
    endpoint.opacity = 0.0f;
    return endpoint;
}

GalleryLayoutAnimation::Pose GalleryLayoutAnimation::SamplePose(
    const std::string& key, const Pose& destination) const {
    const auto from = m_From.find(key);
    if (from == m_From.end()) return destination;
    Pose pose = Interpolate(from->second, destination, m_Progress);
    // The viewport clips motion, not the destination stack's exposed strip.
    // Interpolating those strips cuts a moving photo into a stationary slot.
    pose.clip = from->second.clip;
    pose.clip.Add(destination.clip);
    if (m_Viewport.GetWidth() > 0 && m_Viewport.GetHeight() > 0) pose.clip.Add(m_Viewport);
    if (destination.clip.GetWidth() <= 0 || destination.clip.GetHeight() <= 0) {
        // Let the complete photo arrive behind its cover before retiring it.
        pose.opacity *= std::clamp((1.0f - m_Progress) / .15f, 0.0f, 1.0f);
    }
    return pose;
}

GalleryLayoutAnimation::Layout GalleryLayoutAnimation::Sample() const {
    Layout result;
    result.reserve(m_From.size() + m_Layout.size());
    for (const auto& [key, destination] : m_Layout) {
        result.emplace(key, SamplePose(key, destination));
    }
    for (const auto& [key, origin] : m_From) {
        if (m_Layout.find(key) == m_Layout.end())
            result.emplace(key, Interpolate(origin, OffscreenEndpoint(origin, m_Grid), m_Progress));
    }
    return result;
}

void GalleryLayoutAnimation::Draw(ImDrawList* draw, const ImRect& bounds,
    const std::function<Texture(const std::string&)>& texture) {
    if (!m_Active) return;
    m_Presented.clear();
    for (const auto& [key, destination] : m_Layout) {
        // New images are drawn by their normal tile; the overlay owns only
        // identities that existed before this layout change.
        if (m_From.find(key) != m_From.end() && Participates(key))
            m_Presented.emplace_back(key, SamplePose(key, destination));
    }
    for (const auto& [key, origin] : m_From) {
        if (m_Layout.find(key) == m_Layout.end() && Participates(key))
            m_Presented.emplace_back(key, Interpolate(origin, OffscreenEndpoint(origin, m_Grid), m_Progress));
    }
    std::sort(m_Presented.begin(), m_Presented.end(), [](const auto& a, const auto& b) {
        return a.second.order < b.second.order;
    });
    for (auto& [key, pose] : m_Presented) {
        pose.rendered = false;
        if (pose.opacity <= .001f) continue;
        ImRect clip = pose.clip;
        clip.ClipWith(bounds);
        if (clip.GetWidth() <= 0 || clip.GetHeight() <= 0 || !pose.image.Overlaps(clip)) continue;
        const Texture image = texture(key);
        if (!image.handle || image.width <= 0 || image.height <= 0) {
            pose.rendered = false;
            continue;
        }
        pose.clip = clip;
        pose.rendered = true;
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

bool GalleryLayoutAnimation::DisplayRect(
    const std::string& key, const ImRect& target, ImRect& displayed) const {
    if (!m_Active || m_From.find(key) == m_From.end()) return false;
    const auto destination = m_Layout.find(key);
    if (destination == m_Layout.end()) return false;
    const auto& image = destination->second.image;
    if (image.GetWidth() <= 0 || image.GetHeight() <= 0) return false;
    const auto pose = SamplePose(key, destination->second);
    const ImVec2 scale(pose.image.GetWidth() / image.GetWidth(), pose.image.GetHeight() / image.GetHeight());
    const auto map = [&](const ImVec2& point) {
        return ImVec2(pose.image.Min.x + (point.x - image.Min.x) * scale.x,
            pose.image.Min.y + (point.y - image.Min.y) * scale.y);
    };
    displayed = ImRect(map(target.Min), map(target.Max));
    return true;
}

std::string GalleryLayoutAnimation::ImageAt(const ImVec2& point) const {
    if (!m_Active) return {};
    for (auto presented = m_Presented.rbegin(); presented != m_Presented.rend(); ++presented) {
        if (!presented->second.rendered) continue;
        const auto destination = m_Layout.find(presented->first);
        const auto origin = m_From.find(presented->first);
        if (origin == m_From.end()) continue;
        // Texture readiness and draw order come from the last presentation,
        // but picking must use the same current pose as the tile's hit rectangle.
        // Otherwise a fast move can disable the tile under the pointer for a frame.
        const auto pose = destination != m_Layout.end()
            ? SamplePose(presented->first, destination->second)
            : Interpolate(origin->second, OffscreenEndpoint(origin->second, m_Grid), m_Progress);
        if (pose.opacity > .01f && pose.clip.Contains(point) && pose.image.Contains(point) &&
            (m_Viewport.GetWidth() <= 0 || m_Viewport.GetHeight() <= 0 || m_Viewport.Contains(point)))
            return presented->first;
    }
    return {};
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
