#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace EditorNodeGraph {
namespace {

bool IsChannelSocket(const std::string& socketId) {
    return socketId == "r" ||
        socketId == "g" ||
        socketId == "b" ||
        socketId == "a";
}

struct ScalarVisitKey {
    int nodeId = -1;
    std::string socketId;

    bool operator==(const ScalarVisitKey& other) const {
        return nodeId == other.nodeId && socketId == other.socketId;
    }
};

struct ScalarVisitKeyHash {
    std::size_t operator()(const ScalarVisitKey& key) const {
        const std::size_t nodeHash = std::hash<int>{}(key.nodeId);
        const std::size_t socketHash = std::hash<std::string>{}(key.socketId);
        return nodeHash ^ (socketHash + 0x9e3779b9u + (nodeHash << 6) + (nodeHash >> 2));
    }
};

struct ScalarResult {
    bool visiting = false;
    bool scalar = false;
};

struct ScalarPlan {
    bool immediate = true;
    bool immediateResult = false;
    std::vector<const Link*> dependencies;
};

struct ScalarFrame {
    ScalarVisitKey key;
    ScalarPlan plan;
    std::size_t nextDependency = 0;
    bool allDependenciesScalar = true;
    bool initialized = false;
};

} // namespace

std::string Graph::ResolveSocketChannel(
    int nodeId,
    const std::string& socketId) const {
    int currentNodeId = nodeId;
    std::string currentSocketId = socketId;
    std::unordered_set<int> visited;
    visited.reserve(m_Nodes.size());

    while (true) {
        if (IsChannelSocket(currentSocketId)) {
            return currentSocketId;
        }
        if (!visited.insert(currentNodeId).second) {
            return {};
        }

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return {};
        }
        if (node->kind == NodeKind::ConstantChannel &&
            currentSocketId == kChannelOutputSocketId) {
            return node->constantChannelSettings.generatedOpaqueAlpha
                ? "a"
                : "value";
        }

        std::string upstreamSocketId;
        if ((node->kind == NodeKind::Layer ||
             node->kind == NodeKind::TechnicalImage ||
             node->kind == NodeKind::Reformat) &&
            currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::Lut &&
                   currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (
            node->kind == NodeKind::RawDetailFusion &&
            (currentSocketId == kImageOutputSocketId ||
             currentSocketId == kMaskOutputSocketId)) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::HdrMerge &&
                   currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kHdrMergeInput1SocketId;
        } else if (node->kind == NodeKind::Mfsr &&
                   currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kMfsrReferenceInputSocketId;
        } else if (node->kind == NodeKind::RawDetailAutoMask &&
                   currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::MaskUtility &&
                   currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kMaskUtilityInputSocketId;
        } else if (node->kind == NodeKind::MaskCombine &&
                   currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kMaskCombineInputASocketId;
        } else if (node->kind == NodeKind::ImageToMask &&
                   currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kImageToMaskInputSocketId;
        } else if (node->kind == NodeKind::DataMath &&
                   currentSocketId == kImageOutputSocketId) {
            if (node->dataMathMode == DataMathMode::Average) {
                return {};
            }
            for (int inputIndex = 0;
                 inputIndex < kMaxDataMathInputCount;
                 ++inputIndex) {
                const std::string inputSocketId =
                    DataMathInputSocketId(inputIndex);
                if (FindAnyInputLink(currentNodeId, inputSocketId)) {
                    upstreamSocketId = inputSocketId;
                    break;
                }
            }
        } else if (node->kind == NodeKind::FrequencyFilter &&
                   currentSocketId == kChannelOutputSocketId) {
            upstreamSocketId = kChannelInputSocketId;
        } else if (node->kind == NodeKind::FrequencyIfft &&
                   currentSocketId == kChannelOutputSocketId) {
            upstreamSocketId = kSpectrumInputSocketId;
        } else if (node->kind == NodeKind::FrequencyFft &&
                   currentSocketId == kSpectrumOutputSocketId) {
            upstreamSocketId = kChannelInputSocketId;
        } else if (node->kind == NodeKind::ApplyFrequencyResponse &&
                   currentSocketId == kSpectrumOutputSocketId) {
            upstreamSocketId = kSpectrumInputSocketId;
        } else if (node->kind == NodeKind::CombineSpectra &&
                   currentSocketId == kSpectrumOutputSocketId) {
            upstreamSocketId = kSpectrumInputASocketId;
        } else if (
            node->kind == NodeKind::SpectrumSeparate &&
            (currentSocketId == kSpectrumMagnitudeOutputSocketId ||
             currentSocketId == kSpectrumPhaseOutputSocketId)) {
            upstreamSocketId = kSpectrumInputSocketId;
        } else if (node->kind == NodeKind::SpectrumRecombine &&
                   currentSocketId == kSpectrumOutputSocketId) {
            upstreamSocketId = kSpectrumMagnitudeInputSocketId;
        }

        if (upstreamSocketId.empty()) {
            return {};
        }
        const Link* upstream =
            FindAnyInputLink(currentNodeId, upstreamSocketId);
        if (!upstream) {
            return {};
        }
        currentNodeId = upstream->fromNodeId;
        currentSocketId = upstream->fromSocketId;
    }
}

