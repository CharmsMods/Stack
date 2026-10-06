#pragma once
#include "Raw/RawViewportRegion.h"
#include "Raw/RawViewportSettings.h"
#include "Raw/RawViewportDetail.h"
#include <string>

namespace Raw {
struct ViewportPresentation {
    std::string source;
    std::uint64_t gesture = 0, view = 0;
    ViewportRegion region;
    ViewportStage stage = ViewportStage::RawBase;
    bool interactive = false, diagnostic = false, encodedSrgb = false;
    int width = 0, height = 0;
};
struct ViewportFadeMapping { double x = 0, y = 0, width = 1, height = 1; bool valid = true; };
inline ViewportFadeMapping MapViewportFade(const ViewportRegion& previous, const ViewportRegion& next) {
    const auto bounds = [](const ViewportRegion& r) {
        return r.Valid() ? ViewportFadeMapping{double(r.x)/r.fullWidth,double(r.y)/r.fullHeight,
            double(r.width)/r.fullWidth,double(r.height)/r.fullHeight,true} : ViewportFadeMapping{};
    };
    const auto a = bounds(previous), b = bounds(next);
    const double epsilonX = 2.0/std::max(1,previous.fullWidth);
    const double epsilonY = 2.0/std::max(1,previous.fullHeight);
    if (previous.Partial() && (b.x < a.x-epsilonX || b.y < a.y-epsilonY ||
        b.x+b.width > a.x+a.width+epsilonX || b.y+b.height > a.y+a.height+epsilonY))
        return {0,0,1,1,false};
    const double left = std::clamp((b.x-a.x)/a.width,0.0,1.0);
    const double bottom = std::clamp(1.0-(b.y+b.height-a.y)/a.height,0.0,1.0);
    return {left,bottom,std::min(b.width/a.width,1.0-left),std::min(b.height/a.height,1.0-bottom),true};
}
inline bool ViewportFadeRegionsMatch(const ViewportRegion& a, const ViewportRegion& b) {
    if (!a.Partial() || !b.Partial()) return !a.Partial() && !b.Partial();
    // Proxy and native rasters describe the same visible area in different
    // pixel grids. Allow the rounding at their boundaries, not a changed view.
    const auto matches = [](int originA, int lengthA, int fullA, int originB, int lengthB, int fullB) {
        const double tolerance = 1.0 / fullA + 1.0 / fullB;
        return std::abs(double(originA) / fullA - double(originB) / fullB) <= tolerance &&
            std::abs(double(originA + lengthA) / fullA - double(originB + lengthB) / fullB) <= tolerance;
    };
    return matches(a.x,a.width,a.fullWidth,b.x,b.width,b.fullWidth) &&
        matches(a.y,a.height,a.fullHeight,b.y,b.height,b.fullHeight);
}
inline double ViewportFadeDuration(const ViewportPresentation& oldFrame,
    const ViewportPresentation& next, double gapMs, double workMs, int belowFps,
    double recentCadenceMs = 0, double displayFrameMs = 1000.0/60.0) {
    if (next.diagnostic || oldFrame.diagnostic || next.source.empty() ||
        oldFrame.source != next.source || oldFrame.view != next.view || !MapViewportFade(oldFrame.region,next.region).valid ||
        oldFrame.encodedSrgb != next.encodedSrgb ||
        oldFrame.stage == ViewportStage::PostOutputCrop || next.stage == ViewportStage::PostOutputCrop ||
        !std::isfinite(gapMs) || !std::isfinite(workMs) || gapMs <= 0 || workMs <= 0)
        return 0;
    // Idle time before an edit must not make a fast render look slow.
    const double intervalMs = std::min(gapMs, std::max(workMs,recentCadenceMs));
    const double oldDensity = ViewportDetailDensity(oldFrame.region, oldFrame.width, oldFrame.height);
    const double nextDensity = ViewportDetailDensity(next.region, next.width, next.height);
    if (!next.interactive && oldDensity > 0 && nextDensity > oldDensity * 1.01) {
        // A cached native refinement can finish too fast to meet the cadence
        // threshold, but its increase in detail still needs a visible handoff.
        return std::clamp(intervalMs * 0.00065,
            std::clamp(displayFrameMs * 0.002,0.020,0.100),0.200);
    }
    if (intervalMs <= 1000.0 / ClampViewportFadeBelowFps(belowFps)) return 0;
    return std::min(intervalMs * 0.00065, 0.20);
}
}
