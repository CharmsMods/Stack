#pragma once

#include "NodeMath/NodeDefinition.h"
#include "NodeMath/RegionPlanning.h"

#include <string>
#include <vector>

namespace Stack::NodeMath {

enum class SpecializedStageKind {
    None,
    RawDecode,
    RawDevelopment,
    NeuralOrExternal,
    MultiFrameMerge,
    FrequencyTransform,
    FrequencyInverseTransform,
    FrequencyOperation,
    ScopeAnalysis,
    PreviewReadback,
    ExportReadback
};

enum class RegionRequirement {
    MappedRoi,
    FullFrame,
    GlobalPopulation
};

enum class RenderScalePolicy {
    ExactRequested,
    ProxyAllowed,
    FullQualityOnly
};

enum class CancellationPolicy {
    BetweenTiles,
    BetweenStages,
    CooperativeExternal
};

struct SpecializedStagePlan {
    bool valid = false;
    SpecializedStageKind kind = SpecializedStageKind::None;
    CapabilityClass capability = CapabilityClass::SpecializedExternal;
    LogicalValueType inputType = LogicalValueType::Invalid;
    LogicalValueType outputType = LogicalValueType::Invalid;
    RegionRequirement regionRequirement = RegionRequirement::FullFrame;
    RenderScalePolicy scalePolicy = RenderScalePolicy::ExactRequested;
    CancellationPolicy cancellation = CancellationPolicy::BetweenStages;
    bool opaque = true;
    std::string reason;
    std::vector<ContractIssue> issues;
};

struct ConsumerBoundaryPlan {
    bool valid = false;
    SpecializedStageKind kind = SpecializedStageKind::PreviewReadback;
    SpatialDescriptor inputSpatial;
    SpatialDescriptor outputSpatial;
    RegionRequirement regionRequirement = RegionRequirement::FullFrame;
    RenderScalePolicy scalePolicy = RenderScalePolicy::ExactRequested;
    CancellationPolicy cancellation = CancellationPolicy::BetweenStages;
    bool changesGraphResult = false;
    std::string reason;
    std::vector<ContractIssue> issues;
};

SpecializedStagePlan PlanSpecializedStage(SpecializedStageKind kind);
ConsumerBoundaryPlan PlanConsumerBoundary(
    SpecializedStageKind kind,
    const SpatialDescriptor& inputSpatial,
    int maximumDimension = 0);
const char* SpecializedStageKindName(SpecializedStageKind kind);

} // namespace Stack::NodeMath
