#pragma once

#include <imgui.h>
#include <algorithm>

namespace Stack::Editor {

// Non-rounded scope marks use ImGui's existing primitive and current white UV.
// Finish the batch before any unrelated draw-list operation or state change.
class ScopeRectBatch {
public:
    explicit ScopeRectBatch(ImDrawList& draw) : m_Draw(draw),
        m_Batched(sizeof(ImDrawIdx) != 2 ||
            (draw.Flags & ImDrawListFlags_AllowVtxOffset) != 0) {}
    ~ScopeRectBatch() {
        if (m_Remaining) m_Draw.PrimUnreserve(m_Remaining * 6, m_Remaining * 4);
    }
    ScopeRectBatch(const ScopeRectBatch&) = delete;
    ScopeRectBatch& operator=(const ScopeRectBatch&) = delete;

    void Add(const ImVec2& minimum, const ImVec2& maximum, ImU32 color) {
        if ((color & IM_COL32_A_MASK) == 0) return;
        if (!m_Batched) {
            // Do not assume large-mesh support from a different backend.
            m_Draw.AddRectFilled(minimum, maximum, color);
            return;
        }
        if (!m_Remaining) {
            int count = 256;
            if (sizeof(ImDrawIdx) == 2) {
                // Match AddRectFilled's command boundary, rather than rolling
                // over early merely because the next reservation is larger.
                const unsigned int index = m_Draw._VtxCurrentIdx;
                const auto available = index < 65536u ? (65535u - index) / 4u : 0u;
                if (available) count = std::min(count, static_cast<int>(available));
            }
            m_Draw.PrimReserve(count * 6, count * 4);
            m_Remaining = count;
        }
        m_Draw.PrimRect(minimum, maximum, color);
        --m_Remaining;
    }

private:
    ImDrawList& m_Draw;
    const bool m_Batched;
    int m_Remaining = 0;
};

} // namespace Stack::Editor