bool Graph::IsScalarSocketStream(
    int nodeId,
    const std::string& socketId) const {
    const auto makePlan = [&](const ScalarVisitKey& key) {
        ScalarPlan plan;
        const Node* node = FindNode(key.nodeId);
        if (!node) {
            return plan;
        }

        SocketDefinition currentSocket;
        const bool hasCurrentSocket =
            FindSocket(key.nodeId, key.socketId, &currentSocket);
        if (hasCurrentSocket &&
            currentSocket.direction == SocketDirection::Output &&
            currentSocket.type == SocketType::Channel) {
            plan.immediateResult = true;
            return plan;
        }
        if (node->kind == NodeKind::Compound &&
            hasCurrentSocket &&
            currentSocket.direction == SocketDirection::Output &&
            (currentSocket.type == SocketType::Mask ||
             currentSocket.type == SocketType::ScalarField)) {
            plan.immediateResult = true;
            return plan;
        }
        if (IsChannelSocket(key.socketId) ||
            key.socketId == kMaskOutputSocketId) {
            plan.immediateResult = true;
            return plan;
        }

        const auto requireInput = [&](const std::string& inputSocketId) {
            const Link* upstream =
                FindAnyInputLink(key.nodeId, inputSocketId);
            if (!upstream) {
                return false;
            }
            plan.dependencies.push_back(upstream);
            return true;
        };
        const auto optionalInput = [&](const std::string& inputSocketId) {
            if (const Link* upstream =
                    FindAnyInputLink(key.nodeId, inputSocketId)) {
                plan.dependencies.push_back(upstream);
                return true;
            }
            return false;
        };

        switch (node->kind) {
            case NodeKind::Layer:
            case NodeKind::Lut:
            case NodeKind::TechnicalImage:
            case NodeKind::Reformat:
                if (key.socketId == kImageOutputSocketId &&
                    requireInput(kImageInputSocketId)) {
                    plan.immediate = false;
                }
                break;
            case NodeKind::RawDetailFusion:
                if (key.socketId == kImageOutputSocketId &&
                    requireInput(kImageInputSocketId)) {
                    plan.immediate = false;
                }
                break;
            case NodeKind::Mix:
                if (key.socketId == kImageOutputSocketId) {
                    const bool hasImageInput =
                        optionalInput(kMixInputASocketId) |
                        optionalInput(kMixInputBSocketId);
                    if (hasImageInput) {
                        plan.immediate = false;
                    }
                }
                break;
            case NodeKind::DataMath:
                if (key.socketId == kImageOutputSocketId) {
                    if (node->dataMathMode == DataMathMode::Average) {
                        for (int inputIndex = 0;
                             inputIndex < kMaxDataMathInputCount;
                             ++inputIndex) {
                            if (FindAnyInputLink(
                                    key.nodeId,
                                    DataMathInputSocketId(inputIndex))) {
                                plan.immediateResult = true;
                                break;
                            }
                        }
                        break;
                    }
                    if (node->dataMathMode == DataMathMode::ImageAverage) {
                        break;
                    }

                    bool hasImageInput = false;
                    for (int inputIndex = 0;
                         inputIndex < kMaxDataMathInputCount;
                         ++inputIndex) {
                        hasImageInput =
                            optionalInput(DataMathInputSocketId(inputIndex)) ||
                            hasImageInput;
                    }
                    optionalInput(kDataMathBaseInputSocketId);
                    optionalInput(kMaskInputSocketId);
                    if (hasImageInput) {
                        plan.immediate = false;
                    }
                }
                break;
            default:
                break;
        }
        return plan;
    };

    const ScalarVisitKey rootKey{ nodeId, socketId };
    std::unordered_map<ScalarVisitKey, ScalarResult, ScalarVisitKeyHash> results;
    results.reserve(m_Nodes.size());
    std::vector<ScalarFrame> pending;
    pending.push_back(ScalarFrame{ rootKey });

    while (!pending.empty()) {
        ScalarFrame& frame = pending.back();
        if (!frame.initialized) {
            const auto existing = results.find(frame.key);
            if (existing != results.end()) {
                pending.pop_back();
                continue;
            }
            results.emplace(frame.key, ScalarResult{ true, false });
            frame.plan = makePlan(frame.key);
            frame.initialized = true;
            if (frame.plan.immediate) {
                results[frame.key] =
                    ScalarResult{ false, frame.plan.immediateResult };
                pending.pop_back();
                continue;
            }
        }

        if (frame.nextDependency < frame.plan.dependencies.size()) {
            const Link& dependency =
                *frame.plan.dependencies[frame.nextDependency];
            const ScalarVisitKey dependencyKey{
                dependency.fromNodeId,
                dependency.fromSocketId
            };
            const auto dependencyResult = results.find(dependencyKey);
            if (dependencyResult == results.end()) {
                pending.push_back(ScalarFrame{ dependencyKey });
                continue;
            }
            frame.allDependenciesScalar =
                frame.allDependenciesScalar &&
                !dependencyResult->second.visiting &&
                dependencyResult->second.scalar;
            ++frame.nextDependency;
            continue;
        }

        results[frame.key] =
            ScalarResult{ false, frame.allDependenciesScalar };
        pending.pop_back();
    }

    const auto resolved = results.find(rootKey);
    return resolved != results.end() &&
        !resolved->second.visiting &&
        resolved->second.scalar;
}

} // namespace EditorNodeGraph
