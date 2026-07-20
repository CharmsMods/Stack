#include "App/Validation/ValidationSuites.h"

#include "Raw/RawOptimizerSelection.h"
#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

using namespace RawOptimizer;
using json = nlohmann::json;

constexpr const char* kFrozenPhase03Sha256 =
    "abdc9368b86d10e76b78e912ece70c3854b97ecd38a7cfe2c5b0fdaaed94ad06";

struct Options {
    std::filesystem::path surfaceReport;
    std::filesystem::path output;
};

struct MethodRuns {
    SearchResult warm;
    SearchResult pattern;
    SearchResult trust;
    bool warmDeterministic = false;
    bool patternDeterministic = false;
    bool trustDeterministic = false;
};

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i] ? argv[i] : "";
        if ((argument == "--surface-report" || argument == "--output") && i + 1 < argc) {
            const std::filesystem::path value = argv[++i];
            if (argument == "--surface-report") options.surfaceReport = value;
            else options.output = value;
        } else {
            std::cerr << "Unknown or incomplete optimizer benchmark argument: " << argument << '\n';
            return false;
        }
    }
    if (options.surfaceReport.empty() || options.output.empty()) {
        std::cerr << "Usage: --validate-raw-optimizer-benchmarks --surface-report <phase-03.json> --output <phase-04.json>\n";
        return false;
    }
    return true;
}

bool ReadFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);
    if (size < 0) return false;
    bytes.resize(static_cast<std::size_t>(size));
    if (size > 0) input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input.good() || input.eof();
}

bool WriteJson(const std::filesystem::path& path, const json& value) {
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << value.dump(2) << '\n';
    return output.good();
}

std::string PointId(const std::string& prefix, const std::vector<double>& point) {
    std::ostringstream stream;
    stream << prefix << ':';
    stream.precision(12);
    for (double value : point) stream << value << '|';
    return stream.str();
}

MeritComponent Component(
    std::string id,
    MeritTier tier,
    MeritRole role,
    double value,
    double meaningfulDelta = 0.0,
    double uncertainty = 0.0,
    std::string units = "normalized-loss") {
    MeritComponent result;
    result.id = std::move(id);
    result.tier = tier;
    result.role = role;
    result.valid = std::isfinite(value);
    result.value = value;
    result.meaningfulDelta = meaningfulDelta;
    result.uncertainty = uncertainty;
    result.units = std::move(units);
    return result;
}

SearchEvaluation AnalyticEvaluation(
    const std::string& id,
    const std::vector<double>& point,
    double goalViolation,
    double editDistance,
    double rawViolation = 0.0,
    double meaningfulDelta = 1.0e-6) {
    SearchEvaluation result;
    result.candidateId = PointId(id, point);
    result.disposition = rawViolation > 0.0
        ? EvaluationDisposition::Rejected
        : EvaluationDisposition::Complete;
    result.point = point;
    result.hardConstraintsPassed = rawViolation <= 0.0;
    result.merit = {
        Component("identity.violation", MeritTier::Tier0IdentityState,
            MeritRole::ConstraintViolation, 0.0),
        Component("raw.constraint.violation", MeritTier::Tier1RawArtifact,
            MeritRole::ConstraintViolation, rawViolation, meaningfulDelta),
        Component("technical.goal.violation", MeritTier::Tier2Technical,
            MeritRole::GoalViolation, goalViolation, meaningfulDelta),
        Component("edit.normalized_l1", MeritTier::Tier5TieBreaker,
            MeritRole::TieBreaker, editDistance, meaningfulDelta)
    };
    return result;
}

SearchBudget BenchmarkBudget() {
    SearchBudget budget;
    budget.maxUniqueEvaluations = 48;
    budget.maxAcceptedIterations = 16;
    budget.stableIterationsRequired = 2;
    budget.maxRuntimeMs = 60000.0;
    budget.initialNormalizedRadius = 0.25;
    budget.minimumNormalizedRadius = 0.03125;
    budget.maximumNormalizedRadius = 0.50;
    budget.shrinkFactor = 0.50;
    budget.expandFactor = 1.50;
    budget.maximumBoundaryOscillations = 4;
    return budget;
}

