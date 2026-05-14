#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

bool ContainsNodeId(const std::vector<Node>& nodes, int id) {
    return std::any_of(nodes.begin(), nodes.end(), [id](const Node& node) {
        return node.id == id;
    });
}

} // namespace

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

    if (outputCount < 1) {
        result.messages.push_back(m_Nodes.empty()
            ? "Graph is empty. Press Tab or drop an image to begin."
            : "Add an output node or drop an image to start rendering.");
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
        result.messages.push_back("No completed output chains are connected.");
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
    return GetRenderLayerNodePath(ResolvePreviewOutputNodeId());
}

std::vector<int> Graph::GetRenderLayerNodePath(int outputNodeId) const {
    std::vector<int> reversePath;
    if (outputNodeId <= 0) {
        return {};
    }

    const Link* input = FindInputLink(outputNodeId, kImageInputSocketId);
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
    return GetRenderLayerIndexPath(ResolvePreviewOutputNodeId());
}

std::vector<int> Graph::GetRenderLayerIndexPath(int outputNodeId) const {
    std::vector<int> indices;
    for (int nodeId : GetRenderLayerNodePath(outputNodeId)) {
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

} // namespace EditorNodeGraph
