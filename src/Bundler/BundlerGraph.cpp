#include "BundlerGraph.h"
#include <algorithm>

// ── File classification helpers ──────────────────────────────────────────────

const char* FileKindToString(FileKind kind) {
    switch (kind) {
        case FileKind::HTML:           return "HTML";
        case FileKind::CSS:            return "CSS";
        case FileKind::JavaScript:     return "JavaScript";
        case FileKind::StructuredText: return "Text/Data";
        case FileKind::OpaqueAsset:    return "Asset";
        case FileKind::Unknown:        return "Unknown";
    }
    return "Unknown";
}

FileKind ClassifyByExtension(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);

    if (e == ".html" || e == ".htm")                         return FileKind::HTML;
    if (e == ".css")                                         return FileKind::CSS;
    if (e == ".js" || e == ".mjs")                           return FileKind::JavaScript;

    if (e == ".json" || e == ".txt" || e == ".md" ||
        e == ".xml"  || e == ".svg" ||
        e == ".frag" || e == ".vert" || e == ".glsl" ||
        e == ".toml" || e == ".yaml" || e == ".yml")         return FileKind::StructuredText;

    if (e == ".png" || e == ".jpg" || e == ".jpeg" ||
        e == ".gif" || e == ".webp" || e == ".bmp" ||
        e == ".ico" || e == ".avif")                         return FileKind::OpaqueAsset;

    if (e == ".woff" || e == ".woff2" || e == ".ttf" ||
        e == ".otf" || e == ".eot")                          return FileKind::OpaqueAsset;

    if (e == ".mp3" || e == ".ogg" || e == ".wav" ||
        e == ".mp4" || e == ".webm")                         return FileKind::OpaqueAsset;

    if (e == ".wasm" || e == ".map" || e == ".pdf")           return FileKind::OpaqueAsset;

    return FileKind::Unknown;
}

// ── BundlerGraph ─────────────────────────────────────────────────────────────

void BundlerGraph::Clear() {
    m_Nodes.clear();
    m_Edges.clear();
    m_Diagnostics.clear();
    m_NextNodeId = 1;
    m_NextEdgeId = 1;
}

uint32_t BundlerGraph::AddNode(GraphNode node) {
    node.id = m_NextNodeId++;
    m_Nodes.push_back(std::move(node));
    return m_Nodes.back().id;
}

uint32_t BundlerGraph::AddEdge(GraphEdge edge) {
    edge.id = m_NextEdgeId++;
    m_Edges.push_back(std::move(edge));
    return m_Edges.back().id;
}

void BundlerGraph::AddDiagnostic(GraphDiagnostic diag) {
    m_Diagnostics.push_back(std::move(diag));
}

const GraphNode* BundlerGraph::GetNode(uint32_t id) const {
    for (const auto& n : m_Nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

GraphNode* BundlerGraph::GetNodeMut(uint32_t id) {
    for (auto& n : m_Nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const GraphNode* BundlerGraph::FindNodeByRelativePath(const std::string& relPath) const {
    // Normalize backslashes to forward slashes for comparison
    std::string normalized = relPath;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    for (const auto& n : m_Nodes) {
        std::string nodeRel = n.relativePath;
        std::replace(nodeRel.begin(), nodeRel.end(), '\\', '/');
        if (nodeRel == normalized) return &n;
    }
    return nullptr;
}

std::vector<const GraphEdge*> BundlerGraph::GetOutgoingEdges(uint32_t sourceNodeId) const {
    std::vector<const GraphEdge*> result;
    for (const auto& e : m_Edges) {
        if (e.sourceNodeId == sourceNodeId)
            result.push_back(&e);
    }
    return result;
}

BundlerGraph::KindCounts BundlerGraph::CountByKind() const {
    KindCounts c;
    for (const auto& n : m_Nodes) {
        switch (n.kind) {
            case FileKind::HTML:           ++c.html;    break;
            case FileKind::CSS:            ++c.css;     break;
            case FileKind::JavaScript:     ++c.js;      break;
            case FileKind::StructuredText: ++c.text;    break;
            case FileKind::OpaqueAsset:    ++c.asset;   break;
            case FileKind::Unknown:        ++c.unknown; break;
        }
    }
    return c;
}