SearchBudget SelectedSolverBudget() {
    SearchBudget budget = BenchmarkBudget();
    budget.requireFullResolutionVerification = true;
    return budget;
}

json StableResultJson(SearchResult result) {
    result.runtimeMs = 0.0;
    result.warmStart.runtimeMs = 0.0;
    result.selected.runtimeMs = 0.0;
    json serialized = SerializeSearchResult(result);
    serialized["replayWallRuntimeExcluded"] = true;
    return serialized;
}

json DeterminismSignature(const SearchResult& result) {
    json trace = json::array();
    for (const SearchTraceEvent& event : result.trace) {
        trace.push_back({
            { "kind", event.kind }, { "point", event.point },
            { "comparison", ComparisonResultName(event.comparison) },
            { "accepted", event.accepted }, { "cacheHit", event.cacheHit },
            { "activeMeritIndex", event.activeMeritIndex }
        });
    }
    return {
        { "method", SearchMethodName(result.method) },
        { "status", SolveStatusName(result.status) },
        { "selectedPoint", result.selected.point },
        { "proposedEvaluations", result.proposedEvaluations },
        { "uniqueEvaluations", result.uniqueEvaluations },
        { "cacheHits", result.cacheHits },
        { "acceptedIterations", result.acceptedIterations },
        { "boundaryOscillations", result.boundaryOscillations },
        { "trace", std::move(trace) }
    };
}

MethodRuns RunAllMethods(
    const SearchProblem& problem,
    const SearchBudget& budget,
    const SearchCallbacks& callbacks) {
    MethodRuns runs;
    runs.warm = RunWarmStartOnly(problem, budget, callbacks);
    const SearchResult warmRepeat = RunWarmStartOnly(problem, budget, callbacks);
    runs.pattern = RunStageOrderedPatternSearch(problem, budget, callbacks);
    const SearchResult patternRepeat = RunStageOrderedPatternSearch(problem, budget, callbacks);
    runs.trust = RunDiagonalQuadraticTrustRegion(problem, budget, callbacks);
    const SearchResult trustRepeat = RunDiagonalQuadraticTrustRegion(problem, budget, callbacks);
    runs.warmDeterministic = DeterminismSignature(runs.warm) == DeterminismSignature(warmRepeat);
    runs.patternDeterministic = DeterminismSignature(runs.pattern) == DeterminismSignature(patternRepeat);
    runs.trustDeterministic = DeterminismSignature(runs.trust) == DeterminismSignature(trustRepeat);
    return runs;
}

json SerializeRuns(const MethodRuns& runs) {
    return {
        { "warmOnly", StableResultJson(runs.warm) },
        { "stageOrderedPatternSearch", StableResultJson(runs.pattern) },
        { "diagonalQuadraticTrustRegion", StableResultJson(runs.trust) },
        { "repeatability", {
            { "warmOnly", runs.warmDeterministic },
            { "stageOrderedPatternSearch", runs.patternDeterministic },
            { "diagonalQuadraticTrustRegion", runs.trustDeterministic }
        } }
    };
}

bool SafeImprovement(const SearchResult& result) {
    return result.selected.disposition == EvaluationDisposition::Complete &&
        result.selected.hardConstraintsPassed &&
        CompareEvaluations(result.selected, result.warmStart) == ComparisonResult::Better;
}

