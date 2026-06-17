#include "EditorNodeGraph.h"
#include "EditorNodeGraphDefinitions.h"

#include <algorithm>
#include <functional>

namespace EditorNodeGraph {
namespace {

bool IsChannelSocketId(const std::string& socketId) {
    return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
}

bool SupportsDynamicChannelInputs(const EditorNodeGraph::Node& node) {
    return node.kind == EditorNodeGraph::NodeKind::Output ||
        node.kind == EditorNodeGraph::NodeKind::Lut;
}

const char* ChannelLabel(const std::string& socketId) {
    if (socketId == "r") return "R";
    if (socketId == "g") return "G";
    if (socketId == "b") return "B";
    if (socketId == "a") return "A";
    return "";
}

Link MakeSocketLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    Link link;
    link.fromNodeId = fromNodeId;
    link.fromSocketId = fromSocketId;
    link.toNodeId = toNodeId;
    link.toSocketId = toSocketId;
    return link;
}

} // namespace

void Graph::Clear() {
    m_Nodes.clear();
    m_Links.clear();
    m_Groups.clear();
    m_NextNodeId = 1;
    m_NextGroupId = 1;
    m_SelectedNodeId = -1;
    m_SelectedNodeIds.clear();
    m_SelectedLink = {};
    m_HasSelectedLink = false;
    m_ActiveImageNodeId = -1;
    m_OutputNodeId = -1;
    m_ForceOutputFourPins = false;
    TouchStructure();
}

void Graph::ResetFromLayers(int layerCount, bool hasActiveImage) {
    Clear();

    if (hasActiveImage) {
        ImagePayload image;
        image.label = "Image";
        Node* imageNode = AddImageNode(std::move(image), Vec2{ 20.0f, 120.0f });
        m_ActiveImageNodeId = imageNode ? imageNode->id : -1;
    }

    for (int i = 0; i < layerCount; ++i) {
        Node node;
        node.id = AllocateNodeId();
        node.kind = NodeKind::Layer;
        node.layerIndex = i;
        node.title = "Layer";
        node.position = DefaultLayerPosition(i);
        m_Nodes.push_back(std::move(node));
    }

    RebuildLinks();
}

void Graph::SyncLayerNodes(int layerCount) {
    const std::size_t oldNodeCount = m_Nodes.size();
    bool structureChanged = false;
    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [layerCount](const Node& node) {
            return node.kind == NodeKind::Layer && (node.layerIndex < 0 || node.layerIndex >= layerCount);
        }),
        m_Nodes.end());
    structureChanged = oldNodeCount != m_Nodes.size();

    for (int i = 0; i < layerCount; ++i) {
        if (FindNodeByLayerIndex(i)) {
            continue;
        }

        Node node;
        node.id = AllocateNodeId();
        node.kind = NodeKind::Layer;
        node.layerIndex = i;
        node.title = "Layer";
        node.position = DefaultLayerPosition(i);
        m_Nodes.push_back(std::move(node));
        structureChanged = true;
    }

    const std::size_t oldLinkCount = m_Links.size();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return !FindNode(link.fromNodeId) ||
                !FindNode(link.toNodeId) ||
                !FindSocket(link.fromNodeId, link.fromSocketId) ||
                !FindSocket(link.toNodeId, link.toSocketId);
        }),
        m_Links.end());
    structureChanged = structureChanged || oldLinkCount != m_Links.size();
    m_SelectedNodeIds.erase(
        std::remove_if(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), [this](int nodeId) {
            return FindNode(nodeId) == nullptr;
        }),
        m_SelectedNodeIds.end());
    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
    if (m_HasSelectedLink && !HasLink(m_SelectedLink.fromNodeId, m_SelectedLink.fromSocketId, m_SelectedLink.toNodeId, m_SelectedLink.toSocketId)) {
        ClearSelectedLink();
    }
    if (m_ActiveImageNodeId > 0 && !FindNode(m_ActiveImageNodeId)) {
        m_ActiveImageNodeId = -1;
    }
    if (structureChanged) {
        TouchStructure();
    }
    Validate();
}

