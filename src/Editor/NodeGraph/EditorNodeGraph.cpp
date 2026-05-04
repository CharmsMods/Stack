#include "EditorNodeGraph.h"

#include <algorithm>
#include <functional>
#include <set>
#include <tuple>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

const char* ScopeTitle(ScopeKind kind) {
    switch (kind) {
        case ScopeKind::Histogram: return "Histogram";
        case ScopeKind::Vectorscope: return "Vectorscope";
        case ScopeKind::RGBParade: return "RGB Parade";
    }
    return "Scope";
}

const char* MaskTitle(MaskGeneratorKind kind) {
    switch (kind) {
        case MaskGeneratorKind::Solid: return "Solid Mask";
        case MaskGeneratorKind::LinearGradient: return "Linear Gradient Mask";
        case MaskGeneratorKind::RadialGradient: return "Radial Gradient Mask";
        case MaskGeneratorKind::Noise: return "Noise Mask";
    }
    return "Mask";
}

const char* MaskUtilityTitle(MaskUtilityKind kind) {
    switch (kind) {
        case MaskUtilityKind::Invert: return "Invert Mask";
        case MaskUtilityKind::Levels: return "Levels Mask";
        case MaskUtilityKind::Threshold: return "Threshold Mask";
    }
    return "Mask Utility";
}

const char* ImageToMaskTitle(ImageToMaskKind kind) {
    switch (kind) {
        case ImageToMaskKind::Luminance: return "Luminance Mask";
    }
    return "Image To Mask";
}

const char* ImageGeneratorTitle(ImageGeneratorKind kind) {
    switch (kind) {
        case ImageGeneratorKind::SolidColor: return "Solid Color Image";
        case ImageGeneratorKind::ColorGradient: return "Color Gradient Image";
    }
    return "Generated Image";
}

bool ContainsNodeId(const std::vector<Node>& nodes, int id) {
    return std::any_of(nodes.begin(), nodes.end(), [id](const Node& node) {
        return node.id == id;
    });
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
    m_NextNodeId = 1;
    m_SelectedNodeId = -1;
    m_SelectedNodeIds.clear();
    m_SelectedLink = {};
    m_HasSelectedLink = false;
    m_ActiveImageNodeId = -1;
    m_OutputNodeId = -1;
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

    EnsureOutputNode();
    RebuildLinks();
}

void Graph::SyncLayerNodes(int layerCount) {
    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [layerCount](const Node& node) {
            return node.kind == NodeKind::Layer && (node.layerIndex < 0 || node.layerIndex >= layerCount);
        }),
        m_Nodes.end());

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
    }

    EnsureOutputNode();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return !FindNode(link.fromNodeId) ||
                !FindNode(link.toNodeId) ||
                !FindSocket(link.fromNodeId, link.fromSocketId) ||
                !FindSocket(link.toNodeId, link.toSocketId);
        }),
        m_Links.end());
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
    Validate();
}

