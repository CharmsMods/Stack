#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace EditorNodeGraph {
const ChannelOutput* FindChannelOutput(const Node& node, const std::string& socket);
SocketDefinition ChannelOutputSocket(int nodeId, const ChannelOutput& output);
std::string ChannelOutputId(const std::string& sourceSocket, const std::string& component, bool opaque = false);
std::string ExposeChannelOutput(Graph& graph, int nodeId, const std::string& sourceSocket,
    const std::string& component, bool opaque = false, std::string* error = nullptr);
bool ValidateChannelOutputs(const Node& node, const std::vector<SocketDefinition>& sockets, std::string& error);
bool NeedsOutputLowering(const Graph& graph);
nlohmann::json SerializeChannelOutputs(const std::vector<ChannelOutput>& outputs);
std::vector<ChannelOutput> DeserializeChannelOutputs(const nlohmann::json& value);
struct ChannelSplitFold { int split = 0, producer = 0; };
// Caller owns the candidate transaction. Incomplete/retained-boundary splits
// remain ordinary extractors; no authored image contract is weakened to fold.
std::vector<ChannelSplitFold> FoldChannelSplits(Graph& graph, const std::vector<int>& candidates = {});
}
