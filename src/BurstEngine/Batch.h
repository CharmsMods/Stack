#pragma once
#include "Selection.h"
#include "Output.h"

namespace Stack::BurstEngine {
enum class OutputLocation { BesideOriginals, NewFolder };
struct BatchOptions {
    std::vector<Output::Format> formats{Output::Format::Dng};
    OutputLocation location=OutputLocation::NewFolder;
    bool preferGpuCombining=true;
};
struct BatchProgress {
    std::size_t set=0, total=0, outputs=0;
    double fraction=0;
    std::string detail;
};
struct BatchFailure { std::string set, message; };
struct BatchMeasurement {
    std::string set, resultIdentity, analysisIdentity;
    Raw::Bracketing::BracketingRecipe recipe;
    std::vector<Raw::Bracketing::Source> sources;
    std::map<std::string,double> stageSeconds;
    std::vector<std::string> diagnostics;
    std::uint64_t memoryBudgetBytes=0;
    std::uint32_t requestedWorkers=0;
    unsigned width=0,height=0;
    double processingSeconds=0,outputSeconds=0;
};
struct BatchResult {
    bool canceled=false;
    std::size_t completedSets=0;
    std::vector<std::filesystem::path> outputs;
    std::vector<BatchFailure> failures;
    std::vector<BatchMeasurement> measurements;
};
// Processes each admitted set once, then writes the requested formats from
// that result. Completed files survive cancellation or a later set failure.
BatchResult RunBatch(const SelectionResult&, const BatchOptions&,
    const std::function<bool()>& shouldCancel,
    const std::function<void(const BatchProgress&)>& reportProgress);
}
