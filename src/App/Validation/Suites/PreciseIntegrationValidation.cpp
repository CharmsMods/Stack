#include "App/Validation/ValidationSuites.h"

#include "Raw/RawPreciseIntegration.h"
#include "Raw/RawPreciseNativeRuntime.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Renderer/GLLoader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

namespace Stack::Validation {
namespace {

struct SourceInput {
    std::filesystem::path path;
    std::string partition;
};

struct Options {
    std::vector<SourceInput> sources;
    std::filesystem::path output;
    int proxyMaxDimension = 256;
    int featureMaxDimension = 256;
    int warmMaxDimension = 2048;
};

bool ParseInteger(const char* text, int minimum, int& output) {
    try {
        output = std::max(minimum, std::stoi(text ? text : ""));
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                error = std::string(name) + " requires a value.";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--development-file" || arg == "--validation-file") {
            const char* path = value(arg.c_str());
            if (!path) return false;
            options.sources.push_back({
                path,
                arg == "--development-file" ? "development" : "validation"
            });
        } else if (arg == "--output") {
            const char* path = value("--output");
            if (!path) return false;
            options.output = path;
        } else if (arg == "--proxy-max-dimension") {
            const char* text = value("--proxy-max-dimension");
            if (!text || !ParseInteger(text, 64, options.proxyMaxDimension)) {
                error = "--proxy-max-dimension must be an integer >= 64.";
                return false;
            }
        } else if (arg == "--feature-max-dimension") {
            const char* text = value("--feature-max-dimension");
            if (!text || !ParseInteger(text, 64, options.featureMaxDimension)) {
                error = "--feature-max-dimension must be an integer >= 64.";
                return false;
            }
        } else if (arg == "--warm-max-dimension") {
            const char* text = value("--warm-max-dimension");
            if (!text || !ParseInteger(text, 128, options.warmMaxDimension)) {
                error = "--warm-max-dimension must be an integer >= 128.";
                return false;
            }
        } else {
            error = "Unknown precise integration validation option: " + arg;
            return false;
        }
    }
    if (options.sources.empty() || options.output.empty()) {
        error = "Provide development/validation RAW inputs and an output report.";
        return false;
    }
    for (const SourceInput& source : options.sources) {
        if (!std::filesystem::is_regular_file(source.path)) {
            error = "RAW input does not exist: " + source.path.filename().string();
            return false;
        }
    }
    return true;
}

std::uint64_t SourceHashFromIdentity(const std::string& identity) {
    try {
        const std::uint64_t value = std::stoull(identity.substr(0, 16), nullptr, 16);
        return value == 0 ? 1 : value;
    } catch (...) {
        return 1;
    }
}

bool SameOutsideSolverSpace(
    const RawRecipe::RawDevelopmentRecipe& a,
    const RawRecipe::RawDevelopmentRecipe& b) {
    RawRecipe::RawDevelopmentRecipe normalizedA = a;
    RawRecipe::RawDevelopmentRecipe normalizedB = b;
    normalizedA.preToneExposureEv = normalizedB.preToneExposureEv;
    normalizedA.localRange = normalizedB.localRange;
    normalizedA.finishTone = normalizedB.finishTone;
    normalizedA.viewTransform = normalizedB.viewTransform;
    return RawRecipe::SerializeRecipe(normalizedA) ==
        RawRecipe::SerializeRecipe(normalizedB);
}

nlohmann::json RunSource(
    const SourceInput& source,
    const Options& options,
    std::uint64_t requestId,
    bool& passed) {
    passed = false;
    const RawEvidence::SourceIdentity sourceIdentity =
        RawEvidence::ComputeSourceIdentity(source.path);
    if (!sourceIdentity.valid) {
        return {
            { "fileName", source.path.filename().string() },
            { "partition", source.partition },
            { "status", "source-identity-failed" },
            { "reason", sourceIdentity.reason }
        };
    }

    RawRecipe::RawDevelopmentRecipe base = RawRecipe::MakeDefaultRecipe(
        source.path.string(), source.path.filename().string());
    base.source.relativePathKey = source.path.filename().string();
    base.source.fingerprint = sourceIdentity.sha256;
    base.source.fileSizeBytes = sourceIdentity.byteSize;
    const std::string baseBytes = PreciseRaw::CanonicalRecipeBytes(base);

    PreciseIntegration::IntegrationState lifecycle;
    const std::uint64_t sourceHash = SourceHashFromIdentity(sourceIdentity.sha256);
    PreciseIntegration::SolveIdentity identity = PreciseIntegration::BeginSolve(
        lifecycle,
        base.source.relativePathKey,
        sourceHash,
        sourceIdentity.sha256,
        base);
    PreciseIntegration::MarkRunning(lifecycle, requestId);
    identity = lifecycle.identity;

    PreciseIntegration::NativeSolveRequest request;
    request.identity = identity;
    request.inputRecipe = base;
    request.proxyMaxDimension = options.proxyMaxDimension;
    request.featureMaxDimension = options.featureMaxDimension;
    request.warmMaxDimension = options.warmMaxDimension;
    int progressEvents = 0;
    std::string lastProgressLabel;
    PreciseIntegration::NativeSolveCallbacks callbacks;
    callbacks.reportProgress = [&](const std::string& label, int, int) {
        ++progressEvents;
        if (label != lastProgressLabel) {
            std::cout << "  " << label << std::endl;
            lastProgressLabel = label;
        }
    };
    const PreciseIntegration::NativeSolveResult solved =
        PreciseIntegration::RunNativePreciseSolve(request, callbacks);
    const bool searchDidNotMutate =
        baseBytes == PreciseRaw::CanonicalRecipeBytes(base);

    PreciseIntegration::ApplyContext context;
    context.expected = identity;
    context.activeSourceKey = identity.sourceKey;
    context.selectedSourceKey = identity.sourceKey;
    context.activeSourceHash = sourceHash;
    context.projectActive = true;
    context.recipeBacked = true;
    context.currentRecipe = base;
    const PreciseIntegration::ApplyDecision decision =
        PreciseIntegration::ValidateForAtomicApply(solved.candidate, context);

    RawRecipe::RawDevelopmentRecipe live = base;
    std::vector<RawRecipe::RawDevelopmentRecipe> undo;
    int applyCount = 0;
    if (decision.allowed && decision.changesRecipe) {
        undo.push_back(live);
        live = decision.recipe;
        ++applyCount;
    }
    const bool selectedEqualsApplied = decision.allowed &&
        PreciseIntegration::VisibleProjectionMatches(decision.recipe, live);
    const RawRecipe::RawDevelopmentRecipe reopened =
        RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(live));
    const bool persistenceExact =
        PreciseIntegration::VisibleProjectionMatches(live, reopened);
    const bool noAutomaticRerun = applyCount <= 1;
    bool oneUndoExact = true;
    if (!undo.empty()) {
        live = undo.back();
        undo.pop_back();
        oneUndoExact = undo.empty() &&
            PreciseIntegration::VisibleProjectionMatches(base, live);
    }

