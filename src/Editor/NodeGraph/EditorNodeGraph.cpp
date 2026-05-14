#include "EditorNodeGraph.h"
#include "EditorNodeGraphDefinitions.h"

#include <algorithm>

namespace EditorNodeGraph {
namespace {

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
    m_NextNodeId = 1;
    m_SelectedNodeId = -1;
    m_SelectedNodeIds.clear();
    m_SelectedLink = {};
    m_HasSelectedLink = false;
    m_ActiveImageNodeId = -1;
    m_OutputNodeId = -1;
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

Node* Graph::AddExportBoundsSettingsNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ExportBoundsSettings;
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
    return EditorNodeGraphDefinitions::BuildSockets(node, visibleOnly);
}

bool Graph::FindSocket(int nodeId, const std::string& socketId, SocketDefinition* outSocket) const {
    const Node* node = FindNode(nodeId);
    if (!node) {
        return false;
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

void Graph::ConnectImageToOutput(int nodeId) {
    const Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Image) {
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

} // namespace EditorNodeGraph
