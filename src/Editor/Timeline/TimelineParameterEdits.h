#pragma once
#include "Editor/Timeline/TimelineAnimation.h"

namespace Stack::Timeline {
struct ParameterEditAnimation {
    TimelineAnimationState animation;
    std::vector<AnimatableParameterTarget> livePreviewTargets;
};
ParameterEditAnimation PrepareParameterEditAnimation(const TimelineAnimationState& animation,
    const EditorNodeGraph::Node& node, const std::string& graphId, int frame,
    const nlohmann::json& before, const nlohmann::json& after);
}
