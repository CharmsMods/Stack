#pragma once

#include <imgui.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace EditorNodeGraph { struct Node; }
namespace Stack::Editor { struct GraphEditorContext; }

namespace Stack::Editor::NodeGraphUI {

// Authored text stays in the graph. This owner holds only wrapping, scrolling
// and one uncommitted editing session for a single canvas/project.
class NodeNotesView {
public:
    struct Commit {
        std::string documentId, graphId, instanceUuid, text;
        int nodeId = -1;
    };
    void BeginFrame(bool revealHeld,
        const std::function<bool(const Commit&)>& targetExists = {});
    // Topology/preview redraws discard wrapping caches, not authored drafts.
    void InvalidateLayouts();
    void Reset();
    bool Revealed() const { return held_ || edit_.has_value() || pending_.has_value() || next_.has_value(); }
    bool Editing() const { return edit_.has_value(); }
    int EditingNode() const { return edit_ ? edit_->target.nodeId : -1; }
    bool Draw(const EditorNodeGraph::Node& node, const GraphEditorContext& context,
        std::uint64_t editRevision, ImVec2 minimum, ImVec2 maximum,
        float scale, ImVec4 textColor, ImVec4 mutedColor, bool hovered, float opacity);
    bool HasPendingCommit() const { return pending_.has_value(); }
    // Acknowledge queued text before activating a clicked successor note.
    // Failure restores the outgoing draft rather than consuming its text.
    bool CommitPending(const std::function<bool(const Commit&)>& commit);
    // Explicit owner transitions flush this canvas only. A rejected commit
    // keeps the draft, and finishing a background owner never steals focus.
    bool FinishEditing(const std::function<bool(const Commit&)>& commit);

private:
    struct Line { std::size_t start = 0, length = 0; };
    struct Layout {
        std::string uuid, text;
        std::uint64_t revision = ~std::uint64_t{0};
        ImFont* font = nullptr;
        float width = 0, fontSize = 0, scroll = 0;
        std::vector<Line> lines;
    };
    struct Edit {
        Commit target;
        std::string original;
        bool focus = true, waitForFRelease = true;
    };
    std::unordered_map<int, Layout> layouts_;
    std::optional<Edit> edit_;
    std::optional<Edit> pending_, next_;
    bool held_ = false;
    void Finish(bool cancel);
};
} // namespace Stack::Editor::NodeGraphUI
