#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace Stack {

// Bridges titlebar pointer events to actions consumed on the UI thread.
// Registrations and pointer coordinates must use the same coordinate space.
class HeaderActionRegistry {
public:
    struct Bounds {
        float left;
        float top;
        float right;
        float bottom;
    };
    using Callback = std::function<void()>;

    // Rebuilding hit targets must not discard a click queued between frames.
    void BeginFrame() { m_Actions.clear(); }

    bool Add(Bounds bounds, Bounds clip, Callback action, bool enabled = true) {
        if (!enabled || !action || !IsValid(bounds) || !IsValid(clip)) return false;
        bounds.left = (std::max)(bounds.left, clip.left);
        bounds.top = (std::max)(bounds.top, clip.top);
        bounds.right = (std::min)(bounds.right, clip.right);
        bounds.bottom = (std::min)(bounds.bottom, clip.bottom);
        if (!IsValid(bounds)) return false;
        m_Actions.push_back({bounds, std::move(action)});
        return true;
    }

    bool QueueAt(float x, float y) {
        // The last registered control is on top when controls overlap.
        for (auto it = m_Actions.rbegin(); it != m_Actions.rend(); ++it) {
            const auto& bounds = it->bounds;
            if (x >= bounds.left && x < bounds.right &&
                y >= bounds.top && y < bounds.bottom) {
                // Copy the action so replacement registrations cannot retarget it.
                m_Pending = it->action;
                return true;
            }
        }
        return false;
    }

    Callback Consume() { return std::exchange(m_Pending, Callback{}); }

private:
    struct Action {
        Bounds bounds;
        Callback action;
    };

    static bool IsValid(const Bounds& bounds) {
        return std::isfinite(bounds.left) && std::isfinite(bounds.top) &&
            std::isfinite(bounds.right) && std::isfinite(bounds.bottom) &&
            bounds.left < bounds.right && bounds.top < bounds.bottom;
    }

    std::vector<Action> m_Actions;
    Callback m_Pending;
};

} // namespace Stack
