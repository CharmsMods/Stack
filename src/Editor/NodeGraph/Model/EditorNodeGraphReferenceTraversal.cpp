#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace EditorNodeGraph {
namespace {

struct ReferenceVisitKey {
    int nodeId = -1;
    std::string socketId;

    bool operator==(const ReferenceVisitKey& other) const {
        return nodeId == other.nodeId && socketId == other.socketId;
    }
};

struct ReferenceVisitKeyHash {
    std::size_t operator()(const ReferenceVisitKey& key) const {
        const std::size_t nodeHash = std::hash<int>{}(key.nodeId);
        const std::size_t socketHash = std::hash<std::string>{}(key.socketId);
        return nodeHash ^ (socketHash + 0x9e3779b9u + (nodeHash << 6) + (nodeHash >> 2));
    }
};

struct ReferenceResult {
    bool visiting = false;
    int sourceNodeId = -1;
};

struct ReferencePlan {
    std::vector<const Link*> dependencies;
    bool mixSelection = false;
    bool hasImmediateResult = false;
    int immediateResult = -1;
};

struct ReferenceFrame {
    ReferenceVisitKey key;
    ReferencePlan plan;
    std::vector<int> dependencyResults;
    std::size_t nextDependency = 0;
    bool initialized = false;
};

} // namespace

