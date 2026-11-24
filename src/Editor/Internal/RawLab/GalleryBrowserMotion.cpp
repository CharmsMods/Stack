
#include "GalleryBrowserMotion.h"

#include <algorithm>

namespace Stack::Editor::RawLabInternal {
namespace {

bool Valid(ImRect rect) { return rect.GetWidth() > 0 && rect.GetHeight() > 0; }

ImRect Project(const ImRect& rect, const ImRect& surface, const ImRect& destination) {
    const float scale = destination.GetWidth() / surface.GetWidth();
    const auto map = [&](ImVec2 point) {
        return ImVec2(destination.Min.x + (point.x - surface.Min.x) * scale,
            destination.Min.y + (point.y - surface.Min.y) * scale);
    };
    return {map(rect.Min), map(rect.Max)};
}

void DrawTile(ImDrawList& draw, const GalleryBrowserMotion::Tile& tile,
    ImRect bounds, float alpha, bool lightweight) {
    const float scale = bounds.GetWidth() / tile.bounds.GetWidth();
    if (scale <= 0 || alpha <= 0) return;
    const float unit = tile.unit * scale;
    const auto pos = bounds.Min;
    const auto center = bounds.GetCenter();
    const float artWidth = std::max(1.f, bounds.GetWidth() - 18 * unit);
    const float artHeight = std::max(1.f, bounds.GetHeight() - 40 * unit);
    const auto tint = ImGui::GetColorU32(ImVec4(1, 1, 1, alpha));
    if (tile.texture && tile.width > 0 && tile.height > 0) {
        const float fit = std::min(artWidth / tile.width, artHeight / tile.height);
        const ImVec2 size{tile.width * fit, tile.height * fit};
        const ImVec2 min{center.x - size.x * .5f + 3*unit, pos.y + 8*unit + (artHeight - size.y) * .5f};
        draw.AddImageRounded((ImTextureID)(intptr_t)tile.texture, min, {min.x + size.x, min.y + size.y},
            {0, 0}, {1, 1}, tint, 6 * unit);
    } else if (tile.drawFallback) {
        tile.drawFallback(tile.image, {center.x - 25*unit, pos.y + 28*unit},
            56*unit, ImGui::GetColorU32(ImGuiCol_TextDisabled, alpha));
    } else if (lightweight) {
        draw.AddRectFilled({pos.x + 8*unit, pos.y + 8*unit},
            {bounds.Max.x - 8*unit, pos.y + 8*unit + artHeight},
            ImGui::GetColorU32(ImGuiCol_FrameBg, alpha), 5 * unit);
    }
    if (tile.label && *tile.label && scale > .45f) {
        const float y = bounds.Max.y - 22*unit;
        if (lightweight) {
            const auto right = bounds.Max.x - 4*unit;
            draw.PushClipRect({bounds.Min.x, bounds.Min.y}, {right, bounds.Max.y}, true);
            draw.AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale,
                {pos.x + 8*unit, y}, ImGui::GetColorU32(ImGuiCol_Text, alpha), tile.label);
            draw.PopClipRect();
        } else {
            ImGui::RenderTextEllipsis(&draw, {pos.x + 8*unit, y},
                {bounds.Max.x - 8*unit, bounds.Max.y + 6*unit}, bounds.Max.x - 8*unit,
                tile.label, nullptr, nullptr);
        }
    }
}

} // namespace

void GalleryBrowserMotion::Capture(ImRect surface) {
    snapshot_.clear();
    savedSurface_ = surface;
}

void GalleryBrowserMotion::CaptureTile(const Tile& tile) {
    snapshot_.push_back({tile, tile.label ? tile.label : ""});
    snapshot_.back().tile.label = nullptr;
}

void GalleryBrowserMotion::Start(bool outward, ImRect anchor) {
    if (!outward && !Valid(anchor)) { Stop(); return; }
    outward_ = outward;
    anchor_ = anchor;
    active_ = waiting_ = true;
    waitedForAnchor_ = false;
    progress_ = 0;
}

void GalleryBrowserMotion::Stop() {
    snapshot_.clear();
    active_ = waiting_ = false;
}

void GalleryBrowserMotion::Begin(ImRect surface, bool ready, bool reduced) {
    if (!active_) return;
    if (reduced || !Valid(surface) || !Valid(savedSurface_)) { Stop(); return; }
    surface_ = surface;
    if (waiting_) {
        if (!ready) return;
        if (!Valid(anchor_)) {
            if (waitedForAnchor_) Stop();
            waitedForAnchor_ = true;
            return;
        }
        waiting_ = false;
        started_ = ImGui::GetTime();
    }
    const float t = std::clamp(static_cast<float>((ImGui::GetTime() - started_) / .25), 0.f, 1.f);
    if (t >= 1) { Stop(); return; }
    progress_ = t * t * (3.f - 2.f * t);
}

ImRect GalleryBrowserMotion::ZoomBounds(float amount) const {
    const float fit = .82f * std::min(anchor_.GetWidth() / surface_.GetWidth(),
        anchor_.GetHeight() / surface_.GetHeight());
    const auto cardCenter = anchor_.GetCenter();
    const auto center = ImLerp(cardCenter, surface_.GetCenter(), amount);
    const float scale = ImLerp(fit, 1.f, amount);
    const ImVec2 half{surface_.GetWidth() * scale * .5f, surface_.GetHeight() * scale * .5f};
    return {{center.x - half.x, center.y - half.y}, {center.x + half.x, center.y + half.y}};
}

void GalleryBrowserMotion::DrawSnapshot(ImDrawList& draw, ImRect destination, float alpha) const {
    if (alpha <= 0 || snapshot_.empty()) return;
    draw.PushClipRect(destination.Min, destination.Max, true);
    for (const auto& saved : snapshot_) {
        auto tile = saved.tile;
        tile.label = saved.label.c_str();
        DrawTile(draw, tile, Project(tile.bounds, savedSurface_, destination), alpha, true);
    }
    draw.PopClipRect();
}

void GalleryBrowserMotion::DrawUnder(ImDrawList& draw) const {
    if (!active_ || (!waiting_ && outward_)) return;
    DrawSnapshot(draw, surface_, waiting_ ? 1.f : 1.f - progress_);
    if (waiting_) return;
    const auto zoom = ZoomBounds(progress_);
    draw.AddRectFilled(zoom.Min, zoom.Max, ImGui::GetColorU32(ImGuiCol_PopupBg),
        9.f * (1.f - progress_));
}

void GalleryBrowserMotion::DrawOver(ImDrawList& draw) const {
    if (!active_ || waiting_ || !outward_) return;
    const auto zoom = ZoomBounds(1.f - progress_);
    const float opacity = std::min(1.f, (1.f - progress_) * 4.f);
    draw.AddRectFilled(zoom.Min, zoom.Max, ImGui::GetColorU32(ImGuiCol_PopupBg, opacity),
        9.f * progress_);
    DrawSnapshot(draw, zoom, opacity);
}

void GalleryBrowserMotion::DrawLiveTile(ImDrawList& draw, const Tile& tile) const {
    if (!active_ || outward_) {
        if (!active_ || !waiting_) DrawTile(draw, tile, tile.bounds, 1.f, false);
    } else if (!waiting_) {
        const auto zoom = ZoomBounds(progress_);
        draw.PushClipRect(zoom.Min, zoom.Max, true);
        DrawTile(draw, tile, Project(tile.bounds, surface_, zoom), 1.f, true);
        draw.PopClipRect();
    }
}

} // namespace Stack::Editor::RawLabInternal
