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

bool Graph::CanConnectSockets(
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId,
    std::string* normalizedToSocketId,
    std::string* errorMessage) const {
    if (normalizedToSocketId) {
        *normalizedToSocketId = toSocketId;
    }

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

    const bool fromIsScalarStream = IsScalarSocketStream(fromNodeId, fromSocketId);

    const bool toScope = to->kind == NodeKind::Scope && toSocketId == kScopeInputSocketId;
    if (toScope) {
        if (fromSocket.type != SocketType::Image && !(fromSocket.type == SocketType::Mask && fromIsScalarStream)) {
            if (errorMessage) *errorMessage = "Scopes can analyze image or scalar outputs.";
            return false;
        }
        return true;
    }

    const bool toPreview = to->kind == NodeKind::Preview && toSocketId == kPreviewInputSocketId;
    if (toPreview) {
        if (fromSocket.type != SocketType::Image && !(fromSocket.type == SocketType::Mask && fromIsScalarStream)) {
            if (errorMessage) *errorMessage = "Preview nodes can inspect image or scalar outputs.";
            return false;
        }
        return true;
    }

    const std::string upstreamChannel = ResolveSocketChannel(fromNodeId, fromSocketId);
    if ((to->kind == NodeKind::Output || to->kind == NodeKind::Lut) &&
        toSocketId == kImageInputSocketId &&
        !upstreamChannel.empty()) {
        std::string ignored;
        const bool ok = CanConnectSockets(fromNodeId, fromSocketId, toNodeId, upstreamChannel, nullptr, errorMessage);
        if (ok && normalizedToSocketId) {
            *normalizedToSocketId = upstreamChannel;
        }
        (void)ignored;
        return ok;
    }

    if (fromSocket.type == SocketType::Raw || toSocket.type == SocketType::Raw) {
        const bool validRawLink =
            fromSocket.type == SocketType::Raw &&
            toSocket.type == SocketType::Raw &&
            (from->kind == NodeKind::RawSource || from->kind == NodeKind::RawNeuralDenoise) &&
            fromSocketId == kRawOutputSocketId &&
            (to->kind == NodeKind::RawNeuralDenoise || to->kind == NodeKind::RawDecode || to->kind == NodeKind::RawDevelop) &&
            toSocketId == kRawInputSocketId;
        if (!validRawLink) {
            if (errorMessage) *errorMessage = "RAW sockets connect from RAW Source or RAW neural nodes into RAW neural, RAW Decode, or RAW Develop nodes.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask) {
        const bool validScalarSource =
            ((from->kind == NodeKind::MaskGenerator ||
              from->kind == NodeKind::MaskCombine ||
              from->kind == NodeKind::MaskUtility ||
              from->kind == NodeKind::CustomMask ||
              from->kind == NodeKind::ImageToMask ||
              from->kind == NodeKind::RawDetailAutoMask ||
              from->kind == NodeKind::RawDetailFusion) && fromSocketId == kMaskOutputSocketId) ||
            (from->kind == NodeKind::ChannelSplit && IsChannelSocketId(fromSocketId)) ||
            fromIsScalarStream;

        const bool isScalarToScalar = fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Mask;
        const bool isScalarImageToScalar = fromSocket.type == SocketType::Image && toSocket.type == SocketType::Mask && fromIsScalarStream;
        const bool isScalarToImage = fromSocket.type == SocketType::Mask && toSocket.type == SocketType::Image;

        if (isScalarToScalar || isScalarImageToScalar) {
            const bool validScalarTarget =
                (to->kind == NodeKind::Layer && toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::Lut && toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::RawDevelop && toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::RawDetailFusion && toSocketId == kMaskInputSocketId) ||
                (to->kind == NodeKind::Mix && toSocketId == kMixFactorSocketId) ||
                (to->kind == NodeKind::MaskCombine &&
                    (toSocketId == kMaskCombineInputASocketId || toSocketId == kMaskCombineInputBSocketId)) ||
                (to->kind == NodeKind::MaskUtility && toSocketId == kMaskUtilityInputSocketId) ||
                (to->kind == NodeKind::Lut && IsChannelSocketId(toSocketId)) ||
                (to->kind == NodeKind::ChannelCombine && IsChannelSocketId(toSocketId)) ||
                (to->kind == NodeKind::Output && IsChannelSocketId(toSocketId));
            if (!validScalarSource || !validScalarTarget) {
                if (errorMessage) *errorMessage = "Scalar outputs can connect to layer masks, mix factors, scalar utilities, channels, or RGBA outputs.";
                return false;
            }
        } else if (isScalarToImage) {
            const bool validImageTarget =
                (to->kind == NodeKind::Layer && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::Lut && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::RawDetailAutoMask && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::RawDetailFusion && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::Mix && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId)) ||
                (to->kind == NodeKind::Output && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::ImageToMask && toSocketId == kImageToMaskInputSocketId) ||
                (to->kind == NodeKind::ChannelSplit && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::DataMath && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId));
            if (!validScalarSource || !validImageTarget) {
                if (errorMessage) *errorMessage = "Scalar outputs can connect to layer/image inputs, Blend Images, Data Math, scalar converters, split nodes, or the output node.";
                return false;
            }
        } else {
            if (errorMessage) *errorMessage = "Cannot connect a full image output directly to a scalar input.";
            return false;
        }

        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (fromSocket.type != SocketType::Image || toSocket.type != SocketType::Image) {
        if (errorMessage) *errorMessage = "Only image sockets can be connected in the render chain in this pass.";
        return false;
    }
    if (!IsRenderChainNode(*from) || !IsRenderChainNode(*to) || to->kind == NodeKind::Image || to->kind == NodeKind::RawSource || to->kind == NodeKind::RawDecode || to->kind == NodeKind::RawDevelop || from->kind == NodeKind::Output) {
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
    if (to->kind == NodeKind::Lut && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the LUT image input.";
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
    if (to->kind == NodeKind::HdrMerge &&
        toSocketId != kHdrMergeInput1SocketId &&
        toSocketId != kHdrMergeInput2SocketId &&
        toSocketId != kHdrMergeInput3SocketId) {
        if (errorMessage) *errorMessage = "Image links must target an HDR Merge image input.";
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
    if (to->kind == NodeKind::DataMath && toSocketId != kMixInputASocketId && toSocketId != kMixInputBSocketId) {
        if (errorMessage) *errorMessage = "Image links must target Data Math input A or B.";
        return false;
    }
    if (to->kind != NodeKind::Layer && to->kind != NodeKind::Lut && to->kind != NodeKind::RawDetailAutoMask && to->kind != NodeKind::RawDetailFusion && to->kind != NodeKind::HdrMerge && to->kind != NodeKind::Output && to->kind != NodeKind::Mix && to->kind != NodeKind::ImageToMask && to->kind != NodeKind::ChannelSplit && to->kind != NodeKind::DataMath) {
        if (errorMessage) *errorMessage = "Image links must target a layer, LUT, HDR Merge, blend node, data math node, split node, scalar converter, or the output.";
        return false;
    }
    if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        if (errorMessage) *errorMessage = "That connection would create a cycle.";
        return false;
    }

    return true;
}

bool Graph::TryConnectSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    std::string resolvedToSocketId = toSocketId;
    if (!CanConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, &resolvedToSocketId, errorMessage)) {
        return false;
    }

    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SocketDefinition fromSocket;
    SocketDefinition toSocket;
    if (!from || !to || !FindSocket(fromNodeId, fromSocketId, &fromSocket) || !FindSocket(toNodeId, resolvedToSocketId, &toSocket)) {
        if (errorMessage) *errorMessage = "Invalid socket connection.";
        return false;
    }

    const bool toScope = to->kind == NodeKind::Scope && resolvedToSocketId == kScopeInputSocketId;
    if (toScope) {
        RemoveScopeLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, resolvedToSocketId));
        TouchStructure();
        return true;
    }

    const bool toPreview = to->kind == NodeKind::Preview && resolvedToSocketId == kPreviewInputSocketId;
    if (toPreview) {
        RemoveScopeLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, resolvedToSocketId));
        TouchStructure();
        return true;
    }

    if (fromSocket.type == SocketType::Raw || toSocket.type == SocketType::Raw) {
        RemoveRenderLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, resolvedToSocketId));
        TouchStructure();
        return true;
    }

    if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask) {
        if ((to->kind == NodeKind::Output || to->kind == NodeKind::Lut) && IsChannelSocketId(resolvedToSocketId)) {
            RemoveLinksForNodeInput(toNodeId, kImageInputSocketId);
        }
        RemoveLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, resolvedToSocketId));
        TouchStructure();
        return true;
    }

    if ((to->kind == NodeKind::Output || to->kind == NodeKind::Lut) && resolvedToSocketId == kImageInputSocketId) {
        RemoveLinksForNodeInput(toNodeId, "r");
        RemoveLinksForNodeInput(toNodeId, "g");
        RemoveLinksForNodeInput(toNodeId, "b");
        RemoveLinksForNodeInput(toNodeId, "a");
    }
    RemoveRenderLinksForNodeInput(toNodeId, resolvedToSocketId);
    if (from->kind == NodeKind::Image) {
        ActivateImageNode(fromNodeId);
    }
    m_Links.push_back(MakeSocketLink(fromNodeId, fromSocketId, toNodeId, resolvedToSocketId));
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
    (void)fromSocketId;
    (void)toSocketId;
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
            // Raw Develop's hidden pre-finish output is computed before the integrated finish mask
            // is applied, so image-to-mask links sourced from it do not form a true feedback cycle.
            if (link.fromSocketId == kPreFinishImageOutputSocketId) {
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
