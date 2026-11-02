#pragma once
#include <imgui.h>
#include <algorithm>

namespace Stack::Editor {
inline constexpr float RawViewFitFraction = .80f;
// Presentation transform only. The canonical camera remains unchanged when
// comparison opens, closes, or the available workspace changes size.
struct ViewComparisonLayout {
    ImVec2 center;
    ImVec2 cameraCenter;
    float scale = 1.f, shift = 0.f, blend = 0.f, gap = 0.f;
    static ViewComparisonLayout Resolve(ImVec2 minimum, ImVec2 maximum, ImVec2 fittedSize, float progress) {
        return Resolve(minimum, maximum, minimum, maximum, fittedSize, progress);
    }
    static ViewComparisonLayout Resolve(ImVec2 minimum, ImVec2 maximum,
        ImVec2 visibleMinimum, ImVec2 visibleMaximum, ImVec2 fittedSize, float progress) {
        const float t = std::clamp(progress,0.f,1.f);
        const float blend = t*t*(3.f-2.f*t);
        const float gap = 1.f;
        const float half = std::max(1.f,(visibleMaximum.x-visibleMinimum.x-gap)*.5f);
        // The host supplies its ordinary fit rectangle. RAW and Graph use
        // different fit margins; either camera must fill each pane at fit zoom.
        const float splitScale = half / std::max(.0001f, fittedSize.x);
        const ImVec2 camera{(minimum.x+maximum.x)*.5f,(minimum.y+maximum.y)*.5f};
        const ImVec2 visible{(visibleMinimum.x+visibleMaximum.x)*.5f,(visibleMinimum.y+visibleMaximum.y)*.5f};
        return {{camera.x+(visible.x-camera.x)*blend,camera.y+(visible.y-camera.y)*blend},camera,
            1.f+(splitScale-1.f)*blend,(half+gap)*.5f*blend,blend,gap};
    }
    ImVec2 Present(ImVec2 point, bool after) const {
        return {center.x+(point.x-cameraCenter.x)*scale+(after ? shift : -shift),
            center.y+(point.y-cameraCenter.y)*scale};
    }
    ImVec2 CameraPoint(ImVec2 point, bool after) const {
        return {cameraCenter.x+(point.x-center.x-(after ? shift : -shift))/scale,
            cameraCenter.y+(point.y-center.y)/scale};
    }
};
}
