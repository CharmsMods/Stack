#include "TimelineFrameProducer.h"

#include "Editor/Timeline/TimelinePlayback.h"

#include <algorithm>

namespace Stack::Timeline {

TimelineFrameRequest NormalizeTimelineFrameRequest(
    int frame,
    int durationFrames,
    int framesPerSecond) {
    TimelineFrameRequest request;
    request.durationFrames = std::clamp(durationFrames, 1, 100000);
    request.framesPerSecond = std::clamp(framesPerSecond, 1, 240);
    request.frame = ClampTimelineFrame(frame, request.durationFrames);
    return request;
}

TimelineFrameEvaluation BuildTimelineFrameEvaluation(
    const TimelineAnimationState& animation,
    const TimelineFrameRequest& request) {
    TimelineFrameEvaluation evaluation;
    evaluation.request = NormalizeTimelineFrameRequest(
        request.frame,
        request.durationFrames,
        request.framesPerSecond);
    evaluation.frameContext = BuildFrameEvaluationContext(animation, evaluation.request.frame);
    return evaluation;
}

} // namespace Stack::Timeline
