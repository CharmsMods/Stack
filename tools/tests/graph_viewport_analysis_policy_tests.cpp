#include "Editor/GraphViewportAnalysisPolicy.h"
#include "Editor/RawRenderPurpose.h"

#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    using namespace Stack::EditorRendering;
    GraphViewportAnalysisDemand ready{true, false, true, true, true, false, false, true};
    Require(ShouldSubmitGraphViewportAnalysis(ready), "A paused edit/tool switch must drain pending analysis");
    auto demand = ready;
    demand.requested = true;
    for (int frame = 0; frame != 10; ++frame)
        Require(!ShouldSubmitGraphViewportAnalysis(demand), "Repeated frames must not resubmit an in-flight scope");
    demand = ready; demand.pending = false;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "Completed analysis must remain complete");
    demand = ready; demand.workOutstanding = true;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "Busy/dirty/deferred/native work must retain priority");
    demand = ready; demand.imageEditActive = true;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "A held image edit must not run optional readbacks");
    demand = ready; demand.quietElapsed = false;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "A new edit/tool transition must retain its quiet period");
    demand = ready; demand.currentPresentation = false;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "A stale or another owner's frame is not current presentation");
    demand = ready; demand.settledPresentation = false;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "Interactive or denoise-pending pixels must settle first");
    demand = ready; demand.nativeReadyOrDeferred = false;
    Require(!ShouldSubmitGraphViewportAnalysis(demand), "Available native detail must finish before analysis");
    demand.nativeReadyOrDeferred = true;
    Require(ShouldSubmitGraphViewportAnalysis(demand), "A budget-deferred native request must not starve scopes");
    Require(ShouldRequeueGraphViewportAnalysis(true, true, 0, false),
        "A queued but unsubmitted legacy request must drain through the graph scheduler");
    Require(!ShouldRequeueGraphViewportAnalysis(false, true, 0, false),
        "Completed work must not be revived while normalizing legacy request flags");
    Require(!ShouldRequeueGraphViewportAnalysis(true, true, 20, false) &&
        !ShouldRequeueGraphViewportAnalysis(true, true, 0, true),
        "Tracked or busy/deferred requests must not be requeued");

    Stack::GraphRendering::RequestTag tag{true, 4, 0, false, 3, 123, 10};
    Require(IsCurrentGraphViewportAnalysis(tag, tag), "Current readbacks must be accepted");
    auto changed = tag; ++changed.revision;
    Require(Stack::GraphRendering::MayAdopt(tag, changed, 20, 19), "Image intermediates should retain their existing policy");
    Require(!IsCurrentGraphViewportAnalysis(tag, changed), "Readbacks from before a newer edit must be rejected");
    changed = tag; ++changed.sourceIdentity;
    Require(!IsCurrentGraphViewportAnalysis(tag, changed), "A replaced source must reject old readbacks");
    changed = tag; ++changed.outputNodeId;
    Require(!IsCurrentGraphViewportAnalysis(tag, changed), "A different output must reject old readbacks");
    Require(OwnsGraphViewportAnalysisCompletion(20, 20, 20), "An owned cancellation must release its request");
    Require(!OwnsGraphViewportAnalysisCompletion(20, 21, 21), "An old cancellation must not release a newer scope");
    Require(!OwnsGraphViewportAnalysisCompletion(20, 20, 21), "An old scope must not release another auxiliary consumer");
    Require(!OwnsGraphViewportAnalysisCompletion(0, 0, 0), "An untracked result must not mutate scope state");
    Require(!RawRenderPurposeMayPublishPresentation(RawRenderPurpose::AnalysisScopes),
        "The independent scope packet must never replace the accepted native image");
    std::cout << "Graph viewport analysis scheduling, current-owner and completion policy checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
