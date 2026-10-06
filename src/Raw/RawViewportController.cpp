#include "Raw/RawViewportController.h"

namespace Raw {
const char* ViewportLimitLabel(ViewportLimit limit) {
    switch (limit) {
    case ViewportLimit::None: return "Display detail reached";
    case ViewportLimit::Learning: return "Provisional estimate";
    case ViewportLimit::FrameBudget: return "Resolution limited by target FPS";
    case ViewportLimit::DetailFloor: return "Detail floor limits achievable FPS";
    case ViewportLimit::FixedWork: return "Preparation or fixed work limits achievable FPS";
    case ViewportLimit::Memory: return "Memory or texture limit restricts detail";
    }
    return "";
}
ViewportDecision ViewportController::Choose(const ViewportDecisionInput& in, const ViewportTimingBank& bank,
    const ViewportModules::Recipe& recipe) const {
    ViewportDecision decision;
    const int native = std::max(in.nativeWidth,in.nativeHeight);
    if (native <= 0 || in.maximumEdge <= 0) return decision;
    const int display = std::max(1,ViewportDisplayDetailEdge(in));
    const int maximum = std::max(1,std::min({native,in.maximumEdge,display}));
    const int floor = in.preferences.mode == ViewportInteractionMode::PreserveDetail
        ? ViewportDisplayDetailEdge(in,in.preferences.minimumDetailPercent/100.0) : std::min(128,maximum);
    const int minimum = std::min(maximum,std::max(1,floor));
    const bool regional = in.regionalAllowed && in.visible.Partial();
    decision.region = regional ? in.visible : ViewportRegion{};
    const double area = regional ? double(in.visible.width)*in.visible.height /
        (double(in.visible.fullWidth)*in.visible.fullHeight) : 1.0;
    const auto regionalFirst = regional ? std::size_t(in.regionalFirst) : kViewportStageCount;
    const auto firstFor = [&](int edge) {
        // A reordered graph has no fixed recipe-stage cache sequence. Learn
        // its complete dispatched work instead of claiming recipe reuse.
        if (in.completeGraph) return ViewportStage::RawBase;
        const bool nativeSize = edge == native;
        if (!nativeSize && edge != in.cachedEdge) return ViewportStage::RawBase;
        const auto& cache = nativeSize ? in.nativeCached : in.cached;
        const auto expected = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,
            nativeSize ? 0 : in.cachedRequestEdge);
        std::size_t first = 0;
        for (std::size_t i=0;i<std::size_t(in.changingStage);++i)
            if (i<regionalFirst && cache[i] && cache[i]==expected.For(ViewportStage(i))) first=i+1;
        return ViewportStage(first);
    };
    const auto costFor = [&](int edge, bool& known) {
        double correction=1;
        const auto feedback=m_Feedback.find(ViewportFeedbackKey(m_Context,firstFor(edge)));
        if (feedback!=m_Feedback.end()) correction=feedback->second.correction;
        return bank.Cost(in.keys,edge,firstFor(edge),in.coldStart && firstFor(edge)==ViewportStage::RawBase,&known,area,regionalFirst,
            std::size_t(in.changingStage),in.completeGraph)*correction;
    };
    // Evaluate a bounded ladder plus exact reusable sizes. Cache cost is
    // discontinuous, so binary search over the combined plans is invalid.
    std::vector<int> candidates {minimum,maximum};
    for (int edge=std::max(64,((minimum+63)/64)*64);edge<maximum;edge+=64) candidates.push_back(edge);
    for (int edge : {in.cachedEdge,native}) if (edge>=minimum && edge<=maximum) candidates.push_back(edge);
    double fastest = std::numeric_limits<double>::infinity();
    bool allPlansKnown=true;
    for (int edge : candidates) { bool known=false; const auto ms=costFor(edge,known); allPlansKnown&=known; if(known) fastest=std::min(fastest,ms); }
    const double target = 1000.0/std::max(1,in.fps);
    const double budget = std::max(target,std::isfinite(fastest) ? fastest*1.05 : target);
    for (int edge : candidates) {
        bool known=false; const auto ms=costFor(edge,known);
        if (known && ms<=budget && edge>=decision.edge) {
            decision.edge=edge; decision.predictedMs=ms; decision.measured=true;
            decision.firstStage=firstFor(edge);
        }
    }
    if (!decision.edge) {
        decision.edge = std::clamp(std::min(display,1024),minimum,maximum);
        decision.predictedMs = costFor(decision.edge,decision.measured);
        decision.firstStage=firstFor(decision.edge);
    }
    decision.provisional=!allPlansKnown || !bank.Covers(in.keys,decision.edge,decision.firstStage,area,regionalFirst);
    decision.limit = !decision.measured ? ViewportLimit::Learning :
        decision.predictedMs>target*1.05 ? (floor>128 ? ViewportLimit::DetailFloor : ViewportLimit::FixedWork) :
        decision.edge<maximum ? ViewportLimit::FrameBudget : ViewportLimit::None;
    if (decision.limit==ViewportLimit::FixedWork && !allPlansKnown) decision.limit=ViewportLimit::Learning;
    if (in.maximumEdge<display && (decision.edge>=in.maximumEdge || in.maximumEdge<floor)) decision.limit=ViewportLimit::Memory;
    if (in.evaluationEdge>0) {
        // Report the raster being dispatched during stabilization, while
        // retaining the limitation established by comparing all plans.
        decision.edge=std::clamp(in.evaluationEdge,minimum,maximum);
        decision.firstStage=firstFor(decision.edge);
        decision.predictedMs=costFor(decision.edge,decision.measured);
        decision.provisional|=!bank.Covers(in.keys,decision.edge,decision.firstStage,area,regionalFirst);
        if (decision.limit==ViewportLimit::None && decision.edge<maximum) decision.limit=ViewportLimit::FrameBudget;
    }
    if (!decision.measured) decision.predictedMs=0; // A partial stage sum is not a total prediction.
    m_SelectedContext=ViewportFeedbackKey(m_Context,decision.firstStage);
    if (const auto feedback=m_Feedback.find(m_SelectedContext);feedback!=m_Feedback.end()) decision.measuredMs=feedback->second.lastMs;
    return decision;
}
void ViewportController::Observe(std::size_t context,double predictedMs,double completedMs) {
    if (!context || !std::isfinite(completedMs) || completedMs<=0) return;
    if (m_Feedback.size()>=64 && !m_Feedback.count(context)) m_Feedback.erase(m_Feedback.begin());
    auto& feedback=m_Feedback[context];
    feedback.lastMs=completedMs;
    if (!std::isfinite(predictedMs) || predictedMs<=0) return;
    const double ratio=feedback.window.Observe(completedMs/predictedMs);
    if (ratio<=0) return;
    feedback.error=0.75*feedback.error+0.25*std::abs(completedMs-predictedMs)/predictedMs;
    const double wanted=std::clamp(feedback.correction*ratio,0.5,4.0);
    feedback.correction=AverageViewportCost(feedback.correction,wanted,feedback.count);
}
double ViewportController::LastMeasuredMs() const {
    const auto found=m_Feedback.find(m_SelectedContext); return found==m_Feedback.end() ? 0 : found->second.lastMs;
}
double ViewportController::PredictionError() const {
    const auto found=m_Feedback.find(m_SelectedContext); return found==m_Feedback.end() ? 0 : found->second.error;
}
}
