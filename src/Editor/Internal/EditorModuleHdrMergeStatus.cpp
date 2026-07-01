#include "Editor/EditorModule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::string JoinSummaryList(const std::vector<std::string>& items) {
    if (items.empty()) {
        return {};
    }
    if (items.size() == 1) {
        return items.front();
    }
    if (items.size() == 2) {
        return items[0] + " and " + items[1];
    }

    std::string joined;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index > 0) {
            joined += (index + 1 == items.size()) ? ", and " : ", ";
        }
        joined += items[index];
    }
    return joined;
}

std::string BuildHdrMergeCaptureMetadataSummary(const Raw::RawMetadata& metadata, float developExposureStops) {
    std::vector<std::string> parts;
    parts.reserve(4);

    char buffer[64];
    if (metadata.hasExposureTime) {
        std::snprintf(buffer, sizeof(buffer), "%.4g s", std::max(0.0f, metadata.exposureTimeSeconds));
        parts.emplace_back(buffer);
    }
    if (metadata.hasApertureFNumber) {
        std::snprintf(buffer, sizeof(buffer), "f/%.1f", std::max(0.0f, metadata.apertureFNumber));
        parts.emplace_back(buffer);
    }
    if (metadata.hasIsoSpeed) {
        std::snprintf(buffer, sizeof(buffer), "ISO %.0f", std::max(0.0f, metadata.isoSpeed));
        parts.emplace_back(buffer);
    }
    if (std::fabs(developExposureStops) > 0.001f) {
        std::snprintf(buffer, sizeof(buffer), "Develop %+.2f EV", developExposureStops);
        parts.emplace_back(buffer);
    }

    std::string summary;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            summary += "  ";
        }
        summary += parts[index];
    }
    return summary;
}

bool HasHdrMergeCaptureExposureMetadata(const Raw::RawMetadata& metadata) {
    return metadata.hasExposureTime || metadata.hasIsoSpeed || metadata.hasApertureFNumber;
}

float ComputeHdrMergeCaptureExposureEvForStatus(const Raw::RawMetadata& metadata) {
    const float shutter = metadata.hasExposureTime ? std::max(0.000001f, metadata.exposureTimeSeconds) : 1.0f;
    const float isoFactor = metadata.hasIsoSpeed ? std::max(0.01f, metadata.isoSpeed / 100.0f) : 1.0f;
    const float aperture = metadata.hasApertureFNumber ? std::max(0.1f, metadata.apertureFNumber) : 1.0f;
    return std::log2((shutter * isoFactor) / std::max(0.01f, aperture * aperture));
}

float MedianSummaryFloat(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if ((values.size() & 1u) != 0u) {
        return values[mid];
    }
    return 0.5f * (values[mid - 1] + values[mid]);
}

float SelectHdrMergeExposureAnchorForStatus(
    const std::array<float, 3>& absoluteExposureEv,
    const std::array<bool, 3>& activeInputs) {
    std::vector<float> activeValues;
    activeValues.reserve(3);
    for (int i = 0; i < 3; ++i) {
        if (activeInputs[i]) {
            activeValues.push_back(absoluteExposureEv[i]);
        }
    }
    if (activeValues.empty()) {
        return 0.0f;
    }

    const float median = MedianSummaryFloat(activeValues);
    float bestDistance = std::numeric_limits<float>::infinity();
    float anchor = activeValues.front();
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            continue;
        }
        const float distance = std::fabs(absoluteExposureEv[i] - median);
        if (distance < bestDistance) {
            bestDistance = distance;
            anchor = absoluteExposureEv[i];
        }
    }
    return anchor;
}

} // namespace

std::vector<int> EditorModule::CollectHdrMergeNodesForOutput(int outputNodeId) const {
    std::vector<int> hdrNodes;
    if (outputNodeId <= 0) {
        return hdrNodes;
    }

    std::unordered_set<int> seenNodes;
    std::unordered_set<int> seenHdrNodes;
    std::function<void(int)> visit = [&](int nodeId) {
        if (!seenNodes.insert(nodeId).second) {
            return;
        }

        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            return;
        }
        if (node->kind == EditorNodeGraph::NodeKind::HdrMerge && seenHdrNodes.insert(nodeId).second) {
            hdrNodes.push_back(nodeId);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : m_NodeGraph.GetSockets(*node, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
                continue;
            }
            if (const EditorNodeGraph::Link* input = m_NodeGraph.FindAnyInputLink(nodeId, socket.id)) {
                visit(input->fromNodeId);
            }
        }
    };

    visit(outputNodeId);
    return hdrNodes;
}

