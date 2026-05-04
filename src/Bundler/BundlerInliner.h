#pragma once
#include "BundlerGraph.h"
#include <string>
#include <vector>

// ── Inlining logic for "Single HTML" mode ────────────────────────────────────

struct InlineResult {
    bool success = true;
    std::string content;
    std::string errorMessage;
    int itemsInlined = 0;
};

class BundlerInliner {
public:
    /// Inline all local references (CSS, JS, small assets) into a single HTML string.
    static InlineResult InlineHTML(const GraphNode& entryNode, const BundlerGraph& graph);

private:
    static std::string ProcessHTML(const std::string& content, const GraphNode& sourceNode, const BundlerGraph& graph, int& count);
    static std::string ProcessCSS(const std::string& content, const GraphNode& sourceNode, const BundlerGraph& graph, int& count);
};