Node* Graph::AddImageNode(ImagePayload payload, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Image;
    node.title = payload.label.empty() ? "Image" : payload.label;
    node.position = position;
    node.image = std::move(payload);
    m_Nodes.push_back(std::move(node));
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
    node.title = descriptor ? descriptor->displayName : "Layer";
    node.position = position;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddScopeNode(ScopeKind scopeKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Scope;
    node.scopeKind = scopeKind;
    node.title = ScopeTitle(scopeKind);
    node.position = position;
    node.expanded = true;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddMaskGeneratorNode(MaskGeneratorKind maskKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskGenerator;
    node.maskKind = maskKind;
    node.title = MaskTitle(maskKind);
    node.position = position;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddMaskUtilityNode(MaskUtilityKind utilityKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::MaskUtility;
    node.maskUtilityKind = utilityKind;
    node.title = MaskUtilityTitle(utilityKind);
    node.position = position;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddImageToMaskNode(ImageToMaskKind converterKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ImageToMask;
    node.imageToMaskKind = converterKind;
    node.title = ImageToMaskTitle(converterKind);
    node.position = position;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddImageGeneratorNode(ImageGeneratorKind generatorKind, Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::ImageGenerator;
    node.imageGeneratorKind = generatorKind;
    node.title = ImageGeneratorTitle(generatorKind);
    node.position = position;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddMixNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Mix;
    node.title = "Mix";
    node.position = position;
    node.mixBlendMode = MixBlendMode::Normal;
    node.mixFactor = 0.5f;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::AddPreviewNode(Vec2 position) {
    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Preview;
    node.title = "Preview";
    node.position = position;
    node.expanded = true;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
}

Node* Graph::EnsureOutputNode() {
    if (Node* existing = FindNode(m_OutputNodeId)) {
        return existing;
    }

    Node node;
    node.id = AllocateNodeId();
    node.kind = NodeKind::Output;
    node.title = "Output";
    node.position = Vec2{ 520.0f, 120.0f };
    m_OutputNodeId = node.id;
    m_Nodes.push_back(std::move(node));
    return &m_Nodes.back();
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
    std::vector<SocketDefinition> sockets;
    auto add = [&](const char* id, SocketDirection direction, SocketType type, const char* label, bool optional, bool visible) {
        if (visibleOnly && !visible) {
            return;
        }
        sockets.push_back(SocketDefinition{ id, node.id, direction, type, label, optional, visible });
    };

    switch (node.kind) {
        case NodeKind::Image:
            add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
            break;
        case NodeKind::Layer:
            add(kImageInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, true);
            add(kMaskInputSocketId, SocketDirection::Input, SocketType::Mask, "Mask", true, true);
            add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
            break;
        case NodeKind::Output:
            add(kImageInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, true);
            break;
        case NodeKind::Scope:
            add(kScopeInputSocketId, SocketDirection::Input, SocketType::Analysis, "Scope", false, true);
            break;
        case NodeKind::Preview:
            add(kPreviewInputSocketId, SocketDirection::Input, SocketType::Analysis, "Image / Mask", false, true);
            break;
        case NodeKind::MaskGenerator:
            add(kMaskOutputSocketId, SocketDirection::Output, SocketType::Mask, "Mask", false, true);
            break;
        case NodeKind::MaskUtility:
            add(kMaskUtilityInputSocketId, SocketDirection::Input, SocketType::Mask, "Mask", false, true);
            add(kMaskOutputSocketId, SocketDirection::Output, SocketType::Mask, "Mask", false, true);
            break;
        case NodeKind::ImageToMask:
            add(kImageToMaskInputSocketId, SocketDirection::Input, SocketType::Image, "Image", false, true);
            add(kMaskOutputSocketId, SocketDirection::Output, SocketType::Mask, "Mask", false, true);
            break;
        case NodeKind::ImageGenerator:
            add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
            break;
        case NodeKind::Mix:
            add(kMixInputASocketId, SocketDirection::Input, SocketType::Image, "A", false, true);
            add(kMixInputBSocketId, SocketDirection::Input, SocketType::Image, "B", false, true);
            add(kMixFactorSocketId, SocketDirection::Input, SocketType::Mask, "Factor", true, true);
            add(kImageOutputSocketId, SocketDirection::Output, SocketType::Image, "Image", false, true);
            break;
    }

    return sockets;
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
    switch (node.kind) {
        case NodeKind::Layer:
        case NodeKind::Output:
            return kImageInputSocketId;
        case NodeKind::Mix:
            return kMixInputASocketId;
        case NodeKind::Scope:
            return kScopeInputSocketId;
        case NodeKind::Preview:
            return kPreviewInputSocketId;
        case NodeKind::MaskUtility:
            return kMaskUtilityInputSocketId;
        case NodeKind::ImageToMask:
            return kImageToMaskInputSocketId;
        case NodeKind::MaskGenerator:
        case NodeKind::ImageGenerator:
            break;
        case NodeKind::Image:
            break;
    }
    return {};
}

std::string Graph::DefaultOutputSocket(const Node& node) const {
    switch (node.kind) {
        case NodeKind::Image:
        case NodeKind::Layer:
        case NodeKind::Mix:
            return kImageOutputSocketId;
        case NodeKind::MaskGenerator:
        case NodeKind::MaskUtility:
        case NodeKind::ImageToMask:
            return kMaskOutputSocketId;
        case NodeKind::ImageGenerator:
            return kImageOutputSocketId;
        case NodeKind::Output:
        case NodeKind::Scope:
        case NodeKind::Preview:
            break;
    }
    return {};
}

void Graph::ConnectImageToOutput(int nodeId) {
    const Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Image) {
        return;
    }

    ActivateImageNode(nodeId);
    EnsureOutputNode();
    if (m_OutputNodeId > 0) {
        TryConnectSockets(nodeId, kImageOutputSocketId, m_OutputNodeId, kImageInputSocketId);
    }
}

void Graph::DisconnectOutput() {
    m_ActiveImageNodeId = -1;
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
            return IsRenderLink(link);
        }),
        m_Links.end());
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

    if (m_ActiveImageNodeId <= 0 || !FindNode(m_ActiveImageNodeId)) {
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
}

void Graph::SelectNode(int nodeId, bool additive) {
    if (!FindNode(nodeId)) {
        ClearSelection();
        return;
    }

    ClearSelectedLink();
    if (!additive) {
        m_SelectedNodeIds.clear();
    }

    auto it = std::find(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), nodeId);
    if (it == m_SelectedNodeIds.end()) {
        m_SelectedNodeIds.push_back(nodeId);
    } else if (additive) {
        m_SelectedNodeIds.erase(it);
    }

    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
}

bool Graph::IsNodeSelected(int nodeId) const {
    return std::find(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), nodeId) != m_SelectedNodeIds.end();
}

void Graph::ClearSelection() {
    m_SelectedNodeId = -1;
    m_SelectedNodeIds.clear();
    ClearSelectedLink();
}

void Graph::SelectNodesInRect(Vec2 min, Vec2 max, bool additive) {
    if (!additive) {
        m_SelectedNodeIds.clear();
    }
    ClearSelectedLink();

    const float left = std::min(min.x, max.x);
    const float right = std::max(min.x, max.x);
    const float top = std::min(min.y, max.y);
    const float bottom = std::max(min.y, max.y);

    for (const Node& node : m_Nodes) {
        if (node.kind == NodeKind::Output) {
            continue;
        }
        const Vec2 size = node.kind == NodeKind::Layer && node.expanded
            ? Vec2{ 330.0f, 420.0f }
            : (node.kind == NodeKind::Image ? Vec2{ 230.0f, 120.0f }
            : (node.kind == NodeKind::Scope ? Vec2{ 300.0f, 270.0f }
            : (node.kind == NodeKind::MaskGenerator ? Vec2{ 270.0f, 246.0f }
            : (node.kind == NodeKind::Mix ? Vec2{ 250.0f, 170.0f }
            : (node.kind == NodeKind::Preview ? Vec2{ 240.0f, 178.0f } : Vec2{ 190.0f, 82.0f })))));
        const bool overlaps =
            node.position.x <= right &&
            node.position.x + size.x >= left &&
            node.position.y <= bottom &&
            node.position.y + size.y >= top;
        if (overlaps && !IsNodeSelected(node.id)) {
            m_SelectedNodeIds.push_back(node.id);
        }
    }

    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
}

void Graph::SelectLink(int fromNodeId, int toNodeId) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SelectLink(
        fromNodeId,
        from ? DefaultOutputSocket(*from) : std::string(),
        toNodeId,
        to ? DefaultInputSocket(*to) : std::string());
}