    RawRecipe::RawDevelopmentRecipe failureLive = base;
    const std::string failureBefore = PreciseRaw::CanonicalRecipeBytes(failureLive);
    PreciseIntegration::VerifiedCandidate rejected = solved.candidate;
    rejected.fullVerification = RawOptimizer::FullVerificationDisposition::Rejected;
    const bool failedApplyRejected =
        !PreciseIntegration::ValidateForAtomicApply(rejected, context).allowed;
    const bool failureAtomic = failedApplyRejected &&
        failureBefore == PreciseRaw::CanonicalRecipeBytes(failureLive);

    PreciseIntegration::ApplyContext switched = context;
    switched.selectedSourceKey = "different-source";
    const bool sourceSwitchRejected =
        !PreciseIntegration::ValidateForAtomicApply(solved.candidate, switched).allowed;
    PreciseIntegration::ApplyContext sameKeyChanged = context;
    ++sameKeyChanged.activeSourceHash;
    const bool sameKeyIdentityRejected =
        !PreciseIntegration::ValidateForAtomicApply(
            solved.candidate, sameKeyChanged).allowed;
    PreciseIntegration::ApplyContext recipeEdited = context;
    recipeEdited.currentRecipe.preToneExposureEv += 0.125f;
    const bool recipeEditRejected =
        !PreciseIntegration::ValidateForAtomicApply(
            solved.candidate, recipeEdited).allowed;

    PreciseIntegration::IntegrationState cancelState;
    PreciseIntegration::BeginSolve(
        cancelState,
        identity.sourceKey,
        identity.sourceHash,
        identity.sourceIdentity,
        base);
    const bool cancellationSafe =
        PreciseIntegration::Cancel(cancelState, "validation cancel") &&
        !cancelState.active &&
        cancelState.state == PreciseIntegration::LifecycleState::Canceled;
    PreciseIntegration::IntegrationState fastState;
    fastState.mode = PreciseIntegration::ProductMode::Fast;
    const bool fastModeAvailable =
        std::string(PreciseIntegration::ProductModeName(fastState.mode)) == "Fast";
    const bool outsideFieldsPreserved = decision.allowed &&
        SameOutsideSolverSpace(base, decision.recipe);

    const PreciseIntegration::ProjectionSummary projection = decision.allowed
        ? PreciseIntegration::SummarizeProjection(base, decision.recipe)
        : PreciseIntegration::ProjectionSummary();
    passed = solved.candidate.valid && decision.allowed &&
        searchDidNotMutate && selectedEqualsApplied && persistenceExact &&
        noAutomaticRerun && oneUndoExact && failureAtomic &&
        sourceSwitchRejected && sameKeyIdentityRejected && recipeEditRejected &&
        cancellationSafe && fastModeAvailable && outsideFieldsPreserved &&
        progressEvents > 0;

