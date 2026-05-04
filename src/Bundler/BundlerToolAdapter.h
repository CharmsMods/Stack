#pragma once
#include <string>
#include <vector>

// ── Tool detection and subprocess execution ──────────────────────────────────
//
// The truth-source document recommends delegating transforms to best-in-class
// tools rather than reimplementing parsers.  This adapter detects which tools
// are available on the user's PATH and runs them as subprocesses.
// ─────────────────────────────────────────────────────────────────────────────

struct ToolInfo {
    std::string name;           // e.g. "esbuild"
    std::string path;           // full path or just the command name
    bool        available = false;
    std::string version;        // version string if detected
};

struct SubprocessResult {
    int         exitCode = -1;
    std::string stdOut;
    std::string stdErr;
    bool        succeeded() const { return exitCode == 0; }
};

class BundlerToolAdapter {
public:
    /// Probe the system PATH for known tools.
    void DetectTools();

    const ToolInfo& GetEsbuild()    const { return m_Esbuild; }
    const ToolInfo& GetLightningCSS() const { return m_LightningCSS; }
    const ToolInfo& GetMinifyHTML() const { return m_MinifyHTML; }
    const ToolInfo& GetTerser()     const { return m_Terser; }

    bool HasAnyTool() const;

    /// Run a command and capture stdout + stderr.
    static SubprocessResult RunCommand(const std::string& commandLine);

    // ── Per-file minify helpers ──────────────────────────────────────────

    /// Minify a JS file.  Returns true on success (output written to outPath).
    bool MinifyJS(const std::string& inPath, const std::string& outPath) const;

    /// Minify a CSS file.
    bool MinifyCSS(const std::string& inPath, const std::string& outPath) const;

    /// Minify an HTML file.
    bool MinifyHTML(const std::string& inPath, const std::string& outPath) const;

private:
    ToolInfo m_Esbuild;
    ToolInfo m_LightningCSS;
    ToolInfo m_MinifyHTML;
    ToolInfo m_Terser;
};
