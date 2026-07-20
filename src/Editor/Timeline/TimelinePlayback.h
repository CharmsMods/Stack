#pragma once

namespace Stack::Timeline {

struct TimelinePlaybackAdvanceResult {
    int frame = 0;
    bool frameChanged = false;
    bool wrapped = false;
    bool shouldStop = false;
};

int ClampTimelineFrame(int frame, int durationFrames);
int ResolveTimelineStepFrame(
    int currentFrame,
    int frameDelta,
    int durationFrames,
    int wrapEndFrame);

TimelinePlaybackAdvanceResult AdvanceTimelinePlayback(
    int currentFrame,
    int durationFrames,
    int framesPerSecond,
    double deltaSeconds,
    bool loop,
    double& frameAccumulator);

} // namespace Stack::Timeline
