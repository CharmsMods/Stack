#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "BundlerRefExtractor.h"

// ── File classification ──────────────────────────────────────────────────────

enum class FileKind {
    HTML,            // .html, .htm
    CSS,             // .css
    JavaScript,      // .js, .mjs
    StructuredText,  // .json, .txt, .frag, .vert, .glsl, .xml, .svg, .md
    OpaqueAsset,     // images, fonts, audio, video, wasm, etc.
    Unknown
};

const char* FileKindToString(FileKind kind);
FileKind    ClassifyByExtension(const std::string& extension);

// ── Graph node ───────────────────────────────────────────────────────────────

struct GraphNode {
    uint32_t    id = 0;
    std::string absolutePath;           // canonical filesystem path
    std::string relativePath;           // relative to project root
    FileKind    kind = FileKind::Unknown;
    uint64_t    fileSize = 0;
    bool        parsed = false;         // true after reference extraction
};

// ── Graph edge ───────────────────────────────────────────────────────────────

struct GraphEdge {
    uint32_t    id = 0;
    uint32_t    sourceNodeId = 0;
    uint32_t    targetNodeId = 0;       // 0 = unresolved
    std::string originalSpecifier;      // e.g. "./main.css" as written in source
    RefKind     refKind;
    int         sourceLine = 0;
    bool        resolved = false;
};

// ── Diagnostic ───────────────────────────────────────────────────────────────

struct GraphDiagnostic {
    enum class Severity { Info, Warning, Error };
    Severity    severity = Severity::Warning;
    uint32_t    nodeId = 0;
    int         line = 0;
    std::string message;
};

// ── The graph itself ─────────────────────────────────────────────────────────

class BundlerGraph {
public:
    void Clear();

    /// Add a node, returns its id.
    uint32_t AddNode(GraphNode node);

    /// Add an edge, returns its id.
    uint32_t AddEdge(GraphEdge edge);

    /// Add a diagnostic.
    void AddDiagnostic(GraphDiagnostic diag);

    /// Lookup helpers.
    const GraphNode* GetNode(uint32_t id) const;
    GraphNode*       GetNodeMut(uint32_t id);
    const GraphNode* FindNodeByRelativePath(const std::string& relPath) const;

    const std::vector<GraphNode>& GetNodes() const { return m_Nodes; }
    const std::vector<GraphEdge>& GetEdges() const { return m_Edges; }
    const std::vector<GraphDiagnostic>& GetDiagnostics() const { return m_Diagnostics; }

    /// Get all edges originating from a given source node.
    std::vector<const GraphEdge*> GetOutgoingEdges(uint32_t sourceNodeId) const;

    /// Summary counts by kind.
    struct KindCounts {
        int html = 0;
        int css = 0;
        int js = 0;
        int text = 0;
        int asset = 0;
        int unknown = 0;
        int Total() const { return html + css + js + text + asset + unknown; }
    };
    KindCounts CountByKind() const;

private:
    std::vector<GraphNode>       m_Nodes;
    std::vector<GraphEdge>       m_Edges;
    std::vector<GraphDiagnostic> m_Diagnostics;
    uint32_t m_NextNodeId = 1;
    uint32_t m_NextEdgeId = 1;
};
