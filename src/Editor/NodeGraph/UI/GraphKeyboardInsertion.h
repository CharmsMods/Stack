#pragma once

#include "Editor/NodeGraph/GraphCatalogInsertion.h"
#include "Editor/NodeGraph/GraphRoomLayout.h"
#include <unordered_map>
#include <unordered_set>

namespace Stack::Editor {

struct GraphKeyboardInsertion {
    enum class Stage { Find, Choose, Locked, Results, Closing, Inserted };
    struct Result {
        std::size_t catalogIndex;
        int rank;
        std::string error;
        std::optional<EditorNodeGraph::Vec2> input, output;
        EditorNodeGraph::GraphRect bounds{};
    };
    struct Position {
        EditorNodeGraph::Vec2 start, current, target;
        double delay = 0;
    };
    struct Frame {
        EditorNodeGraph::GraphRect start, current, target;
    };

    Stage stage = Stage::Find;
    int lastFrame = -1;
    std::uint64_t revision = 0, documentRevision = 0;
    const EditorNodeGraph::Graph* graph = nullptr;
    std::string query, error, roomError;
    bool selectAll = false, prepared = false, commit = false, roomMoving = false;
    int matched = 0, previousMatch = 0, anchor = 0, inserted = 0;
    std::vector<GraphModel::CatalogInsertionTarget> targets;
    std::size_t target = 0, result = 0;
    std::vector<Result> results;
    std::unordered_map<std::size_t, Result> compatibility;
    std::unordered_set<std::string> available;
    std::optional<EditorNodeGraph::Link> oldWire;
    GraphLayout::PlacementEdit placement;
    std::unordered_map<int, Position> nodes;
    std::unordered_map<int, Frame> groups;
    EditorNodeGraph::Vec2 ghostPosition{};
    EditorNodeGraph::GraphRect ghostBounds{};
    EditorNodeGraph::GraphRect ghostStart{}, ghostCurrent{};
    EditorNodeGraph::GraphRect ghostVisual{};
    std::size_t previewCatalog = ~std::size_t{};
    bool pickerPlaced = false;
    EditorNodeGraph::Vec2 pickerPosition{};
    float rowStart = 0, rowCurrent = 0, rowTarget = 0;
    double rowChanged = 0;
    EditorNodeGraph::Vec2 panStart{}, panTarget{};
    double opened = 0, ghostOpened = -1, matchChanged = 0, roomAfter = 0, roomStart = 0, panStartTime = -1, resultsStart = 0;
};
}