    return {
        { "fileName", source.path.filename().string() },
        { "partition", source.partition },
        { "status", passed ? "passed" : "failed" },
        { "reason", decision.allowed ? solved.reason : decision.reason },
        { "solverVersion", solved.candidate.solverVersion },
        { "integrationVersion", solved.candidate.integrationVersion },
        { "runtimeVersion", solved.runtimeVersion },
        { "solveStatus", RawOptimizer::SolveStatusName(solved.candidate.solveStatus) },
        { "fullVerification", RawOptimizer::FullVerificationDispositionName(
            solved.candidate.fullVerification) },
        { "selectedParameters", PreciseRaw::SerializeParameters(
            PreciseRaw::ExtractParameters(solved.candidate.selected.recipe)) },
        { "changedGroups", projection.changedGroups },
        { "unchangedGroups", projection.unchangedGroups },
        { "proxyRenderCount", solved.candidate.proxyRenderCount },
        { "proxyCacheHits", solved.candidate.proxyCacheHits },
        { "fullResolution", {
            { "width", solved.candidate.fullWidth },
            { "height", solved.candidate.fullHeight },
            { "trueFullResolution", solved.candidate.trueFullResolution },
            { "allFiveStages", solved.candidate.allFiveFullResolutionStages }
        } },
        { "gates", {
            { "searchDidNotMutate", searchDidNotMutate },
            { "applyDecisionAllowed", decision.allowed },
            { "singleAtomicApply", applyCount <= 1 },
            { "selectedEqualsApplied", selectedEqualsApplied },
            { "oneUndoExact", oneUndoExact },
            { "persistenceExact", persistenceExact },
            { "noAutomaticRerun", noAutomaticRerun },
            { "failedApplyAtomic", failureAtomic },
            { "sourceSwitchRejected", sourceSwitchRejected },
            { "sameKeyIdentityRejected", sameKeyIdentityRejected },
            { "recipeEditRejected", recipeEditRejected },
            { "cancellationSafe", cancellationSafe },
            { "fastModeAvailable", fastModeAvailable },
            { "outsideSolverFieldsPreserved", outsideFieldsPreserved },
            { "progressReported", progressEvents > 0 },
            { "passed", passed }
        } }
    };
}

bool WriteReport(
    const std::filesystem::path& output,
    const nlohmann::json& report,
    std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        error = "Could not create integration report folder.";
        return false;
    }
    const std::filesystem::path temporary = output.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            error = "Could not open temporary integration report.";
            return false;
        }
        stream << report.dump(2) << "\n";
        if (!stream.good()) {
            error = "Could not write temporary integration report.";
            return false;
        }
    }
    std::filesystem::remove(output, ec);
    ec.clear();
    std::filesystem::rename(temporary, output, ec);
    if (ec) {
        error = "Could not publish integration report: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

bool ValidatePreciseIntegration(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "Phase 06 precise integration validation failed: " << error << "\n";
        return false;
    }
    if (!glfwInit()) {
        std::cerr << "Phase 06 precise integration could not initialize GLFW.\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(
        64, 64, "Phase 06 Precise Integration", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "Phase 06 precise integration could not create a hidden OpenGL window.\n";
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "Phase 06 precise integration could not load OpenGL functions.\n";
        return false;
    }
    nlohmann::json records = nlohmann::json::array();
    bool allPassed = true;
    std::uint64_t requestId = 1;
    for (const SourceInput& source : options.sources) {
        std::cout << "Phase 06 integration: " << source.path.filename().string() << std::endl;
        bool passed = false;
        records.push_back(RunSource(source, options, requestId++, passed));
        allPassed = allPassed && passed;
    }
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    const nlohmann::json report = {
        { "reportVersion", "raw-precise-integration-report-v1" },
        { "integrationVersion", PreciseIntegration::kIntegrationVersion },
        { "runtimeVersion", PreciseIntegration::kNativeRuntimeVersion },
        { "solverVersion", PreciseDryRun::kDryRunSolverVersion },
        { "sourceManifestVersion", "corpus-manifest-v1" },
        { "validationSubsetVersion", "phase-06-engineering-subset-v1" },
        { "sourceCount", records.size() },
        { "lockedPartitionTouched", false },
        { "humanReviewClaimed", false },
        { "fastModeRetained", true },
        { "continuousAutoRerun", false },
        { "status", allPassed ? "complete" : "failed" },
        { "records", std::move(records) }
    };
    if (!WriteReport(options.output, report, error)) {
        std::cerr << "Phase 06 precise integration validation failed: " << error << "\n";
        return false;
    }
    if (!allPassed) {
        std::cerr << "Phase 06 precise integration gates failed.\n";
        return false;
    }
    std::cout << "Phase 06 precise integration validation passed; no hidden output was applied.\n";
    return true;
}

} // namespace Stack::Validation
