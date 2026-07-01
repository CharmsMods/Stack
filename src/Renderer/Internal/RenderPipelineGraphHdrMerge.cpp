#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

int RenderPipeline::FindReferenceSourceNode(const GraphExecutionContext& executionContext, int nodeId) {
    const auto nodeIt = executionContext.nodes.find(nodeId);
    if (nodeIt == executionContext.nodes.end() || !nodeIt->second) {
        return -1;
    }

    const RenderGraphNode& node = *nodeIt->second;
    auto findInputLink = [&](int inputNodeId, std::string_view socketId) {
        return executionContext.FindInputLink(inputNodeId, socketId);
    };

    switch (node.kind) {
        case RenderGraphNodeKind::Image:
        case RenderGraphNodeKind::RawSource:
        case RenderGraphNodeKind::RawDevelopment:
        case RenderGraphNodeKind::ImageGenerator:
        case RenderGraphNodeKind::RawDecode:
        case RenderGraphNodeKind::RawDevelop:
            return node.nodeId;
        case RenderGraphNodeKind::RawDetailFusion: {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            return input ? FindReferenceSourceNode(executionContext, input->fromNodeId) : -1;
        }
        case RenderGraphNodeKind::HdrMerge: {
            const char* sockets[] = { "image1", "image2", "image3" };
            for (const char* socket : sockets) {
                if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                    const int source = FindReferenceSourceNode(executionContext, input->fromNodeId);
                    if (source > 0) {
                        return source;
                    }
                }
            }
            return -1;
        }
        case RenderGraphNodeKind::Mfsr: {
            const RenderGraphLink* input = findInputLink(node.nodeId, EditorNodeGraph::kMfsrReferenceInputSocketId);
            return input ? FindReferenceSourceNode(executionContext, input->fromNodeId) : -1;
        }
        case RenderGraphNodeKind::RawDetailAutoMask: {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            return input ? FindReferenceSourceNode(executionContext, input->fromNodeId) : -1;
        }
        case RenderGraphNodeKind::RawNeuralDenoise: {
            const RenderGraphLink* input = findInputLink(node.nodeId, "rawIn");
            return input ? FindReferenceSourceNode(executionContext, input->fromNodeId) : -1;
        }
        case RenderGraphNodeKind::Lut: {
            if (const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn")) {
                return FindReferenceSourceNode(executionContext, input->fromNodeId);
            }
            for (const char* socket : { "r", "g", "b", "a" }) {
                if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                    const int source = FindReferenceSourceNode(executionContext, input->fromNodeId);
                    if (source > 0) {
                        return source;
                    }
                }
            }
            return -1;
        }
        case RenderGraphNodeKind::Layer:
        case RenderGraphNodeKind::Output:
        case RenderGraphNodeKind::ImageToMask:
        case RenderGraphNodeKind::MaskCombine:
        case RenderGraphNodeKind::MaskUtility:
        case RenderGraphNodeKind::ChannelSplit: {
            const RenderGraphLink* input = findInputLink(node.nodeId, "imageIn");
            if (!input && node.kind == RenderGraphNodeKind::MaskCombine) {
                input = findInputLink(node.nodeId, "maskA");
            }
            if (!input && node.kind == RenderGraphNodeKind::MaskUtility) {
                input = findInputLink(node.nodeId, "maskIn");
            }
            if (!input && node.kind == RenderGraphNodeKind::ChannelSplit) {
                input = findInputLink(node.nodeId, "imageIn");
            }
            return input ? FindReferenceSourceNode(executionContext, input->fromNodeId) : -1;
        }
        case RenderGraphNodeKind::Mix: {
            const RenderGraphLink* inputA = findInputLink(node.nodeId, "imageA");
            const RenderGraphLink* inputB = findInputLink(node.nodeId, "imageB");
            const int sourceA = inputA ? FindReferenceSourceNode(executionContext, inputA->fromNodeId) : -1;
            const int sourceB = inputB ? FindReferenceSourceNode(executionContext, inputB->fromNodeId) : -1;
            auto isGeneratedReference = [&](int sourceId) {
                const auto sourceIt = executionContext.nodes.find(sourceId);
                if (sourceIt == executionContext.nodes.end() || !sourceIt->second) {
                    return false;
                }
                return sourceIt->second->kind == RenderGraphNodeKind::ImageGenerator ||
                       sourceIt->second->kind == RenderGraphNodeKind::MaskGenerator ||
                       sourceIt->second->kind == RenderGraphNodeKind::CustomMask;
            };
            if (sourceA > 0 && sourceB > 0 && isGeneratedReference(sourceA) && !isGeneratedReference(sourceB)) {
                return sourceB;
            }
            return sourceA > 0 ? sourceA : sourceB;
        }
        case RenderGraphNodeKind::DataMath: {
            const RenderGraphLink* input = FindFirstDataMathAverageInput(executionContext, node.nodeId);
            return input ? FindReferenceSourceNode(executionContext, input->fromNodeId) : -1;
        }
        case RenderGraphNodeKind::ChannelCombine: {
            const char* sockets[] = { "r", "g", "b", "a" };
            for (const char* socket : sockets) {
                if (const RenderGraphLink* input = findInputLink(node.nodeId, socket)) {
                    const int source = FindReferenceSourceNode(executionContext, input->fromNodeId);
                    if (source > 0) {
                        return source;
                    }
                }
            }
            return -1;
        }
        case RenderGraphNodeKind::MaskGenerator:
        default:
            return -1;
    }
}

