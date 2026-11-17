#pragma once

#include "Renderer/ViewportPreviewTypes.h"
#include <atomic>
#include <climits>
#include <memory>
#include <stdexcept>

namespace Stack::Renderer {

// One immutable image grid. Publication is monotonic: an entry becomes ready
// only after its complete payload is stored. UI queries never acquire the
// storage mutex, copy pixels, or touch a file. A new revision owns a new index.
class ViewportSectionIndex {
public:
    static constexpr int Edge = 1024;

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
        return {m_Width, m_Height, x, y, std::min(Edge, m_Width - x), std::min(Edge, m_Height - y)};
    }
    bool Has(int index) const {
        return index >= 0 && index < m_Count && m_Ready[index].load(std::memory_order_acquire);
    }
    bool Complete() const { return m_Completed.load(std::memory_order_acquire) == m_Count; }

    // The payload owner serializes writers and must finish installing an entry
    // before calling this. Repeated publication does not inflate completion.
    void Publish(int index) {
        if (index < 0 || index >= m_Count || m_Ready[index].exchange(true, std::memory_order_acq_rel)) return;
        m_Completed.fetch_add(1, std::memory_order_release);
        int first = m_FirstMissing.load(std::memory_order_relaxed);
        for (;;) {
            int next = first;
            while (next < m_Count && Has(next)) ++next;
            if (next == first || m_FirstMissing.compare_exchange_weak(first, next,
                    std::memory_order_release, std::memory_order_relaxed)) break;
        }
    }

    bool Covers(const ImageRegion& visible) const {
        if (!Compatible(visible)) return false;
        return FirstVisibleMissing(visible) < 0;
    }
    int NextMissing(const ImageRegion& visible, bool finishWhole) const {
        if (Compatible(visible)) {
            const int shown = FirstVisibleMissing(visible);
            if (shown >= 0) return shown;
        }
        if (!finishWhole) return -1;
        // Publication can be between setting ready and advancing the cursor.
        // Recheck that small prefix rather than returning an already-ready job.
        for (int first = m_FirstMissing.load(std::memory_order_acquire); first < m_Count; ++first)
            if (!Has(first)) return first;
        return -1;
    }

private:
    int m_Width, m_Height, m_Columns = 0, m_Rows = 0, m_Count = 0;
    std::unique_ptr<std::atomic<bool>[]> m_Ready;
    std::atomic<int> m_Completed{0}, m_FirstMissing{0};

    bool Compatible(const ImageRegion& visible) const {
        return visible.Valid() && visible.fullWidth == m_Width && visible.fullHeight == m_Height;
    }
    int FirstVisibleMissing(const ImageRegion& visible) const {
        const int x0 = visible.x / Edge, x1 = (visible.x + visible.width - 1) / Edge;
        const int y0 = visible.y / Edge, y1 = (visible.y + visible.height - 1) / Edge;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int index = y * m_Columns + x;
                if (!Has(index)) return index;
            }
        return -1;
    }
};

} // namespace Stack::Renderer
