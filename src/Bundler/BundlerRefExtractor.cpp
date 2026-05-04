#include "BundlerRefExtractor.h"
#include <regex>
#include <sstream>

// ─────────────────────────────────────────────────────────────────────────────

const char* RefKindToString(RefKind kind) {
    switch (kind) {
        case RefKind::HtmlLink:        return "link";
        case RefKind::HtmlScript:      return "script";
        case RefKind::HtmlImg:         return "img";
        case RefKind::HtmlSource:      return "source";
        case RefKind::HtmlAnchor:      return "anchor";
        case RefKind::HtmlBase:        return "base";
        case RefKind::CssImport:       return "@import";
        case RefKind::CssUrl:          return "url()";
        case RefKind::JsImport:        return "import";
        case RefKind::JsDynamicImport: return "import()";
        case RefKind::JsWorker:        return "Worker";
    }
    return "unknown";
}

// ── Helper: count newlines before a position to get approximate line number ──

static int LineAtOffset(const std::string& content, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset && i < content.size(); ++i) {
        if (content[i] == '\n') ++line;
    }
    return line;
}

// ── Helper: skip external/data/mailto/javascript/blob URLs ───────────────────

static bool IsExternalOrSpecial(const std::string& specifier) {
    if (specifier.empty()) return true;
    if (specifier[0] == '#') return true;   // fragment-only
    // Check common schemes
    static const char* skipPrefixes[] = {
        "http://", "https://", "data:", "mailto:", "tel:",
        "blob:", "javascript:", "//", "about:"
    };
    for (const char* prefix : skipPrefixes) {
        if (specifier.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTML extraction
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ExtractedRef> ExtractHtmlRefs(const std::string& content) {
    std::vector<ExtractedRef> refs;

    // <base href="...">
    {
        std::regex re(R"(<base\b[^>]*\bhref\s*=\s*["']([^"']+)["'])",
                      std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            refs.push_back({RefKind::HtmlBase, spec,
                            LineAtOffset(content, (size_t)it->position())});
        }
    }

    // <link ... href="...">
    {
        std::regex re(R"(<link\b[^>]*\bhref\s*=\s*["']([^"']+)["'])",
                      std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::HtmlLink, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    // <script ... src="...">
    {
        std::regex re(R"(<script\b[^>]*\bsrc\s*=\s*["']([^"']+)["'])",
                      std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::HtmlScript, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    // <img ... src="...">
    {
        std::regex re(R"(<img\b[^>]*\bsrc\s*=\s*["']([^"']+)["'])",
                      std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::HtmlImg, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    // <source ... src="...">
    {
        std::regex re(R"(<source\b[^>]*\bsrc\s*=\s*["']([^"']+)["'])",
                      std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::HtmlSource, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    return refs;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSS extraction
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ExtractedRef> ExtractCssRefs(const std::string& content) {
    std::vector<ExtractedRef> refs;

    // @import "..." or @import url("...")
    {
        // @import url("...") or @import url('...') or @import url(...)
        std::regex reUrl(R"(@import\s+url\(\s*["']?([^"')]+)["']?\s*\))",
                         std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), reUrl);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::CssImport, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }

        // @import "..." or @import '...'  (no url() wrapper)
        std::regex reStr(R"(@import\s+["']([^"']+)["'])", std::regex::icase);
        begin = std::sregex_iterator(content.begin(), content.end(), reStr);
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::CssImport, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    // url("...") — general CSS references (fonts, backgrounds, etc.)
    {
        std::regex re(R"(url\(\s*["']?([^"')]+?)["']?\s*\))", std::regex::icase);
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            // Skip if already captured as @import above
            // Check if preceded by @import — simple heuristic
            size_t pos = (size_t)it->position();
            if (pos >= 8) {
                std::string before = content.substr(pos - 8, 8);
                // Normalize
                for (auto& c : before) c = (char)tolower(c);
                if (before.find("@import") != std::string::npos) continue;
            }
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::CssUrl, spec,
                                LineAtOffset(content, pos)});
        }
    }

    return refs;
}

// ─────────────────────────────────────────────────────────────────────────────
// JavaScript extraction
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ExtractedRef> ExtractJsRefs(const std::string& content) {
    std::vector<ExtractedRef> refs;

    // Static imports:  import ... from "..."  or  import "..."
    {
        // import ... from "..."
        std::regex re1(R"(import\s+(?:[\w{}\s,*]+\s+from\s+)?["']([^"']+)["'])");
        auto begin = std::sregex_iterator(content.begin(), content.end(), re1);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::JsImport, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    // Dynamic import:  import("...")
    {
        std::regex re(R"(import\s*\(\s*["']([^"']+)["']\s*\))");
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::JsDynamicImport, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    // new Worker("...")
    {
        std::regex re(R"(new\s+Worker\s*\(\s*["']([^"']+)["'])");
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string spec = (*it)[1].str();
            if (!IsExternalOrSpecial(spec))
                refs.push_back({RefKind::JsWorker, spec,
                                LineAtOffset(content, (size_t)it->position())});
        }
    }

    return refs;
}
