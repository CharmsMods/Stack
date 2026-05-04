#include "BundlerInliner.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>

// ── Base64 Encoding Helper ───────────────────────────────────────────────────

static const std::string base64_chars =
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static std::string base64_encode(const std::vector<unsigned char>& data) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t in_len = data.size();
    const unsigned char* bytes_to_encode = data.data();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

// ── File Utility ─────────────────────────────────────────────────────────────

static std::string ReadTextFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::vector<unsigned char> ReadBinaryFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

static std::string GetMimeType(const std::string& path) {
    std::string ext = "";
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    
    for (auto& c : ext) c = (char)tolower(c);
    
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    
    return "application/octet-stream";
}

// ─────────────────────────────────────────────────────────────────────────────
// Inliner Implementation
// ─────────────────────────────────────────────────────────────────────────────

InlineResult BundlerInliner::InlineHTML(const GraphNode& entryNode, const BundlerGraph& graph) {
    InlineResult result;
    std::string content = ReadTextFile(entryNode.absolutePath);
    if (content.empty()) {
        result.success = false;
        result.errorMessage = "Could not read entry file: " + entryNode.absolutePath;
        return result;
    }

    int count = 0;
    result.content = ProcessHTML(content, entryNode, graph, count);
    result.itemsInlined = count;
    
    return result;
}

std::string BundlerInliner::ProcessHTML(const std::string& content, const GraphNode& sourceNode, const BundlerGraph& graph, int& count) {
    std::string result = content;
    auto edges = graph.GetOutgoingEdges(sourceNode.id);

    // Sort edges by reverse position in file to avoid offset issues during replacement
    // But our extractor doesn't save character offsets, only lines.
    // For MVP, we'll do string replacements of the unique specifiers.
    // WARNING: This assumes specifiers are unique enough or we do one-by-one replacement.
    
    for (const auto* edge : edges) {
        if (!edge->resolved) continue;
        const GraphNode* target = graph.GetNode(edge->targetNodeId);
        if (!target) continue;

        if (edge->refKind == RefKind::HtmlScript) {
            std::string js = ReadTextFile(target->absolutePath);
            if (!js.empty()) {
                // Replace <script src="spec"> with <script>js</script>
                // We use a simplified regex approach
                std::string tagPattern = "<script\\b[^>]*\\bsrc\\s*=\\s*[\"']" + std::regex_replace(edge->originalSpecifier, std::regex(R"([-[\]{}()*+?.,\^$|#\s])"), R"(\$&)") + "[\"'][^>]*>\\s*</script>";
                std::regex re(tagPattern, std::regex::icase);
                result = std::regex_replace(result, re, "<script>\n" + js + "\n</script>");
                count++;
            }
        }
        else if (edge->refKind == RefKind::HtmlLink) {
            // Check if it's a stylesheet
            if (target->kind == FileKind::CSS) {
                std::string css = ReadTextFile(target->absolutePath);
                if (!css.empty()) {
                    // Recursive CSS inlining (e.g. images in CSS)
                    int subCount = 0;
                    css = ProcessCSS(css, *target, graph, subCount);
                    count += subCount;

                    std::string tagPattern = "<link\\b[^>]*\\bhref\\s*=\\s*[\"']" + std::regex_replace(edge->originalSpecifier, std::regex(R"([-[\]{}()*+?.,\^$|#\s])"), R"(\$&)") + "[\"'][^>]*>";
                    std::regex re(tagPattern, std::regex::icase);
                    result = std::regex_replace(result, re, "<style>\n" + css + "\n</style>");
                    count++;
                }
            }
        }
        else if (edge->refKind == RefKind::HtmlImg) {
            auto data = ReadBinaryFile(target->absolutePath);
            if (!data.empty()) {
                std::string b64 = base64_encode(data);
                std::string dataUrl = "data:" + GetMimeType(target->absolutePath) + ";base64," + b64;
                
                size_t pos = 0;
                while ((pos = result.find(edge->originalSpecifier, pos)) != std::string::npos) {
                    result.replace(pos, edge->originalSpecifier.length(), dataUrl);
                    pos += dataUrl.length();
                }
                count++;
            }
        }
    }

    return result;
}

std::string BundlerInliner::ProcessCSS(const std::string& content, const GraphNode& sourceNode, const BundlerGraph& graph, int& count) {
    std::string result = content;
    auto edges = graph.GetOutgoingEdges(sourceNode.id);

    for (const auto* edge : edges) {
        if (!edge->resolved) continue;
        const GraphNode* target = graph.GetNode(edge->targetNodeId);
        if (!target) continue;

        if (edge->refKind == RefKind::CssUrl || edge->refKind == RefKind::CssImport) {
            if (target->kind == FileKind::OpaqueAsset || target->kind == FileKind::StructuredText) {
                auto data = ReadBinaryFile(target->absolutePath);
                if (!data.empty()) {
                    std::string b64 = base64_encode(data);
                    std::string dataUrl = "data:" + GetMimeType(target->absolutePath) + ";base64," + b64;
                    
                    size_t pos = 0;
                    while ((pos = result.find(edge->originalSpecifier, pos)) != std::string::npos) {
                        result.replace(pos, edge->originalSpecifier.length(), dataUrl);
                        pos += dataUrl.length();
                    }
                    count++;
                }
            }
        }
    }

    return result;
}
