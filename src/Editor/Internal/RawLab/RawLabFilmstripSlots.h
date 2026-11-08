#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "RawLabGalleryLayout.h"

namespace Stack::Editor::RawLabInternal {
// Per-Gallery presentation geometry. All positions are local logical pixels.
struct FilmstripSlots {
    float available = -1.f, requested = -1.f, requestedHeight = -1.f, unit = 1.f;
    float tile = 124.f, gap = 10.f, stride = 134.f;
    float width = 0.f, margin = 0.f, maximum = 0.f;
    std::size_t count = 0, visible = 0;
    bool enabled = false, dragging = false;
    float wheelRemainder = 0.f;

    std::vector<float> ratios, widths, offsets, spans;
    std::string contentScope;
    std::uint64_t contentRevision = ~std::uint64_t{0}, organizationRevision = ~std::uint64_t{0}, queueRevision = ~std::uint64_t{0};
    bool dirty = true;
    float framedTarget = -1.f;
    std::size_t lastStop = 0;
    bool SetContent(const std::string& scope, std::uint64_t content,
        std::uint64_t organization, std::uint64_t queue) {
        if (contentScope == scope && contentRevision == content &&
            organizationRevision == organization && queueRevision == queue) return false;
        const bool scopeChanged = contentScope != scope;
        contentScope = scope; contentRevision = content;
        organizationRevision = organization; queueRevision = queue;
        // Measurements belong to the ordered cards, not to their numeric slots.
        // Selection and thumbnail readiness do not advance these revisions.
        ratios.clear(); dirty = true; wheelRemainder = 0.0f;
        return scopeChanged;
    }
    float Position(std::size_t i) const { return i < offsets.size() ? offsets[i] : 0.f; }
    float ItemWidth(std::size_t i) const { return i < widths.size() ? widths[i] : tile; }
    std::size_t Nearest(float value) const {
        if (offsets.empty()) return 0;
        auto it = std::lower_bound(offsets.begin(), offsets.begin() + lastStop + 1, value);
        if (it == offsets.begin()) return 0;
        if (it == offsets.begin() + lastStop + 1) return lastStop;
        auto i = static_cast<std::size_t>(it - offsets.begin());
        return value - offsets[i-1] < offsets[i] - value ? i-1 : i;
    }
    float Snap(float value) const { return Position(Nearest(value)); }
    void Report(std::size_t i, int w, int h) {
        if (!enabled || i >= ratios.size() || w <= 0 || h <= 0) return;
        const float ratio = float(w) / h;
        if (std::abs(ratios[i] - ratio) > .001f) { ratios[i] = ratio; dirty = true; }
    }
    GalleryVisibleRange Range(float origin, float low, float high, float prefetch) const {
        if (offsets.empty()) return {};
        auto first = std::lower_bound(offsets.begin(), offsets.end(), low - origin - prefetch - tile);
        auto last = std::upper_bound(offsets.begin(), offsets.end(), high - origin + prefetch);
        return {int(first-offsets.begin()), int(last-offsets.begin())};
    }
    void Frame(float target) {
        if(target==framedTarget)return;
        framedTarget=target;
        if (offsets.empty()) { width = available; margin = 0.f; return; }
        auto end = std::upper_bound(offsets.begin(), offsets.begin()+lastStop+1, target);
        const std::size_t first = end == offsets.begin() ? 0 : std::size_t(end-offsets.begin()-1);
        const std::size_t next = std::min(first+1,lastStop);
        const float blend = next == first ? 0.f : std::clamp((target-offsets[first])/(offsets[next]-offsets[first]),0.f,1.f);
        width = spans[first] + (spans[next]-spans[first])*blend;
        margin = std::max(0.f,(available-width)*.5f);
    }
    bool NeedsUpdate(float room,float thumbnail,std::size_t items,float scale,float imageHeight) const {
        return available!=std::max(1.f,room) || requested!=thumbnail || count!=items || unit!=scale ||
            requestedHeight!=imageHeight || dirty;
    }
    bool Update(float room, float thumbnail, std::size_t items, float scale, float imageHeight) {
        room = std::max(1.f, room);
        if (!NeedsUpdate(room,thumbnail,items,scale,imageHeight)) return false;
        available = room; requested = thumbnail; requestedHeight = imageHeight; count = items; unit = scale;
        const float minimumGap = std::min(10.f * unit, room * .25f);
        tile = std::max(1.f, std::min(thumbnail, room - minimumGap));
        ratios.resize(items, 1.5f);
        widths.resize(items); offsets.resize(items);
        float totalWidth = 0.f;
        for (std::size_t i=0;i<items;++i) {
            widths[i] = std::max(1.f, std::min(tile, imageHeight * ratios[i]));
            totalWidth += widths[i];
        }
        const float average = items ? totalWidth / items : tile;
        const float capacity = std::max(1.f, std::floor(room / (average + minimumGap)));
        gap = std::clamp(room / capacity - average, minimumGap, 16.f * unit);
        stride = tile + gap; // Legacy fallback only; Gallery uses offsets.
        float cursor = 0.f;
        for (std::size_t i=0;i<items;++i) {
            offsets[i] = cursor;
            cursor += widths[i] + gap;
        }
        lastStop = items ? items-1 : 0;
        while (lastStop > 0 && cursor-offsets[lastStop-1] <= room) --lastStop;
        maximum = Position(lastStop);
        spans.resize(items);
        std::size_t last = 0;
        for (std::size_t first=0;first<items;++first) {
            last = std::max(last, first);
            while (last+1<items && offsets[last+1]+widths[last+1]-offsets[first]+gap <= room) ++last;
            spans[first] = offsets[last]+widths[last]-offsets[first]+gap;
        }
        dirty = false;
        framedTarget=-1.f;
        Frame(0.f);
        return true;
    }
    void Wheel(float wheel, float& target) {
        if (wheel * wheelRemainder < 0.f) wheelRemainder = 0.f;
        wheelRemainder += wheel;
        const float ticks = std::trunc(wheelRemainder);
        wheelRemainder -= ticks;
        if (ticks != 0.f) target = Position(static_cast<std::size_t>(std::clamp(float(Nearest(target)) - ticks, 0.f, float(lastStop))));
    }
};
}
