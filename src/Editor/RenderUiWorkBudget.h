#pragma once

#include <chrono>
#include <cstddef>

namespace Stack::EditorRendering {

// One editor's non-preemptible completion/retirement work. Repeated pumps in
// the same UI frame share the allowance; idle time between pumps is not work.
// The first item always gets a turn, so a slow driver call cannot permanently
// starve cleanup or a terminal operation. Headless callers use frame -1.
class RenderUiWorkBudget {
public:
    void BeginFrame(int frame) {
        if (frame < 0 || frame != m_Frame) {
            m_Frame = frame;
            m_Items = 0;
            m_Elapsed = {};
        }
    }

    bool CanStart(std::size_t maximumItems, double milliseconds) const {
        return m_Items < maximumItems && (m_Items == 0 ||
            std::chrono::duration<double, std::milli>(m_Elapsed).count() < milliseconds);
    }

    class Item {
    public:
        explicit Item(RenderUiWorkBudget& budget, bool enabled = true)
            : m_Budget(enabled ? &budget : nullptr), m_Started(Clock::now()) {
            if (m_Budget) ++m_Budget->m_Items;
        }
        ~Item() {
            if (m_Budget) m_Budget->m_Elapsed += Clock::now() - m_Started;
        }
        Item(const Item&) = delete;
        Item& operator=(const Item&) = delete;
    private:
        using Clock = std::chrono::steady_clock;
        RenderUiWorkBudget* m_Budget;
        Clock::time_point m_Started;
    };

private:
    int m_Frame = -1;
    std::size_t m_Items = 0;
    std::chrono::steady_clock::duration m_Elapsed {};
};

} // namespace Stack::EditorRendering
