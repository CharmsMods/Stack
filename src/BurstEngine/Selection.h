#pragma once

#include "Raw/Bracketing/Processor.h"
#include <functional>

namespace Stack::BurstEngine {

enum class SelectionIssue { Duplicate, Unsupported, Singleton, Unreadable, Incompatible };
struct SelectionDiagnostic {
    std::filesystem::path path;
    SelectionIssue issue = SelectionIssue::Unreadable;
    std::string message;
    std::filesystem::path relatedPath;
};
struct SelectedBurst {
    std::string identity, name;
    std::filesystem::path parentDirectory;
    std::vector<Raw::Bracketing::Source> sources;
    Raw::Bracketing::BracketingRecipe recipe;
};
struct SelectionRequest {
    std::vector<std::filesystem::path> paths;
    std::function<bool()> shouldCancel;
    std::function<void(std::size_t completed, std::size_t total, const std::string&)> reportProgress;
};
struct SelectionResult {
    bool canceled = false;
    std::vector<SelectedBurst> bursts;
    std::vector<SelectionDiagnostic> skipped;
    std::string error;
};

// Worker-thread operation. Reads only the explicit paths, orders them
// canonically, and applies Gallery's descriptor and folder-grouping policy.
// No catalog, workspace scan, project, output directory or cache is created.
// A canceled/failed analysis publishes no partial burst plan.
SelectionResult AnalyzeSelection(const SelectionRequest& request);

} // namespace Stack::BurstEngine