Node* Graph::AddImageNode(ImagePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Image;
    node.position = position;
    node.image = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawSourceNode(RawSourcePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawSource;
    node.position = position;
    node.rawSource = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawNeuralDenoiseNode(RawNeuralDenoisePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawNeuralDenoise;
    node.position = position;
    node.rawNeuralDenoise = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDecodeNode(RawDecodePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDecode;
    node.position = position;
    node.rawDecode = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDevelopNode(RawDevelopPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDevelop;
    node.position = position;
    node.rawDevelop = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDetailAutoMaskNode(RawDetailAutoMaskPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDetailAutoMask;
    node.position = position;
    node.rawDetailAutoMask = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddRawDetailFusionNode(RawDetailFusionPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::RawDetailFusion;
    node.position = position;
    node.rawDetailFusion = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddHdrMergeNode(HdrMergePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::HdrMerge;
    node.position = position;
    node.hdrMerge = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddLutNode(LutPayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Lut;
    node.position = position;
    node.lut = std::move(payload);
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddLayerNode(LayerType type, int layerIndex, Vec2 position) {
    const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(type);

    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Layer;
    node.layerType = type;
    node.layerIndex = layerIndex;
    node.typeId = descriptor ? descriptor->typeId : "";
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddScopeNode(ScopeKind scopeKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Scope;
    node.scopeKind = scopeKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMaskGeneratorNode(MaskGeneratorKind maskKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskGenerator;
    node.maskKind = maskKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMaskCombineNode(MaskCombineMode combineMode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskCombine;
    node.maskCombineMode = combineMode;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMaskUtilityNode(MaskUtilityKind utilityKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskUtility;
    node.maskUtilityKind = utilityKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddCustomMaskNode(CustomMaskPayload payload, Vec2 position) {
    if (payload.width <= 0) payload.width = 1024;
    if (payload.height <= 0) payload.height = 1024;
    const std::size_t expected =
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height);
    if (payload.rasterLayer.size() != expected) {
        payload.rasterLayer.assign(expected, 0.0f);
    }

    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::CustomMask;
    node.customMask = std::move(payload);
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddImageToMaskNode(ImageToMaskKind converterKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ImageToMask;
    node.imageToMaskKind = converterKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddImageGeneratorNode(ImageGeneratorKind generatorKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ImageGenerator;
    node.imageGeneratorKind = generatorKind;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddMixNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Mix;
    node.position = position;
    node.mixBlendMode = MixBlendMode::Normal;
    node.mixFactor = 0.5f;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddDataMathNode(DataMathMode mode, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::DataMath;
    node.position = position;
    node.dataMathMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddPreviewNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Preview;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddChannelSplitNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ChannelSplit;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddChannelCombineNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ChannelCombine;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddOutputNode(Vec2 position, bool makePrimary) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Output;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    if (makePrimary || m_OutputNodeId <= 0) {
        m_OutputNodeId = m_Nodes.back().id;
    }
    TouchStructure();
    return &m_Nodes.back();
}

Node* Graph::AddCompositeNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Composite;
    node.position = position;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
    m_Nodes.push_back(std::move(node));
    TouchStructure();
    return &m_Nodes.back();
}


Node* Graph::EnsureOutputNode() {
    if (Node* existing = FindNode(m_OutputNodeId)) {
        return existing;
    }

    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Output) {
            m_OutputNodeId = node.id;
            return &node;
        }
    }

    return AddOutputNode(Vec2{ 520.0f, 120.0f }, true);
}

void Graph::RemoveLayerNode(int layerIndex) {
    std::vector<int> removedNodeIds;
    for (const Node& node : m_Nodes) {
        if (node.kind == NodeKind::Layer && node.layerIndex == layerIndex) {
            removedNodeIds.push_back(node.id);
        }
    }

    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [layerIndex](const Node& node) {
            return node.kind == NodeKind::Layer && node.layerIndex == layerIndex;
        }),
        m_Nodes.end());

    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Layer && node.layerIndex > layerIndex) {
            --node.layerIndex;
        }
    }

    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return !FindNode(link.fromNodeId) || !FindNode(link.toNodeId);
        }),
        m_Links.end());

    for (int id : removedNodeIds) {
        m_SelectedNodeIds.erase(
            std::remove(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), id),
            m_SelectedNodeIds.end());
    }
    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
    TouchStructure();
}

Node* Graph::FindNode(int nodeId) {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](const Node& node) {
        return node.id == nodeId;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

const Node* Graph::FindNode(int nodeId) const {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](const Node& node) {
        return node.id == nodeId;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

Node* Graph::FindNodeByLayerIndex(int layerIndex) {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [layerIndex](const Node& node) {
        return node.kind == NodeKind::Layer && node.layerIndex == layerIndex;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

const Node* Graph::FindNodeByLayerIndex(int layerIndex) const {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [layerIndex](const Node& node) {
        return node.kind == NodeKind::Layer && node.layerIndex == layerIndex;
    });
    return it != m_Nodes.end() ? &(*it) : nullptr;
}

std::vector<SocketDefinition> Graph::GetSockets(const Node& node, bool visibleOnly) const {
    if (SupportsDynamicChannelInputs(node)) {
        bool useFourPins = node.kind == NodeKind::Output && m_ForceOutputFourPins;
        if (!useFourPins) {
            for (const Link& link : m_Links) {
                if (link.toNodeId == node.id &&
                    (IsChannelSocketId(link.toSocketId) ||
                     (link.toSocketId == kImageInputSocketId && !ResolveSocketChannel(link.fromNodeId, link.fromSocketId).empty()))) {
                    useFourPins = true;
                    break;
                }
            }
        }

        std::vector<SocketDefinition> sockets;
        auto add = [&](const char* id, SocketDirection direction, SocketType type, const char* label, bool optional, bool visible) {
            if (visibleOnly && !visible) {
                return;
            }
            sockets.push_back(SocketDefinition{ id, node.id, direction, type, label, optional, visible });
        };

        if (useFourPins) {
            add("r", SocketDirection::Input, SocketType::Mask, "R", true, true);
            add("g", SocketDirection::Input, SocketType::Mask, "G", true, true);
            add("b", SocketDirection::Input, SocketType::Mask, "B", true, true);
            add("a", SocketDirection::Input, SocketType::Mask, "A", true, true);
            add(kImageInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, false);
        } else {
            add(kImageInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, true);
            add("r", SocketDirection::Input, SocketType::Mask, "R", true, false);
            add("g", SocketDirection::Input, SocketType::Mask, "G", true, false);
            add("b", SocketDirection::Input, SocketType::Mask, "B", true, false);
            add("a", SocketDirection::Input, SocketType::Mask, "A", true, false);
        }
        if (node.kind == NodeKind::Lut) {
            add(kMaskInputSocketId, SocketDirection::Input, SocketType::Mask, "Mask", true, true);
            add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
        }
        return sockets;
    }

    if (node.kind == NodeKind::ChannelSplit) {
        std::vector<SocketDefinition> sockets = EditorNodeGraphDefinitions::BuildSockets(node, visibleOnly);
        bool hasAlpha = true;
        const Link* inputLink = FindAnyInputLink(node.id, kImageInputSocketId);
        if (inputLink) {
            const Node* upstreamNode = FindNode(inputLink->fromNodeId);
            if (upstreamNode && upstreamNode->kind == NodeKind::Image) {
                if (upstreamNode->image.originalChannels < 4) {
                    hasAlpha = false;
                }
            }
        }
        if (!hasAlpha) {
            for (SocketDefinition& socket : sockets) {
                if (socket.id == "a") {
                    socket.label = "A (Generated)";
                }
            }
        }
        return sockets;
    }

    return EditorNodeGraphDefinitions::BuildSockets(node, visibleOnly);
}

bool Graph::FindSocket(int nodeId, const std::string& socketId, SocketDefinition* outSocket) const {
    const Node* node = FindNode(nodeId);
    if (!node) {
        return false;
    }
    if (SupportsDynamicChannelInputs(*node) && IsChannelSocketId(socketId)) {
        if (outSocket) {
            *outSocket = SocketDefinition{
                socketId,
                node->id,
                SocketDirection::Input,
                SocketType::Mask,
                ChannelLabel(socketId),
                true,
                true
            };
        }
        return true;
    }
    for (const SocketDefinition& socket : GetSockets(*node)) {
        if (socket.id == socketId) {
            if (outSocket) {
                *outSocket = socket;
            }
            return true;
        }
    }
    return false;
}

std::string Graph::DefaultInputSocket(const Node& node) const {
    return EditorNodeGraphDefinitions::DefaultInputSocket(node);
}

std::string Graph::DefaultOutputSocket(const Node& node) const {
    return EditorNodeGraphDefinitions::DefaultOutputSocket(node);
}

std::string Graph::ResolveSocketChannel(int nodeId, const std::string& socketId) const {
    std::unordered_set<int> visited;
    std::function<std::string(int, const std::string&)> resolve = [&](int currentNodeId, const std::string& currentSocketId) -> std::string {
        if (IsChannelSocketId(currentSocketId)) {
            return currentSocketId;
        }
        if (!visited.insert(currentNodeId).second) {
            return {};
        }

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return {};
        }

        const char* upstreamSocketId = nullptr;
        if (node->kind == NodeKind::Layer && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::Lut && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::RawDetailFusion &&
                   (currentSocketId == kImageOutputSocketId || currentSocketId == kMaskOutputSocketId)) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::HdrMerge && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kHdrMergeInput1SocketId;
        } else if (node->kind == NodeKind::RawDetailAutoMask && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kImageInputSocketId;
        } else if (node->kind == NodeKind::MaskUtility && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kMaskUtilityInputSocketId;
        } else if (node->kind == NodeKind::MaskCombine && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kMaskCombineInputASocketId;
        } else if (node->kind == NodeKind::ImageToMask && currentSocketId == kMaskOutputSocketId) {
            upstreamSocketId = kImageToMaskInputSocketId;
        } else if (node->kind == NodeKind::DataMath && currentSocketId == kImageOutputSocketId) {
            upstreamSocketId = kMixInputASocketId;
        }

        if (!upstreamSocketId) {
            return {};
        }
        const Link* upstream = FindAnyInputLink(currentNodeId, upstreamSocketId);
        return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : std::string();
    };
    return resolve(nodeId, socketId);
}

bool Graph::IsScalarSocketStream(int nodeId, const std::string& socketId) const {
    std::unordered_set<std::string> visiting;
    std::function<bool(int, const std::string&)> resolve = [&](int currentNodeId, const std::string& currentSocketId) -> bool {
        const std::string key = std::to_string(currentNodeId) + ":" + currentSocketId;
        if (!visiting.insert(key).second) {
            return false;
        }

        auto finish = [&](bool result) {
            visiting.erase(key);
            return result;
        };

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return finish(false);
        }
        if (IsChannelSocketId(currentSocketId) || currentSocketId == kMaskOutputSocketId) {
            return finish(true);
        }

        auto inputIsScalar = [&](const char* inputSocketId) {
            const Link* upstream = FindAnyInputLink(currentNodeId, inputSocketId);
            return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : false;
        };

        switch (node->kind) {
            case NodeKind::Layer:
            case NodeKind::Lut:
                if (currentSocketId == kImageOutputSocketId) {
                    return finish(inputIsScalar(kImageInputSocketId));
                }
                break;
            case NodeKind::RawDetailFusion:
                if (currentSocketId == kImageOutputSocketId) {
                    return finish(inputIsScalar(kImageInputSocketId));
                }
                if (currentSocketId == kMaskOutputSocketId) {
                    return finish(true);
                }
                break;
            case NodeKind::RawDetailAutoMask:
            case NodeKind::MaskGenerator:
            case NodeKind::MaskCombine:
            case NodeKind::MaskUtility:
            case NodeKind::CustomMask:
            case NodeKind::ImageToMask:
                if (currentSocketId == kMaskOutputSocketId) {
                    return finish(true);
                }
                break;
            case NodeKind::Mix:
            case NodeKind::DataMath:
                if (currentSocketId == kImageOutputSocketId) {
                    const Link* inputA = FindAnyInputLink(currentNodeId, kMixInputASocketId);
                    const Link* inputB = FindAnyInputLink(currentNodeId, kMixInputBSocketId);
                    const bool scalarA = inputA && resolve(inputA->fromNodeId, inputA->fromSocketId);
                    const bool scalarB = inputB && resolve(inputB->fromNodeId, inputB->fromSocketId);
                    const bool hasA = inputA != nullptr;
                    const bool hasB = inputB != nullptr;
                    return finish((hasA || hasB) && (!hasA || scalarA) && (!hasB || scalarB));
                }
                break;
            case NodeKind::ChannelSplit:
                if (IsChannelSocketId(currentSocketId)) {
                    return finish(true);
                }
                break;
            default:
                break;
        }

        return finish(false);
    };
    return resolve(nodeId, socketId);
}

int Graph::ResolveReferenceSourceNodeId(int nodeId, const std::string& socketId) const {
    std::unordered_set<int> visited;
    std::function<int(int, const std::string&)> resolve = [&](int currentNodeId, const std::string& currentSocketId) -> int {
        if (!visited.insert(currentNodeId).second) {
            return -1;
        }

        const Node* node = FindNode(currentNodeId);
        if (!node) {
            return -1;
        }

        switch (node->kind) {
            case NodeKind::Image:
            case NodeKind::RawSource:
            case NodeKind::ImageGenerator:
            case NodeKind::MaskGenerator:
            case NodeKind::CustomMask:
                return currentNodeId;
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
                return currentNodeId;
            case NodeKind::RawNeuralDenoise: {
                const Link* upstream = FindInputLink(currentNodeId, kRawInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Layer: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Lut: {
                if (const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId)) {
                    return resolve(upstream->fromNodeId, upstream->fromSocketId);
                }
                for (const char* channelSocketId : { "r", "g", "b", "a" }) {
                    if (const Link* channelInput = FindInputLink(currentNodeId, channelSocketId)) {
                        const int sourceId = resolve(channelInput->fromNodeId, channelInput->fromSocketId);
                        if (sourceId > 0) {
                            return sourceId;
                        }
                    }
                }
                return -1;
            }
            case NodeKind::RawDetailFusion: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::HdrMerge: {
                const char* preferredSocketId = kHdrMergeInput1SocketId;
                if (currentSocketId == kHdrMergeInput2SocketId) {
                    preferredSocketId = kHdrMergeInput2SocketId;
                } else if (currentSocketId == kHdrMergeInput3SocketId) {
                    preferredSocketId = kHdrMergeInput3SocketId;
                }
                const Link* upstream = FindInputLink(currentNodeId, preferredSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::RawDetailAutoMask: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Mix: {
                const Link* inputA = FindInputLink(currentNodeId, kMixInputASocketId);
                if (inputA) {
                    const int sourceId = resolve(inputA->fromNodeId, inputA->fromSocketId);
                    if (sourceId > 0) return sourceId;
                }
                const Link* inputB = FindInputLink(currentNodeId, kMixInputBSocketId);
                return inputB ? resolve(inputB->fromNodeId, inputB->fromSocketId) : -1;
            }
            case NodeKind::DataMath: {
                const Link* inputA = FindInputLink(currentNodeId, kMixInputASocketId);
                if (inputA) {
                    const int sourceId = resolve(inputA->fromNodeId, inputA->fromSocketId);
                    if (sourceId > 0) return sourceId;
                }
                const Link* inputB = FindInputLink(currentNodeId, kMixInputBSocketId);
                return inputB ? resolve(inputB->fromNodeId, inputB->fromSocketId) : -1;
            }
            case NodeKind::ChannelSplit: {
                const Link* upstream = FindInputLink(currentNodeId, kImageInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::ChannelCombine:
            case NodeKind::Output: {
                if (currentSocketId == kImageInputSocketId) {
                    if (const Link* input = FindInputLink(currentNodeId, kImageInputSocketId)) {
                        return resolve(input->fromNodeId, input->fromSocketId);
                    }
                }
                for (const char* channelSocketId : { "r", "g", "b", "a" }) {
                    if (const Link* channelInput = FindInputLink(currentNodeId, channelSocketId)) {
                        const int sourceId = resolve(channelInput->fromNodeId, channelInput->fromSocketId);
                        if (sourceId > 0) return sourceId;
                    }
                }
                return -1;
            }
            case NodeKind::MaskUtility: {
                const Link* upstream = FindInputLink(currentNodeId, kMaskUtilityInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::MaskCombine: {
                if (const Link* inputA = FindInputLink(currentNodeId, kMaskCombineInputASocketId)) {
                    const int sourceId = resolve(inputA->fromNodeId, inputA->fromSocketId);
                    if (sourceId > 0) return sourceId;
                }
                const Link* inputB = FindInputLink(currentNodeId, kMaskCombineInputBSocketId);
                return inputB ? resolve(inputB->fromNodeId, inputB->fromSocketId) : -1;
            }
            case NodeKind::ImageToMask: {
                const Link* upstream = FindInputLink(currentNodeId, kImageToMaskInputSocketId);
                return upstream ? resolve(upstream->fromNodeId, upstream->fromSocketId) : -1;
            }
            case NodeKind::Composite:
            case NodeKind::Scope:
            case NodeKind::Preview:
                return -1;
        }
        return -1;
    };
    return resolve(nodeId, socketId);
}

int Graph::ResolveReferenceSourceNodeIdForOutput(int outputNodeId) const {
    const Node* output = FindNode(outputNodeId);
    if (!output || output->kind != NodeKind::Output) {
        return -1;
    }
    return ResolveReferenceSourceNodeId(outputNodeId, kImageInputSocketId);
}

void Graph::ConnectImageToOutput(int nodeId) {
    const Node* node = FindNode(nodeId);
    if (!node || (node->kind != NodeKind::Image &&
                  node->kind != NodeKind::RawDecode &&
                  node->kind != NodeKind::RawDevelop &&
                  node->kind != NodeKind::RawDetailFusion &&
                  node->kind != NodeKind::HdrMerge &&
                  node->kind != NodeKind::Lut)) {
        return;
    }

    ActivateImageNode(nodeId);
    Node* output = EnsureOutputNode();
    if (output && output->id > 0) {
        TryConnectSockets(nodeId, kImageOutputSocketId, output->id, kImageInputSocketId);
    }
}

void Graph::DisconnectOutput() {
    m_ActiveImageNodeId = -1;
    const std::size_t oldLinkCount = m_Links.size();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return IsRenderLink(link);
        }),
        m_Links.end());
    if (oldLinkCount != m_Links.size()) {
        TouchStructure();
    }
}

void Graph::RebuildLinks() {
    const std::vector<Link> scopeLinks = [&]() {
        std::vector<Link> links;
        for (const Link& link : m_Links) {
            if (GetLinkRole(link) == LinkRole::Scope) {
                links.push_back(link);
            }
        }
        return links;
    }();

    m_Links = scopeLinks;
    const bool hadNoActiveImage = m_ActiveImageNodeId <= 0 || !FindNode(m_ActiveImageNodeId);

    if (hadNoActiveImage) {
        TouchStructure();
        return;
    }

    std::vector<Node*> layers;
    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Layer) {
            layers.push_back(&node);
        }
    }
    std::sort(layers.begin(), layers.end(), [](const Node* a, const Node* b) {
        return a->layerIndex < b->layerIndex;
    });

    int previous = m_ActiveImageNodeId;
    for (const Node* layer : layers) {
        m_Links.push_back(MakeSocketLink(previous, kImageOutputSocketId, layer->id, kImageInputSocketId));
        previous = layer->id;
    }

    Node* output = EnsureOutputNode();
    if (output) {
        m_Links.push_back(MakeSocketLink(previous, kImageOutputSocketId, output->id, kImageInputSocketId));
    }
    TouchStructure();
}

void Graph::TouchStructure() {
    ++m_StructureRevision;
}

int Graph::AllocateNodeId() {
    return m_NextNodeId++;
}

Vec2 Graph::DefaultLayerPosition(int layerIndex) const {
    return Vec2{ 260.0f + static_cast<float>(layerIndex) * 220.0f, 120.0f };
}

NodeGroup* Graph::AddGroup(std::string title, Vec2 position, Vec2 size) {
    NodeGroup group;
    group.id = m_NextGroupId++;
    group.title = title;
    group.position = position;
    group.size = size;
    m_Groups.push_back(std::move(group));
    TouchStructure();
    return &m_Groups.back();
}

bool Graph::RemoveGroup(int groupId) {
    auto it = std::remove_if(m_Groups.begin(), m_Groups.end(), [groupId](const NodeGroup& g) {
        return g.id == groupId;
    });
    if (it != m_Groups.end()) {
        m_Groups.erase(it, m_Groups.end());
        TouchStructure();
        return true;
    }
    return false;
}

NodeGroup* Graph::FindGroup(int groupId) {
    auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [groupId](const NodeGroup& g) {
        return g.id == groupId;
    });
    return it != m_Groups.end() ? &(*it) : nullptr;
}

const NodeGroup* Graph::FindGroup(int groupId) const {
    auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [groupId](const NodeGroup& g) {
        return g.id == groupId;
    });
    return it != m_Groups.end() ? &(*it) : nullptr;
}

} // namespace EditorNodeGraph
