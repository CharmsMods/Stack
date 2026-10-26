#pragma once

#include "Editor/GraphRenderPolicy.h"

namespace Stack::EditorRendering {

struct GraphViewportAnalysisDemand {
    bool pending = false, requested = false;
    bool currentPresentation = false, settledPresentation = false;
    bool nativeReadyOrDeferred = false;
    bool workOutstanding = false, imageEditActive = false, quietElapsed = false;
};

inline bool ShouldSubmitGraphViewportAnalysis(const GraphViewportAnalysisDemand& demand) {
    return demand.pending && !demand.requested && demand.currentPresentation &&
        demand.settledPresentation && demand.nativeReadyOrDeferred &&
        !demand.workOutstanding && !demand.imageEditActive && demand.quietElapsed;
}

inline bool ShouldRequeueGraphViewportAnalysis(bool pending, bool requested,
    std::uint64_t trackedGeneration, bool workOutstanding) {
    return pending && requested && !trackedGeneration && !workOutstanding;
}

// Unlike useful intermediate image frames, optional readbacks must describe
// the exact current document. A later edit cannot adopt an older scope.
inline bool IsCurrentGraphViewportAnalysis(const GraphRendering::RequestTag& submitted,
    const GraphRendering::RequestTag& current) {
    return GraphRendering::SameContext(submitted, current) && submitted.revision == current.revision;
}

inline bool OwnsGraphViewportAnalysisCompletion(std::uint64_t generation,
    std::uint64_t trackedGeneration, std::uint64_t latestAuxiliaryGeneration) {
    return trackedGeneration && generation == trackedGeneration && generation == latestAuxiliaryGeneration;
}

} // namespace Stack::EditorRendering
