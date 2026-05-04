#pragma once
#include "BundlerGraph.h"
#include "BundlerToolAdapter.h"
#include <string>
#include <vector>
#include <functional>

// ── Build configuration ──────────────────────────────────────────────────────

enum class BuildMode {
    SingleHTML  = 0,
    PerFile     = 1,
    BundleByType = 2
};

struct BuildConfig {
    BuildMode   mode = BuildMode::PerFile;
    std::string projectRoot;
    std::string outputDir;       // defaults to projectRoot/dist if empty
};

// ── Per-file build result ────────────────────────────────────────────────────

struct FileBuildResult {
    uint32_t    nodeId = 0;
    std::string inputPath;
    std::string outputPath;
    uint64_t    inputSize = 0;
    uint64_t    outputSize = 0;
    bool        minified = false;   // true if a tool was used; false = plain copy
    bool        success = true;
    std::string errorMessage;
};

// ── Overall build result ─────────────────────────────────────────────────────

struct BuildResult {
    bool success = true;
    std::string errorMessage;
    std::vector<FileBuildResult> fileResults;
    int filesProcessed = 0;
    int filesMinified = 0;
    int filesCopied = 0;
    int filesFailed = 0;
    uint64_t totalInputSize = 0;
    uint64_t totalOutputSize = 0;
};

// ── Progress callback ────────────────────────────────────────────────────────

using BuildProgressFn = std::function<void(int current, int total, const std::string& fileName)>;

// ── Build functions (run on worker thread) ───────────────────────────────────

/// Run a per-file minify build.
BuildResult RunPerFileBuild(const BundlerGraph& graph,
                            const BuildConfig& config,
                            const BundlerToolAdapter& tools,
                            BuildProgressFn progressFn);

/// Run a single-HTML build (inlining all resources).
BuildResult RunSingleHTMLBuild(const BundlerGraph& graph,
                               const BuildConfig& config,
                               const BundlerToolAdapter& tools,
                               BuildProgressFn progressFn);

/// Run a bundle-by-type build (bundle.js, bundle.css).
BuildResult RunBundleByTypeBuild(const BundlerGraph& graph,
                                 const BuildConfig& config,
                                 const BundlerToolAdapter& tools,
                                 BuildProgressFn progressFn);