void Graph::SelectLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    m_SelectedLink = MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    m_HasSelectedLink = HasLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    if (m_HasSelectedLink) {
        m_SelectedNodeId = -1;
        m_SelectedNodeIds.clear();
    }
}

void Graph::ClearSelectedLink() {
    m_SelectedLink = {};
    m_HasSelectedLink = false;
}

const Link* Graph::GetSelectedLink() const {
    if (!m_HasSelectedLink) {
        return nullptr;
    }
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
        return link.fromNodeId == m_SelectedLink.fromNodeId &&
            link.fromSocketId == m_SelectedLink.fromSocketId &&
            link.toNodeId == m_SelectedLink.toNodeId &&
            link.toSocketId == m_SelectedLink.toSocketId;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

bool Graph::HasSelectedLink() const {
    return GetSelectedLink() != nullptr;
}

bool Graph::IsOutputConnected() const {
    if (m_OutputNodeId <= 0) {
        return false;
    }

    std::function<bool(int, std::unordered_set<int>&)> reachesImage = [&](int nodeId, std::unordered_set<int>& visiting) -> bool {
        if (visiting.count(nodeId)) {
            return false;
        }
        visiting.insert(nodeId);
        const Node* node = FindNode(nodeId);
        if (!node) {
            return false;
        }
        if (node->kind == NodeKind::Image || node->kind == NodeKind::ImageGenerator) {
            return true;
        }
        if (node->kind == NodeKind::Layer) {
            const Link* upstream = FindInputLink(node->id, kImageInputSocketId);
            return upstream ? reachesImage(upstream->fromNodeId, visiting) : false;
        }
        if (node->kind == NodeKind::Mix) {
            const Link* inputA = FindInputLink(node->id, kMixInputASocketId);
            const Link* inputB = FindInputLink(node->id, kMixInputBSocketId);
            return inputA && inputB &&
                reachesImage(inputA->fromNodeId, visiting) &&
                reachesImage(inputB->fromNodeId, visiting);
        }
        return false;
    };

    const Link* input = FindInputLink(m_OutputNodeId, kImageInputSocketId);
    if (!input) {
        return false;
    }
    std::unordered_set<int> visiting;
    return reachesImage(input->fromNodeId, visiting);
}

bool Graph::TryConnect(int fromNodeId, int toNodeId, std::string* errorMessage) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    if (!from || !to) {
        if (errorMessage) *errorMessage = "Invalid node connection.";
        return false;
    }
    return TryConnectSockets(fromNodeId, DefaultOutputSocket(*from), toNodeId, DefaultInputSocket(*to), errorMessage);
}

