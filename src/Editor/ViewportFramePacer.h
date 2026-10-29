#pragma once
#include <algorithm>

namespace Stack::EditorRendering {
// One view's submission clock. Preserve phase across UI-frame quantization,
// but skip missed slots instead of accumulating a queue of catch-up renders.
class ViewportFramePacer {
public:
    bool Ready(double now, int targetFps) {
        const int fps = std::max(1, targetFps);
        if (fps != m_Fps || (m_LastSubmittedAt >= 0 && now < m_LastSubmittedAt)) {
            Reset();
            m_Fps = fps;
        }
        return m_NextAt < 0 || now + 1e-9 >= m_NextAt;
    }
    void Submitted(double now, int targetFps) {
        const int fps = std::max(1, targetFps);
        const double interval = 1.0 / fps;
        if (fps != m_Fps || m_NextAt < 0 || now < m_LastSubmittedAt ||
            now - m_NextAt >= interval) {
            // A new gesture after idle, a slow render, or a new rate starts a
            // fresh interval. Never repay old slots in a burst.
            m_NextAt = now + interval;
        } else {
            m_NextAt += interval;
        }
        m_Fps = fps;
        m_LastSubmittedAt = now;
    }
    void Reset() { m_NextAt = m_LastSubmittedAt = -1; m_Fps = 0; }
private:
    double m_NextAt = -1, m_LastSubmittedAt = -1;
    int m_Fps = 0;
};
}
