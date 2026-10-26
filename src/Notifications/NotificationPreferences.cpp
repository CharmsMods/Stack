#include "Notifications/NotificationPresenter.h"
#include <imgui_internal.h>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace Stack::Notifications {
bool SystemReducedMotion() {
#if defined(_WIN32)
    // All presentation consumers share one preference snapshot in a UI frame.
    // Outside a frame, query normally so non-UI callers never retain stale state.
    struct FramePreference {
        ImGuiContext* context = nullptr;
        int frame = -1;
        double time = -1;
        bool reduced = false;
    };
    static thread_local FramePreference cached;
    const auto* context = ImGui::GetCurrentContext();
    const bool inFrame = context && context->WithinFrameScope;
    if (inFrame && cached.context == context && cached.frame == context->FrameCount &&
        cached.time == context->Time) return cached.reduced;
    BOOL animate = TRUE;
    const bool reduced = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && !animate;
    if (inFrame) cached = {ImGui::GetCurrentContext(), context->FrameCount, context->Time, reduced};
    return reduced;
#else
    return false;
#endif
}
}
