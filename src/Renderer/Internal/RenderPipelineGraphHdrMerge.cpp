#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <unordered_map>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

int RenderPipeline::FindReferenceSourceNode(const GraphExecutionContext& executionContext, int nodeId) {
    struct ReferenceResult {
        bool visiting = false;
        int sourceNodeId = -1;
    };
    struct ReferencePlan {
        bool immediate = false;
        int immediateResult = -1;
        bool mixSelection = false;
        std::vector<const RenderGraphLink*> dependencies;
    };
    struct ReferenceFrame {
        int nodeId = -1;
        ReferencePlan plan;
        std::vector<int> dependencyResults;
        std::size_t nextDependency = 0;
        bool initialized = false;
    };

    const auto makePlan = [&](int currentNodeId) {
        ReferencePlan plan;
        const auto nodeIt = executionContext.nodes.find(currentNodeId);
        if (nodeIt == executionContext.nodes.end() || !nodeIt->second) {
            plan.immediate = true;
            return plan;
        }

        const RenderGraphNode& node = *nodeIt->second;
        const auto addInput = [&](std::string_view socketId) {
            if (const RenderGraphLink* input =
                    executionContext.FindInputLink(node.nodeId, socketId)) {
                plan.dependencies.push_back(input);
                return true;
            }
            return false;
        };
        const auto useCurrentNode = [&]() {
            plan.immediate = true;
            plan.immediateResult = node.nodeId;
        };

        switch (node.kind) {
            case RenderGraphNodeKind::Image:
            case RenderGraphNodeKind::RawSource:
            case RenderGraphNodeKind::RawDevelopment:
            case RenderGraphNodeKind::RawProjectSourceSet:
            case RenderGraphNodeKind::ImageGenerator:
            case RenderGraphNodeKind::RawDecode:
            case RenderGraphNodeKind::RawDevelop:
                useCurrentNode();
                break;
            case RenderGraphNodeKind::RawNeuralDenoise:
                addInput(EditorNodeGraph::kRawInputSocketId);
                break;
            case RenderGraphNodeKind::Layer:
            case RenderGraphNodeKind::TechnicalImage:
            case RenderGraphNodeKind::Reformat:
            case RenderGraphNodeKind::RawDetailFusion:
            case RenderGraphNodeKind::RawDetailAutoMask:
            case RenderGraphNodeKind::ChannelSplit:
            case RenderGraphNodeKind::ImageToMask:
                addInput(EditorNodeGraph::kImageInputSocketId);
                break;
            case RenderGraphNodeKind::Lut:
                if (!addInput(EditorNodeGraph::kImageInputSocketId)) {
                    addInput("r");
                    addInput("g");
                    addInput("b");
                    addInput("a");
                }
                break;
            case RenderGraphNodeKind::HdrMerge:
                addInput(EditorNodeGraph::kHdrMergeInput1SocketId);
                addInput(EditorNodeGraph::kHdrMergeInput2SocketId);
                addInput(EditorNodeGraph::kHdrMergeInput3SocketId);
                break;
            case RenderGraphNodeKind::Mfsr:
                addInput(EditorNodeGraph::kMfsrReferenceInputSocketId);
                break;
            case RenderGraphNodeKind::Mix:
                addInput(EditorNodeGraph::kMixInputASocketId);
                addInput(EditorNodeGraph::kMixInputBSocketId);
                plan.mixSelection = true;
                break;
            case RenderGraphNodeKind::DataMath:
                for (int inputIndex = 0;
                     inputIndex < EditorNodeGraph::kMaxDataMathInputCount;
                     ++inputIndex) {
                    addInput(
                        EditorNodeGraph::DataMathInputSocketId(inputIndex));
                }
                addInput(EditorNodeGraph::kDataMathBaseInputSocketId);
                break;
            case RenderGraphNodeKind::Output:
                if (!addInput(EditorNodeGraph::kImageInputSocketId)) {
                    addInput("r");
                    addInput("g");
                    addInput("b");
                    addInput("a");
                }
                break;
            case RenderGraphNodeKind::ChannelCombine:
                addInput("r");
                addInput("g");
                addInput("b");
                addInput("a");
                break;
            case RenderGraphNodeKind::ConstantChannel:
                addInput(EditorNodeGraph::kMatchExtentInputSocketId);
                break;
            case RenderGraphNodeKind::MaskUtility:
                addInput(EditorNodeGraph::kMaskUtilityInputSocketId);
                break;
            case RenderGraphNodeKind::MaskCombine:
                addInput(EditorNodeGraph::kMaskCombineInputASocketId);
                addInput(EditorNodeGraph::kMaskCombineInputBSocketId);
                break;
            case RenderGraphNodeKind::FrequencyFilter:
            case RenderGraphNodeKind::FrequencyFft:
                addInput(EditorNodeGraph::kChannelInputSocketId);
                break;
            case RenderGraphNodeKind::FrequencyIfft:
            case RenderGraphNodeKind::SpectrumView:
            case RenderGraphNodeKind::SpectrumSeparate:
            case RenderGraphNodeKind::SpectrumAnalyzer:
                addInput(EditorNodeGraph::kSpectrumInputSocketId);
                break;
            case RenderGraphNodeKind::ApplyFrequencyResponse:
                addInput(EditorNodeGraph::kSpectrumInputSocketId);
                break;
            case RenderGraphNodeKind::CombineSpectra:
                addInput(EditorNodeGraph::kSpectrumInputASocketId);
                break;
            case RenderGraphNodeKind::SpectrumRecombine:
                addInput(EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
                break;
            case RenderGraphNodeKind::SpectrumMath:
                addInput(EditorNodeGraph::kMixInputASocketId);
                break;
            case RenderGraphNodeKind::MagnitudePhase:
                addInput(
                    node.magnitudePhaseMode ==
                            RenderMagnitudePhaseMode::Recombine
                        ? EditorNodeGraph::kSpectrumMagnitudeInputSocketId
                        : EditorNodeGraph::kImageInputSocketId);
                break;
            default:
                break;
        }
        return plan;
    };

    std::unordered_map<int, ReferenceResult> results;
    results.reserve(executionContext.nodes.size());
    std::vector<ReferenceFrame> pending;
    pending.push_back(ReferenceFrame{ nodeId });

    while (!pending.empty()) {
        ReferenceFrame& frame = pending.back();
        if (!frame.initialized) {
            if (results.count(frame.nodeId) > 0) {
                pending.pop_back();
                continue;
            }
            results.emplace(frame.nodeId, ReferenceResult{ true, -1 });
            frame.plan = makePlan(frame.nodeId);
            frame.initialized = true;
            if (frame.plan.immediate) {
                results[frame.nodeId] =
                    ReferenceResult{ false, frame.plan.immediateResult };
                pending.pop_back();
                continue;
            }
        }

        if (frame.nextDependency < frame.plan.dependencies.size()) {
            const int dependencyNodeId =
                frame.plan.dependencies[frame.nextDependency]->fromNodeId;
            const auto dependencyResult = results.find(dependencyNodeId);
            if (dependencyResult == results.end()) {
                pending.push_back(ReferenceFrame{ dependencyNodeId });
                continue;
            }
            frame.dependencyResults.push_back(
                dependencyResult->second.visiting
                    ? -1
                    : dependencyResult->second.sourceNodeId);
            ++frame.nextDependency;
            continue;
        }

        int resolvedSourceNodeId = -1;
        if (frame.plan.mixSelection &&
            frame.dependencyResults.size() >= 2) {
            const int sourceA = frame.dependencyResults[0];
            const int sourceB = frame.dependencyResults[1];
            const auto isGeneratedReference = [&](int sourceId) {
                const auto sourceIt =
                    executionContext.nodes.find(sourceId);
                return sourceIt != executionContext.nodes.end() &&
                    sourceIt->second &&
                    sourceIt->second->kind ==
                        RenderGraphNodeKind::ImageGenerator;
            };
            if (sourceA > 0 &&
                sourceB > 0 &&
                isGeneratedReference(sourceA) &&
                !isGeneratedReference(sourceB)) {
                resolvedSourceNodeId = sourceB;
            } else {
                resolvedSourceNodeId = sourceA > 0 ? sourceA : sourceB;
            }
        } else {
            for (int dependencyResult : frame.dependencyResults) {
                if (dependencyResult > 0) {
                    resolvedSourceNodeId = dependencyResult;
                    break;
                }
            }
        }

        results[frame.nodeId] =
            ReferenceResult{ false, resolvedSourceNodeId };
        pending.pop_back();
    }

    const auto resolved = results.find(nodeId);
    return resolved != results.end() && !resolved->second.visiting
        ? resolved->second.sourceNodeId
        : -1;
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