json RunAnalyticCases(bool& allPatternGates) {
    json cases = json::array();
    allPatternGates = true;
    auto add = [&](const std::string& id, const SearchProblem& problem,
                   SearchBudget budget, const SearchCallbacks& callbacks,
                   const std::function<bool(const SearchResult&)>& patternGate,
                   const std::string& expected) {
        const MethodRuns runs = RunAllMethods(problem, budget, callbacks);
        const bool gate = patternGate(runs.pattern) && runs.patternDeterministic;
        allPatternGates = allPatternGates && gate;
        cases.push_back({
            { "id", id }, { "expectedPatternBehavior", expected },
            { "patternGatePassed", gate }, { "methods", SerializeRuns(runs) }
        });
    };

    SearchProblem quadratic;
    quadratic.id = "analytic/bounded-quadratic";
    quadratic.dimensions = { { "x", -1.0, 1.0, true, 0, {} } };
    quadratic.warmStart = { 0.85 };
    SearchCallbacks quadraticCallbacks;
    quadraticCallbacks.evaluate = [](const std::vector<double>& point) {
        return AnalyticEvaluation("quadratic", point,
            std::pow(point[0] + 0.2, 2.0), std::abs(point[0] - 0.85));
    };
    add(quadratic.id, quadratic, BenchmarkBudget(), quadraticCallbacks,
        SafeImprovement, "meaningful safe improvement");

    SearchProblem coupled;
    coupled.id = "analytic/coupled-valley";
    coupled.dimensions = {
        { "x", -1.0, 1.0, true, 0, {} },
        { "y", -1.0, 1.0, true, 1, {} }
    };
    coupled.warmStart = { 0.8, 0.7 };
    coupled.interactionPairs = { { 0, 1 } };
    SearchCallbacks coupledCallbacks;
    coupledCallbacks.evaluate = [](const std::vector<double>& point) {
        const double u = point[0] + point[1] - 0.2;
        const double v = point[0] - point[1] + 0.1;
        return AnalyticEvaluation("coupled", point, u * u + 0.2 * v * v,
            std::abs(point[0] - 0.8) + std::abs(point[1] - 0.7));
    };
    add(coupled.id, coupled, BenchmarkBudget(), coupledCallbacks,
        SafeImprovement, "interaction-aware safe improvement");

    SearchProblem plateau = quadratic;
    plateau.id = "analytic/plateau-discontinuity";
    plateau.warmStart = { 0.75 };
    SearchCallbacks plateauCallbacks;
    plateauCallbacks.evaluate = [](const std::vector<double>& point) {
        const double loss = point[0] > 0.30 ? 1.0 :
            (point[0] < -0.55 ? 0.15 : 0.35);
        return AnalyticEvaluation("plateau", point, loss,
            std::abs(point[0] - 0.75), 0.0, 0.02);
    };
    add(plateau.id, plateau, BenchmarkBudget(), plateauCallbacks,
        SafeImprovement, "deterministic improvement across a plateau edge");

    SearchProblem ripple = quadratic;
    ripple.id = "analytic/sub-tolerance-ripple";
    ripple.warmStart = { 0.0 };
    SearchCallbacks rippleCallbacks;
    rippleCallbacks.evaluate = [](const std::vector<double>& point) {
        return AnalyticEvaluation("ripple", point,
            1.0 + 1.0e-5 * std::sin(100.0 * point[0]), std::abs(point[0]), 0.0, 0.001);
    };
    add(ripple.id, ripple, BenchmarkBudget(), rippleCallbacks,
        [](const SearchResult& result) {
            return result.status == SolveStatus::WarmStartRetained && result.fallbackToWarmStart;
        }, "retain warm start below meaningful delta");

    SearchProblem unsafe = quadratic;
    unsafe.id = "analytic/unsafe-warm-feasibility";
    unsafe.dimensions[0].lower = -1.0;
    unsafe.dimensions[0].upper = 0.5;
    unsafe.warmStart = { 0.5 };
    SearchCallbacks unsafeCallbacks;
    unsafeCallbacks.evaluate = [](const std::vector<double>& point) {
        return AnalyticEvaluation("unsafe", point, std::abs(point[0] + 0.4),
            std::abs(point[0] - 0.5), std::max(0.0, point[0] + 0.2), 1.0e-4);
    };
    add(unsafe.id, unsafe, BenchmarkBudget(), unsafeCallbacks,
        SafeImprovement, "restore a hard-constraint-safe candidate");

    SearchCallbacks failedCallbacks;
    failedCallbacks.evaluate = [](const std::vector<double>& point) {
        SearchEvaluation result;
        result.candidateId = PointId("failed", point);
        result.point = point;
        result.disposition = EvaluationDisposition::Failed;
        result.reason = "injected candidate render failure";
        return result;
    };
    add("analytic/render-failure", quadratic, BenchmarkBudget(), failedCallbacks,
        [](const SearchResult& result) { return result.status == SolveStatus::CandidateRenderFailed; },
        "report candidate render failure distinctly");

    SearchCallbacks missingCallbacks = failedCallbacks;
    missingCallbacks.evaluate = [](const std::vector<double>& point) {
        SearchEvaluation result;
        result.candidateId = PointId("missing", point);
        result.point = point;
        result.disposition = EvaluationDisposition::MissingEvidence;
        result.reason = "injected missing stage evidence";
        return result;
    };
    add("analytic/missing-evidence", quadratic, BenchmarkBudget(), missingCallbacks,
        [](const SearchResult& result) { return result.status == SolveStatus::BlockedByMissingEvidence; },
        "report missing evidence distinctly");

    SearchCallbacks canceledCallbacks = quadraticCallbacks;
    canceledCallbacks.shouldCancel = [] { return true; };
    add("analytic/cancellation", quadratic, BenchmarkBudget(), canceledCallbacks,
        [](const SearchResult& result) { return result.status == SolveStatus::Canceled; },
        "cancel before evaluation without mutation");

    SearchBudget fullBudget = BenchmarkBudget();
    fullBudget.requireFullResolutionVerification = true;
    SearchCallbacks fullCallbacks = quadraticCallbacks;
    fullCallbacks.verifyFullResolution = [&](const SearchEvaluation& evaluation) {
        return evaluation.point == quadratic.warmStart
            ? FullVerificationDisposition::Accepted
            : FullVerificationDisposition::Rejected;
    };
    add("analytic/full-resolution-rejection", quadratic, fullBudget, fullCallbacks,
        [&](const SearchResult& result) {
            return result.status == SolveStatus::FullResolutionVerificationRejected &&
                result.selected.point == quadratic.warmStart && result.fallbackToWarmStart;
        }, "reject proxy winner and retain warm fallback");
    return cases;
}

