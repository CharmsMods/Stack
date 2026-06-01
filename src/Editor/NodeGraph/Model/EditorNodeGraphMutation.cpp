#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

bool IsChannelSocketId(const std::string& socketId) {
    return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
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

bool Graph::IsOutputConnected() const {
    return !GetCompletedChains().empty();
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
    if (from->kind == NodeKind::Output || to->kind == NodeKind::Image ||
        to->kind == NodeKind::RawSource ||
        from->kind == NodeKind::Composite ||
        to->kind == NodeKind::Composite) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }

    const bool toScope = to->kind == NodeKind::Scope && toSocketId == kScopeInputSocketId;
    if (toScope) {
        const bool validImageScope = fromSocket.type == SocketType::Image;
        const bool validMaskScope = fromSocket.type == SocketType::Mask &&
            (fromSocketId == kMaskOutputSocketId ||
             (from->kind == NodeKind::ChannelSplit && (fromSocketId == "r" || fromSocketId == "g" || fromSocketId == "b" || fromSocketId == "a")));
        if (!validImageScope && !validMaskScope) {
            if (errorMessage) *errorMessage = "Scopes can analyze image or mask outputs.";
            return false;
        }
        RemoveScopeLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        TouchStructure();
        return true;
    }

    const bool toPreview = to->kind == NodeKind::Preview && toSocketId == kPreviewInputSocketId;
    if (toPreview) {
        const bool validImagePreview = fromSocket.type == SocketType::Image;
        const bool validMaskPreview = fromSocket.type == SocketType::Mask &&
            (fromSocketId == kMaskOutputSocketId ||
             (from->kind == NodeKind::ChannelSplit && (fromSocketId == "r" || fromSocketId == "g" || fromSocketId == "b" || fromSocketId == "a")));
        if (!validImagePreview && !validMaskPreview) {
            if (errorMessage) *errorMessage = "Preview nodes can inspect image or mask outputs.";
            return false;
        }
        RemoveScopeLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        TouchStructure();
        return true;
    }

    const std::string upstreamChannel = ResolveSocketChannel(fromNodeId, fromSocketId);
    if (to->kind == NodeKind::Output &&
        toSocketId == kImageInputSocketId &&
        !upstreamChannel.empty()) {
        return TryConnectSockets(fromNodeId, fromSocketId, toNodeId, upstreamChannel, errorMessage);
    }

    if (fromSocket.type == SocketType::Raw || toSocket.type == SocketType::Raw) {
        const bool validRawLink =
            fromSocket.type == SocketType::Raw &&
            toSocket.type == SocketType::Raw &&
            (from->kind == NodeKind::RawSource || from->kind == NodeKind::RawNeuralDenoise) &&
            fromSocketId == kRawOutputSocketId &&
            (to->kind == NodeKind::RawNeuralDenoise || to->kind == NodeKind::RawDevelop) &&
            toSocketId == kRawInputSocketId;
        if (!validRawLink) {
            if (errorMessage) *errorMessage = "RAW sockets connect from RAW Source or RAW neural nodes into RAW neural or RAW Develop nodes.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        RemoveRenderLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        TouchStructure();
        return true;
    }

    if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask) {
        const bool validMaskSource =
            ((from->kind == NodeKind::MaskGenerator ||
              from->kind == NodeKind::MaskUtility ||
              from->kind == NodeKind::ImageToMask ||
              from->kind == NodeKind::RawDetailAutoMask ||
              from->kind == NodeKind::RawDetailFusion) && fromSocketId == kMaskOutputSocketId) ||
            (from->kind == NodeKind::ChannelSplit && IsChannelSocketId(fromSocketId)) ||
            (!upstreamChannel.empty() && fromSocket.type == SocketType::Image);

        const bool isMaskToMask = fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Mask;
        const bool isChannelImageToMask = fromSocket.type == SocketType::Image && toSocket.type == SocketType::Mask && !upstreamChannel.empty();
        const bool isMaskToImage = fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Image;

        if (isMaskToMask || isChannelImageToMask) {
            const bool validMaskTarget =
                (to->kind == NodeKind::Layer && toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::RawDetailFusion && toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::Mix && toSocketId == kMixFactorSocketId) ||
                (to->kind == NodeKind::MaskUtility && toSocketId == kMaskUtilityInputSocketId) ||
                (to->kind == NodeKind::ChannelCombine && IsChannelSocketId(toSocketId)) ||
                (to->kind == NodeKind::Output && IsChannelSocketId(toSocketId));
            if (!validMaskSource || !validMaskTarget) {
                if (errorMessage) *errorMessage = "Mask outputs can connect to layer masks, mix factors, mask utilities, channels, or color-split outputs.";
                return false;
            }
        } else if (isMaskToImage) {
            const bool validImageTarget =
                (to->kind == NodeKind::Layer && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::RawDetailAutoMask && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::RawDetailFusion && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::Mix && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId)) ||
                (to->kind == NodeKind::Output && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::ImageToMask && toSocketId == kImageToMaskInputSocketId) ||
                (to->kind == NodeKind::ChannelSplit && toSocketId == kImageInputSocketId);
            if (!validMaskSource || !validImageTarget) {
                if (errorMessage) *errorMessage = "Mask outputs can connect to main image inputs of layers, mix inputs, or the output node.";
                return false;
            }
        } else {
            if (errorMessage) *errorMessage = "Cannot connect an image output directly to a mask input.";
            return false;
        }

        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        if (to->kind == NodeKind::Output && IsChannelSocketId(toSocketId)) {
            RemoveLinksForNodeInput(toNodeId, kImageInputSocketId);
        }
        RemoveLinksForNodeInput(toNodeId, toSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
        TouchStructure();
        return true;
    }

    if (fromSocket.type != SocketType::Image || toSocket.type != SocketType::Image) {
        if (errorMessage) *errorMessage = "Only image sockets can be connected in the render chain in this pass.";
        return false;
    }
    if (!IsRenderChainNode(*from) || !IsRenderChainNode(*to) || to->kind == NodeKind::Image || to->kind == NodeKind::RawSource || to->kind == NodeKind::RawDevelop || from->kind == NodeKind::Output) {
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
    if (to->kind == NodeKind::RawDetailAutoMask && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the RAW Detail Auto Mask image input.";
        return false;
    }
    if (to->kind == NodeKind::RawDetailFusion && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the RAW Detail Fusion image input.";
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
    if (to->kind == NodeKind::ChannelSplit && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the split image input.";
        return false;
    }
    if (to->kind != NodeKind::Layer && to->kind != NodeKind::RawDetailAutoMask && to->kind != NodeKind::RawDetailFusion && to->kind != NodeKind::Output && to->kind != NodeKind::Mix && to->kind != NodeKind::ImageToMask && to->kind != NodeKind::ChannelSplit) {
        if (errorMessage) *errorMessage = "Image links must target a layer, mix node, split node, mask converter, or the output.";
        return false;
    }
    if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        if (errorMessage) *errorMessage = "That connection would create a cycle.";
        return false;
    }

    if (to->kind == NodeKind::Output && toSocketId == kImageInputSocketId) {
        RemoveLinksForNodeInput(toNodeId, "r");
        RemoveLinksForNodeInput(toNodeId, "g");
        RemoveLinksForNodeInput(toNodeId, "b");
        RemoveLinksForNodeInput(toNodeId, "a");
    }
    RemoveRenderLinksForNodeInput(toNodeId, toSocketId);
    if (from->kind == NodeKind::Image) {
        ActivateImageNode(fromNodeId);
    }
    m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId));
    TouchStructure();
    return true;
}

