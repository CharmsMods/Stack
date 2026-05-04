#pragma once
#include "BundlerGraph.h"
#include <string>
#include <vector>

// ── Scan result passed back through the job queue ────────────────────────────

struct ScanResult {
    BundlerGraph graph;
    std::string  errorMessage;       // non-empty on failure
    int          refsFound = 0;
    int          refsResolved = 0;
    int          refsUnresolved = 0;
};

/// Scan a set of input paths (files or folders).
ScanResult ScanInputs(const std::vector<std::string>& inputPaths, const std::string& computedRoot);

/// After scanning, read parseable files and extract references, populating
/// edges and diagnostics in the graph.  Also runs on a worker thread.
void ParseReferences(ScanResult& result);
