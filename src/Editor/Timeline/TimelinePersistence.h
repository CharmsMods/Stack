#pragma once

#include "Editor/Timeline/TimelineAnimation.h"

#include <functional>

namespace Stack::Timeline {

struct TimelineDocumentState {
    int currentFrame = 0;
    int durationFrames = 120;
    int framesPerSecond = 30;
    TimelineAnimationState animation;
};

using TimelineTargetValidator = std::function<bool(const AnimatableParameterTarget&)>;

nlohmann::json SerializeTimelineDocument(const TimelineDocumentState& document);

TimelineDocumentState DeserializeTimelineDocument(
    const nlohmann::json& value,
    const TimelineTargetValidator& targetValidator = {});

} // namespace Stack::Timeline
