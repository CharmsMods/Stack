#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include <algorithm>

namespace Stack::Editor::NodeGraphUI {

struct OutputCardPresentation {
    bool connected = false;
    bool projectExportTargetsNode = false;
};

// The current export command has no endpoint argument. A node card may use it
// only when that command's project graph has exactly this one completed output.
// Connection and descriptor presentation remain useful for every output card.
inline OutputCardPresentation DescribeOutputCard(
    int nodeId, bool enabled,
    const std::vector<EditorNodeGraph::CompletedChainInfo>& completed,
    bool projectOutputCommandAvailable) {
    OutputCardPresentation result;
    result.connected = enabled && std::any_of(completed.begin(), completed.end(),
        [nodeId](const auto& chain) { return chain.outputNodeId == nodeId; });
    result.projectExportTargetsNode = result.connected && completed.size() == 1 &&
        projectOutputCommandAvailable;
    return result;
}

} // namespace Stack::Editor::NodeGraphUI