bool Graph::TryConnectSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SocketDefinition fromSocket;
    SocketDefinition toSocket;
    if (!from || !to || fromNodeId == toNodeId || !FindSocket(fromNodeId, fromSocketId, &fromSocket) || !FindSocket(toNodeId, toSocketId, &toSocket)) {
        if (errorMessage) *errorMessage = "Invalid socket connection.";
        return false;
    }
    if (fromSocket.direction != SocketDirection::Output || toSocket.direction != SocketDirection::Input) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }
    if (from->kind == NodeKind::Output || to->kind == NodeKind::Image) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }

    const bool toScope = to->kind == NodeKind::Scope && toSocketId == kScopeInputSocketId;
    if (toScope) {
        const bool validImageScope = fromSocket.type == SocketType::Image;
        const bool validMaskScope = fromSocket.type == SocketType::Mask && fromSocketId == kMaskOutputSocketId;
        if (!validImageScope && !validMaskScope) {
            if (errorMessage) *errorMessage = "Scopes can analyze image or mask outputs.";
            return false;
        }
        RemoveScopeLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        return true;
    }

    const bool toPreview = to->kind == NodeKind::Preview && toSocketId == kPreviewInputSocketId;
    if (toPreview) {
        const bool validImagePreview = fromSocket.type == SocketType::Image;
        const bool validMaskPreview = fromSocket.type == SocketType::Mask && fromSocketId == kMaskOutputSocketId;
        if (!validImagePreview && !validMaskPreview) {
            if (errorMessage) *errorMessage = "Preview nodes can inspect image or mask outputs.";
            return false;
        }
        RemoveScopeLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        return true;
    }

    if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask) {
        const bool validMaskSource =
            (from->kind == NodeKind::MaskGenerator ||
             from->kind == NodeKind::MaskUtility ||
             from->kind == NodeKind::ImageToMask) &&
            fromSocketId == kMaskOutputSocketId;
        const bool validMaskTarget =
            (to->kind == NodeKind::Layer && toSocketId == kMaskInputSocketId) ||
            (to->kind == NodeKind::Mix && toSocketId == kMixFactorSocketId) ||
            (to->kind == NodeKind::MaskUtility && toSocketId == kMaskUtilityInputSocketId);
        if (!validMaskSource ||
            !validMaskTarget ||
            fromSocket.type != SocketType::Mask ||
            toSocket.type != SocketType::Mask) {
            if (errorMessage) *errorMessage = "Mask outputs can connect to layer masks, mix factors, or mask utilities.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        RemoveLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        return true;
    }

    if (fromSocket.type != SocketType::Image || toSocket.type != SocketType::Image) {
        if (errorMessage) *errorMessage = "Only image sockets can be connected in the render chain in this pass.";
        return false;
    }
    if (!IsRenderChainNode(*from) || !IsRenderChainNode(*to) || to->kind == NodeKind::Image || from->kind == NodeKind::Output) {
        if (errorMessage) *errorMessage = "Only image-producing nodes, layer, mix, and output nodes can be in the render chain.";
        return false;
    }
    if (to->kind == NodeKind::Mix && toSocketId != kMixInputASocketId && toSocketId != kMixInputBSocketId) {
        if (errorMessage) *errorMessage = "Image links must target Mix input A or B.";
        return false;
    }
    if (to->kind == NodeKind::Layer && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the layer image input.";
        return false;
    }
    if (to->kind == NodeKind::Output && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the output image input.";
        return false;
    }
    if (to->kind == NodeKind::ImageToMask && toSocketId != kImageToMaskInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the luminance image input.";
        return false;
    }
    if (to->kind != NodeKind::Layer && to->kind != NodeKind::Output && to->kind != NodeKind::Mix && to->kind != NodeKind::ImageToMask) {
        if (errorMessage) *errorMessage = "Image links must target a layer, mix node, mask converter, or the output.";
        return false;
    }
    if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        if (errorMessage) *errorMessage = "That connection would create a cycle.";
        return false;
    }

    RemoveRenderLinksForNodeInput(toNodeId, toSocketId);
    if (from->kind == NodeKind::Image &&
        ((to->kind == NodeKind::Layer && toSocketId == kImageInputSocketId) ||
         (to->kind == NodeKind::Output && toSocketId == kImageInputSocketId) ||
         (to->kind == NodeKind::Mix && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId)))) {
        ActivateImageNode(fromNodeId);
    }
    m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
    return true;
}

