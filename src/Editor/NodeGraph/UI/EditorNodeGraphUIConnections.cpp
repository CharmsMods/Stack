#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"

#include <string>
#include <unordered_set>

bool EditorNodeGraphUI::ConnectOutputToBestInput(EditorModule* editor, int fromNodeId, const std::string& fromSocketId, int toNodeId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to) {
        return false;
    }

    EditorNodeGraph::SocketDefinition fromSocket;
    if (!graph.FindSocket(fromNodeId, fromSocketId, &fromSocket)) {
        return false;
    }

    // Heuristic: If carrying specific color channel, prioritize exact matching channel socket first!
    std::unordered_set<int> visited;
    std::string channel = GetUpstreamChannel(graph, fromNodeId, fromSocketId, visited);
    const bool fromScalarStream = graph.IsScalarSocketStream(fromNodeId, fromSocketId);
    if (fromSocket.type == EditorNodeGraph::SocketType::Mask || fromScalarStream) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input ||
                socket.id != EditorNodeGraph::kMaskInputSocketId) {
                continue;
            }
            std::string error;
            if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                return true;
            }
        }
    }
    if (!channel.empty()) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
            if (socket.direction == EditorNodeGraph::SocketDirection::Input && socket.id == channel) {
                std::string error;
                if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                    editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                    return true;
                }
            }
        }
    }

    if (fromScalarStream && to->kind == EditorNodeGraph::NodeKind::Layer) {
        std::string error;
        if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, fromSocketId, toNodeId, EditorNodeGraph::kImageInputSocketId) &&
            editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, EditorNodeGraph::kImageInputSocketId, &error)) {
            return true;
        }
    }

    EditorNodeGraph::SocketType preferredType = fromSocket.type;
    if (from->kind == EditorNodeGraph::NodeKind::ChannelSplit || fromScalarStream) {
        preferredType = EditorNodeGraph::SocketType::Image;
    }

    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (socket.type == preferredType) {
            std::string error;
            if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                return true;
            }
        }
    }

    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (socket.type != preferredType) {
            std::string error;
            if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                return true;
            }
        }
    }

    return false;
}

bool EditorNodeGraphUI::ConnectBestOutputToInput(EditorModule* editor, int fromNodeId, int toNodeId, const std::string& toSocketId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to) {
        return false;
    }

    EditorNodeGraph::SocketDefinition toSocket;
    if (!graph.FindSocket(toNodeId, toSocketId, &toSocket)) {
        return false;
    }

    if (toSocketId == "r" || toSocketId == "g" || toSocketId == "b" || toSocketId == "a") {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
            if (socket.direction == EditorNodeGraph::SocketDirection::Output && socket.id == toSocketId) {
                std::string error;
                if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, socket.id, toNodeId, toSocketId) &&
                    editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
                    return true;
                }
            }
        }
    }

    EditorNodeGraph::SocketType preferredType = toSocket.type;
    if (to->kind == EditorNodeGraph::NodeKind::ChannelSplit && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        preferredType = EditorNodeGraph::SocketType::Image;
    }

    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (socket.type == preferredType) {
            std::string error;
            if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, socket.id, toNodeId, toSocketId) &&
                editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
                return true;
            }
        }
    }

    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (socket.type != preferredType) {
            std::string error;
            if (graph.CanConnectSocketsOrInsertExtractor(fromNodeId, socket.id, toNodeId, toSocketId) &&
                editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
                return true;
            }
        }
    }

    return false;
}

std::string EditorNodeGraphUI::GetUpstreamChannel(const EditorNodeGraph::Graph& graph, int nodeId, const std::string& socketId, std::unordered_set<int>& visited) {
    (void)visited;
    return graph.ResolveSocketChannel(nodeId, socketId);
}
