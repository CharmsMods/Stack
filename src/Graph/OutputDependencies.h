#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace Stack::GraphModel {

// An omitted output conservatively depends on every connected input. An
// explicitly listed output depends only on the listed inputs, including none.
struct OutputDependency {
    std::string output;
    std::vector<std::string> inputs;
};

inline bool OutputDependsOnInput(const std::vector<OutputDependency>& declarations,
    const std::string& output, const std::string& input) {
    const auto found = std::find_if(declarations.begin(), declarations.end(),
        [&](const auto& rule) { return rule.output == output; });
    return found == declarations.end() ||
        std::find(found->inputs.begin(), found->inputs.end(), input) != found->inputs.end();
}

enum class NodeRole { Ordinary, OriginalImage, CurrentImage, LayerResult, Reference };

struct Endpoint {
    std::string graphId;
    std::string nodeUuid;
    std::string portId;

    bool operator==(const Endpoint& other) const {
        return graphId == other.graphId && nodeUuid == other.nodeUuid && portId == other.portId;
    }
    bool Empty() const { return graphId.empty() || nodeUuid.empty() || portId.empty(); }
};

} // namespace Stack::GraphModel