bool Graph::RemoveNode(int nodeId) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind == NodeKind::Output) {
        return false;
    }

    if (node->kind == NodeKind::Layer) {
        return false;
    }

    const bool wasActiveImage = node->kind == NodeKind::Image && node->id == m_ActiveImageNodeId;
    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](const Node& item) {
            return item.id == nodeId;
        }),
        m_Nodes.end());
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [nodeId](const Link& link) {
            return link.fromNodeId == nodeId || link.toNodeId == nodeId;
        }),
        m_Links.end());
    m_SelectedNodeIds.erase(
        std::remove(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), nodeId),
        m_SelectedNodeIds.end());
    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
    if (wasActiveImage) {
        m_ActiveImageNodeId = -1;
    }
    return true;
}

bool Graph::RemoveLink(int fromNodeId, int toNodeId) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    return RemoveLink(
        fromNodeId,
        from ? DefaultOutputSocket(*from) : std::string(),
        toNodeId,
        to ? DefaultInputSocket(*to) : std::string());
}

bool Graph::RemoveLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    const auto oldSize = m_Links.size();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [&](const Link& link) {
            return link.fromNodeId == fromNodeId &&
                link.fromSocketId == fromSocketId &&
                link.toNodeId == toNodeId &&
                link.toSocketId == toSocketId;
        }),
        m_Links.end());
    if (m_HasSelectedLink &&
        m_SelectedLink.fromNodeId == fromNodeId &&
        m_SelectedLink.fromSocketId == fromSocketId &&
        m_SelectedLink.toNodeId == toNodeId &&
        m_SelectedLink.toSocketId == toSocketId) {
        m_HasSelectedLink = false;
    }
    if (fromNodeId == m_ActiveImageNodeId && FindOutputLink(fromNodeId, kImageOutputSocketId) == nullptr) {
        m_ActiveImageNodeId = -1;
    }
    return oldSize != m_Links.size();
}

bool Graph::RemoveSelectedLink() {
    const Link* selected = GetSelectedLink();
    if (!selected) {
        return false;
    }
    const Link copy = *selected;
    return RemoveLink(copy.fromNodeId, copy.fromSocketId, copy.toNodeId, copy.toSocketId);
}

bool Graph::HasLink(int fromNodeId, int toNodeId) const {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    return HasLink(
        fromNodeId,
        from ? DefaultOutputSocket(*from) : std::string(),
        toNodeId,
        to ? DefaultInputSocket(*to) : std::string());
}

bool Graph::HasLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const {
    return std::any_of(m_Links.begin(), m_Links.end(), [&](const Link& link) {
        return link.fromNodeId == fromNodeId &&
            link.fromSocketId == fromSocketId &&
            link.toNodeId == toNodeId &&
            link.toSocketId == toSocketId;
    });
}

