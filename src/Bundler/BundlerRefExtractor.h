#pragma once
#include <string>
#include <vector>

// ── A single extracted reference from a source file ──────────────────────────

enum class RefKind {
    HtmlLink,           // <link href="...">
    HtmlScript,         // <script src="...">
    HtmlImg,            // <img src="...">
    HtmlSource,         // <source src="..." srcset="...">
    HtmlAnchor,         // <a href="..."> (informational only)
    HtmlBase,           // <base href="...">
    CssImport,          // @import "..." or @import url("...")
    CssUrl,             // url("...")
    JsImport,           // import ... from "..."
    JsDynamicImport,    // import("...")
    JsWorker            // new Worker("...")
};

const char* RefKindToString(RefKind kind);

struct ExtractedRef {
    RefKind     kind;
    std::string specifier;      // the raw URL/path as written in the source
    int         line = 0;       // 1-based line number (approximate)
};

// ── Extraction functions — run on worker thread, no ImGui ────────────────────

/// Extract references from an HTML file's content.
std::vector<ExtractedRef> ExtractHtmlRefs(const std::string& content);

/// Extract references from a CSS file's content.
std::vector<ExtractedRef> ExtractCssRefs(const std::string& content);

/// Extract references from a JavaScript file's content.
std::vector<ExtractedRef> ExtractJsRefs(const std::string& content);
