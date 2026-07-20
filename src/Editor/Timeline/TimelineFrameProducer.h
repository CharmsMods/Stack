#pragma once

#include "Editor/Timeline/TimelineAnimation.h"

namespace Stack::Timeline {

struct TimelineFrameRequest {
    int frame = 0;
    int durationFrames = 120;
    int framesPerSecond = 30;
};

struct TimelineFrameEvaluation {
    TimelineFrameRequest request;
    FrameEvaluationContext frameContext;

    bool HasAnimatedValues() const { return !frameContext.empty(); }
};

TimelineFrameRequest NormalizeTimelineFrameRequest(
    int frame,
    int durationFrames,
    int framesPerSecond);

TimelineFrameEvaluation BuildTimelineFrameEvaluation(
    const TimelineAnimationState& animation,
    const TimelineFrameRequest& request);

} // namespace Stack::Timeline
