#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EditorNodeGraph {
namespace {

struct SocketVisitKey {
    int nodeId = -1;
    std::string socketId;

    bool operator==(const SocketVisitKey& other) const {
        return nodeId == other.nodeId && socketId == other.socketId;
    }
};

struct SocketVisitKeyHash {
    std::size_t operator()(const SocketVisitKey& key) const {
        const std::size_t nodeHash = std::hash<int>{}(key.nodeId);
        const std::size_t socketHash = std::hash<std::string>{}(key.socketId);
        return nodeHash ^ (socketHash + 0x9e3779b9u + (nodeHash << 6) + (nodeHash >> 2));
    }
};

enum class VisitState {
    Visiting,
    Valid,
    Invalid
};

struct NodeVisitPlan {
    bool valid = false;
    bool source = false;
    std::vector<const Link*> dependencies;
};

struct VisitFrame {
    SocketVisitKey key;
    NodeVisitPlan plan;
    std::size_t nextDependency = 0;
    bool initialized = false;
};

} // namespace

const std::vector<CompletedChainInfo>& Graph::GetCompletedChains() const {
    if (m_CompletedChainsCacheRevision == m_StructureRevision) {
        return m_CompletedChainsCache;
    }

    std::unordered_map<int, const Node*> nodes;
    nodes.reserve(m_Nodes.size());
    for (const Node& node : m_Nodes) {
        nodes.emplace(node.id, &node);
    }

    std::unordered_map<SocketVisitKey, const Link*, SocketVisitKeyHash> inputLinks;
    inputLinks.reserve(m_Links.size());
    std::unordered_map<int, std::vector<const Link*>>
        renderInputsByNode;
    renderInputsByNode.reserve(m_Nodes.size());
    for (const Link& link : m_Links) {
        if (IsRenderLink(link)) {
            inputLinks.emplace(
                SocketVisitKey{ link.toNodeId, link.toSocketId },
                &link);
            renderInputsByNode[link.toNodeId].push_back(&link);
        }
    }

    const auto findNode = [&](int nodeId) -> const Node* {
        const auto found = nodes.find(nodeId);
        return found != nodes.end() ? found->second : nullptr;
    };
    const auto findInput = [&](int nodeId, const std::string& socketId) -> const Link* {
        const auto found = inputLinks.find(SocketVisitKey{ nodeId, socketId });
        return found != inputLinks.end() ? found->second : nullptr;
    };

    std::vector<CompletedChainInfo> chains;
    std::string outputConnectionDiagnostic;

    const auto rejectUnresolvedDefinition = [&](const Node& node) {
        if (node.definitionResolved) {
            return false;
        }
        if (outputConnectionDiagnostic.empty()) {
            outputConnectionDiagnostic =
                "Node " + std::to_string(node.id) +
                " cannot render because its exact definition is unresolved";
            if (!node.definitionResolutionError.empty()) {
                outputConnectionDiagnostic +=
                    ": " + node.definitionResolutionError;
            } else {
                outputConnectionDiagnostic += ".";
            }
        }
        return true;
    };

    const auto collectChain = [&](int outputNodeId) {
        CompletedChainInfo chain;
        chain.outputNodeId = outputNodeId;
        const Node* outputNode = findNode(outputNodeId);
        if (!outputNode || rejectUnresolvedDefinition(*outputNode)) {
            return chain;
        }

        const Link* outputInput = findInput(outputNodeId, kImageInputSocketId);
        if (!outputInput) {
            return chain;
        }

        std::unordered_set<int> added;
        added.reserve(m_Nodes.size());
        std::unordered_map<SocketVisitKey, VisitState, SocketVisitKeyHash> states;
        states.reserve(m_Nodes.size());

        const auto makePlan = [&](const SocketVisitKey& key) {
            NodeVisitPlan plan;
            const Node* node = findNode(key.nodeId);
            if (!node) {
                return plan;
            }
            if (rejectUnresolvedDefinition(*node)) {
                return plan;
            }

            if (added.insert(key.nodeId).second) {
                chain.nodeIds.push_back(key.nodeId);
            }

            const auto requireInput = [&](const std::string& socketId) {
                const Link* input = findInput(key.nodeId, socketId);
                if (!input) {
                    plan.valid = false;
                    return false;
                }
                plan.dependencies.push_back(input);
                return true;
            };
            const auto optionalInput = [&](const std::string& socketId) {
                if (const Link* input = findInput(key.nodeId, socketId)) {
                    plan.dependencies.push_back(input);
                }
            };

            switch (node->kind) {
                case NodeKind::Image:
                case NodeKind::RawDevelopment:
                case NodeKind::RawSource:
                case NodeKind::ImageGenerator:
                case NodeKind::MaskGenerator:
                case NodeKind::CustomMask:
                case NodeKind::FrequencyMask:
                    plan.valid = true;
                    plan.source = true;
                    break;
                case NodeKind::RawDecode:
                case NodeKind::RawDevelop:
                case NodeKind::RawNeuralDenoise:
                    plan.valid = requireInput(kRawInputSocketId);
                    break;
                case NodeKind::Layer:
                case NodeKind::TechnicalImage:
                case NodeKind::Reformat:
                case NodeKind::RawDetailFusion:
                case NodeKind::RawDetailAutoMask:
                case NodeKind::ChannelSplit:
                    plan.valid = requireInput(kImageInputSocketId);
                    break;
                case NodeKind::RawOperation:
                    if (key.socketId == "measurementImageOut" && findInput(key.nodeId, "referenceIn"))
                        plan.valid = requireInput("referenceIn");
                    else plan.valid = requireInput(kImageInputSocketId);
                    break;
                case NodeKind::Compound: {
                    std::vector<std::string> dependencies;
                    std::string dependencyError;
                    plan.valid = ResolveCompoundOutputInputDependencies(
                        key.nodeId,
                        key.socketId,
                        dependencies,
                        &dependencyError);
                    if (!plan.valid &&
                        !dependencyError.empty() &&
                        outputConnectionDiagnostic.empty()) {
                        outputConnectionDiagnostic =
                            "Compound node " + std::to_string(key.nodeId) +
                            " cannot render: " + dependencyError;
                    }

                    std::unordered_set<std::string> validatedInputs;
                    for (const std::string& inputSocketId : dependencies) {
                        if (!plan.valid) {
                            break;
                        }
                        SocketDefinition inputSocket;
                        if (!FindSocket(key.nodeId, inputSocketId, &inputSocket) ||
                            inputSocket.direction != SocketDirection::Input) {
                            plan.valid = false;
                            break;
                        }
                        const Link* upstream = findInput(key.nodeId, inputSocketId);
                        if (!upstream) {
                            if (!inputSocket.optional) {
                                plan.valid = false;
                            }
                        } else {
                            plan.dependencies.push_back(upstream);
                        }
                        validatedInputs.insert(inputSocketId);
                    }
                    if (plan.valid) {
                        for (const SocketDefinition& inputSocket :
                             GetSockets(*node, false)) {
                            if (inputSocket.direction != SocketDirection::Input ||
                                !inputSocket.optional ||
                                validatedInputs.count(inputSocket.id) != 0) {
                                continue;
                            }
                            optionalInput(inputSocket.id);
                        }
                    }
                    break;
                }
                case NodeKind::Lut: {
                    if (const Link* image = findInput(key.nodeId, kImageInputSocketId)) {
                        plan.dependencies.push_back(image);
                    } else {
                        optionalInput("r");
                        optionalInput("g");
                        optionalInput("b");
                        optionalInput("a");
                    }
                    plan.valid = !plan.dependencies.empty();
                    break;
                }
                case NodeKind::HdrMerge:
                    plan.valid =
                        requireInput(kHdrMergeInput1SocketId) &&
                        requireInput(kHdrMergeInput2SocketId);
                    if (plan.valid) {
                        optionalInput(kHdrMergeInput3SocketId);
                    }
                    break;
                case NodeKind::Mfsr:
                    plan.valid =
                        requireInput(kMfsrReferenceInputSocketId) &&
                        requireInput(MfsrInputSocketId(1));
                    if (plan.valid) {
                        for (int inputIndex = 2;
                             inputIndex < kMaxMfsrInputCount;
                             ++inputIndex) {
                            optionalInput(MfsrInputSocketId(inputIndex));
                        }
                    }
                    break;
                case NodeKind::MultiFrameDenoise:
                    // An unprocessed MFD project is a normal waiting state,
                    // not a graph-definition error. Once the in-memory MFD
                    // result is adopted, graph snapshot construction supplies
                    // its developed RAW source.
                    plan.valid = node->multiFrameDenoise.resultState == "ready";
                    plan.source = plan.valid;
                    break;
                case NodeKind::MultiFrameHdr:
                    // Like MFD, the managed RAW inputs describe provenance;
                    // the adopted virtual Bayer mosaic is the executable
                    // source once processing has published it.
                    plan.valid = node->multiFrameHdr.resultState == "ready";
                    plan.source = plan.valid;
                    break;
                case NodeKind::RawProjectSourceSet:
                    plan.valid = false;
                    if (outputConnectionDiagnostic.empty()) {
                        outputConnectionDiagnostic =
                            "RAW Project Source Set result unavailable: processing is not implemented yet.";
                    }
                    break;
                case NodeKind::Mix:
                    plan.valid =
                        requireInput(kMixInputASocketId) &&
                        requireInput(kMixInputBSocketId);
                    break;
                case NodeKind::DataMath: {
                    int inputCount = 0;
                    for (int inputIndex = 0;
                         inputIndex < kMaxDataMathInputCount;
                         ++inputIndex) {
                        const Link* input =
                            findInput(key.nodeId, DataMathInputSocketId(inputIndex));
                        if (input) {
                            plan.dependencies.push_back(input);
                            ++inputCount;
                        }
                    }
                    plan.valid = node->dataMathMode == DataMathMode::ImageAverage
                        ? inputCount >= 2
                        : inputCount >= 1;
                    break;
                }
                case NodeKind::FrequencyFilter:
                    plan.valid = requireInput(kChannelInputSocketId);
                    if (plan.valid) {
                        optionalInput(kFrequencyResponseInputSocketId);
                    }
                    break;
                case NodeKind::FrequencyResponse:
                    plan.valid = true;
                    break;
                case NodeKind::FrequencyFft:
                    plan.valid = requireInput(kChannelInputSocketId);
                    break;
                case NodeKind::FrequencyIfft:
                case NodeKind::SpectrumView:
                case NodeKind::SpectrumSeparate:
                case NodeKind::SpectrumAnalyzer:
                    plan.valid = requireInput(kSpectrumInputSocketId);
                    break;
                case NodeKind::ApplyFrequencyResponse:
                    plan.valid =
                        requireInput(kSpectrumInputSocketId) &&
                        requireInput(kFrequencyResponseInputSocketId);
                    break;
                case NodeKind::CombineSpectra:
                    plan.valid =
                        requireInput(kSpectrumInputASocketId) &&
                        requireInput(kSpectrumInputBSocketId);
                    break;
                case NodeKind::SpectrumRecombine:
                    plan.valid =
                        requireInput(kSpectrumMagnitudeInputSocketId) &&
                        requireInput(kSpectrumPhaseInputSocketId);
                    break;
                case NodeKind::SpectrumMath:
                    plan.valid = requireInput(kMixInputASocketId);
                    if (plan.valid) {
                        optionalInput(kMixInputBSocketId);
                        optionalInput(kMaskInputSocketId);
                    }
                    break;
                case NodeKind::MagnitudePhase:
                    if (node->magnitudePhaseMode == MagnitudePhaseMode::Recombine) {
                        plan.valid =
                            requireInput("magnitude") &&
                            requireInput("phase");
                    } else {
                        plan.valid = requireInput(kImageInputSocketId);
                    }
                    break;
                case NodeKind::ChannelCombine:
                    optionalInput("r");
                    optionalInput("g");
                    optionalInput("b");
                    optionalInput("a");
                    plan.valid = !plan.dependencies.empty();
                    break;
                case NodeKind::ConstantChannel:
                    plan.valid =
                        requireInput(kMatchExtentInputSocketId);
                    break;
                case NodeKind::ImageToMask:
                    plan.valid = requireInput(kImageToMaskInputSocketId);
                    break;
                case NodeKind::MaskCombine:
                    plan.valid =
                        requireInput(kMaskCombineInputASocketId) &&
                        requireInput(kMaskCombineInputBSocketId);
                    break;
                case NodeKind::MaskUtility:
                    plan.valid = requireInput(kMaskUtilityInputSocketId);
                    break;
                case NodeKind::FieldMean:
                    plan.valid =
                        requireInput(kReductionFieldInputSocketId);
                    break;
                case NodeKind::Value:
                    plan.valid =
                        node->value.value.availability ==
                            Stack::NodeMath::ValueAvailability::Known &&
                        Stack::NodeMath::ValidateFirstClassValue(
                            node->value.value).empty();
                    if (!plan.valid &&
                        outputConnectionDiagnostic.empty()) {
                        outputConnectionDiagnostic =
                            "Value node " + std::to_string(node->id) +
                            " cannot render because its typed value is "
                            "unknown, missing, failed, or invalid.";
                    }
                    plan.source = plan.valid;
                    break;
                case NodeKind::Output:
                case NodeKind::Composite:
                case NodeKind::Scope:
                case NodeKind::Preview:
                    plan.valid = false;
                    break;
            }

            // Required-port checks establish whether the node can execute at
            // all. Every additional authored render input still influences
            // the result and must participate in exact-definition, cycle,
            // and dependency validation. Otherwise an unresolved mask or
            // dynamic scalar branch can disappear from the render snapshot
            // and silently turn a conditional edit into a global one.
            const bool adoptedMultiFrameSource =
                (node->kind == NodeKind::MultiFrameDenoise ||
                 node->kind == NodeKind::MultiFrameHdr) && plan.source;
            if (plan.valid && !adoptedMultiFrameSource) {
                const auto authoredInputs =
                    renderInputsByNode.find(key.nodeId);
                if (authoredInputs != renderInputsByNode.end()) {
                    for (const Link* input :
                         authoredInputs->second) {
                        if (!EditorNodeGraphDefinitions::OutputDependsOnInput(*this, *node, key.socketId, input->toSocketId)) continue;
                        const bool coverage = (input->toSocketId == kMaskInputSocketId &&
                            (node->kind == NodeKind::RawOperation || node->kind == NodeKind::Layer || node->kind == NodeKind::Lut || node->kind == NodeKind::RawDevelop)) ||
                            (node->kind == NodeKind::Mix && input->toSocketId == kMixFactorSocketId) ||
                            (node->kind == NodeKind::RawOperation && (input->toSocketId.rfind("area:", 0) == 0 || input->toSocketId.rfind("gradient:", 0) == 0));
                        // Attached unfinished coverage is empty. Its branch remains
                        // authored and participates in document cycle analysis.
                        if (coverage) continue;
                        if (std::find(
                                plan.dependencies.begin(),
                                plan.dependencies.end(),
                                input) == plan.dependencies.end()) {
                            plan.dependencies.push_back(input);
                        }
                    }
                }
            }
            return plan;
        };

        const auto validate = [&](int rootNodeId, const std::string& rootSocketId) {
            const SocketVisitKey rootKey{ rootNodeId, rootSocketId };
            std::vector<VisitFrame> pending;
            pending.push_back(VisitFrame{ rootKey });

            while (!pending.empty()) {
                VisitFrame& frame = pending.back();
                if (!frame.initialized) {
                    const auto existing = states.find(frame.key);
                    if (existing != states.end()) {
                        pending.pop_back();
                        continue;
                    }
                    states.emplace(frame.key, VisitState::Visiting);
                    frame.plan = makePlan(frame.key);
                    frame.initialized = true;
                    if (frame.plan.source && chain.sourceNodeId <= 0) {
                        chain.sourceNodeId = frame.key.nodeId;
                    }
                    if (!frame.plan.valid) {
                        states[frame.key] = VisitState::Invalid;
                        pending.pop_back();
                        continue;
                    }
                }

                if (frame.nextDependency >= frame.plan.dependencies.size()) {
                    states[frame.key] = VisitState::Valid;
                    pending.pop_back();
                    continue;
                }

                const Link& dependency =
                    *frame.plan.dependencies[frame.nextDependency];
                const SocketVisitKey dependencyKey{
                    dependency.fromNodeId,
                    dependency.fromSocketId
                };
                const auto dependencyState = states.find(dependencyKey);
                if (dependencyState == states.end()) {
                    pending.push_back(VisitFrame{ dependencyKey });
                    continue;
                }
                if (dependencyState->second == VisitState::Valid) {
                    ++frame.nextDependency;
                    continue;
                }

                states[frame.key] = VisitState::Invalid;
                pending.pop_back();
            }

            const auto result = states.find(rootKey);
            return result != states.end() && result->second == VisitState::Valid;
        };

        chain.terminalNodeId = outputInput->fromNodeId;
        if (!validate(outputInput->fromNodeId, outputInput->fromSocketId)) {
            chain.terminalNodeId = -1;
            chain.sourceNodeId = -1;
            chain.nodeIds.clear();
        }

        if (chain.terminalNodeId > 0) {
            const int referenceSourceNodeId =
                ResolveReferenceSourceNodeIdForOutput(outputNodeId);
            if (referenceSourceNodeId > 0) {
                chain.sourceNodeId = referenceSourceNodeId;
            }
        }
        return chain;
    };

    for (const Node& node : m_Nodes) {
        if (node.kind != NodeKind::Output || !node.outputEnabled) {
            continue;
        }
        CompletedChainInfo chain = collectChain(node.id);
        if (chain.outputNodeId > 0 &&
            chain.terminalNodeId > 0 &&
            !chain.nodeIds.empty()) {
            chains.push_back(std::move(chain));
        }
    }

    m_CompletedChainsCache = std::move(chains);
    m_OutputConnectionDiagnosticCache = std::move(outputConnectionDiagnostic);
    m_CompletedChainsCacheRevision = m_StructureRevision;
    return m_CompletedChainsCache;
}

} // namespace EditorNodeGraph
