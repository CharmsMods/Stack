#pragma once

#include "Renderer/ViewportPreviewTypes.h"
#include <atomic>
#include <climits>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace Stack::Renderer {

// Immutable geometry and monotonic readiness for one image-content revision.
// Readers never acquire the pixel store's I/O lock. Publish only after a complete
// payload is stored; the payload must remain immutable for this index's lifetime.
class ViewportSectionIndex {
public:
    static constexpr int Edge = 1024;
    static constexpr int Guard = 4;

    ViewportSectionIndex(int width, int height) : m_Width(width), m_Height(height) {
        if (width <= 0 || height <= 0 || width > 1048576 || height > 1048576 ||
            std::uint64_t(width) * height > std::uint64_t(INT_MAX))
            throw std::invalid_argument("Invalid native section extent");
        m_Columns = (width + Edge - 1) / Edge;
        m_Rows = (height + Edge - 1) / Edge;
        m_Count = m_Columns * m_Rows;
        m_Ready = std::make_unique<std::atomic<bool>[]>(m_Count);
        for (int i = 0; i < m_Count; ++i) m_Ready[i].store(false, std::memory_order_relaxed);
    }

    int Count() const { return m_Count; }
    ImageRegion Bounds(int index) const {
        if (index < 0 || index >= m_Count) return {};
        const int x = (index % m_Columns) * Edge, y = (index / m_Columns) * Edge;
        return {m_Width, m_Height, x, y, std::min(Edge, m_Width-x), std::min(Edge, m_Height-y)};
    }
    ImageRegion Raster(int index) const {
        auto r = Bounds(index);
        if (!r.Valid()) return r;
        const int right = std::min(m_Width, r.x+r.width+Guard);
        const int bottom = std::min(m_Height, r.y+r.height+Guard);
        r.x = std::max(0, r.x-Guard); r.y = std::max(0, r.y-Guard);
        r.width = right-r.x; r.height = bottom-r.y;
        return r;
    }
    bool Has(int index) const {
        return index >= 0 && index < m_Count && m_Ready[index].load(std::memory_order_acquire);
    }
    bool Complete() const { return m_Completed.load(std::memory_order_acquire) == m_Count; }

    // The payload owner serializes writes. Duplicate publication cannot count a
    // section twice. Acquire readers see every write preceding first publication.
    bool Publish(int index) {
        if (index < 0 || index >= m_Count || m_Ready[index].exchange(true, std::memory_order_acq_rel))
            return false;
        m_Completed.fetch_add(1, std::memory_order_release);
        return true;
    }
    bool Covers(const ImageRegion& visible) const {
        if (!Compatible(visible)) return false;
        const int right = (visible.x+visible.width-1)/Edge;
        const int bottom = (visible.y+visible.height-1)/Edge;
        for (int y = visible.y/Edge; y <= bottom; ++y)
            for (int x = visible.x/Edge; x <= right; ++x)
                if (!Has(y*m_Columns+x)) return false;
        return true;
    }

    // Scheduling preference, not a pixel-quality rule: reuse finished sections
    // first, then bring the camera center into focus before the viewport edges.
    bool Prefer(int candidate, int current, const ImageRegion& visible) const {
        if (candidate < 0 || candidate >= m_Count) return false;
        if (current < 0 || current >= m_Count) return true;
        const bool cached = Has(candidate), currentCached = Has(current);
        if (cached != currentCached) return cached;
        if (!Compatible(visible)) return candidate < current;
        const auto distance = [&](int index) {
            const auto r = Bounds(index);
            const std::int64_t dx = 2ll*r.x+r.width-(2ll*visible.x+visible.width);
            const std::int64_t dy = 2ll*r.y+r.height-(2ll*visible.y+visible.height);
            return dx*dx+dy*dy;
        };
        const auto a = distance(candidate), b = distance(current);
        return a < b || (a == b && candidate < current);
    }
    int NextMissing(const ImageRegion& visible, bool finishWhole) const {
        int next = -1;
        if (Compatible(visible)) {
            const int right = (visible.x+visible.width-1)/Edge;
            const int bottom = (visible.y+visible.height-1)/Edge;
            for (int y = visible.y/Edge; y <= bottom; ++y)
                for (int x = visible.x/Edge; x <= right; ++x) {
                    const int index = y*m_Columns+x;
                    if (!Has(index) && Prefer(index, next, visible)) next = index;
                }
        }
        if (next >= 0 || !finishWhole) return next;
        for (int i = 0; i < m_Count; ++i) if (!Has(i)) return i;
        return -1;
    }

private:
    bool Compatible(const ImageRegion& r) const {
        return r.Valid() && r.fullWidth == m_Width && r.fullHeight == m_Height;
    }
    const int m_Width, m_Height;
    int m_Columns = 0, m_Rows = 0, m_Count = 0;
    std::unique_ptr<std::atomic<bool>[]> m_Ready;
    std::atomic<int> m_Completed{0};
};

} // namespace Stack::Renderer