void Graph::AutoLayout() {
    int layerColumn = 0;
    const float x0 = 40.0f;
    const float y0 = 130.0f;

    if (Node* active = FindNode(m_ActiveImageNodeId)) {
        active->position = { x0, y0 };
    }

    for (int nodeId : GetRenderLayerNodePath()) {
        if (Node* node = FindNode(nodeId)) {
            node->position = { x0 + 260.0f + static_cast<float>(layerColumn) * 250.0f, y0 };
            ++layerColumn;
        }
    }

    if (Node* output = EnsureOutputNode()) {
        output->position = { x0 + 260.0f + static_cast<float>(layerColumn) * 250.0f, y0 };
    }

    int imageRow = 0;
    int scopeRow = 0;
    int maskRow = 0;
    for (Node& node : m_Nodes) {
        if (node.kind == NodeKind::Image && node.id != m_ActiveImageNodeId) {
            node.position = { x0, y0 + 170.0f + static_cast<float>(imageRow) * 145.0f };
            ++imageRow;
        } else if (node.kind == NodeKind::Scope || node.kind == NodeKind::Preview) {
            const Link* input = FindScopeInputLink(node.id);
            if (input) {
                const Node* from = FindNode(input->fromNodeId);
                if (from) {
                    node.position = { from->position.x, from->position.y + 190.0f };
                    continue;
                }
            }
            node.position = { x0 + 260.0f + static_cast<float>(scopeRow) * 300.0f, y0 + 370.0f };
            ++scopeRow;
        } else if (node.kind == NodeKind::MaskGenerator) {
            bool placed = false;
            for (const Link& link : m_Links) {
                if (link.fromNodeId == node.id && link.fromSocketId == kMaskOutputSocketId) {
                    const Node* to = FindNode(link.toNodeId);
                    if (to) {
                        node.position = { to->position.x, to->position.y + 170.0f };
                        placed = true;
                        break;
                    }
                }
            }
            if (!placed) {
                node.position = { x0 + 120.0f, y0 + 370.0f + static_cast<float>(maskRow) * 160.0f };
                ++maskRow;
            }
        }
    }
}

