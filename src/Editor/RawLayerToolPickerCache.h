#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <imgui.h>

namespace Stack::Editor {
// Presentation stores identities, never borrowed node pointers. A committed
// edit can replace the graph's storage while the picker remains visible.
struct RawLayerToolPickerCache {
    struct Card { int id; int choice; ImVec2 position; std::string label; };
    struct Wire { ImVec2 from, to; };
    std::string documentId, layerId;
    std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t sourceRevision = std::numeric_limits<std::uint64_t>::max();
    std::string sourceUuid;
    std::vector<std::string> operationUuids, labels;
    std::vector<Card> cards;
    std::vector<Wire> wires;
};
}
