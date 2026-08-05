#pragma once

#include "Editor/NodeGraph/NodeGraphModelTypes.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace EditorNodeGraph {

// Immutable, derived topology state shared cheaply by graph copies. The
// authoritative node/link vectors remain on Graph; signatures prevent a cache
// built for one vector allocation or revision from being used by another.
struct GraphLookupCache {
    std::uint64_t revision = 0;
    const Node* nodesData = nullptr;
    const Link* linksData = nullptr;
    std::size_t nodeCount = 0;
    std::size_t linkCount = 0;
    std::unordered_map<int, std::size_t> nodeIndexById;
    std::unordered_map<int, std::vector<const Link*>> inputLinksByNode;
    std::unordered_map<int, std::vector<const Link*>> outputLinksByNode;
};

} // namespace EditorNodeGraph
