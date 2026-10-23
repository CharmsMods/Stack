#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Notifications/NotificationPresenter.h"

#include <string>
#include <unordered_set>

namespace {
EditorNodeGraph::GraphOutputContext CurrentOutputDescriptions(EditorModule& editor) {
    EditorNodeGraph::GraphOutputContext context;
    context.resolveOutput = [&editor](const auto& node,const auto& socket)
        -> std::optional<EditorNodeGraph::GraphOutputDescription> {
        if (const auto* value=editor.GetGraphOutputDescription(node.id,socket)) return *value;
        return std::nullopt;
    };
    return context;
}
}

bool EditorNodeGraphUI::ConnectOutputToBestInput(EditorModule* editor, int fromNodeId, const std::string& fromSocketId, int toNodeId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    // Rank sockets using current local contracts. The selected connection's
    // command performs the one authoritative whole-document validation.
    EditorNodeGraph::ScopedGraphOutputAnalysis descriptions(graph,CurrentOutputDescriptions(*editor));
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
    const bool fromScalarImageStream =
        fromScalarStream && fromSocket.type == EditorNodeGraph::SocketType::Image;

    // A scalar image stream is already data being processed as an image (for example,
    // Channel Split -> Average).  When it is dropped onto an un-fed layer, keep it on
    // the layer's image input so image-safe operations such as Contrast continue to
    // operate on the scalar field.  Routing it to the layer's optional Mask input
    // instead leaves the layer's image path full-RGB and later causes an unnecessary
    // Image To Mask extractor when the result is used as a mask.
    const bool targetImageInputIsConnected =
        graph.FindAnyInputLink(toNodeId, EditorNodeGraph::kImageInputSocketId) != nullptr;
    if (fromScalarImageStream &&
        to->kind == EditorNodeGraph::NodeKind::Layer &&
        !targetImageInputIsConnected) {
        std::string error;
        if (graph.CanConnectSockets(
                fromNodeId,
                fromSocketId,
                toNodeId,
                EditorNodeGraph::kImageInputSocketId, nullptr) &&
            ConnectWithLayout(editor,
                fromNodeId,
                fromSocketId,
                toNodeId,
                EditorNodeGraph::kImageInputSocketId,
                &error)) {
            return true;
        }
    }

    if (fromSocket.type == EditorNodeGraph::SocketType::Mask || fromScalarStream) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input ||
                socket.id != EditorNodeGraph::kMaskInputSocketId) {
                continue;
            }
            std::string error;
            if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id, nullptr) &&
                ConnectWithLayout(editor,fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                return true;
            }
        }
    }
    if (!channel.empty()) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
            if (socket.direction == EditorNodeGraph::SocketDirection::Input && socket.id == channel) {
                std::string error;
                if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id, nullptr) &&
                    ConnectWithLayout(editor,fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                    return true;
                }
            }
        }
    }

    if (fromScalarStream && to->kind == EditorNodeGraph::NodeKind::Layer) {
        std::string error;
        if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, EditorNodeGraph::kImageInputSocketId, nullptr) &&
            ConnectWithLayout(editor,fromNodeId, fromSocketId, toNodeId, EditorNodeGraph::kImageInputSocketId, &error)) {
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
            if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id, nullptr) &&
                ConnectWithLayout(editor,fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
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
            if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id, nullptr) &&
                ConnectWithLayout(editor,fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
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
    EditorNodeGraph::ScopedGraphOutputAnalysis descriptions(graph,CurrentOutputDescriptions(*editor));
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
                if (graph.CanConnectSockets(fromNodeId, socket.id, toNodeId, toSocketId, nullptr) &&
                    ConnectWithLayout(editor,fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
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
            if (graph.CanConnectSockets(fromNodeId, socket.id, toNodeId, toSocketId, nullptr) &&
                ConnectWithLayout(editor,fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
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
            if (graph.CanConnectSockets(fromNodeId, socket.id, toNodeId, toSocketId, nullptr) &&
                ConnectWithLayout(editor,fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
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

bool EditorNodeGraphUI::ConnectWithLayout(EditorModule* editor,int from,const std::string& output,int to,
    const std::string& input,std::string* error) {
    const auto before=editor->GetGraphEditorContext();
    int splitId=0,producer=0;
    std::string sourceSocket;
    std::unordered_set<std::string> previousOutputs;
    std::unordered_map<std::string,EditorNodeGraph::Vec2> starts;
    for(int id:{from,to})if(const auto* split=before.graph->FindNode(id);split && split->kind==EditorNodeGraph::NodeKind::ChannelSplit)
        if(const auto* edge=before.graph->FindInputLink(id,EditorNodeGraph::kImageInputSocketId)) {
            splitId=id;producer=edge->fromNodeId;sourceSocket=edge->fromSocketId;
            for(const char* component:{"r","g","b","a"})starts.emplace(component,ScreenToGraph(OutputPinScreenPos(*split,component)));
            break;
        }
    // The input may be connected last, after the split's outputs were wired.
    if(!splitId)if(const auto* split=before.graph->FindNode(to);split && split->kind==EditorNodeGraph::NodeKind::ChannelSplit && input==EditorNodeGraph::kImageInputSocketId) {
        splitId=to;producer=from;sourceSocket=output;
        for(const char* component:{"r","g","b","a"})starts.emplace(component,ScreenToGraph(OutputPinScreenPos(*split,component)));
    }
    if(const auto* owner=before.graph->FindNode(producer))for(const auto& channel:owner->channelOutputs)previousOutputs.insert(channel.id);
    std::unordered_map<std::string,EditorNodeGraph::Vec2> placements;
    if(splitId)for(const auto& node:before.graph->GetNodes())placements.emplace(node.instanceUuid,DisplayNodePosition(node));
    auto motion=std::make_unique<ExpansionMotion>();
    if(splitId && ImGui::GetCurrentContext()->WithinFrameScope && !Stack::Notifications::SystemReducedMotion()) {
        motion->origin=CaptureNodeAppearance(editor,splitId,{m_CanvasMin.x,m_CanvasMin.y},{m_CanvasMax.x,m_CanvasMax.y},true);
        motion->originRect={ScreenToGraph({motion->origin.minimum.x,motion->origin.minimum.y}),
            ScreenToGraph({motion->origin.maximum.x,motion->origin.maximum.y})};
    }
    if(!editor->ConnectGraphSockets(from,output,to,input,error,
        [&](const auto& graph,const auto& node){return MeasureNodeForLayout(graph,node);}))return false;
    const auto after=editor->GetGraphEditorContext();
    if(splitId && !after.graph->FindNode(splitId) && !Stack::Notifications::SystemReducedMotion()) {
        const auto* owner=after.graph->FindNode(producer);
        if(owner) {
            motion->channelFold=true;
            motion->columns.emplace(producer,0.f);
            for(const auto& channel:owner->channelOutputs)if(!previousOutputs.count(channel.id) && channel.sourceSocket==sourceSocket && starts.count(channel.component))
                motion->socketOrigins.emplace(EditorNodeGraph::GraphOutputIdentity(producer,channel.id),starts.at(channel.component));
            const auto target=ScreenToGraph(OutputPinScreenPos(*owner,sourceSocket));
            motion->foldTarget={{target.x-4,target.y-4},{target.x+4,target.y+4}};
            m_ExpansionMotion=std::move(motion);
            for(const auto& node:after.graph->GetNodes())if(const auto start=placements.find(node.instanceUuid);start!=placements.end())
                AnimateNodePlacement(node,start->second);
            AdoptLayoutMotionOwner(after);++m_WireWorldRevision;
        }
    }
    return true;
}