RenderPipeline::HdrMergeInputContext RenderPipeline::ResolveHdrMergeInputContext(
    const GraphExecutionContext& executionContext,
    int sourceNodeId) {
    HdrMergeInputContext context;
    const int referenceNodeId = FindReferenceSourceNode(executionContext, sourceNodeId);
    if (referenceNodeId <= 0) {
        return context;
    }

    const auto referenceIt = executionContext.nodes.find(referenceNodeId);
    if (referenceIt == executionContext.nodes.end() || !referenceIt->second) {
        return context;
    }

    const RenderGraphNode& referenceNode = *referenceIt->second;
    context.active = true;
    if (referenceNode.kind == RenderGraphNodeKind::RawSource) {
        context.hasRawMetadata = true;
        context.metadata = referenceNode.rawSource.metadata;
    } else if (referenceNode.kind == RenderGraphNodeKind::RawDevelopment) {
        context.developExposureStops = referenceNode.rawDevelopment.recipe.preToneExposureEv;
        context.developExposureScale = std::exp2(context.developExposureStops);
    } else if (referenceNode.kind == RenderGraphNodeKind::RawDecode ||
               referenceNode.kind == RenderGraphNodeKind::RawDevelop) {
        const Raw::RawDevelopSettings& settings =
            referenceNode.kind == RenderGraphNodeKind::RawDecode
                ? referenceNode.rawDecode.settings
                : referenceNode.rawDevelop.settings;
        context.developExposureStops = settings.exposureStops;
        context.developExposureScale = std::exp2(context.developExposureStops);

        const RenderGraphLink* rawInput = executionContext.FindInputLink(referenceNode.nodeId, "rawIn");
        std::set<int> rawVisit;
        while (rawInput) {
            if (!rawVisit.insert(rawInput->fromNodeId).second) {
                break;
            }
            const auto rawIt = executionContext.nodes.find(rawInput->fromNodeId);
            if (rawIt == executionContext.nodes.end() || !rawIt->second) {
                break;
            }
            if (rawIt->second->kind == RenderGraphNodeKind::RawSource) {
                context.hasRawMetadata = true;
                context.metadata = rawIt->second->rawSource.metadata;
                break;
            }
            if (rawIt->second->kind != RenderGraphNodeKind::RawNeuralDenoise) {
                break;
            }
            rawInput = executionContext.FindInputLink(rawIt->second->nodeId, "rawIn");
        }
    }

    if (context.hasRawMetadata && HasHdrMergeCaptureExposure(context.metadata)) {
        context.hasCaptureExposure = true;
        context.captureExposureEv = ComputeHdrMergeCaptureExposureEv(context.metadata);
    }
    return context;
}