ValidationResult Graph::Validate() const {
    ValidationResult result;
    std::set<int> ids;
    int outputCount = 0;
    std::set<int> outgoingImages;

    for (const Node& node : m_Nodes) {
        if (node.id <= 0 || !ids.insert(node.id).second) {
            result.valid = false;
            result.messages.push_back("Duplicate or invalid node id.");
        }
        if (node.kind == NodeKind::Output) {
            ++outputCount;
        }
        if (node.kind == NodeKind::Layer && node.layerIndex < 0) {
            result.valid = false;
            result.messages.push_back("Layer node has no layer reference.");
        }
    }

    if (outputCount != 1) {
        result.valid = false;
        result.messages.push_back("Graph must have exactly one output node.");
    }

    std::set<std::tuple<int, std::string, int, std::string>> seenLinks;
    for (const Link& link : m_Links) {
        if (!ContainsNodeId(m_Nodes, link.fromNodeId) || !ContainsNodeId(m_Nodes, link.toNodeId)) {
            result.valid = false;
            result.messages.push_back("Link references a missing node.");
        }
        if (!seenLinks.insert({ link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId }).second) {
            result.valid = false;
            result.messages.push_back("Duplicate graph socket link.");
        }

        const Node* from = FindNode(link.fromNodeId);
        const Node* to = FindNode(link.toNodeId);
        SocketDefinition fromSocket;
        SocketDefinition toSocket;
        if (!from || !to) {
            continue;
        }
        if (!FindSocket(link.fromNodeId, link.fromSocketId, &fromSocket) ||
            !FindSocket(link.toNodeId, link.toSocketId, &toSocket)) {
            result.valid = false;
            result.messages.push_back("Link references a missing socket.");
            continue;
        }
        if (fromSocket.direction != SocketDirection::Output || toSocket.direction != SocketDirection::Input) {
            result.valid = false;
            result.messages.push_back("Link uses an invalid socket direction.");
        }
        if (from->kind == NodeKind::Image) {
            outgoingImages.insert(from->id);
        }

        if (GetLinkRole(link) == LinkRole::Scope) {
            if (to->kind == NodeKind::Scope) {
                const bool validScopeInput = link.toSocketId == kScopeInputSocketId &&
                    (fromSocket.type == SocketType::Image ||
                     (fromSocket.type == SocketType::Mask && link.fromSocketId == kMaskOutputSocketId));
                if (!validScopeInput) {
                    result.valid = false;
                    result.messages.push_back("Scope nodes can only analyze image or mask outputs.");
                }
            } else if (to->kind == NodeKind::Preview) {
                const bool validPreview = link.toSocketId == kPreviewInputSocketId &&
                    (fromSocket.type == SocketType::Image ||
                     (fromSocket.type == SocketType::Mask && link.fromSocketId == kMaskOutputSocketId));
                if (!validPreview) {
                    result.valid = false;
                    result.messages.push_back("Preview nodes can only inspect image or mask outputs.");
                }
            } else {
                result.valid = false;
                result.messages.push_back("Invalid analysis link.");
            }
        } else if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask) {
            const bool validMaskSource =
                (from->kind == NodeKind::MaskGenerator ||
                 from->kind == NodeKind::MaskUtility ||
                 from->kind == NodeKind::ImageToMask) &&
                fromSocket.type == SocketType::Mask &&
                link.fromSocketId == kMaskOutputSocketId;
            const bool validMaskTarget =
                (to->kind == NodeKind::Layer && link.toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::Mix && link.toSocketId == kMixFactorSocketId) ||
                (to->kind == NodeKind::MaskUtility && link.toSocketId == kMaskUtilityInputSocketId);
            if (!validMaskSource ||
                !validMaskTarget ||
                toSocket.type != SocketType::Mask) {
                result.valid = false;
                result.messages.push_back("Invalid mask link.");
            }
        } else {
            if (fromSocket.type != SocketType::Image || toSocket.type != SocketType::Image) {
                result.valid = false;
                result.messages.push_back("Render-chain links must connect image sockets.");
            }
            if (!IsRenderChainNode(*from) || !IsRenderChainNode(*to) || to->kind == NodeKind::Image || from->kind == NodeKind::Output) {
                result.valid = false;
                result.messages.push_back("Invalid render-chain link.");
            }
        }
    }

    result.outputConnected = IsOutputConnected();
    if (!result.outputConnected) {
        result.messages.push_back("Output is disconnected.");
    }

    std::unordered_set<int> visiting;
    std::unordered_set<int> visited;
    std::function<bool(int)> hasCycle = [&](int nodeId) {
        if (visiting.count(nodeId)) return true;
        if (visited.count(nodeId)) return false;
        visiting.insert(nodeId);
        for (const Link& link : m_Links) {
            if (link.fromNodeId == nodeId && IsRenderLink(link)) {
                if (hasCycle(link.toNodeId)) return true;
            }
        }
        visiting.erase(nodeId);
        visited.insert(nodeId);
        return false;
    };
    for (const Node& node : m_Nodes) {
        if (IsRenderChainNode(node) && hasCycle(node.id)) {
            result.valid = false;
            result.messages.push_back("Render chain contains a cycle.");
            break;
        }
    }

    return result;
}

std::vector<int> Graph::GetRenderLayerNodePath() const {
    std::vector<int> reversePath;
    if (m_OutputNodeId <= 0) {
        return {};
    }

    const Link* input = FindInputLink(m_OutputNodeId, kImageInputSocketId);
    std::unordered_set<int> visited;
    while (input) {
        const Node* from = FindNode(input->fromNodeId);
        if (!from || visited.count(from->id)) {
            return {};
        }
        visited.insert(from->id);
        if (from->kind == NodeKind::Image) {
            std::reverse(reversePath.begin(), reversePath.end());
            return reversePath;
        }
        if (from->kind != NodeKind::Layer) {
            return {};
        }
        reversePath.push_back(from->id);
        input = FindInputLink(from->id, kImageInputSocketId);
    }

    return {};
}

std::vector<int> Graph::GetRenderLayerIndexPath() const {
    std::vector<int> indices;
    for (int nodeId : GetRenderLayerNodePath()) {
        const Node* node = FindNode(nodeId);
        if (node && node->kind == NodeKind::Layer && node->layerIndex >= 0) {
            indices.push_back(node->layerIndex);
        }
    }
    return indices;
}

