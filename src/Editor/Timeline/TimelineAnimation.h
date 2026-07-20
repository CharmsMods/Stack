#pragma once

#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include "ThirdParty/json.hpp"

#include <cstddef>
#include <string>
#include <vector>

class LayerBase;

namespace Stack::Timeline {

enum class AnimatableValueType {
    Float,
    Integer,
    Boolean,
    Enum
};

enum class TimelineInterpolation {
    Hold,
    Linear
};

struct AnimatableParameterTarget {
    int nodeId = -1;
    std::string parameterId;
};

struct AnimatableParameterDefinition {
    AnimatableParameterTarget target;
    std::string nodeLabel;
    std::string parameterLabel;
    std::string storageKey;
    AnimatableValueType valueType = AnimatableValueType::Float;
    float defaultValue = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float currentValue = 0.0f;
    bool hasCurrentValue = false;
};

struct TimelineKeyframe {
    int frame = 0;
    float value = 0.0f;
    TimelineInterpolation interpolation = TimelineInterpolation::Linear;
};

struct TimelineTrack {
    AnimatableParameterTarget target;
    std::vector<TimelineKeyframe> keyframes;
};

struct TimelineAnimationState {
    std::vector<TimelineTrack> tracks;
};

struct FrameParameterValue {
    AnimatableParameterTarget target;
    float value = 0.0f;
};

struct FrameEvaluationContext {
    int frame = 0;
    std::vector<FrameParameterValue> values;

    bool empty() const { return values.empty(); }
};

bool SameTarget(const AnimatableParameterTarget& a, const AnimatableParameterTarget& b);
bool IsValidTarget(const AnimatableParameterTarget& target);
std::string BuildTargetKey(const AnimatableParameterTarget& target);

std::vector<AnimatableParameterDefinition> CollectAnimatableParametersForNode(
    const EditorNodeGraph::Node& node,
    const LayerBase* layer = nullptr);

// Returns the same declarative parameter catalog used by the timeline without
// requiring a live node instance. The unified node-definition registry uses
// this so layer UI, serialization identities, and animation policy cannot
// silently drift into separate parameter lists.
std::vector<AnimatableParameterDefinition> DescribeAnimatableParametersForLayer(
    LayerType layerType);

bool TryReadAnimatableParameterValue(
    const EditorNodeGraph::Node& node,
    const LayerBase* layer,
    const std::string& parameterId,
    float& outValue);

TimelineTrack* FindTimelineTrack(TimelineAnimationState& state, const AnimatableParameterTarget& target);
const TimelineTrack* FindTimelineTrack(const TimelineAnimationState& state, const AnimatableParameterTarget& target);

TimelineTrack& EnsureTimelineTrack(TimelineAnimationState& state, const AnimatableParameterTarget& target);
void SetOrReplaceKeyframe(
    TimelineAnimationState& state,
    const AnimatableParameterTarget& target,
    int frame,
    float value,
    TimelineInterpolation interpolation = TimelineInterpolation::Linear);

const TimelineKeyframe* FindKeyframeAtFrame(const TimelineTrack& track, int frame);
bool UpdateExistingKeyframeValue(
    TimelineAnimationState& state,
    const AnimatableParameterTarget& target,
    int frame,
    float value);
bool TrackAffectsCompletedChain(const TimelineTrack& track, const EditorNodeGraph::CompletedChainInfo& chain);
bool TargetAffectsCompletedChain(const AnimatableParameterTarget& target, const EditorNodeGraph::CompletedChainInfo& chain);
int FindLastTimelineKeyframeFrame(const TimelineAnimationState& state);
std::size_t CountDistinctTimelineKeyframeFrames(const TimelineAnimationState& state);

bool EvaluateTimelineTrackAtFrame(const TimelineTrack& track, int frame, float& outValue);
FrameEvaluationContext BuildFrameEvaluationContext(const TimelineAnimationState& state, int frame);
bool TryGetFrameParameterValue(
    const FrameEvaluationContext& context,
    const AnimatableParameterTarget& target,
    float& outValue);
bool RemoveFrameParameterValue(
    FrameEvaluationContext& context,
    const AnimatableParameterTarget& target);
bool ApplyFrameEvaluationContextToLayerJson(
    const FrameEvaluationContext& context,
    int nodeId,
    LayerType layerType,
    nlohmann::json& layerJson);

} // namespace Stack::Timeline