EditorModule::HdrMergeConnectionTopology EditorModule::ResolveHdrMergeConnectionTopology(const EditorNodeGraph::Node& node) const {
    HdrMergeConnectionTopology topology;
    if (node.kind != EditorNodeGraph::NodeKind::HdrMerge) {
        return topology;
    }

    topology.hasInput1 = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kHdrMergeInput1SocketId) != nullptr;
    topology.hasInput2 = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kHdrMergeInput2SocketId) != nullptr;
    topology.hasInput3 = m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kHdrMergeInput3SocketId) != nullptr;
    topology.hasGap = topology.hasInput3 && !topology.hasInput2;
    topology.usesInput3 = topology.hasInput1 && topology.hasInput2 && topology.hasInput3 && !topology.hasGap;
    if (topology.hasInput1 && topology.hasInput2) {
        topology.activeInputCount = topology.usesInput3 ? 3 : 2;
    }
    return topology;
}

EditorModule::HdrMergeNodeStatus EditorModule::BuildHdrMergeNodeStatus(const EditorNodeGraph::Node& node) const {
    HdrMergeNodeStatus status;
    status.debugView = node.hdrMerge.settings.debugView;
    const HdrMergeConnectionTopology topology = ResolveHdrMergeConnectionTopology(node);
    status.metadataNormalizationReady = node.hdrMerge.settings.exposureMode != Raw::HdrMergeExposureMode::Metadata;
    status.automaticReliabilityReady = !node.hdrMerge.settings.autoReliability;

    struct HdrMergeInputContext {
        bool hasRawMetadata = false;
        bool hasCaptureExposure = false;
        float developExposureStops = 0.0f;
        Raw::RawMetadata metadata;
    };
    std::array<HdrMergeInputContext, 3> contexts {};

    const auto resolveInputContext = [&](int sourceNodeId) {
        HdrMergeInputContext context;
        const int referenceNodeId = m_NodeGraph.ResolveReferenceSourceNodeId(sourceNodeId, EditorNodeGraph::kImageOutputSocketId);
        const EditorNodeGraph::Node* referenceNode = m_NodeGraph.FindNode(referenceNodeId);
        if (!referenceNode) {
            return context;
        }

        if (referenceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
            context.hasRawMetadata = true;
            context.metadata = referenceNode->rawSource.metadata;
        } else if (referenceNode->kind == EditorNodeGraph::NodeKind::RawDecode ||
                   referenceNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            context.developExposureStops =
                referenceNode->kind == EditorNodeGraph::NodeKind::RawDecode
                    ? referenceNode->rawDecode.settings.exposureStops
                    : referenceNode->rawDevelop.settings.exposureStops;
            const EditorNodeGraph::Link* rawInput = m_NodeGraph.FindInputLink(referenceNode->id, EditorNodeGraph::kRawInputSocketId);
            std::unordered_set<int> visitedRawNodes;
            while (rawInput && visitedRawNodes.insert(rawInput->fromNodeId).second) {
                const EditorNodeGraph::Node* rawNode = m_NodeGraph.FindNode(rawInput->fromNodeId);
                if (!rawNode) {
                    break;
                }
                if (rawNode->kind == EditorNodeGraph::NodeKind::RawSource) {
                    context.hasRawMetadata = true;
                    context.metadata = rawNode->rawSource.metadata;
                    break;
                }
                if (rawNode->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
                    break;
                }
                rawInput = m_NodeGraph.FindInputLink(rawNode->id, EditorNodeGraph::kRawInputSocketId);
            }
        }

        if (context.hasRawMetadata) {
            context.hasCaptureExposure = HasHdrMergeCaptureExposureMetadata(context.metadata);
        }
        return context;
    };

    const auto fillInput = [&](std::size_t index, const char* socketId, const char* label, bool active) {
        HdrMergeInputSummary& input = status.inputs[index];
        input.socketId = socketId;
        input.label = label;
        input.active = active;
        const EditorNodeGraph::Link* link = m_NodeGraph.FindInputLink(node.id, socketId);
        input.connected = link != nullptr;
        input.sourceNodeId = link ? link->fromNodeId : -1;
        input.sourceLabel = "Missing";
        input.compatible = true;
        if (const EditorNodeGraph::Node* sourceNode = link ? m_NodeGraph.FindNode(link->fromNodeId) : nullptr) {
            input.sourceLabel = sourceNode->title.empty() ? std::string("Node ") + std::to_string(sourceNode->id) : sourceNode->title;
        } else if (!active) {
            input.sourceLabel = "Inactive";
        }
        if (input.connected) {
            TryResolveReferenceSourceDimensions(node.id, socketId, input.width, input.height);
            contexts[index] = resolveInputContext(input.sourceNodeId);
            input.hasRawMetadata = contexts[index].hasRawMetadata;
            input.hasCaptureExposure = contexts[index].hasCaptureExposure;
            if (input.hasRawMetadata) {
                const std::string summary =
                    BuildHdrMergeCaptureMetadataSummary(contexts[index].metadata, contexts[index].developExposureStops);
                input.metadataSummary = summary.empty()
                    ? std::string("RAW metadata is available, but capture exposure fields are incomplete.")
                    : summary;
            } else {
                input.metadataSummary = "Scene-linear input only; automatic RAW-derived exposure and reliability may be unavailable.";
            }
        }
    };

    fillInput(0, EditorNodeGraph::kHdrMergeInput1SocketId, "Image 1", true);
    fillInput(1, EditorNodeGraph::kHdrMergeInput2SocketId, "Image 2", true);
    fillInput(2, EditorNodeGraph::kHdrMergeInput3SocketId, "Image 3", topology.hasInput3);

    std::vector<std::string> metadataMissingInputs;
    std::vector<std::string> reliabilityMissingInputs;
    std::vector<float> apertureValues;
    std::array<bool, 3> activeConnectedInputs { false, false, false };
    for (std::size_t index = 0; index < status.inputs.size(); ++index) {
        const HdrMergeInputSummary& input = status.inputs[index];
        const HdrMergeInputContext& context = contexts[index];
        if (!input.active || !input.connected) {
            continue;
        }
        activeConnectedInputs[index] = true;
        if (node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata && !input.hasCaptureExposure) {
            metadataMissingInputs.push_back(input.label);
        }
        if (node.hdrMerge.settings.autoReliability && !input.hasRawMetadata) {
            reliabilityMissingInputs.push_back(input.label);
        }
        if (context.hasRawMetadata && context.metadata.hasApertureFNumber) {
            apertureValues.push_back(context.metadata.apertureFNumber);
        }
    }

    if (node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata) {
        if (metadataMissingInputs.empty()) {
            status.metadataNormalizationReady = true;
            status.normalizationMessage = "Automatic normalization is ready for all active frames.";
        } else {
            status.metadataNormalizationReady = false;
            status.normalizationMessage =
                "Automatic normalization falls back to manual EV for " + JoinSummaryList(metadataMissingInputs) + ".";
        }
    } else {
        status.normalizationMessage = "Manual EV normalization is active.";
    }

    if (node.hdrMerge.settings.autoReliability) {
        if (reliabilityMissingInputs.empty()) {
            status.automaticReliabilityReady = true;
            status.reliabilityMessage = "Automatic reliability is using RAW metadata for all active frames.";
        } else {
            status.automaticReliabilityReady = false;
            status.reliabilityMessage =
                "Automatic reliability falls back to manual thresholds for " + JoinSummaryList(reliabilityMissingInputs) + ".";
        }
    } else {
        status.reliabilityMessage = "Manual reliability thresholds are active.";
    }

    const bool usingMetadataNormalization =
        node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata && metadataMissingInputs.empty();
    std::array<float, 3> resolvedNormalizationEv {
        node.hdrMerge.settings.manualExposureEv[0],
        node.hdrMerge.settings.manualExposureEv[1],
        node.hdrMerge.settings.manualExposureEv[2]
    };
    if (usingMetadataNormalization) {
        std::array<float, 3> absoluteExposureEv { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 3; ++i) {
            if (!activeConnectedInputs[i]) {
                continue;
            }
            absoluteExposureEv[i] = ComputeHdrMergeCaptureExposureEvForStatus(contexts[i].metadata) +
                contexts[i].developExposureStops +
                node.hdrMerge.settings.exposureOffsetEv[i];
        }
        const float exposureAnchor = SelectHdrMergeExposureAnchorForStatus(absoluteExposureEv, activeConnectedInputs);
        for (int i = 0; i < 3; ++i) {
            if (!activeConnectedInputs[i]) {
                continue;
            }
            resolvedNormalizationEv[i] = absoluteExposureEv[i] - exposureAnchor;
        }
    }

    for (int i = 0; i < 3; ++i) {
        HdrMergeInputSummary& input = status.inputs[i];
        if (!activeConnectedInputs[i]) {
            continue;
        }

        char buffer[96];
        if (usingMetadataNormalization) {
            std::snprintf(buffer, sizeof(buffer), "Normalized %+.2f EV relative to the merge reference.", resolvedNormalizationEv[i]);
        } else if (node.hdrMerge.settings.exposureMode == Raw::HdrMergeExposureMode::Metadata) {
            std::snprintf(buffer, sizeof(buffer), "Manual fallback %+.2f EV.", resolvedNormalizationEv[i]);
        } else {
            std::snprintf(buffer, sizeof(buffer), "Manual normalization %+.2f EV.", resolvedNormalizationEv[i]);
        }
        input.normalizationSummary = buffer;
    }

    if (apertureValues.size() >= 2) {
        const float referenceAperture = apertureValues.front();
        for (float aperture : apertureValues) {
            if (std::fabs(aperture - referenceAperture) > 0.05f) {
                status.warningMessage =
                    "Aperture varies across the active frames. Automatic exposure matching may need per-frame offsets.";
                break;
            }
        }
    }

    const int previewOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    if (previewOutputNodeId > 0) {
        const std::vector<int> activeHdrNodes = CollectHdrMergeNodesForOutput(previewOutputNodeId);
        status.feedsActiveOutput = std::find(activeHdrNodes.begin(), activeHdrNodes.end(), node.id) != activeHdrNodes.end();
    }

    if (!status.inputs[0].connected) {
        status.state = HdrMergeRenderState::BlockedMissingInput;
        status.message = "Needs Image 1";
        return status;
    }
    if (!status.inputs[1].connected) {
        status.state = HdrMergeRenderState::BlockedMissingInput;
        status.message = "Needs Image 2";
        return status;
    }
    if (topology.hasGap) {
        status.state = HdrMergeRenderState::BlockedMissingInput;
        status.message = "Image 3 requires Image 2";
        return status;
    }

    int referenceWidth = status.inputs[0].width;
    int referenceHeight = status.inputs[0].height;
    for (HdrMergeInputSummary& input : status.inputs) {
        if ((!input.active && input.socketId == EditorNodeGraph::kHdrMergeInput3SocketId) || !input.connected) {
            continue;
        }
        if (referenceWidth <= 0 || referenceHeight <= 0 || input.width <= 0 || input.height <= 0) {
            status.state = HdrMergeRenderState::Failed;
            status.message = "Reference canvas unavailable";
            return status;
        }
        input.compatible = (input.width == referenceWidth && input.height == referenceHeight);
        if (!input.compatible) {
            status.state = HdrMergeRenderState::IncompatibleInput;
            status.message = "Dimension mismatch";
            return status;
        }
    }

    const std::uint64_t dirtyGeneration = GetNodeDirtyGeneration(node.id);
    const std::uint64_t requestedGeneration =
        m_HdrMergeRequestedGenerations.count(node.id) ? m_HdrMergeRequestedGenerations.at(node.id) : 0;
    const std::uint64_t completedGeneration =
        m_HdrMergeCompletedGenerations.count(node.id) ? m_HdrMergeCompletedGenerations.at(node.id) : 0;
    status.hasRenderedResult = completedGeneration > 0;
    status.stale = completedGeneration > 0 && completedGeneration < dirtyGeneration;

    const auto failureIt = m_HdrMergeFailureMessages.find(node.id);
    const bool hasFailureMessage = failureIt != m_HdrMergeFailureMessages.end() &&
        !failureIt->second.empty() &&
        requestedGeneration > completedGeneration;

    if (!status.feedsActiveOutput) {
        if (hasFailureMessage) {
            status.state = HdrMergeRenderState::Failed;
            status.message = failureIt->second;
            return status;
        }
        status.state = HdrMergeRenderState::Ready;
        status.message = status.stale ? "Stale" : "Ready";
        return status;
    }
    if (completedGeneration >= dirtyGeneration && dirtyGeneration > 0) {
        status.state = HdrMergeRenderState::Rendered;
        status.message = "Rendered";
        return status;
    }
    if (m_HdrMergeRenderingNodeIds.count(node.id) > 0) {
        status.state = HdrMergeRenderState::Rendering;
        status.message = "Rendering";
        return status;
    }
    if (requestedGeneration >= dirtyGeneration && dirtyGeneration > 0) {
        status.state = HdrMergeRenderState::Queued;
        status.message = "Queued";
        return status;
    }
    if (hasFailureMessage) {
        status.state = HdrMergeRenderState::Failed;
        status.message = failureIt->second;
        status.stale = true;
        return status;
    }

    status.state = HdrMergeRenderState::Ready;
    status.message = status.stale ? "Stale" : "Ready";
    return status;
}

EditorModule::HdrMergeNodeStatus EditorModule::GetHdrMergeNodeStatus(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::HdrMerge) {
        return {};
    }
    return BuildHdrMergeNodeStatus(*node);
}


