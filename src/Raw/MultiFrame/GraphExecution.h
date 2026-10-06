#pragma once

#include "Persistence/RawProjectModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

enum class GraphExecutionAdapter : std::uint8_t {
    CaptureSource = 0,
    CaptureSubset,
    SharedBurstV1,
    HdrV4,
    PublishOutput
};

const char* GraphExecutionAdapterName(GraphExecutionAdapter adapter);

struct GraphExecutionInput {
    std::string linkId;
    std::string producerNodeId;
    Stack::Project::MultiFrameGraphResourceType resourceType =
        Stack::Project::MultiFrameGraphResourceType::RawMeasurement;
    std::uint32_t variadicOrder = 0;
    std::vector<std::string> originalFrameIds;
};

struct GraphExecutionStep {
    std::string nodeId;
    Stack::Project::MultiFrameGraphNodeKind nodeKind =
        Stack::Project::MultiFrameGraphNodeKind::CaptureSet;
    GraphExecutionAdapter adapter = GraphExecutionAdapter::CaptureSource;
    std::vector<GraphExecutionInput> inputs;
    std::vector<std::string> originalFrameIds;
    std::size_t inputMeasurementCount = 0u;
    std::string contentIdentitySha256;
    bool requiresCovarianceAwareFusion = false;
    std::vector<std::string> overlappingOriginalFrameIds;
};

struct GraphExecutionPlan {
    bool valid = true;
    bool executableWithCurrentAdapters = true;
    std::string outputNodeId;
    std::string outputProducerNodeId;
    std::string contentIdentitySha256;
    std::vector<GraphExecutionStep> steps;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Plans only the ancestors of the authoritative Output. Disconnected drafts
// are preserved in the document but never scheduled. Input order is the
// persisted variadic order, and every step identity includes the ordered
// upstream identities plus canonical settings.
GraphExecutionPlan BuildMultiFrameGraphExecutionPlan(
    const Stack::Project::RawProjectSnapshot& snapshot);

// Manual controls on this HDR node do not invalidate its fixed analysis.
// Upstream changes and registration settings do.
std::string MultiFrameFusionInputIdentity(
    const Stack::Project::RawProjectSnapshot& snapshot, const std::string& nodeId);

const GraphExecutionStep* FindGraphExecutionStep(
    const GraphExecutionPlan& plan,
    const std::string& nodeId);

} // namespace Raw::MultiFrame
