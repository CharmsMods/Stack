#include "TimelinePlayback.h"

#include <algorithm>
#include <cmath>

namespace Stack::Timeline {

int ClampTimelineFrame(int frame, int durationFrames) {
    const int duration = std::max(1, durationFrames);
    return std::clamp(frame, 0, duration - 1);
}

int ResolveTimelineStepFrame(
    int currentFrame,
    int frameDelta,
    int durationFrames,
    int wrapEndFrame) {
    const int duration = std::max(1, durationFrames);
    const int startFrame = 0;
    const int endFrame = std::clamp(wrapEndFrame, startFrame, duration - 1);
    const int rangeLength = endFrame - startFrame + 1;
    if (rangeLength <= 1 || frameDelta == 0) {
        return std::clamp(currentFrame, startFrame, endFrame);
    }

    if (currentFrame < startFrame || currentFrame > endFrame) {
        return frameDelta < 0 ? endFrame : startFrame;
    }

    const int targetFrame = currentFrame + frameDelta;
    if (targetFrame < startFrame) {
        const int distanceBeforeStart = startFrame - targetFrame;
        return endFrame - ((distanceBeforeStart - 1) % rangeLength);
    }
    if (targetFrame > endFrame) {
        const int distanceAfterEnd = targetFrame - endFrame;
        return startFrame + ((distanceAfterEnd - 1) % rangeLength);
    }
    return targetFrame;
}

TimelinePlaybackAdvanceResult AdvanceTimelinePlayback(
    int currentFrame,
    int durationFrames,
    int framesPerSecond,
    double deltaSeconds,
    bool loop,
    double& frameAccumulator) {
    const int duration = std::max(1, durationFrames);
    const int fps = std::clamp(framesPerSecond, 1, 240);

    TimelinePlaybackAdvanceResult result;
    result.frame = ClampTimelineFrame(currentFrame, duration);
    if (duration <= 1) {
        frameAccumulator = 0.0;
        result.shouldStop = true;
        return result;
    }

    if (deltaSeconds <= 0.0) {
        return result;
    }

    frameAccumulator = std::max(0.0, frameAccumulator + deltaSeconds * static_cast<double>(fps));
    const int framesToAdvance = static_cast<int>(std::floor(frameAccumulator));
    if (framesToAdvance <= 0) {
        return result;
    }

    frameAccumulator -= static_cast<double>(framesToAdvance);
    const int targetFrame = result.frame + framesToAdvance;
    if (targetFrame >= duration) {
        if (loop) {
            result.frame = targetFrame % duration;
            result.wrapped = true;
        } else {
            result.frame = duration - 1;
            result.shouldStop = true;
            frameAccumulator = 0.0;
        }
    } else {
        result.frame = targetFrame;
    }

    result.frameChanged = result.frame != currentFrame;
    return result;
}

} // namespace Stack::Timeline