LinkRole Graph::GetLinkRole(const Link& link) const {
    const Node* to = FindNode(link.toNodeId);
    const bool analysisTarget = to &&
        ((to->kind == NodeKind::Scope && link.toSocketId == kScopeInputSocketId) ||
         (to->kind == NodeKind::Preview && link.toSocketId == kPreviewInputSocketId));
    return analysisTarget ? LinkRole::Scope : LinkRole::Render;
}

bool Graph::IsRenderChainNode(const Node& node) const {
    return node.kind == NodeKind::Image ||
        node.kind == NodeKind::ImageGenerator ||
        node.kind == NodeKind::Layer ||
        node.kind == NodeKind::Output ||
        node.kind == NodeKind::Mix ||
        node.kind == NodeKind::ImageToMask;
}

bool Graph::IsRenderLink(const Link& link) const {
    return GetLinkRole(link) == LinkRole::Render &&
        link.fromSocketId == kImageOutputSocketId &&
        (link.toSocketId == kImageInputSocketId ||
         link.toSocketId == kMixInputASocketId ||
         link.toSocketId == kMixInputBSocketId ||
         link.toSocketId == kImageToMaskInputSocketId);
}

const Link* Graph::FindInputLink(int nodeId, const std::string& socketId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
        return link.toNodeId == nodeId && link.toSocketId == socketId && IsRenderLink(link);
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindAnyInputLink(int nodeId, const std::string& socketId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [nodeId, &socketId](const Link& link) {
        return link.toNodeId == nodeId && link.toSocketId == socketId;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindOutputLink(int nodeId, const std::string& socketId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
        return link.fromNodeId == nodeId && link.fromSocketId == socketId && IsRenderLink(link);
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

const Link* Graph::FindScopeInputLink(int nodeId) const {
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this, nodeId](const Link& link) {
        return link.toNodeId == nodeId && GetLinkRole(link) == LinkRole::Scope;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

int Graph::AllocateNodeId() {
    return m_NextNodeId++;
}

Vec2 Graph::DefaultLayerPosition(int layerIndex) const {
    return Vec2{ 260.0f + static_cast<float>(layerIndex) * 220.0f, 120.0f };
}

bool Graph::WouldCreateCycle(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const {
    const bool imageEdge = fromSocketId == kImageOutputSocketId &&
        (toSocketId == kImageInputSocketId ||
         toSocketId == kMixInputASocketId ||
         toSocketId == kMixInputBSocketId ||
         toSocketId == kImageToMaskInputSocketId);
    const bool maskEdge = fromSocketId == kMaskOutputSocketId &&
        (toSocketId == kMaskInputSocketId ||
         toSocketId == kMixFactorSocketId ||
         toSocketId == kMaskUtilityInputSocketId);
    if (!imageEdge && !maskEdge) {
        return false;
    }

    std::unordered_set<int> visited;
    std::function<bool(int)> reachesFrom = [&](int current) {
        if (current == fromNodeId) {
            return true;
        }
        if (!visited.insert(current).second) {
            return false;
        }
        for (const Link& link : m_Links) {
            if (link.fromNodeId != current || GetLinkRole(link) != LinkRole::Render) {
                continue;
            }
            if (reachesFrom(link.toNodeId)) {
                return true;
            }
        }
        return false;
    };
    return reachesFrom(toNodeId);
}

void Graph::RemoveRenderLinksForNodeInput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
            return link.toNodeId == nodeId && link.toSocketId == socketId && IsRenderLink(link);
        }),
        m_Links.end());
}

void Graph::RemoveRenderLinksForNodeOutput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
            return link.fromNodeId == nodeId && link.fromSocketId == socketId && IsRenderLink(link);
        }),
        m_Links.end());
}

void Graph::RemoveScopeLinksForNodeInput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
            return link.toNodeId == nodeId && link.toSocketId == socketId && GetLinkRole(link) == LinkRole::Scope;
        }),
        m_Links.end());
}

void Graph::RemoveLinksForNodeInput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [nodeId, &socketId](const Link& link) {
            return link.toNodeId == nodeId && link.toSocketId == socketId;
        }),
        m_Links.end());
}

void Graph::ActivateImageNode(int nodeId) {
    m_ActiveImageNodeId = nodeId;
}

} // namespace EditorNodeGraph
