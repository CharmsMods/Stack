#pragma once
#include "Raw/RawViewportCalibration.h"
#include "Raw/RawViewportPreferences.h"
#include "Raw/RawViewportRegion.h"
#include <functional>

namespace Raw {
enum class ViewportLimit { None, Learning, FrameBudget, DetailFloor, FixedWork, Memory };
struct ViewportDecision {
    int edge = 0;
    ViewportRegion region;
    ViewportStage firstStage = ViewportStage::RawBase;
    double predictedMs = 0;
    double measuredMs = 0;
    bool measured = false;
    bool provisional = true;
    ViewportLimit limit = ViewportLimit::Learning;
};
struct ViewportDecisionInput {
    int nativeWidth = 0, nativeHeight = 0;
    int physicalWidth = 0, physicalHeight = 0;
    int maximumEdge = 0;
    int fps = 30;
    int evaluationEdge = 0;
    ViewportPreferences preferences;
    ViewportRegion visible;
    bool regionalAllowed = false;
    bool coldStart = false;
    bool completeGraph = false;
    ViewportStage regionalFirst = ViewportStage::PostOutputCrop;
    ViewportStage changingStage = ViewportStage::RawBase;
    ViewportTimingBank::Keys keys {};
    int cachedEdge = 0, cachedRequestEdge = 0;
    std::array<std::size_t,kViewportStageCount> cached {}, nativeCached {};
};
inline std::size_t ViewportFeedbackKey(std::size_t workload, ViewportStage first) {
    Stack::Renderer::RawDevelopmentCache::HashTypedValue(workload,static_cast<int>(first));
    return workload;
}

// Display detail is measured before the native cap. At 400% zoom, a 50%
// display floor still requires native source pixels, not half-native pixels.
inline int ViewportDisplayDetailEdge(const ViewportDecisionInput& in, double detail = 1.0) {
    const int native = std::max(in.nativeWidth,in.nativeHeight);
    if (native <= 0 || in.physicalWidth <= 0 || in.physicalHeight <= 0) return 0;
    const int w = in.visible.Valid() ? in.visible.width : in.nativeWidth;
    const int h = in.visible.Valid() ? in.visible.height : in.nativeHeight;
    const double scale = std::min(1.0,detail*std::max(double(in.physicalWidth)/std::max(1,w),
        double(in.physicalHeight)/std::max(1,h)));
    return std::max(1,int(std::ceil(native*scale)));
}

class ViewportController {
public:
    ViewportDecision Choose(const ViewportDecisionInput& input, const ViewportTimingBank& bank,
        const ViewportModules::Recipe& recipe) const;
    void Observe(std::size_t context, double predictedMs, double completedMs);
    void SetContext(std::size_t context) { m_Context = context; }
    void Reset() { m_Feedback.clear(); m_Context = m_SelectedContext = 0; }
    double LastMeasuredMs() const;
    double PredictionError() const;
private:
    struct Feedback {
        ViewportEditTimingWindow window;
        double correction = 1, lastMs = 0, error = 0;
        unsigned count = 0;
    };
    std::map<std::size_t,Feedback> m_Feedback;
    std::size_t m_Context = 0;
    mutable std::size_t m_SelectedContext = 0;
};
const char* ViewportLimitLabel(ViewportLimit limit);
}