const json* FindSurface(const json& record, const std::string& id) {
    if (!record.contains("surfaces") || !record["surfaces"].is_array()) return nullptr;
    for (const json& surface : record["surfaces"]) {
        if (surface.value("id", "") == id) return &surface;
    }
    return nullptr;
}

const json* FindEvaluation(const json& record, const std::string& evaluationId) {
    if (!record.contains("uniqueEvaluations") || !record["uniqueEvaluations"].is_array()) return nullptr;
    for (const json& evaluation : record["uniqueEvaluations"]) {
        if (evaluation.value("evaluationId", "") == evaluationId) return &evaluation;
    }
    return nullptr;
}

const json* FindConstraint(const json& evaluation, const std::string& id) {
    if (!evaluation.contains("constraints") || !evaluation["constraints"].is_array()) return nullptr;
    for (const json& constraint : evaluation["constraints"]) {
        if (constraint.value("id", "") == id) return &constraint;
    }
    return nullptr;
}

double JsonNumber(const json& object, const char* key, double fallback = 0.0) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<double>() : fallback;
}

SearchEvaluation FrozenEvaluation(
    const json& record,
    const json& sample,
    double warmExposure) {
    SearchEvaluation result;
    result.candidateId = sample.value("candidateId", "missing-candidate-id");
    result.point = { JsonNumber(sample, "axisX") };
    const std::string status = sample.value("status", "failed");
    if (status == "complete") result.disposition = EvaluationDisposition::Complete;
    else if (status == "rejected") result.disposition = EvaluationDisposition::Rejected;
    else if (status == "missing-evidence") result.disposition = EvaluationDisposition::MissingEvidence;
    else result.disposition = EvaluationDisposition::Failed;
    result.hardConstraintsPassed = result.disposition == EvaluationDisposition::Complete;

    const json* evaluation = FindEvaluation(record, sample.value("evaluationId", ""));
    if (!evaluation) {
        result.disposition = EvaluationDisposition::MissingEvidence;
        result.hardConstraintsPassed = false;
        result.reason = "Phase 03 evaluation record is missing.";
        return result;
    }

    double tier0Violations = 0.0;
    double otherRawViolations = 0.0;
    for (const json& constraint : evaluation->value("constraints", json::array())) {
        if (constraint.value("status", "") == "passed") continue;
        const std::string tier = constraint.value("tier", "");
        const std::string id = constraint.value("id", "");
        if (tier == "tier-0-identity-state") tier0Violations += 1.0;
        if (tier == "tier-1-raw-artifact" && id != "raw.wb_scaled_headroom") {
            otherRawViolations += 1.0;
        }
    }
    const json* headroom = FindConstraint(*evaluation, "raw.wb_scaled_headroom");
    const bool headroomValid = headroom && headroom->contains("value") && headroom->contains("limit") &&
        (*headroom)["value"].is_number() && (*headroom)["limit"].is_number();
    const double headroomViolation = headroomValid
        ? std::max(0.0, JsonNumber(*headroom, "value") - JsonNumber(*headroom, "limit"))
        : std::numeric_limits<double>::infinity();
    const double headroomUncertainty = headroom ? JsonNumber(*headroom, "uncertainty01") : 0.0;
    double editDistance = std::abs(result.point[0] - warmExposure);
    const auto terms = sample.find("terms");
    if (terms != sample.end() && terms->is_object()) {
        const auto edit = terms->find("edit.normalized_l1");
        if (edit != terms->end() && edit->is_object()) editDistance = JsonNumber(*edit, "value", editDistance);
    }
    result.merit = {
        Component("identity.violation_count", MeritTier::Tier0IdentityState,
            MeritRole::ConstraintViolation, tier0Violations),
        Component("raw.other_violation_count", MeritTier::Tier1RawArtifact,
            MeritRole::ConstraintViolation, otherRawViolations),
        Component("raw.wb_scaled_headroom", MeritTier::Tier1RawArtifact,
            MeritRole::ConstraintViolation, headroomViolation, 0.0, headroomUncertainty, "EV"),
        Component("technical.declared_goal_gap", MeritTier::Tier2Technical,
            MeritRole::GoalViolation, 0.0, 0.0, 0.0,
            "no Phase 04 universal photographic target"),
        Component("edit.normalized_l1", MeritTier::Tier5TieBreaker,
            MeritRole::TieBreaker, editDistance)
    };
    result.reason = status == "complete"
        ? "Replayed complete Phase 03 surface sample."
        : "Replayed rejected Phase 03 surface sample without weakening its constraint.";
    return result;
}

