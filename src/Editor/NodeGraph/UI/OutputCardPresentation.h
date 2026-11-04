#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"
#include <algorithm>

namespace Stack::Editor::NodeGraphUI {

struct OutputCardPresentation {
    bool connected = false;
    bool projectExportTargetsNode = false;
};

// Main-pipeline statistics belong to this card only for a sole completed output
// in the same graph. Projected RAW cards resolve export ownership separately
// through EditorModule::CanExportGraphOutput.
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
