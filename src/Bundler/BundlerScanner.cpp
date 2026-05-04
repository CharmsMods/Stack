#include "BundlerScanner.h"
#include "BundlerRefExtractor.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Directory scanning (Phase 2 — unchanged logic)
// ─────────────────────────────────────────────────────────────────────────────

ScanResult ScanInputs(const std::vector<std::string>& inputPaths, const std::string& computedRoot) {
    ScanResult result;

    std::error_code ec;
    fs::path root = fs::canonical(computedRoot, ec);
    if (ec) {
        result.errorMessage = "Cannot resolve computed root: " + computedRoot + " (" + ec.message() + ")";
        return result;
    }

    int htmlCount = 0;

    for (const auto& pathStr : inputPaths) {
        fs::path p = fs::canonical(pathStr, ec);
        if (ec) { ec.clear(); continue; }

        if (fs::is_directory(p, ec)) {
            for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) { ec.clear(); continue; }
                if (!it->is_regular_file(ec)) continue;

                const fs::path& filePath = it->path();

                // Skip hidden files / directories
                bool skipHidden = false;
                for (const auto& component : filePath.lexically_relative(p)) {
                    std::string part = component.string();
                    if (!part.empty() && part[0] == '.') { skipHidden = true; break; }
                }
                if (skipHidden) continue;

                // Skip common tooling / output directories
                std::string relStrRoot = filePath.lexically_relative(root).string();
                std::string relStr = filePath.lexically_relative(p).string();
                if (relStr.rfind("node_modules", 0) == 0 ||
                    relStr.rfind("dist",         0) == 0 ||
                    relStr.rfind("build",        0) == 0)
                { continue; }

                GraphNode node;
                node.absolutePath = filePath.string();
                node.relativePath = relStrRoot;
                node.kind         = ClassifyByExtension(filePath.extension().string());
                if (node.kind == FileKind::HTML) htmlCount++;

                node.fileSize     = static_cast<uint64_t>(fs::file_size(filePath, ec));
                if (ec) { node.fileSize = 0; ec.clear(); }

                result.graph.AddNode(std::move(node));
            }
        } else if (fs::is_regular_file(p, ec)) {
            std::string relStrRoot = p.lexically_relative(root).string();
            GraphNode node;
            node.absolutePath = p.string();
            node.relativePath = relStrRoot;
            node.kind         = ClassifyByExtension(p.extension().string());
            if (node.kind == FileKind::HTML) htmlCount++;

            node.fileSize     = static_cast<uint64_t>(fs::file_size(p, ec));
            if (ec) { node.fileSize = 0; ec.clear(); }

            result.graph.AddNode(std::move(node));
        }
    }

    if (htmlCount > 1) {
        result.errorMessage = "Multiple HTML files found (" + std::to_string(htmlCount) + "). Only a single HTML file is supported right now.";
        result.graph = BundlerGraph(); // Clear partial graph
    } else if (htmlCount == 0) {
        // Warning or error? Some people might just want to minify JS.
        // We'll let it pass, minifying just JS/CSS is valid too.
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reference parsing (Phase 3)
// ─────────────────────────────────────────────────────────────────────────────

/// Read an entire text file into a string.
static std::string ReadFileContent(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

/// Resolve a specifier relative to a source node's directory.
/// Returns the relative path from the project root if a matching node exists.
static std::string ResolveSpecifier(const std::string& specifier,
                                    const std::string& sourceRelPath)
{
    // Get the directory of the source file (relative to root)
    fs::path sourceDir = fs::path(sourceRelPath).parent_path();

    // Build the candidate path: sourceDir / specifier
    fs::path candidate = sourceDir / specifier;

    // Normalize (resolve ../ and ./ )
    fs::path normalized = candidate.lexically_normal();

    // Convert to forward slashes for consistent comparison
    std::string result = normalized.string();
    std::replace(result.begin(), result.end(), '\\', '/');

    return result;
}

void ParseReferences(ScanResult& result) {
    // We need a copy of the nodes list because we iterate and mutate
    auto& graph = result.graph;
    const auto& nodes = graph.GetNodes();

    for (const auto& node : nodes) {
        // Only parse first-class file types
        if (node.kind != FileKind::HTML &&
            node.kind != FileKind::CSS &&
            node.kind != FileKind::JavaScript)
        { continue; }

        std::string content = ReadFileContent(node.absolutePath);
        if (content.empty()) continue;

        // Extract references
        std::vector<ExtractedRef> refs;
        switch (node.kind) {
            case FileKind::HTML:       refs = ExtractHtmlRefs(content); break;
            case FileKind::CSS:        refs = ExtractCssRefs(content);  break;
            case FileKind::JavaScript: refs = ExtractJsRefs(content);   break;
            default: break;
        }

        // Mark node as parsed
        GraphNode* mutableNode = graph.GetNodeMut(node.id);
        if (mutableNode) mutableNode->parsed = true;

        // Resolve each reference
        for (const auto& ref : refs) {
            std::string resolvedRel = ResolveSpecifier(ref.specifier,
                                                        node.relativePath);

            const GraphNode* target = graph.FindNodeByRelativePath(resolvedRel);

            GraphEdge edge;
            edge.sourceNodeId     = node.id;
            edge.originalSpecifier = ref.specifier;
            edge.refKind          = ref.kind;
            edge.sourceLine       = ref.line;

            if (target) {
                edge.targetNodeId = target->id;
                edge.resolved     = true;
                ++result.refsResolved;
            } else {
                edge.targetNodeId = 0;
                edge.resolved     = false;
                ++result.refsUnresolved;

                // Add diagnostic
                GraphDiagnostic diag;
                diag.severity = GraphDiagnostic::Severity::Warning;
                diag.nodeId   = node.id;
                diag.line     = ref.line;
                diag.message  = "Unresolved reference: \"" + ref.specifier
                              + "\" (tried: " + resolvedRel + ")";
                graph.AddDiagnostic(diag);
            }

            graph.AddEdge(std::move(edge));
            ++result.refsFound;
        }
    }
}
