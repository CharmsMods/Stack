#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace Stack::Platform {

// One state per ImGui GLFW backend instance. The upstream backend zeroes its
// storage, so this must remain trivially copyable with zero as the initial state.
struct GlfwMonitorRefreshState {
    std::uint64_t revision = 0;
    double checkedAt = 0;
    bool initialized = false;
};
static_assert(std::is_trivially_copyable_v<GlfwMonitorRefreshState>);

enum class GlfwMonitorRefresh { Cached, WorkArea, Complete };
struct GlfwMonitorRefreshCounters {
    std::uint64_t complete = 0, workArea = 0, cached = 0;
    double completeMs = 0, completeMaxMs = 0, workAreaMs = 0, workAreaMaxMs = 0;
    std::uint64_t passthroughChanges = 0, passthroughReused = 0;
};
inline std::atomic<std::uint64_t> glfwMonitorRevision {1};
inline GlfwMonitorRefreshCounters glfwMonitorRefreshCounters;

inline bool NeedsGlfwMousePassthroughUpdate(int current, bool requested) {
    if ((current != 0) != requested) {
        ++glfwMonitorRefreshCounters.passthroughChanges;
        return true;
    }
    ++glfwMonitorRefreshCounters.passthroughReused;
    return false;
}

inline void ObserveGlfwMonitorRefresh(GlfwMonitorRefresh refresh, double milliseconds) {
    auto& counters = glfwMonitorRefreshCounters;
    if (refresh == GlfwMonitorRefresh::Complete) {
        counters.completeMs += milliseconds;
        if (milliseconds > counters.completeMaxMs) counters.completeMaxMs = milliseconds;
    } else if (refresh == GlfwMonitorRefresh::WorkArea) {
        counters.workAreaMs += milliseconds;
        if (milliseconds > counters.workAreaMaxMs) counters.workAreaMaxMs = milliseconds;
    }
}

// Native display/settings/DPI messages and GLFW topology callbacks invalidate
// every backend context. Neither the policy nor its signals retain monitor handles.
inline void InvalidateGlfwMonitors() {
    glfwMonitorRevision.fetch_add(1, std::memory_order_relaxed);
}

inline GlfwMonitorRefresh BeginGlfwMonitorRefresh(GlfwMonitorRefreshState& state, double now) {
#if defined(_WIN32) && STACK_CACHE_GLFW_MONITORS
    const auto revision = glfwMonitorRevision.load(std::memory_order_relaxed);
    if (!state.initialized || state.revision != revision || now < state.checkedAt) {
        state = {revision, now, true};
        ++glfwMonitorRefreshCounters.complete;
        return GlfwMonitorRefresh::Complete;
    }
    if (now - state.checkedAt >= 1.0) {
        // Work-area/scale reads are cheap; avoid mode/position enumeration in
        // this fallback. Native notifications refresh those immediately.
        state.checkedAt = now;
        ++glfwMonitorRefreshCounters.workArea;
        return GlfwMonitorRefresh::WorkArea;
    }
    ++glfwMonitorRefreshCounters.cached;
    return GlfwMonitorRefresh::Cached;
#else
    (void)state;
    (void)now;
    ++glfwMonitorRefreshCounters.complete;
    return GlfwMonitorRefresh::Complete;
#endif
}

} // namespace Stack::Platform