bool SameValue(double a, double b) {
    return std::abs(a - b) <= 1.0e-9;
}

json RunFrozenCases(const json& phase03, bool& patternRealGates, bool& trustRecorded) {
    json cases = json::array();
    patternRealGates = true;
    trustRecorded = true;
    for (const json& record : phase03.value("records", json::array())) {
        const json* surface = FindSurface(record, "one-d/raw_exposure_ev");
        if (!surface) {
            patternRealGates = false;
            continue;
        }
        const double warmExposure = record["warmStart"]["parameters"].value("raw_exposure_ev", 0.0);
        std::vector<double> values;
        const json* warmSample = nullptr;
        std::vector<const json*> samples;
        if (!surface->contains("samples") || !(*surface)["samples"].is_array()) {
            patternRealGates = false;
            continue;
        }
        const json& surfaceSamples = (*surface)["samples"];
        for (const json& sample : surfaceSamples) {
            const double value = JsonNumber(sample, "axisX");
            if (std::none_of(values.begin(), values.end(), [&](double existing) { return SameValue(existing, value); })) {
                values.push_back(value);
                samples.push_back(&sample);
            }
            if (sample.value("warmStart", false)) warmSample = &sample;
        }
        std::sort(values.begin(), values.end());
        if (values.empty() || !warmSample) {
            patternRealGates = false;
            continue;
        }
        SearchProblem problem;
        problem.id = "frozen-phase-03/" + record.value("partition", "unknown") + "/raw-exposure";
        problem.dimensions = { { "raw_exposure_ev", values.front(), values.back(), true, 0, values } };
        problem.warmStart = { warmExposure };
        SearchCallbacks callbacks;
        callbacks.evaluate = [&](const std::vector<double>& point) {
            for (const json* sample : samples) {
                if (SameValue(JsonNumber(*sample, "axisX"), point[0])) {
                    return FrozenEvaluation(record, *sample, warmExposure);
                }
            }
            SearchEvaluation missing;
            missing.candidateId = PointId("frozen-missing", point);
            missing.point = point;
            missing.disposition = EvaluationDisposition::MissingEvidence;
            missing.reason = "Requested point is absent from the frozen surface.";
            return missing;
        };
        const MethodRuns runs = RunAllMethods(problem, BenchmarkBudget(), callbacks);
        const bool warmWasSafe = warmSample->value("status", "") == "complete";
        const bool patternGate = warmWasSafe
            ? runs.pattern.status == SolveStatus::WarmStartRetained &&
                SameValue(runs.pattern.selected.point[0], warmExposure)
            : SafeImprovement(runs.pattern);
        const bool trustGate = warmWasSafe
            ? runs.trust.status == SolveStatus::WarmStartRetained
            : SafeImprovement(runs.trust);
        patternRealGates = patternRealGates && patternGate && runs.patternDeterministic;
        trustRecorded = trustRecorded && trustGate && runs.trustDeterministic;
        cases.push_back({
            { "sourceFileName", record.value("fileName", "") },
            { "partition", record.value("partition", "") },
            { "surfaceId", "one-d/raw_exposure_ev" },
            { "warmExposureEv", warmExposure },
            { "warmWasSafe", warmWasSafe },
            { "sampledExposureValuesEv", values },
            { "declaredTechnicalGoal", nullptr },
            { "selectionInterpretation", warmWasSafe
                ? "No declared lower-tier target justified moving a safe warm start; edit distance retained it."
                : "The selected candidate restores WB-scaled headroom feasibility; the constraint was not weakened." },
            { "patternGatePassed", patternGate },
            { "trustComparatorGatePassed", trustGate },
            { "methods", SerializeRuns(runs) }
        });
    }
    return cases;
}

