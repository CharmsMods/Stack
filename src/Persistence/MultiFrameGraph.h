#pragma once

#include "ThirdParty/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Stack::Project {

using json = nlohmann::json;

struct RawProjectSnapshot;
struct MultiFrameSourceSet;

inline constexpr std::uint32_t kMultiFrameGraphSchemaVersion = 1;
inline constexpr const char* kMultiFrameGraphContractId =
    "stack-multiframe-measurement-graph-v1";

enum class MultiFrameGraphNodeKind {
    CaptureSet,
    CaptureSubset,
    BurstDenoise,
    HdrMerge,
    Output
};

enum class MultiFrameGraphResourceType {
    RawMeasurement,
    RawMeasurementSet,
    VirtualBayer
};

struct MultiFrameGraphNode {
    std::string nodeId;
    MultiFrameGraphNodeKind kind = MultiFrameGraphNodeKind::CaptureSet;
    std::string title;
    std::string sourceSetId;
    std::vector<std::string> frameIds;
    double positionX = 0.0;
    double positionY = 0.0;
    bool enabled = true;
    bool suggested = false;
    json settings = json::object();
};

struct MultiFrameGraphLink {
    std::string linkId;
    std::string fromNodeId;
    std::string fromPortId;
    std::string toNodeId;
    std::string toPortId;
    MultiFrameGraphResourceType resourceType =
        MultiFrameGraphResourceType::RawMeasurement;
    std::uint32_t variadicOrder = 0;
};

struct MultiFrameGraphDocument {
    std::uint32_t schemaVersion = kMultiFrameGraphSchemaVersion;
    std::string contractId = kMultiFrameGraphContractId;
    std::vector<MultiFrameGraphNode> nodes;
    std::vector<MultiFrameGraphLink> links;
    std::string outputNodeId;
    bool userEdited = false;
    std::string suggestionFingerprint;
    json automaticDecisionSnapshot = json::object();
    double viewPanX = 0.0;
    double viewPanY = 0.0;
    double viewZoom = 1.0;
};

struct MultiFrameGraphValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

const char* MultiFrameGraphNodeKindName(MultiFrameGraphNodeKind value);
const char* MultiFrameGraphResourceTypeName(MultiFrameGraphResourceType value);
bool ParseMultiFrameGraphNodeKind(
    const std::string& value,
    MultiFrameGraphNodeKind& result);
bool ParseMultiFrameGraphResourceType(
    const std::string& value,
    MultiFrameGraphResourceType& result);

json SerializeMultiFrameGraph(const MultiFrameGraphDocument& graph);
bool DeserializeMultiFrameGraph(
    const json& value,
    MultiFrameGraphDocument& graph,
    std::string* errorMessage = nullptr);

MultiFrameGraphValidationResult ValidateMultiFrameGraph(
    const MultiFrameGraphDocument& graph,
    const RawProjectSnapshot& snapshot,
    bool requireExecutableOutput = false);

// Builds the normal user-facing starting point: one visible RAW-file node per
// capture plus the single authoritative Output, with no inferred grouping,
// processor choice, or authored links.
MultiFrameGraphDocument BuildManualMultiFrameGraph(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet);

MultiFrameGraphDocument BuildOperationMultiFrameGraph(
    const RawProjectSnapshot& snapshot);

bool BuildExposureGroupedMultiFrameGraph(
    const RawProjectSnapshot& snapshot, const MultiFrameSourceSet& sourceSet,
    MultiFrameGraphDocument& graph, std::string* error = nullptr);

const MultiFrameGraphNode* FindMultiFrameGraphNode(
    const MultiFrameGraphDocument& graph,
    const std::string& nodeId);
MultiFrameGraphNode* FindMultiFrameGraphNode(
    MultiFrameGraphDocument& graph,
    const std::string& nodeId);

bool WouldCreateMultiFrameGraphCycle(
    const MultiFrameGraphDocument& graph,
    const std::string& fromNodeId,
    const std::string& toNodeId);

} // namespace Stack::Project
