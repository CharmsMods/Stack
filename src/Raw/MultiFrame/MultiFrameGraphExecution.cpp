#include "Raw/MultiFrame/GraphExecution.h"

#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Raw::MultiFrame {
namespace {

using Stack::Project::EmbeddedAssetRecord;
using Stack::Project::FindEmbeddedAsset;
using Stack::Project::FindMultiFrameGraphNode;
using Stack::Project::FindSourceSet;
using Stack::Project::MultiFrameGraphLink;
using Stack::Project::MultiFrameGraphNode;
using Stack::Project::MultiFrameGraphNodeKind;
using Stack::Project::RawProjectSnapshot;
using Stack::Project::SourceSetFrame;

GraphExecutionAdapter AdapterForNode(MultiFrameGraphNodeKind kind) {
    switch (kind) {
    case MultiFrameGraphNodeKind::CaptureSet:
        return GraphExecutionAdapter::CaptureSource;
    case MultiFrameGraphNodeKind::CaptureSubset:
        return GraphExecutionAdapter::CaptureSubset;
    case MultiFrameGraphNodeKind::BurstDenoise:
        return GraphExecutionAdapter::SharedBurstV1;
    case MultiFrameGraphNodeKind::HdrMerge:
        return GraphExecutionAdapter::HdrV4;
    case MultiFrameGraphNodeKind::Output:
        return GraphExecutionAdapter::PublishOutput;
    }
    return GraphExecutionAdapter::CaptureSource;
}

std::vector<std::string> SortedUnique(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<std::string> Intersection(
    const std::vector<std::string>& a,
    const std::vector<std::string>& b) {
    std::vector<std::string> result;
    std::set_intersection(
        a.begin(), a.end(), b.begin(), b.end(),
        std::back_inserter(result));
    return result;
}

std::string CanonicalSettings(const nlohmann::json& settings) {
    return settings.is_object() ? settings.dump() : nlohmann::json::object().dump();
}

std::string Sha256(const std::string& content) {
    const std::vector<std::uint8_t> bytes(content.begin(), content.end());
    return Stack::RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

std::string StepIdentity(
    const MultiFrameGraphNode& node,
    GraphExecutionAdapter adapter,
    const std::vector<GraphExecutionInput>& inputs,
    const std::unordered_map<std::string, std::string>& identities,
    const RawProjectSnapshot& snapshot) {
    nlohmann::json effectiveSettings = node.settings.is_object()
        ? node.settings : nlohmann::json::object();
    const auto* activeSet = FindSourceSet(snapshot, snapshot.activeSourceSetId);
    if (activeSet &&
        (node.kind == MultiFrameGraphNodeKind::BurstDenoise ||
         node.kind == MultiFrameGraphNodeKind::HdrMerge)) {
        if (!effectiveSettings.contains("parameters") &&
            activeSet->settings.contains("parameters")) {
            effectiveSettings["parameters"] = activeSet->settings["parameters"];
        }
        if (node.kind == MultiFrameGraphNodeKind::BurstDenoise) {
            if (!effectiveSettings.contains("sharedBurstSettings") &&
                activeSet->settings.contains("sharedBurstSettings")) {
                effectiveSettings["sharedBurstSettings"] =
                    activeSet->settings["sharedBurstSettings"];
            }
            effectiveSettings["compatibilityAlignmentMode"] =
                activeSet->settings.value(
                    "experimentalAlignmentMode", std::string("full"));
        }
    }
    std::ostringstream content;
    content << "stack-multiframe-execution-step-v1\n"
            << node.nodeId << '\n'
            << GraphExecutionAdapterName(adapter) << '\n'
            << CanonicalSettings(effectiveSettings) << '\n';
    if (node.kind == MultiFrameGraphNodeKind::CaptureSet ||
        node.kind == MultiFrameGraphNodeKind::CaptureSubset) {
        const auto* sourceSet = FindSourceSet(snapshot, node.sourceSetId);
        for (const std::string& frameId : node.frameIds) {
            content << "frame:" << frameId;
            if (sourceSet) {
                const auto frame = std::find_if(
                    sourceSet->frames.begin(), sourceSet->frames.end(),
                    [&](const SourceSetFrame& candidate) {
                        return candidate.frameId == frameId;
                    });
                if (frame != sourceSet->frames.end()) {
                    content << ":enabled:" << frame->enabled;
                    if (const EmbeddedAssetRecord* asset =
                            FindEmbeddedAsset(snapshot, frame->assetId)) {
                        content << ':' << asset->sha256 << ':' << asset->byteLength;
                    }
                }
            }
            content << '\n';
        }
    }
    for (const GraphExecutionInput& input : inputs) {
        const auto identity = identities.find(input.producerNodeId);
        content << "input:" << input.variadicOrder << ':' << input.linkId << ':'
                << input.producerNodeId << ':'
                << (identity == identities.end() ? std::string() : identity->second)
                << '\n';
    }
    return Sha256(content.str());
}

} // namespace

const char* GraphExecutionAdapterName(GraphExecutionAdapter adapter) {
    switch (adapter) {
    case GraphExecutionAdapter::CaptureSource: return "capture-source-v1";
    case GraphExecutionAdapter::CaptureSubset: return "capture-subset-v1";
    case GraphExecutionAdapter::SharedBurstV1: return "shared-burst-v1";
    case GraphExecutionAdapter::HdrV4: return "raw-hdr-v4";
    case GraphExecutionAdapter::PublishOutput: return "atomic-output-v1";
    }
    return "unknown";
}

GraphExecutionPlan BuildMultiFrameGraphExecutionPlan(
    const RawProjectSnapshot& snapshot) {
    GraphExecutionPlan plan;
    plan.outputNodeId = snapshot.multiFrameGraph.outputNodeId;
    const auto validation = Stack::Project::ValidateMultiFrameGraph(
        snapshot.multiFrameGraph, snapshot, true);
    plan.errors = validation.errors;
    plan.warnings = validation.warnings;
    if (!validation.valid) {
        plan.valid = false;
        plan.executableWithCurrentAdapters = false;
        return plan;
    }

    const auto& graph = snapshot.multiFrameGraph;
    std::unordered_map<std::string, std::vector<const MultiFrameGraphLink*>> incoming;
    for (const MultiFrameGraphLink& link : graph.links) {
        incoming[link.toNodeId].push_back(&link);
    }
    for (auto& [nodeId, links] : incoming) {
        (void)nodeId;
        std::sort(links.begin(), links.end(), [](const auto* a, const auto* b) {
            if (a->variadicOrder != b->variadicOrder)
                return a->variadicOrder < b->variadicOrder;
            return a->linkId < b->linkId;
        });
    }

    std::unordered_set<std::string> ancestors;
    std::function<void(const std::string&)> visitAncestors =
        [&](const std::string& nodeId) {
            if (!ancestors.insert(nodeId).second) return;
            const auto found = incoming.find(nodeId);
            if (found == incoming.end()) return;
            for (const MultiFrameGraphLink* link : found->second)
                visitAncestors(link->fromNodeId);
        };
    visitAncestors(plan.outputNodeId);

    std::unordered_set<std::string> emitted;
    std::vector<std::string> order;
    std::function<void(const std::string&)> schedule = [&](const std::string& nodeId) {
        if (emitted.count(nodeId)) return;
        const auto found = incoming.find(nodeId);
        if (found != incoming.end()) {
            for (const MultiFrameGraphLink* link : found->second)
                schedule(link->fromNodeId);
        }
        if (emitted.insert(nodeId).second) order.push_back(nodeId);
    };
    schedule(plan.outputNodeId);

    std::unordered_map<std::string, std::vector<std::string>> lineage;
    std::unordered_map<std::string, std::string> identities;
    for (const std::string& nodeId : order) {
        const MultiFrameGraphNode* node = FindMultiFrameGraphNode(graph, nodeId);
        if (!node || !ancestors.count(nodeId)) continue;
        GraphExecutionStep step;
        step.nodeId = nodeId;
        step.nodeKind = node->kind;
        step.adapter = AdapterForNode(node->kind);
        const auto found = incoming.find(nodeId);
        if (found != incoming.end()) {
            for (const MultiFrameGraphLink* link : found->second) {
                GraphExecutionInput input;
                input.linkId = link->linkId;
                input.producerNodeId = link->fromNodeId;
                input.resourceType = link->resourceType;
                input.variadicOrder = link->variadicOrder;
                input.originalFrameIds = lineage[link->fromNodeId];
                step.inputs.push_back(std::move(input));
            }
        }
        if (node->kind == MultiFrameGraphNodeKind::CaptureSet ||
            node->kind == MultiFrameGraphNodeKind::CaptureSubset) {
            const auto* set = FindSourceSet(snapshot, node->sourceSetId);
            for (const auto& id : node->frameIds) {
                if (!set) continue;
                const auto f = std::find_if(set->frames.begin(), set->frames.end(),
                    [&](const SourceSetFrame& frame) { return frame.frameId == id; });
                if (f != set->frames.end() && f->enabled) step.originalFrameIds.push_back(id);
            }
            step.originalFrameIds = SortedUnique(step.originalFrameIds);
        } else {
            for (const GraphExecutionInput& input : step.inputs) {
                step.originalFrameIds.insert(
                    step.originalFrameIds.end(),
                    input.originalFrameIds.begin(), input.originalFrameIds.end());
            }
            step.originalFrameIds = SortedUnique(std::move(step.originalFrameIds));
        }
        if (node->kind == MultiFrameGraphNodeKind::BurstDenoise ||
            node->kind == MultiFrameGraphNodeKind::HdrMerge) {
            for (const GraphExecutionInput& input : step.inputs) {
                step.inputMeasurementCount +=
                    input.resourceType == Stack::Project::
                        MultiFrameGraphResourceType::RawMeasurementSet
                    ? input.originalFrameIds.size()
                    : 1u;
            }
            for (std::size_t i = 0; i < step.inputs.size(); ++i) {
                for (std::size_t j = i + 1u; j < step.inputs.size(); ++j) {
                    auto overlap = Intersection(
                        step.inputs[i].originalFrameIds,
                        step.inputs[j].originalFrameIds);
                    step.overlappingOriginalFrameIds.insert(
                        step.overlappingOriginalFrameIds.end(),
                        overlap.begin(), overlap.end());
                }
            }
            step.overlappingOriginalFrameIds = SortedUnique(
                std::move(step.overlappingOriginalFrameIds));
            step.requiresCovarianceAwareFusion =
                !step.overlappingOriginalFrameIds.empty();
            if (step.requiresCovarianceAwareFusion) {
                plan.executableWithCurrentAdapters = false;
                plan.warnings.push_back(
                    "Node '" + node->title +
                    "' reuses original sensor evidence. Shared Burst V1 and RAW HDR V4 "
                    "do not yet model that covariance, so this topology is inspectable "
                    "but cannot be processed as independent captures.");
            }
            const std::size_t maximum =
                node->kind == MultiFrameGraphNodeKind::BurstDenoise
                ? 30u : 20u;
            const bool feedsOutput = std::any_of(graph.links.begin(), graph.links.end(), [&](const auto& link) {
                return link.fromNodeId == nodeId && link.toNodeId == graph.outputNodeId;
            });
            const std::size_t minimum = node->kind == MultiFrameGraphNodeKind::BurstDenoise && !feedsOutput ? 1u : 2u;
            if (step.inputMeasurementCount < minimum ||
                step.inputMeasurementCount > maximum) {
                plan.executableWithCurrentAdapters = false;
                plan.warnings.push_back(
                    "Node '" + node->title + "' has " +
                    std::to_string(step.inputMeasurementCount) +
                    " connected measurements; the current " +
                    (node->kind == MultiFrameGraphNodeKind::BurstDenoise
                        ? "Shared Burst adapter requires 2-30."
                        : "RAW HDR adapter requires 2-20."));
            }
        }
        step.contentIdentitySha256 = StepIdentity(
            *node, step.adapter, step.inputs, identities, snapshot);
        lineage[nodeId] = step.originalFrameIds;
        identities[nodeId] = step.contentIdentitySha256;
        plan.steps.push_back(std::move(step));
    }

    const auto outputInputs = incoming.find(plan.outputNodeId);
    if (outputInputs != incoming.end() && outputInputs->second.size() == 1u)
        plan.outputProducerNodeId = outputInputs->second.front()->fromNodeId;
    const auto outputIdentity = identities.find(plan.outputNodeId);
    if (outputIdentity != identities.end())
        plan.contentIdentitySha256 = outputIdentity->second;
    return plan;
}

const GraphExecutionStep* FindGraphExecutionStep(
    const GraphExecutionPlan& plan,
    const std::string& nodeId) {
    const auto found = std::find_if(
        plan.steps.begin(), plan.steps.end(),
        [&](const GraphExecutionStep& step) { return step.nodeId == nodeId; });
    return found == plan.steps.end() ? nullptr : &*found;
}

std::string MultiFrameFusionInputIdentity(const Stack::Project::RawProjectSnapshot& snapshot,
    const std::string& nodeId) {
    auto analysisSnapshot = snapshot;
    auto* node = Stack::Project::FindMultiFrameGraphNode(analysisSnapshot.multiFrameGraph, nodeId);
    if (!node) return {};
    node->settings.erase("fusion");
    const auto plan = BuildMultiFrameGraphExecutionPlan(analysisSnapshot);
    const auto* step = FindGraphExecutionStep(plan, nodeId);
    return step ? step->contentIdentitySha256 : std::string();
}

} // namespace Raw::MultiFrame