bool ValidatePhase03Envelope(const json& report, std::string& reason) {
    const bool versions =
        report.value("candidateEngineVersion", "") == "raw-candidate-engine-v1" &&
        report.value("parameterSpaceVersion", "") == "raw-parameter-space-v1" &&
        report.value("objectiveConstraintVersion", "") == "raw-objective-constraints-v1";
    const bool nonMutating = !report.value("currentProjectRecipeMutation", true) &&
        !report.value("productionApply", true) && !report.value("lockedPartitionTouched", true);
    const bool noScore = report.contains("combinedObjectiveScore") &&
        report["combinedObjectiveScore"].is_null() && !report.value("optimizerSelected", true);
    if (!versions || !nonMutating || !noScore || !report.contains("records")) {
        reason = "Phase 03 report versions, non-mutation envelope, score policy, or records are invalid.";
        return false;
    }
    return true;
}

} // namespace

bool ValidateRawOptimizerBenchmarks(int rawArgCount, char** rawArgs) {
    Options options;
    if (!ParseOptions(rawArgCount, rawArgs, options)) return false;
    std::cerr << "Phase 04 benchmark: reading frozen Phase 03 report." << std::endl;
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(options.surfaceReport, bytes)) {
        std::cerr << "Could not read frozen Phase 03 surface report.\n";
        return false;
    }
    const RawEvidence::SourceIdentity identity = RawEvidence::ComputeSourceIdentity(bytes);
    if (!identity.valid || identity.sha256 != kFrozenPhase03Sha256) {
        std::cerr << "Frozen Phase 03 SHA-256 mismatch: " << identity.sha256 << '\n';
        return false;
    }
    json phase03;
    try {
        phase03 = json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception& error) {
        std::cerr << "Could not parse frozen Phase 03 surface report: " << error.what() << '\n';
        return false;
    }
    std::string envelopeReason;
    if (!ValidatePhase03Envelope(phase03, envelopeReason)) {
        std::cerr << envelopeReason << '\n';
        return false;
    }

    std::cerr << "Phase 04 benchmark: running analytic cases." << std::endl;
    bool analyticGates = false;
    bool realPatternGates = false;
    bool trustRecorded = false;
    const json analyticCases = RunAnalyticCases(analyticGates);
    std::cerr << "Phase 04 benchmark: replaying frozen surfaces." << std::endl;
    const json frozenCases = RunFrozenCases(phase03, realPatternGates, trustRecorded);
    const bool selected = analyticGates && realPatternGates && trustRecorded && frozenCases.size() >= 2;
    json report = {
        { "schemaVersion", 1 },
        { "benchmarkVersion", kOptimizerBenchmarkVersion },
        { "selectedOptimizerVersion", selected ? json(kSelectedOptimizerVersion) : json(nullptr) },
        { "trustRegionComparatorVersion", kTrustRegionBenchmarkVersion },
        { "convergenceContractVersion", kConvergenceContractVersion },
        { "evaluationBudgetVersion", kEvaluationBudgetVersion },
        { "phase03SurfaceReportSha256", identity.sha256 },
        { "phase03CandidateEngineVersion", phase03.value("candidateEngineVersion", "") },
        { "phase03ParameterSpaceVersion", phase03.value("parameterSpaceVersion", "") },
        { "phase03ObjectiveConstraintVersion", phase03.value("objectiveConstraintVersion", "") },
        { "combinedTotalScore", nullptr },
        { "singleAverageWinnerUsed", false },
        { "currentProjectRecipeMutation", false },
        { "productionSolverIntegration", false },
        { "productionApply", false },
        { "undoHistoryMutation", false },
        { "projectDirtyStateMutation", false },
        { "lockedPartitionTouched", false },
        { "benchmarkSearchBudget", SerializeBudget(BenchmarkBudget()) },
        { "selectedDryRunBudget", SerializeBudget(SelectedSolverBudget()) },
        { "stageEvaluationQuotas", {
            { "rawExposure", 8 }, { "localRange", 16 },
            { "finishTone", 12 }, { "displayFit", 12 }, { "total", 48 }
        } },
        { "analyticCases", analyticCases },
        { "frozenSurfaceCases", frozenCases },
        { "selectionGates", {
            { "analyticPatternCasesPassed", analyticGates },
            { "frozenPatternCasesPassed", realPatternGates },
            { "trustRegionComparatorRecorded", trustRecorded },
            { "deterministicReplayRequired", true },
            { "individualLexicographicComponentsOnly", true },
            { "fullResolutionIndependentRecheckRequired", true },
            { "passed", selected }
        } },
        { "selection", {
            { "method", selected ? json("stage-ordered-pattern-search") : json(nullptr) },
            { "reason", "The selected method restores frozen RAW headroom feasibility, retains a safe warm start when no lower-tier target exists, handles plateaus and failure states deterministically, and is simpler to inspect than the model-based comparator." },
            { "orderedGroups", json::array({ "raw-exposure", "local-range", "finish-tone", "display-fit" }) },
            { "declaredPairInteractionsRequired", true },
            { "pass94WarmFallbackRetained", true }
        } },
        { "deferredAlternatives", json::array({
            { { "method", "diagonal-quadratic-trust-region" },
              { "status", "benchmark-comparator-only" },
              { "reason", "Useful on smooth analytic surfaces, but the frozen real evidence does not justify interpolation/model-geometry complexity for the first production solver." } },
            { { "method", "bayesian-optimization-or-cma-es" },
              { "status", "not-benchmarked" },
              { "reason", "Phase 03 surface cost and dimensional structure do not justify stochastic or population-search complexity." } }
        }) }
    };
    std::cerr << "Phase 04 benchmark: writing durable report." << std::endl;
    if (!WriteJson(options.output, report)) {
        std::cerr << "Could not write Phase 04 optimizer benchmark report.\n";
        return false;
    }
    if (!selected) {
        std::cerr << "Phase 04 optimizer selection gates failed; diagnostic report was written.\n";
        return false;
    }
    std::cout << "Phase 04 optimizer benchmarks passed; selected stage-ordered-pattern-search-v1.\n";
    return true;
}

} // namespace Stack::Validation