RenderPipeline::HdrMergeResolvedSettings RenderPipeline::ResolveHdrMergeSettings(
    const Raw::HdrMergeSettings& settings,
    const std::array<HdrMergeInputContext, 3>& contexts,
    const std::array<bool, 3>& activeInputs) {
    HdrMergeResolvedSettings resolved;
    std::array<float, 3> absoluteExposureEv {
        settings.manualExposureEv[0],
        settings.manualExposureEv[1],
        settings.manualExposureEv[2]
    };

    bool metadataExposureValid = settings.exposureMode == Raw::HdrMergeExposureMode::Metadata;
    std::vector<float> referenceDistances;
    referenceDistances.reserve(3);
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            continue;
        }
        if (metadataExposureValid) {
            if (!contexts[i].hasCaptureExposure) {
                metadataExposureValid = false;
                break;
            }
            absoluteExposureEv[i] = contexts[i].captureExposureEv + contexts[i].developExposureStops + settings.exposureOffsetEv[i];
        }
        referenceDistances.push_back(absoluteExposureEv[i]);
    }

    if (metadataExposureValid) {
        resolved.metadataExposureValid = true;
        const float exposureAnchor = SelectRepresentativeExposureAnchor(absoluteExposureEv, activeInputs);
        const float medianExposure = MedianFloat(referenceDistances);
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i]) {
                continue;
            }
            resolved.exposureEv[i] = absoluteExposureEv[i] - exposureAnchor;
            resolved.referenceExposureDistance[i] = std::abs(absoluteExposureEv[i] - medianExposure);
        }
    } else {
        referenceDistances.clear();
        for (int i = 0; i < 3; ++i) {
            if (activeInputs[i]) {
                referenceDistances.push_back(settings.manualExposureEv[i]);
                resolved.exposureEv[i] = settings.manualExposureEv[i];
            }
        }
        const float medianExposure = MedianFloat(referenceDistances);
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i]) {
                continue;
            }
            resolved.referenceExposureDistance[i] = std::abs(settings.manualExposureEv[i] - medianExposure);
        }
    }

    for (int i = 0; i < 3; ++i) {
        resolved.clipThreshold[i] = settings.clipThreshold;
        resolved.clipFeather[i] = settings.clipFeather;
        resolved.blackThreshold[i] = settings.blackThreshold;
        resolved.blackFeather[i] = settings.blackFeather;
        resolved.readNoise[i] = settings.readNoise;

        if (!activeInputs[i] || !settings.autoReliability || !contexts[i].hasRawMetadata) {
            continue;
        }

        const float sourceScale = std::max(0.0625f, contexts[i].developExposureScale);
        const float rangeStep = EstimateHdrMergeRangeStep(contexts[i].metadata);
        const float isoMultiplier = EstimateHdrMergeIsoMultiplier(contexts[i].metadata);

        resolved.clipThreshold[i] = std::clamp(0.98f * sourceScale, 0.50f, 4.0f);
        resolved.clipFeather[i] = std::clamp(std::max(0.04f * sourceScale, rangeStep * 48.0f * sourceScale), 0.001f, 1.0f);
        resolved.blackThreshold[i] = std::clamp(rangeStep * (12.0f + 4.0f * isoMultiplier) * sourceScale, 0.0f, 0.25f);
        resolved.blackFeather[i] = std::clamp(std::max(resolved.blackThreshold[i] * 6.0f, rangeStep * 24.0f * sourceScale), 0.001f, 0.50f);
        resolved.readNoise[i] = std::clamp(rangeStep * (6.0f + 2.0f * isoMultiplier) * sourceScale, 0.0f, 0.10f);
    }

    return resolved;
}