int Graph::ResolveReferenceSourceNodeId(
    int nodeId,
    const std::string& socketId) const {
    const auto makePlan = [&](const ReferenceVisitKey& key) {
        ReferencePlan plan;
        const Node* node = FindNode(key.nodeId);
        if (!node) {
            plan.hasImmediateResult = true;
            return plan;
        }

        const auto addInput = [&](const std::string& inputSocketId) {
            if (const Link* input = FindInputLink(key.nodeId, inputSocketId)) {
                plan.dependencies.push_back(input);
                return true;
            }
            return false;
        };
        const auto useCurrentNode = [&]() {
            plan.hasImmediateResult = true;
            plan.immediateResult = key.nodeId;
        };

        switch (node->kind) {
            case NodeKind::Image:
            case NodeKind::RawSource:
            case NodeKind::RawDevelopment:
            case NodeKind::ImageGenerator:
            case NodeKind::MaskGenerator:
            case NodeKind::CustomMask:
            case NodeKind::FrequencyMask:
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
                useCurrentNode();
                break;
            case NodeKind::RawNeuralDenoise:
                addInput(kRawInputSocketId);
                break;
            case NodeKind::Layer:
            case NodeKind::TechnicalImage:
            case NodeKind::Reformat:
            case NodeKind::RawDetailFusion:
            case NodeKind::RawDetailAutoMask:
            case NodeKind::ChannelSplit:
                addInput(kImageInputSocketId);
                break;
            case NodeKind::Compound: {
                std::vector<std::string> dependencies;
                std::string dependencyError;
                if (ResolveCompoundOutputInputDependencies(
                        key.nodeId,
                        key.socketId,
                        dependencies,
                        &dependencyError)) {
                    for (const std::string& inputSocketId : dependencies) {
                        addInput(inputSocketId);
                    }
                    for (const SocketDefinition& inputSocket :
                         GetSockets(*node, false)) {
                        if (inputSocket.direction == SocketDirection::Input &&
                            inputSocket.optional) {
                            addInput(inputSocket.id);
                        }
                    }
                }
                break;
            }
            case NodeKind::Lut:
                if (!addInput(kImageInputSocketId)) {
                    addInput("r");
                    addInput("g");
                    addInput("b");
                    addInput("a");
                }
                break;
            case NodeKind::HdrMerge: {
                const char* preferredSocketId = kHdrMergeInput1SocketId;
                if (key.socketId == kHdrMergeInput2SocketId) {
                    preferredSocketId = kHdrMergeInput2SocketId;
                } else if (key.socketId == kHdrMergeInput3SocketId) {
                    preferredSocketId = kHdrMergeInput3SocketId;
                }
                addInput(preferredSocketId);
                break;
            }
            case NodeKind::Mfsr: {
                const std::string preferredSocketId =
                    IsMfsrInputSocketId(key.socketId)
                    ? key.socketId
                    : std::string(kMfsrReferenceInputSocketId);
                addInput(preferredSocketId);
                break;
            }
            case NodeKind::Mix:
                addInput(kMixInputASocketId);
                addInput(kMixInputBSocketId);
                plan.mixSelection = true;
                break;
            case NodeKind::DataMath:
                for (int inputIndex = 0;
                     inputIndex < kMaxDataMathInputCount;
                     ++inputIndex) {
                    addInput(DataMathInputSocketId(inputIndex));
                }
                break;
            case NodeKind::ChannelCombine:
            case NodeKind::Output:
                if (key.socketId == kImageInputSocketId &&
                    addInput(kImageInputSocketId)) {
                    break;
                }
                addInput("r");
                addInput("g");
                addInput("b");
                addInput("a");
                break;
            case NodeKind::ConstantChannel:
                addInput(kMatchExtentInputSocketId);
                break;
            case NodeKind::MaskUtility:
                addInput(kMaskUtilityInputSocketId);
                break;
            case NodeKind::MaskCombine:
                addInput(kMaskCombineInputASocketId);
                addInput(kMaskCombineInputBSocketId);
                break;
            case NodeKind::ImageToMask:
                addInput(kImageToMaskInputSocketId);
                break;
            case NodeKind::FrequencyFilter:
            case NodeKind::FrequencyFft:
                addInput(kChannelInputSocketId);
                break;
            case NodeKind::FrequencyIfft:
            case NodeKind::SpectrumView:
            case NodeKind::SpectrumSeparate:
            case NodeKind::SpectrumAnalyzer:
                addInput(kSpectrumInputSocketId);
                break;
            case NodeKind::ApplyFrequencyResponse:
                addInput(kSpectrumInputSocketId);
                break;
            case NodeKind::CombineSpectra:
                addInput(kSpectrumInputASocketId);
                break;
            case NodeKind::SpectrumRecombine:
                addInput(kSpectrumMagnitudeInputSocketId);
                break;
            case NodeKind::SpectrumMath:
                addInput(kMixInputASocketId);
                break;
            case NodeKind::MagnitudePhase:
                addInput(
                    node->magnitudePhaseMode == MagnitudePhaseMode::Recombine
                        ? "magnitude"
                        : kImageInputSocketId);
                break;
            case NodeKind::Composite:
            case NodeKind::Scope:
            case NodeKind::Preview:
            case NodeKind::FieldMean:
            case NodeKind::Value:
            case NodeKind::FrequencyResponse:
                break;
        }
        return plan;
    };

    const ReferenceVisitKey rootKey{ nodeId, socketId };
    std::unordered_map<ReferenceVisitKey, ReferenceResult, ReferenceVisitKeyHash>
        results;
    results.reserve(m_Nodes.size());
    std::vector<ReferenceFrame> pending;
    pending.push_back(ReferenceFrame{ rootKey });

    while (!pending.empty()) {
        ReferenceFrame& frame = pending.back();
        if (!frame.initialized) {
            const auto existing = results.find(frame.key);
            if (existing != results.end()) {
                pending.pop_back();
                continue;
            }
            results.emplace(frame.key, ReferenceResult{ true, -1 });
            frame.plan = makePlan(frame.key);
            frame.initialized = true;
            if (frame.plan.hasImmediateResult) {
                results[frame.key] =
                    ReferenceResult{ false, frame.plan.immediateResult };
                pending.pop_back();
                continue;
            }
        }

        if (frame.nextDependency < frame.plan.dependencies.size()) {
            const Link& dependency =
                *frame.plan.dependencies[frame.nextDependency];
            const ReferenceVisitKey dependencyKey{
                dependency.fromNodeId,
                dependency.fromSocketId
            };
            const auto dependencyResult = results.find(dependencyKey);
            if (dependencyResult == results.end()) {
                pending.push_back(ReferenceFrame{ dependencyKey });
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
                const Node* source = FindNode(sourceId);
                return source &&
                    (source->kind == NodeKind::ImageGenerator ||
                     source->kind == NodeKind::MaskGenerator ||
                     source->kind == NodeKind::CustomMask);
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

        results[frame.key] =
            ReferenceResult{ false, resolvedSourceNodeId };
        pending.pop_back();
    }

    const auto resolved = results.find(rootKey);
    return resolved != results.end() && !resolved->second.visiting
        ? resolved->second.sourceNodeId
        : -1;
}

int Graph::ResolveReferenceSourceNodeIdForOutput(int outputNodeId) const {
    const Node* output = FindNode(outputNodeId);
    if (!output || output->kind != NodeKind::Output) {
        return -1;
    }
    return ResolveReferenceSourceNodeId(outputNodeId, kImageInputSocketId);
}

} // namespace EditorNodeGraph
