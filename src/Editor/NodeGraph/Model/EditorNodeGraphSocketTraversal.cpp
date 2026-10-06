#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"

namespace EditorNodeGraph {
std::string Graph::ResolveSocketChannel(int nodeId, const std::string& socketId) const {
    return OutputChannelColor(DescribeGraphOutput(*this, nodeId, socketId));
}

bool Graph::IsScalarSocketStream(int nodeId, const std::string& socketId) const {
    return IsSingleChannelValue(DescribeGraphOutput(*this, nodeId, socketId).descriptor.logicalType);
}
} // namespace EditorNodeGraph