bool Graph::RemoveNode(int nodeId) {
    Node* node = FindNode(nodeId);
    if (!node) {
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
    RefreshPrimaryOutputNode();
    TouchStructure();
    return true;
}

bool Graph::SetOutputNodeEnabled(int nodeId, bool enabled) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Output) {
        return false;
    }
    if (node->outputEnabled == enabled) {
        return true;
    }
    node->outputEnabled = enabled;
    TouchStructure();
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
    if (oldSize != m_Links.size()) {
        TouchStructure();
        return true;
    }
    return false;
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

bool Graph::WouldCreateCycle(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const {
    const bool imageEdge = fromSocketId == kImageOutputSocketId &&
        (toSocketId == kImageInputSocketId ||
         toSocketId == kMixInputASocketId ||
         toSocketId == kMixInputBSocketId ||
         toSocketId == kImageToMaskInputSocketId);
    const bool maskEdge = (fromSocketId == kMaskOutputSocketId || fromSocketId == "r" || fromSocketId == "g" || fromSocketId == "b" || fromSocketId == "a") &&
        (toSocketId == kMaskInputSocketId ||
         toSocketId == kMixFactorSocketId ||
         toSocketId == kMaskUtilityInputSocketId ||
         toSocketId == "r" || toSocketId == "g" || toSocketId == "b" || toSocketId == "a");
    const bool rawEdge = fromSocketId == kRawOutputSocketId && toSocketId == kRawInputSocketId;
    if (!imageEdge && !maskEdge && !rawEdge) {
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

void Graph::RefreshPrimaryOutputNode() {
    if (m_OutputNodeId > 0 && FindNode(m_OutputNodeId) && FindNode(m_OutputNodeId)->kind == NodeKind::Output) {
        return;
    }
    m_OutputNodeId = -1;
    for (const Node& node : m_Nodes) {
        if (node.kind == NodeKind::Output) {
            m_OutputNodeId = node.id;
            return;
        }
    }
}

} // namespace EditorNodeGraph
