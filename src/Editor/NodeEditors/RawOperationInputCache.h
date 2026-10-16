#pragma once

#include "Project/NodeEditTarget.h"
#include "Graph/OutputDependencies.h"
#include "Raw/RawImageData.h"
#include <optional>
#include <unordered_map>

namespace Stack::Editor {

// Published by the existing graph snapshot pass. An edit may keep displaying
// the previous result while its next render is pending, so retain that input
// interpretation only for the same authored target and incoming endpoint.
class RawOperationInputCache {
public:
    struct Interpretation {
        GraphModel::Endpoint input;
        std::optional<Raw::RawWorkingSpace> workingSpace;
    };
    void Store(const Project::NodeEditTarget& target, const GraphModel::Endpoint& input,
        std::optional<Raw::RawWorkingSpace> workingSpace) {
        entries[target.Key()] = {input,workingSpace};
    }
    const Interpretation* Find(const Project::NodeEditTarget& target,
        const GraphModel::Endpoint& input) const {
        const auto found = entries.find(target.Key());
        return found != entries.end() && found->second.input == input ? &found->second : nullptr;
    }
private:
    std::unordered_map<std::string,Interpretation> entries;
};

} // namespace Stack::Editor
