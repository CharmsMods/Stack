#include "Persistence/MultiFrameGraph.h"

#include "Persistence/RawProjectModel.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Project {
namespace {

MultiFrameGraphNode MakeNode(
    MultiFrameGraphNodeKind kind,
    const std::string& title,
    double x,
    double y,
    bool suggested = true) {
    MultiFrameGraphNode node;
    node.nodeId = GenerateStableUuid();
    node.kind = kind;
    node.title = title;
    node.positionX = x;
    node.positionY = y;
    node.suggested = suggested;
    return node;
}

void AddLink(
    MultiFrameGraphDocument& graph,
    const MultiFrameGraphNode& from,
    const char* fromPort,
    const MultiFrameGraphNode& to,
    const char* toPort,
    MultiFrameGraphResourceType type,
    std::uint32_t order) {
    MultiFrameGraphLink link;
    link.linkId = GenerateStableUuid();
    link.fromNodeId = from.nodeId;
    link.fromPortId = fromPort;
    link.toNodeId = to.nodeId;
    link.toPortId = toPort;
    link.resourceType = type;
    link.variadicOrder = order;
    graph.links.push_back(std::move(link));
}

std::string ManualFileGraphFingerprint(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet) {
    std::ostringstream stream;
    stream << "manual-raw-files-v1|" << sourceSet.sourceSetId;
    for (const SourceSetFrame& frame : sourceSet.frames) {
        const EmbeddedAssetRecord* asset = FindEmbeddedAsset(
            snapshot,
            frame.assetId);
        stream << '|' << frame.frameId << ':' << frame.assetId << ':'
               << (frame.enabled ? '1' : '0');
        if (asset) {
            stream << ':' << asset->sha256 << ':' << asset->byteLength;
        }
    }
    const std::string content = stream.str();
    const std::vector<std::uint8_t> bytes(content.begin(), content.end());
    return Stack::RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

std::string RawFileNodeTitle(
    const RawProjectSnapshot& snapshot,
    const SourceSetFrame& frame) {
    if (!frame.userLabel.empty()) {
        return frame.userLabel;
    }
    const EmbeddedAssetRecord* asset = FindEmbeddedAsset(snapshot, frame.assetId);
    if (asset && !asset->originalFilename.empty()) {
        return asset->originalFilename;
    }
    return frame.frameId.empty() ? "RAW File" : frame.frameId;
}

MultiFrameGraphResourceType OutputTypeForNode(const MultiFrameGraphNode& node) {
    if (node.kind == MultiFrameGraphNodeKind::CaptureSet ||
        node.kind == MultiFrameGraphNodeKind::CaptureSubset) {
        return MultiFrameGraphResourceType::RawMeasurementSet;
    }
    if (node.kind == MultiFrameGraphNodeKind::Output) {
        return MultiFrameGraphResourceType::VirtualBayer;
    }
    return MultiFrameGraphResourceType::RawMeasurement;
}

bool NodeAcceptsInput(
    const MultiFrameGraphNode& node,
    MultiFrameGraphResourceType resourceType) {
    switch (node.kind) {
    case MultiFrameGraphNodeKind::BurstDenoise:
    case MultiFrameGraphNodeKind::HdrMerge:
        return resourceType == MultiFrameGraphResourceType::RawMeasurement ||
            resourceType == MultiFrameGraphResourceType::RawMeasurementSet;
    case MultiFrameGraphNodeKind::Output:
        return resourceType == MultiFrameGraphResourceType::RawMeasurement ||
            resourceType == MultiFrameGraphResourceType::RawMeasurementSet;
    case MultiFrameGraphNodeKind::CaptureSet:
    case MultiFrameGraphNodeKind::CaptureSubset:
        return false;
    }
    return false;
}

} // namespace

const char* MultiFrameGraphNodeKindName(MultiFrameGraphNodeKind value) {
    switch (value) {
    case MultiFrameGraphNodeKind::CaptureSet: return "capture-set";
    case MultiFrameGraphNodeKind::CaptureSubset: return "capture-subset";
    case MultiFrameGraphNodeKind::BurstDenoise: return "burst-denoise";
    case MultiFrameGraphNodeKind::HdrMerge: return "hdr-merge";
    case MultiFrameGraphNodeKind::Output: return "output";
    }
    return "capture-set";
}

const char* MultiFrameGraphResourceTypeName(MultiFrameGraphResourceType value) {
    switch (value) {
    case MultiFrameGraphResourceType::RawMeasurement: return "raw-measurement";
    case MultiFrameGraphResourceType::RawMeasurementSet: return "raw-measurement-set";
    case MultiFrameGraphResourceType::VirtualBayer: return "virtual-bayer";
    }
    return "raw-measurement";
}

bool ParseMultiFrameGraphNodeKind(
    const std::string& value,
    MultiFrameGraphNodeKind& result) {
    if (value == "capture-set") result = MultiFrameGraphNodeKind::CaptureSet;
    else if (value == "capture-subset") result = MultiFrameGraphNodeKind::CaptureSubset;
    else if (value == "burst-denoise") result = MultiFrameGraphNodeKind::BurstDenoise;
    else if (value == "hdr-merge") result = MultiFrameGraphNodeKind::HdrMerge;
    else if (value == "output") result = MultiFrameGraphNodeKind::Output;
    else return false;
    return true;
}

bool ParseMultiFrameGraphResourceType(
    const std::string& value,
    MultiFrameGraphResourceType& result) {
    if (value == "raw-measurement") {
        result = MultiFrameGraphResourceType::RawMeasurement;
    } else if (value == "raw-measurement-set") {
        result = MultiFrameGraphResourceType::RawMeasurementSet;
    } else if (value == "virtual-bayer") {
        result = MultiFrameGraphResourceType::VirtualBayer;
    } else {
        return false;
    }
    return true;
}

json SerializeMultiFrameGraph(const MultiFrameGraphDocument& graph) {
    json value = json::object();
    value["schemaVersion"] = graph.schemaVersion;
    value["contractId"] = graph.contractId;
    value["nodes"] = json::array();
    for (const MultiFrameGraphNode& node : graph.nodes) {
        json nodeValue = json::object();
        nodeValue["nodeId"] = node.nodeId;
        nodeValue["kind"] = MultiFrameGraphNodeKindName(node.kind);
        nodeValue["title"] = node.title;
        nodeValue["sourceSetId"] = node.sourceSetId.empty()
            ? json(nullptr)
            : json(node.sourceSetId);
        nodeValue["frameIds"] = node.frameIds;
        nodeValue["position"] = { { "x", node.positionX }, { "y", node.positionY } };
        nodeValue["enabled"] = node.enabled;
        nodeValue["suggested"] = node.suggested;
        nodeValue["settings"] = node.settings;
        value["nodes"].push_back(std::move(nodeValue));
    }
    value["links"] = json::array();
    for (const MultiFrameGraphLink& link : graph.links) {
        json linkValue = json::object();
        linkValue["linkId"] = link.linkId;
        linkValue["fromNodeId"] = link.fromNodeId;
        linkValue["fromPortId"] = link.fromPortId;
        linkValue["toNodeId"] = link.toNodeId;
        linkValue["toPortId"] = link.toPortId;
        linkValue["resourceType"] = MultiFrameGraphResourceTypeName(link.resourceType);
        linkValue["variadicOrder"] = link.variadicOrder;
        value["links"].push_back(std::move(linkValue));
    }
    value["outputNodeId"] = graph.outputNodeId;
    value["userEdited"] = graph.userEdited;
    value["suggestionFingerprint"] = graph.suggestionFingerprint;
    value["automaticDecisionSnapshot"] = graph.automaticDecisionSnapshot;
    value["view"] = {
        { "panX", graph.viewPanX },
        { "panY", graph.viewPanY },
        { "zoom", graph.viewZoom }
    };
    return value;
}

bool DeserializeMultiFrameGraph(
    const json& value,
    MultiFrameGraphDocument& graph,
    std::string* errorMessage) {
    const auto fail = [&](const std::string& message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };
    if (!value.is_object()) return fail("MultiFrame graph is not an object.");
    MultiFrameGraphDocument decoded;
    decoded.schemaVersion = value.value("schemaVersion", 0u);
    decoded.contractId = value.value("contractId", std::string());
    if (decoded.schemaVersion != kMultiFrameGraphSchemaVersion ||
        decoded.contractId != kMultiFrameGraphContractId) {
        return fail("MultiFrame graph contract identity is unsupported.");
    }
    const json nodes = value.value("nodes", json());
    const json links = value.value("links", json());
    if (!nodes.is_array() || !links.is_array()) {
        return fail("MultiFrame graph nodes or links are missing.");
    }
    for (const json& nodeValue : nodes) {
        if (!nodeValue.is_object()) return fail("A MultiFrame graph node is invalid.");
        MultiFrameGraphNode node;
        node.nodeId = nodeValue.value("nodeId", std::string());
        if (!ParseMultiFrameGraphNodeKind(
                nodeValue.value("kind", std::string()), node.kind)) {
            return fail("A MultiFrame graph node kind is invalid.");
        }
        node.title = nodeValue.value("title", std::string());
        const auto sourceSet = nodeValue.find("sourceSetId");
        if (sourceSet != nodeValue.end() && sourceSet->is_string()) {
            node.sourceSetId = sourceSet->get<std::string>();
        }
        node.frameIds = nodeValue.value("frameIds", std::vector<std::string>());
        const json position = nodeValue.value("position", json::object());
        node.positionX = position.value("x", 0.0);
        node.positionY = position.value("y", 0.0);
        node.enabled = nodeValue.value("enabled", true);
        node.suggested = nodeValue.value("suggested", false);
        node.settings = nodeValue.value("settings", json::object());
        decoded.nodes.push_back(std::move(node));
    }
    for (const json& linkValue : links) {
        if (!linkValue.is_object()) return fail("A MultiFrame graph link is invalid.");
        MultiFrameGraphLink link;
        link.linkId = linkValue.value("linkId", std::string());
        link.fromNodeId = linkValue.value("fromNodeId", std::string());
        link.fromPortId = linkValue.value("fromPortId", std::string());
        link.toNodeId = linkValue.value("toNodeId", std::string());
        link.toPortId = linkValue.value("toPortId", std::string());
        if (!ParseMultiFrameGraphResourceType(
                linkValue.value("resourceType", std::string()),
                link.resourceType)) {
            return fail("A MultiFrame graph link resource type is invalid.");
        }
        link.variadicOrder = linkValue.value("variadicOrder", 0u);
        decoded.links.push_back(std::move(link));
    }
    decoded.outputNodeId = value.value("outputNodeId", std::string());
    decoded.userEdited = value.value("userEdited", false);
    decoded.suggestionFingerprint = value.value("suggestionFingerprint", std::string());
    decoded.automaticDecisionSnapshot = value.value(
        "automaticDecisionSnapshot", json::object());
    const json view = value.value("view", json::object());
    decoded.viewPanX = view.value("panX", 0.0);
    decoded.viewPanY = view.value("panY", 0.0);
    decoded.viewZoom = view.value("zoom", 1.0);
    graph = std::move(decoded);
    if (errorMessage) errorMessage->clear();
    return true;
}

const MultiFrameGraphNode* FindMultiFrameGraphNode(
    const MultiFrameGraphDocument& graph,
    const std::string& nodeId) {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [&](const MultiFrameGraphNode& node) { return node.nodeId == nodeId; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

MultiFrameGraphNode* FindMultiFrameGraphNode(
    MultiFrameGraphDocument& graph,
    const std::string& nodeId) {
    return const_cast<MultiFrameGraphNode*>(FindMultiFrameGraphNode(
        static_cast<const MultiFrameGraphDocument&>(graph), nodeId));
}

bool WouldCreateMultiFrameGraphCycle(
    const MultiFrameGraphDocument& graph,
    const std::string& fromNodeId,
    const std::string& toNodeId) {
    if (fromNodeId.empty() || toNodeId.empty() || fromNodeId == toNodeId) return true;
    std::unordered_map<std::string, std::vector<std::string>> outgoing;
    for (const MultiFrameGraphLink& link : graph.links) {
        outgoing[link.fromNodeId].push_back(link.toNodeId);
    }
    std::vector<std::string> pending { toNodeId };
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        const std::string current = std::move(pending.back());
        pending.pop_back();
        if (current == fromNodeId) return true;
        if (!visited.insert(current).second) continue;
        const auto next = outgoing.find(current);
        if (next != outgoing.end()) {
            pending.insert(pending.end(), next->second.begin(), next->second.end());
        }
    }
    return false;
}

MultiFrameGraphValidationResult ValidateMultiFrameGraph(
    const MultiFrameGraphDocument& graph,
    const RawProjectSnapshot& snapshot,
    bool requireExecutableOutput) {
    MultiFrameGraphValidationResult result;
    const auto fail = [&](const std::string& message) {
        result.valid = false;
        result.errors.push_back(message);
    };
    if (graph.nodes.empty() && graph.links.empty() && graph.outputNodeId.empty()) {
        result.warnings.push_back(
            "This project has not authored its MultiFrame measurement graph yet.");
        return result;
    }
    if (graph.schemaVersion != kMultiFrameGraphSchemaVersion ||
        graph.contractId != kMultiFrameGraphContractId) {
        fail("MultiFrame graph identity is invalid.");
    }
    if (!std::isfinite(graph.viewPanX) || !std::isfinite(graph.viewPanY) ||
        !std::isfinite(graph.viewZoom) || graph.viewZoom < 0.25 ||
        graph.viewZoom > 4.0) {
        fail("MultiFrame graph view transform is invalid.");
    }
    std::unordered_map<std::string, const MultiFrameGraphNode*> nodes;
    std::size_t outputCount = 0;
    for (const MultiFrameGraphNode& node : graph.nodes) {
        if (node.nodeId.empty() || !nodes.emplace(node.nodeId, &node).second) {
            fail("MultiFrame graph node IDs must be present and unique.");
            continue;
        }
        if (!std::isfinite(node.positionX) || !std::isfinite(node.positionY)) {
            fail("MultiFrame graph node positions must be finite.");
        }
        if (!node.settings.is_object()) {
            fail("MultiFrame graph node settings must be objects.");
        }
        if (node.kind == MultiFrameGraphNodeKind::Output) {
            ++outputCount;
            if (node.nodeId != graph.outputNodeId) {
                fail("The authoritative MultiFrame Output binding is inconsistent.");
            }
        }
        if (node.kind == MultiFrameGraphNodeKind::CaptureSet ||
            node.kind == MultiFrameGraphNodeKind::CaptureSubset) {
            const MultiFrameSourceSet* sourceSet = FindSourceSet(snapshot, node.sourceSetId);
            if (!sourceSet) {
                fail("A MultiFrame source node references a missing capture set.");
                continue;
            }
            std::unordered_set<std::string> sourceFrames;
            for (const SourceSetFrame& frame : sourceSet->frames) {
                sourceFrames.insert(frame.frameId);
            }
            std::unordered_set<std::string> subsetFrames;
            for (const std::string& frameId : node.frameIds) {
                if (!sourceFrames.count(frameId) || !subsetFrames.insert(frameId).second) {
                    fail("A capture subset contains a missing or duplicate frame.");
                }
            }
            if (node.kind == MultiFrameGraphNodeKind::CaptureSubset &&
                node.frameIds.empty()) {
                fail("A capture subset must contain at least one frame.");
            }
        }
    }
    if (outputCount != 1u || graph.outputNodeId.empty()) {
        fail("A MultiFrame graph requires exactly one authoritative Output node.");
    }

    std::unordered_set<std::string> linkIds;
    std::unordered_set<std::string> endpointKeys;
    std::unordered_set<std::string> slotKeys;
    std::size_t outputInputCount = 0;
    for (const MultiFrameGraphLink& link : graph.links) {
        if (link.linkId.empty() || !linkIds.insert(link.linkId).second) {
            fail("MultiFrame graph link IDs must be present and unique.");
            continue;
        }
        const auto from = nodes.find(link.fromNodeId);
        const auto to = nodes.find(link.toNodeId);
        if (from == nodes.end() || to == nodes.end()) {
            fail("A MultiFrame graph link references a missing node.");
            continue;
        }
        const MultiFrameGraphResourceType expected = OutputTypeForNode(*from->second);
        if (link.resourceType != expected ||
            !NodeAcceptsInput(*to->second, link.resourceType)) {
            fail("A MultiFrame graph link has an incompatible RAW measurement resource type.");
        }
        const std::string expectedFromPort =
            expected == MultiFrameGraphResourceType::RawMeasurementSet
                ? "measurements"
                : "estimate";
        if (link.fromPortId != expectedFromPort || link.toPortId != "measurements") {
            fail("A MultiFrame graph link uses an unknown typed port.");
        }
        const std::string endpoints = link.fromNodeId + "\n" + link.fromPortId +
            "\n" + link.toNodeId + "\n" + link.toPortId;
        if (!endpointKeys.insert(endpoints).second) {
            fail("Duplicate wires cannot create independent sensor evidence.");
        }
        const std::string slot = link.toNodeId + "\n" + link.toPortId + "\n" +
            std::to_string(link.variadicOrder);
        if (!slotKeys.insert(slot).second) {
            fail("Variadic MultiFrame input slots must be stable and unique.");
        }
        if (link.toNodeId == graph.outputNodeId) ++outputInputCount;
        if (WouldCreateMultiFrameGraphCycle(
                graph, link.fromNodeId, link.toNodeId)) {
            // WouldCreate includes this existing edge, so only a reverse path
            // from the target back to the source indicates a cycle.
            std::unordered_map<std::string, std::vector<std::string>> reverseCheck;
            for (const MultiFrameGraphLink& candidate : graph.links) {
                if (candidate.linkId != link.linkId) {
                    reverseCheck[candidate.fromNodeId].push_back(candidate.toNodeId);
                }
            }
            std::vector<std::string> pending { link.toNodeId };
            std::unordered_set<std::string> visited;
            bool cycle = false;
            while (!pending.empty()) {
                const std::string current = std::move(pending.back());
                pending.pop_back();
                if (current == link.fromNodeId) { cycle = true; break; }
                if (!visited.insert(current).second) continue;
                const auto next = reverseCheck.find(current);
                if (next != reverseCheck.end()) {
                    pending.insert(pending.end(), next->second.begin(), next->second.end());
                }
            }
            if (cycle) fail("The MultiFrame graph must remain acyclic.");
        }
    }
    if (outputInputCount > 1u) {
        fail("The authoritative MultiFrame Output accepts exactly one input.");
    }
    if (requireExecutableOutput && outputInputCount != 1u) {
        fail("The MultiFrame Output is not connected to an executable measurement.");
    }
    return result;
}

MultiFrameGraphDocument BuildManualMultiFrameGraph(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet) {
    MultiFrameGraphDocument graph;
    graph.suggestionFingerprint = ManualFileGraphFingerprint(snapshot, sourceSet);
    graph.automaticDecisionSnapshot = {
        { "schemaVersion", 1 },
        { "source", "manual-raw-files" },
        { "processingSuggested", false },
        { "groupingSuggested", false }
    };

    constexpr double kSourceX = 48.0;
    constexpr double kSourceStartY = 54.0;
    constexpr double kSourceSpacingY = 148.0;
    std::size_t sourceIndex = 0u;
    for (const SourceSetFrame& frame : sourceSet.frames) {
        MultiFrameGraphNode source = MakeNode(
            MultiFrameGraphNodeKind::CaptureSubset,
            RawFileNodeTitle(snapshot, frame),
            kSourceX,
            kSourceStartY +
                kSourceSpacingY * static_cast<double>(sourceIndex),
            false);
        source.sourceSetId = sourceSet.sourceSetId;
        source.frameIds = { frame.frameId };
        source.enabled = frame.enabled;
        source.settings = {
            { "source", "raw-file" },
            { "frameId", frame.frameId },
            { "assetId", frame.assetId }
        };
        graph.nodes.push_back(std::move(source));
        ++sourceIndex;
    }

    const double outputY = sourceIndex > 1u
        ? kSourceStartY +
            kSourceSpacingY * static_cast<double>(sourceIndex - 1u) * 0.5
        : kSourceStartY;
    MultiFrameGraphNode output = MakeNode(
        MultiFrameGraphNodeKind::Output,
        "Output",
        760.0,
        outputY,
        false);
    output.settings = {
        { "publicationDomain", "virtual-cfa" },
        { "atomicPublication", true }
    };
    graph.outputNodeId = output.nodeId;
    graph.nodes.push_back(std::move(output));
    return graph;
}

MultiFrameGraphDocument BuildOperationMultiFrameGraph(
    const RawProjectSnapshot& snapshot) {
    if (snapshot.sourceSets.empty()) return {};
    const MultiFrameSourceSet* sourceSet = FindSourceSet(
        snapshot, snapshot.activeSourceSetId);
    if (!sourceSet) sourceSet = &snapshot.sourceSets.front();
    MultiFrameGraphDocument graph = BuildManualMultiFrameGraph(
        snapshot, *sourceSet);
    if (sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
        sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
        MultiFrameGraphNode process = MakeNode(
            sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr
                ? MultiFrameGraphNodeKind::HdrMerge
                : MultiFrameGraphNodeKind::BurstDenoise,
            sourceSet->operationIntent == MultiFrameOperationIntent::RawBurstHdr
                ? "HDR Merge"
                : "Burst Denoise",
            420.0,
            graph.nodes.size() > 2u
                ? 54.0 + 148.0 * static_cast<double>(graph.nodes.size() - 2u) * 0.5
                : 54.0,
            false);
        process.settings = sourceSet->settings;
        process.settings["evidencePolicy"] = "disjoint-original-evidence-v1";
        const std::string processNodeId = process.nodeId;
        graph.nodes.push_back(std::move(process));
        MultiFrameGraphNode* output = FindMultiFrameGraphNode(
            graph, graph.outputNodeId);
        if (output) {
            output->positionX = 780.0;
        }
        std::uint32_t inputOrder = 0u;
        for (const MultiFrameGraphNode& source : graph.nodes) {
            if (source.kind != MultiFrameGraphNodeKind::CaptureSubset ||
                !source.enabled) {
                continue;
            }
            const MultiFrameGraphNode* processNode = FindMultiFrameGraphNode(
                graph, processNodeId);
            if (!processNode) break;
            AddLink(graph, source, "measurements", *processNode, "measurements",
                MultiFrameGraphResourceType::RawMeasurementSet, inputOrder++);
        }
        const MultiFrameGraphNode* processNode = FindMultiFrameGraphNode(
            graph, processNodeId);
        output = FindMultiFrameGraphNode(graph, graph.outputNodeId);
        if (processNode && output) {
            AddLink(graph, *processNode, "estimate", *output, "measurements",
            MultiFrameGraphResourceType::RawMeasurement, 0u);
        }
    }
    return graph;
}

} // namespace Stack::Project
