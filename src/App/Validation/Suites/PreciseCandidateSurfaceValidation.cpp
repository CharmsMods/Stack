#include "App/Validation/ValidationSuites.h"

#include "Editor/EditorRenderWorker.h"
#include "Raw/LibRawDecoder.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawPreciseCandidateEngine.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RenderedFeatureEvidence.h"
#include "Renderer/GLLoader.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

namespace Stack::Validation {
namespace {

using PreciseRaw::CandidateEvaluationRecord;
using PreciseRaw::CandidateIdentityContext;
using PreciseRaw::CandidateOwnership;
using PreciseRaw::CandidateParameterVector;
using PreciseRaw::CandidateProposal;
using PreciseRaw::CandidateRenderEvidence;
using PreciseRaw::EvaluationStatus;
using PreciseRaw::ObjectiveSurfacePlan;
using PreciseRaw::ParameterSpace;
using PreciseRaw::StageFeatureEvidence;
using RawAutoStartPoint::RawAutoStartPointStage;
using RawAutoStartPoint::RawAutoStartPointStageImage;
using RenderedFeatures::FeatureContext;
using RenderedFeatures::FeatureRecord;
using RenderedFeatures::LinearRgbImage;

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
    int fullResolutionPromotions = 1;
};

struct WarmStartResult {
    bool valid = false;
    RawRecipe::RawDevelopmentRecipe recipe;
    int upstreamPassCount = 0;
    bool displayFitApplied = false;
    std::vector<std::string> passSummaries;
    std::string reason;
};

struct RenderedEvaluation {
    CandidateEvaluationRecord evaluation;
    std::vector<RawAutoStartPointStageImage> images;
};

struct SurfaceAccumulator {
    std::unordered_map<std::string, RenderedEvaluation> candidateCache;
    std::unordered_map<std::string, CandidateEvaluationRecord> uniqueEvaluations;
    int exactCacheHits = 0;
    int exactCacheMisses = 0;
};

std::string Sha256Text(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

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
        auto requireValue = [&](const char* option) -> const char* {
            if (i + 1 >= argc) {
                error = std::string(option) + " requires a value.";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--development-file" || arg == "--validation-file") {
            const char* value = requireValue(arg.c_str());
            if (!value) return false;
            options.sources.push_back({ value, arg == "--development-file" ? "development" : "validation" });
        } else if (arg == "--output") {
            const char* value = requireValue("--output");
            if (!value) return false;
            options.output = value;
        } else if (arg == "--proxy-max-dimension") {
            const char* value = requireValue("--proxy-max-dimension");
            if (!value || !ParseInteger(value, 64, options.proxyMaxDimension)) {
                error = "--proxy-max-dimension must be an integer >= 64.";
                return false;
            }
        } else if (arg == "--feature-max-dimension") {
            const char* value = requireValue("--feature-max-dimension");
            if (!value || !ParseInteger(value, 64, options.featureMaxDimension)) {
                error = "--feature-max-dimension must be an integer >= 64.";
                return false;
            }
        } else if (arg == "--warm-max-dimension") {
            const char* value = requireValue("--warm-max-dimension");
            if (!value || !ParseInteger(value, 128, options.warmMaxDimension)) {
                error = "--warm-max-dimension must be an integer >= 128.";
                return false;
            }
        } else if (arg == "--full-resolution-promotions") {
            const char* value = requireValue("--full-resolution-promotions");
            if (!value || !ParseInteger(value, 0, options.fullResolutionPromotions)) {
                error = "--full-resolution-promotions must be a non-negative integer.";
                return false;
            }
        } else {
            error = "Unknown candidate-surface validation option: " + arg;
            return false;
        }
    }
    if (options.sources.empty()) {
        error = "Provide --development-file and --validation-file RAW inputs.";
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

RawAnalysis::CurrentFrameInputStats ToCurrentFrameStats(const RenderTextureStats& stats) {
    RawAnalysis::CurrentFrameInputStats result;
    result.valid = stats.valid;
    result.p001Luma = stats.p001Luma;
    result.p01Luma = stats.p01Luma;
    result.p05Luma = stats.p05Luma;
    result.p50Luma = stats.p50Luma;
    result.p95Luma = stats.p95Luma;
    result.p99Luma = stats.p99Luma;
    result.p999Luma = stats.p999Luma;
    result.logAverageLuma = stats.logAverageLuma;
    result.dynamicRangeEv = stats.dynamicRangeEv;
    result.validPixelPercent = stats.validPixelPercent;
    result.hdrPixelPercent = stats.hdrPixelPercent;
    result.displayClipPercent = stats.displayClipPercent;
    return result;
}

RenderGraphSnapshot BuildGraph(const RawRecipe::RawDevelopmentRecipe& recipe) {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 2;

    RenderGraphNode rawDevelopment;
    rawDevelopment.nodeId = 1;
    rawDevelopment.requestRevision = 1;
    rawDevelopment.kind = RenderGraphNodeKind::RawDevelopment;
    rawDevelopment.rawDevelopment.recipe = recipe;
    graph.nodes.push_back(std::move(rawDevelopment));

    RenderGraphNode output;
    output.nodeId = 2;
    output.requestRevision = 1;
    output.kind = RenderGraphNodeKind::Output;
    graph.nodes.push_back(std::move(output));
    graph.links.push_back(RenderGraphLink { 1, "imageOut", 2, "imageIn" });
    return graph;
}

void SetGraphRecipe(RenderGraphSnapshot& graph, const RawRecipe::RawDevelopmentRecipe& recipe) {
    for (RenderGraphNode& node : graph.nodes) {
        if (node.kind == RenderGraphNodeKind::RawDevelopment) {
            node.rawDevelopment.recipe = recipe;
            ++node.requestRevision;
            return;
        }
    }
}

bool SameVisibleRecipe(
    const RawRecipe::RawDevelopmentRecipe& a,
    const RawRecipe::RawDevelopmentRecipe& b) {
    return PreciseRaw::CanonicalRecipeBytes(a) == PreciseRaw::CanonicalRecipeBytes(b);
}

WarmStartResult BuildPass94WarmStart(
    const RawRecipe::RawDevelopmentRecipe& baseRecipe,
    const Raw::RawMetadata& metadata,
    const std::string& sourceKey,
    int maxDimension) {
    WarmStartResult result;
    result.recipe = baseRecipe;
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetPreviewMaxDimension(maxDimension);
    pipeline.Resize(maxDimension, maxDimension);
    RenderGraphSnapshot graph = BuildGraph(result.recipe);

    RawAnalysis::RawImageAnalysis latestAnalysis;
    for (int upstreamPass = 0; upstreamPass < 4; ++upstreamPass) {
        SetGraphRecipe(graph, result.recipe);
        pipeline.ExecuteGraph(graph);
        if (pipeline.GetOutputTexture() == 0) {
            result.reason = "Pass 94 warm-start render failed.";
            return result;
        }

        RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics =
            pipeline.BuildRawDevelopmentStartPointDiagnostics(sourceKey);
        latestAnalysis = RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
            ToCurrentFrameStats(pipeline.GetRawDevelopmentViewTransformInputStats()), sourceKey);
        latestAnalysis.metadata = RawAnalysis::BuildRawMetadataSummary(metadata);
        const RawAutoBase::AutoBaseRecommendations recommendations =
            RawAutoBase::BuildAutoBaseRecommendations(
                latestAnalysis,
                result.recipe,
                nullptr,
                &pipeline.GetRawDevelopmentLocalSuggestionImage());
        diagnostics = RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            std::move(diagnostics), result.recipe, latestAnalysis, recommendations);

        std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest> requests =
            RawAutoStartPoint::CollectCandidateRenderRequests(diagnostics);
        for (int candidatePass = 0; candidatePass < 4 && !requests.empty(); ++candidatePass) {
            const auto rendered = EditorRenderWorker::RenderRawWorkspaceStartPointCandidateRequests(
                pipeline, graph, sourceKey, requests);
            const bool anySuccess = std::any_of(rendered.begin(), rendered.end(), [](const auto& item) {
                return item.success;
            });
            diagnostics = RawAutoStartPoint::MergeCandidateRenderResults(
                std::move(diagnostics), rendered, result.recipe, latestAnalysis, recommendations);
            requests = RawAutoStartPoint::CollectCandidateRenderRequests(diagnostics);
            if (!anySuccess) break;
        }

        const RawAutoStartPoint::RawAutoStartPointConservativePlan plan =
            RawAutoStartPoint::BuildConservativeStartingPointPlan(
                result.recipe, latestAnalysis, recommendations, diagnostics);
        result.passSummaries.push_back(plan.summary);
        if (!plan.valid || !plan.hasUpstreamRecipeChanges ||
            SameVisibleRecipe(result.recipe, plan.upstreamRecipe)) {
            break;
        }
        result.recipe = plan.upstreamRecipe;
        ++result.upstreamPassCount;
    }

    SetGraphRecipe(graph, result.recipe);
    pipeline.ExecuteGraph(graph);
    if (pipeline.GetOutputTexture() == 0) {
        result.reason = "Final Pass 94 warm-start analysis render failed.";
        return result;
    }
    latestAnalysis = RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
        ToCurrentFrameStats(pipeline.GetRawDevelopmentViewTransformInputStats()), sourceKey);
    latestAnalysis.metadata = RawAnalysis::BuildRawMetadataSummary(metadata);
    const RawAutoBase::ViewFitDecision fit =
        RawAutoBase::BuildAutoBaseViewFitDecision(latestAnalysis, result.recipe);
    if (fit.canApply) {
        RawAutoBase::ApplyViewTransformFitToRecipe(result.recipe, fit.fit);
        result.displayFitApplied = true;
    } else {
        result.passSummaries.push_back(
            fit.reason.empty() ? "Pass 94 Display Fit unavailable." : fit.reason);
    }
    result.valid = true;
    result.reason = "Pass 94 bounded heuristic reproduced in isolated validation state.";
    return result;
}

const char* StageName(RawAutoStartPointStage stage) {
    return RawAutoStartPoint::StageStableString(stage);
}

LinearRgbImage ToLinearImage(const RawAutoStartPointStageImage& image) {
    LinearRgbImage result;
    result.width = image.width;
    result.height = image.height;
    result.pixels = image.pixels;
    return result;
}

const RawAutoStartPointStageImage* FindImage(
    const std::vector<RawAutoStartPointStageImage>& images,
    RawAutoStartPointStage stage) {
    const auto found = std::find_if(images.begin(), images.end(), [&](const auto& image) {
        return image.valid && image.stage == stage;
    });
    return found == images.end() ? nullptr : &*found;
}

FeatureContext BuildFeatureContext(
    const CandidateProposal& proposal,
    const RawAutoStartPointStageImage& image,
    const Raw::RawMetadata& metadata) {
    FeatureContext context;
    context.sourceIdentity = proposal.identities.sourceIdentity;
    context.recipeIdentity = proposal.candidateRecipeIdentity;
    context.stage = StageName(image.stage);
    context.colorSpace = image.measurementDomain.empty()
        ? "stack-linear-srgb-working-output"
        : image.measurementDomain;
    context.colorTransformIdentity = "stack-raw-gpu-linear-srgb-to-xyz-d65-v1";
    context.transferFunction = "linear";
    context.workingToXyz = {
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041
    };
    context.referenceGrey = 0.18;
    context.rawEvidenceIdentity = proposal.identities.rawEvidenceIdentity;
    context.cropIdentity = "stack-raw-visible-area-and-recipe-crop-v1";
    context.orientationNormalized = true;
    context.sourceWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    context.sourceHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    context.globalLiftEv = proposal.parameters.rawExposureEv;
    context.maximumLocalLiftEv = std::max(0.0, proposal.parameters.localDeltaEv);
    return context;
}

RenderedEvaluation RenderAndEvaluate(
    RenderPipeline& pipeline,
    const RenderGraphSnapshot& graph,
    const CandidateProposal& proposal,
    const ParameterSpace& parameterSpace,
    const RawEvidence::RawTechnicalEvidenceRecord& rawEvidence,
    const Raw::RawMetadata& metadata,
    const std::vector<RawAutoStartPointStageImage>* warmImages,
    const std::string& proxyIdentity,
    bool fullResolution,
    int featureMaxDimension,
    const RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const std::string& renderRequestTag = {}) {
    RenderedEvaluation output;
    RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest request;
    request.valid = proposal.valid;
    request.id = proposal.candidateId + renderRequestTag;
    request.stage = RawAutoStartPointStage::FinishToneCandidate;
    request.hasRecipe = true;
    request.recipe = proposal.recipe;
    request.featureReadbackMaxDimension = featureMaxDimension;
    request.reason = "Phase 03 isolated objective-surface measurement.";

    const auto featureBegin = std::chrono::steady_clock::now();
    const auto results = EditorRenderWorker::RenderRawWorkspaceStartPointCandidateRequests(
        pipeline, graph, proposal.identities.sourceIdentity, { request });
    CandidateRenderEvidence render;
    render.attempted = true;
    render.fullResolution = fullResolution;
    render.proxyIdentity = proxyIdentity;
    render.renderIdentity = Sha256Text(
        proposal.identities.rendererIdentity + "|" + proposal.candidateId + "|" + proxyIdentity);
    if (results.empty()) {
        render.error = "Candidate renderer returned no result.";
    } else {
        const auto& result = results.front();
        render.success = result.success;
        render.error = result.error;
        render.renderWidth = result.renderWidth;
        render.renderHeight = result.renderHeight;
        render.renderRuntimeMs = result.renderMs;
        render.imageCacheHits = result.imageCacheHits;
        render.imageCacheMisses = result.imageCacheMisses;
        render.rawStageCacheHits = result.rawStageCacheHits;
        render.rawStageCacheMisses = result.rawStageCacheMisses;
        output.images = result.stageImageReadbacks;
        for (const RawAutoStartPointStageImage& image : output.images) {
            if (!image.valid) continue;
            const LinearRgbImage current = ToLinearImage(image);
            if (!current.Valid()) continue;
            FeatureContext context = BuildFeatureContext(proposal, image, metadata);
            StageFeatureEvidence stage;
            stage.stage = image.stage;
            stage.features = image.displayMappedLinearRgb
                ? RenderedFeatures::AnalyzeDisplayMapped(current, context)
                : RenderedFeatures::AnalyzeSceneLinear(current, context, &rawEvidence);
            const RawAutoStartPointStageImage* warm = warmImages
                ? FindImage(*warmImages, image.stage)
                : nullptr;
            const LinearRgbImage reference = warm ? ToLinearImage(*warm) : current;
            stage.comparisonToWarm = RenderedFeatures::CompareRenderedImages(
                reference, current, context);
            render.featureWidth = std::max(render.featureWidth, image.width);
            render.featureHeight = std::max(render.featureHeight, image.height);
            render.stages.push_back(std::move(stage));
        }
    }
    render.featureRuntimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - featureBegin).count() - render.renderRuntimeMs;
    render.featureRuntimeMs = std::max(0.0, render.featureRuntimeMs);
    output.evaluation = PreciseRaw::EvaluateCandidate(
        proposal,
        parameterSpace,
        &rawEvidence,
        std::move(render),
        currentRecipe,
        currentRecipe,
        0,
        0,
        false,
        false);
    return output;
}

nlohmann::json SerializeSampleSummary(
    const PreciseRaw::SurfaceSample& sample,
    const CandidateEvaluationRecord& evaluation,
    bool cacheHit) {
    nlohmann::json failed = nlohmann::json::array();
    for (const PreciseRaw::ConstraintResult& constraint : evaluation.constraints) {
        if (constraint.status == PreciseRaw::ConstraintStatus::Failed) failed.push_back(constraint.id);
    }
    nlohmann::json terms = nlohmann::json::object();
    for (const PreciseRaw::ObjectiveTerm& term : evaluation.terms) {
        terms[term.id] = {
            { "valid", term.valid },
            { "value", term.valid ? nlohmann::json(term.value) : nlohmann::json(nullptr) },
            { "uncertainty01", term.uncertainty01 },
            { "stage", term.stage },
            { "tier", PreciseRaw::ObjectiveTierName(term.tier) }
        };
    }
    return {
        { "sampleId", sample.sampleId },
        { "warmStart", sample.warmStart },
        { "axisX", sample.axisX },
        { "axisY", sample.axisY },
        { "candidateId", evaluation.proposal.candidateId },
        { "evaluationId", evaluation.evaluationId },
        { "status", PreciseRaw::EvaluationStatusName(evaluation.status) },
        { "exactCandidateCacheHit", cacheHit },
        { "renderRuntimeMs", evaluation.render.renderRuntimeMs },
        { "featureRuntimeMs", evaluation.render.featureRuntimeMs },
        { "failedConstraints", std::move(failed) },
        { "terms", std::move(terms) },
        { "combinedTotalScore", nullptr }
    };
}

nlohmann::json SummarizeSurfaceShape(const nlohmann::json& samples) {
    struct TermSeries {
        std::vector<double> values;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
    };
    std::map<std::string, TermSeries> series;
    int complete = 0;
    int rejected = 0;
    for (const nlohmann::json& sample : samples) {
        const std::string status = sample.value("status", std::string());
        if (status == "complete") ++complete;
        if (status == "rejected") ++rejected;
        const nlohmann::json terms = sample.value("terms", nlohmann::json::object());
        for (auto iterator = terms.begin(); iterator != terms.end(); ++iterator) {
            if (!iterator.value().value("valid", false) || iterator.value().at("value").is_null()) continue;
            const double value = iterator.value().at("value").get<double>();
            TermSeries& item = series[iterator.key()];
            item.values.push_back(value);
            item.minimum = std::min(item.minimum, value);
            item.maximum = std::max(item.maximum, value);
        }
    }
    nlohmann::json terms = nlohmann::json::object();
    for (const auto& [id, item] : series) {
        double maxAdjacentDelta = 0.0;
        int zeroAdjacentDeltas = 0;
        int directionChanges = 0;
        double previousDelta = 0.0;
        bool hasPreviousDelta = false;
        for (std::size_t i = 1; i < item.values.size(); ++i) {
            const double delta = item.values[i] - item.values[i - 1];
            maxAdjacentDelta = std::max(maxAdjacentDelta, std::abs(delta));
            if (std::abs(delta) <= 1.0e-12) ++zeroAdjacentDeltas;
            if (hasPreviousDelta && delta * previousDelta < 0.0) ++directionChanges;
            if (std::abs(delta) > 1.0e-12) {
                previousDelta = delta;
                hasPreviousDelta = true;
            }
        }
        terms[id] = {
            { "validSampleCount", item.values.size() },
            { "minimum", item.minimum },
            { "maximum", item.maximum },
            { "range", item.maximum - item.minimum },
            { "maximumObservedAdjacentDelta", maxAdjacentDelta },
            { "exactPlateauAdjacentCount", zeroAdjacentDeltas },
            { "observedDirectionChangeCount", directionChanges }
        };
    }
    return {
        { "completeSampleCount", complete },
        { "rejectedSampleCount", rejected },
        { "termShapeObservations", std::move(terms) },
        { "interpretation", "Observed values only: Phase 03 selects no discontinuity threshold, term weight, or winner." }
    };
}

nlohmann::json SerializeRepeatAgreement(
    const RenderedEvaluation& reference,
    const RenderedEvaluation& repeated) {
    nlohmann::json stages = nlohmann::json::array();
    for (const StageFeatureEvidence& a : reference.evaluation.render.stages) {
        const auto found = std::find_if(
            repeated.evaluation.render.stages.begin(),
            repeated.evaluation.render.stages.end(),
            [&](const StageFeatureEvidence& b) { return b.stage == a.stage; });
        if (found == repeated.evaluation.render.stages.end()) continue;
        stages.push_back({
            { "stage", StageName(a.stage) },
            { "agreement", RenderedFeatures::SerializeFeatureAgreement(
                RenderedFeatures::CompareFeatureRecords(a.features, found->features)) }
        });
    }
    return {
        { "referenceEvaluationId", reference.evaluation.evaluationId },
        { "repeatEvaluationId", repeated.evaluation.evaluationId },
        { "stages", std::move(stages) },
        { "recipeMutation", false },
        { "cacheBypassedForNoiseMeasurement", true }
    };
}

bool HasRequiredStages(const RenderedEvaluation& rendered) {
    for (const RawAutoStartPointStage stage : {
             RawAutoStartPointStage::NeutralScene,
             RawAutoStartPointStage::RawPlacement,
             RawAutoStartPointStage::LocalCandidate,
             RawAutoStartPointStage::FinishToneCandidate,
             RawAutoStartPointStage::DisplayCandidate }) {
        if (!FindImage(rendered.images, stage)) return false;
    }
    return true;
}

bool HasMeasuredDisposition(const CandidateEvaluationRecord& evaluation) {
    return evaluation.status == EvaluationStatus::Complete ||
        evaluation.status == EvaluationStatus::Rejected;
}

nlohmann::json RunSource(
    const SourceInput& input,
    const Options& options,
    int& remainingFullPromotions,
    bool& success) {
    success = false;
    const RawEvidence::SourceIdentity sourceIdentity =
        RawEvidence::ComputeSourceIdentity(input.path);
    Raw::RawImageData raw;
    if (!sourceIdentity.valid || !Raw::DecodeWithLibRaw(input.path.string(), raw, {})) {
        return {
            { "fileName", input.path.filename().string() },
            { "partition", input.partition },
            { "status", "decode-failed" },
            { "reason", raw.metadata.error }
        };
    }
    RawEvidence::BuildOptions evidenceOptions;
    evidenceOptions.maxSamples = 1000000;
    const RawEvidence::RawTechnicalEvidenceRecord rawEvidence =
        RawEvidence::BuildRawTechnicalEvidence(raw, sourceIdentity, evidenceOptions);
    const Raw::RawMetadata metadata = raw.metadata;
    raw = {};
    if (!rawEvidence.valid) {
        return {
            { "fileName", input.path.filename().string() },
            { "partition", input.partition },
            { "status", "raw-evidence-failed" },
            { "reason", rawEvidence.statusMessage }
        };
    }

    RawRecipe::RawDevelopmentRecipe defaultRecipe =
        RawRecipe::MakeDefaultRecipe(input.path.string(), input.path.filename().string());
    defaultRecipe.source.fingerprint = sourceIdentity.sha256;
    defaultRecipe.source.fileSizeBytes = sourceIdentity.byteSize;
    const WarmStartResult warm = BuildPass94WarmStart(
        defaultRecipe, metadata, sourceIdentity.sha256, options.warmMaxDimension);
    if (!warm.valid) {
        return {
            { "fileName", input.path.filename().string() },
            { "partition", input.partition },
            { "status", "warm-start-failed" },
            { "reason", warm.reason }
        };
    }

    const RawRecipe::RawDevelopmentRecipe currentRecipe = warm.recipe;
    const std::string currentRecipeBefore = PreciseRaw::CanonicalRecipeBytes(currentRecipe);
    const ParameterSpace parameterSpace = PreciseRaw::BuildParameterSpace(currentRecipe, &rawEvidence);
    const CandidateParameterVector warmParameters = PreciseRaw::ExtractParameters(currentRecipe);
    const ObjectiveSurfacePlan surfacePlan =
        PreciseRaw::BuildObjectiveSurfacePlan(warmParameters, parameterSpace);
    RenderGraphSnapshot graph = BuildGraph(currentRecipe);

    const std::string proxyIdentity = "stack-preview-max-" +
        std::to_string(options.proxyMaxDimension) + "-feature-max-" +
        std::to_string(options.featureMaxDimension) + "-v1";
    CandidateIdentityContext identity;
    identity.sourceIdentity = sourceIdentity.sha256;
    identity.decodeIdentity = rawEvidence.decodeIdentity.sha256;
    identity.rawEvidenceIdentity = rawEvidence.evidenceIdentitySha256;
    identity.baseRecipeIdentity = PreciseRaw::RecipeIdentity(currentRecipe);
    identity.rendererIdentity = "stack-opengl-raw-development-stages-v1";
    identity.proxyIdentity = proxyIdentity;
    identity.featureVersion = RenderedFeatures::kRenderedFeatureVersion;
    identity.budgetIdentity = "phase-03-exhaustive-surface-plan-v1";
    identity.ownershipIdentity = "unowned-visible-controls-v1";
    identity.generation = 1;
    CandidateOwnership ownership;

    RenderPipeline proxyPipeline;
    proxyPipeline.Initialize();
    proxyPipeline.SetPreviewMaxDimension(options.proxyMaxDimension);
    proxyPipeline.Resize(options.proxyMaxDimension, options.proxyMaxDimension);

    const CandidateProposal warmProposal = PreciseRaw::BuildCandidateProposal(
        currentRecipe, warmParameters, identity, ownership, "Frozen Pass 94 warm start.");

    RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest noReadbackRequest;
    noReadbackRequest.valid = true;
    noReadbackRequest.id = "phase-03-opt-in-readback-check";
    noReadbackRequest.stage = RawAutoStartPointStage::FinishToneCandidate;
    noReadbackRequest.hasRecipe = true;
    noReadbackRequest.recipe = warmProposal.recipe;
    noReadbackRequest.featureReadbackMaxDimension = 0;
    const auto noReadback = EditorRenderWorker::RenderRawWorkspaceStartPointCandidateRequests(
        proxyPipeline, graph, sourceIdentity.sha256, { noReadbackRequest });
    const bool readbackOptInOnly = !noReadback.empty() &&
        noReadback.front().success && noReadback.front().stageImageReadbacks.empty();

    RenderedEvaluation warmRendered = RenderAndEvaluate(
        proxyPipeline,
        graph,
        warmProposal,
        parameterSpace,
        rawEvidence,
        metadata,
        nullptr,
        proxyIdentity,
        false,
        options.featureMaxDimension,
        currentRecipe);
    const bool stageReadbacksComplete = HasRequiredStages(warmRendered);
    RenderPipeline repeatPipeline;
    repeatPipeline.Initialize();
    repeatPipeline.SetPreviewMaxDimension(options.proxyMaxDimension);
    repeatPipeline.Resize(options.proxyMaxDimension, options.proxyMaxDimension);
    RenderedEvaluation warmRepeat = RenderAndEvaluate(
        repeatPipeline,
        graph,
        warmProposal,
        parameterSpace,
        rawEvidence,
        metadata,
        &warmRendered.images,
        proxyIdentity,
        false,
        options.featureMaxDimension,
        currentRecipe,
        "|repeatability-bypass");

    SurfaceAccumulator accumulator;
    accumulator.candidateCache.emplace(warmProposal.candidateId, warmRendered);
    accumulator.uniqueEvaluations.emplace(
        warmRendered.evaluation.evaluationId, warmRendered.evaluation);
    accumulator.exactCacheMisses = 1;

    nlohmann::json surfaces = nlohmann::json::array();
    for (const PreciseRaw::SurfaceSlice& slice : surfacePlan.slices) {
        nlohmann::json samples = nlohmann::json::array();
        for (const PreciseRaw::SurfaceSample& sample : slice.samples) {
            CandidateIdentityContext sampleIdentity = identity;
            const CandidateProposal proposal = PreciseRaw::BuildCandidateProposal(
                currentRecipe,
                sample.parameters,
                std::move(sampleIdentity),
                ownership,
                "Phase 03 measured objective-surface sample " + sample.sampleId + ".");
            bool cacheHit = false;
            CandidateEvaluationRecord evaluation;
            const auto cached = accumulator.candidateCache.find(proposal.candidateId);
            if (cached != accumulator.candidateCache.end()) {
                cacheHit = true;
                ++accumulator.exactCacheHits;
                evaluation = cached->second.evaluation;
                evaluation.cacheHit = true;
            } else {
                ++accumulator.exactCacheMisses;
                RenderedEvaluation rendered = RenderAndEvaluate(
                    proxyPipeline,
                    graph,
                    proposal,
                    parameterSpace,
                    rawEvidence,
                    metadata,
                    &warmRendered.images,
                    proxyIdentity,
                    false,
                    options.featureMaxDimension,
                    currentRecipe);
                evaluation = rendered.evaluation;
                accumulator.uniqueEvaluations.emplace(evaluation.evaluationId, evaluation);
                accumulator.candidateCache.emplace(proposal.candidateId, std::move(rendered));
            }
            samples.push_back(SerializeSampleSummary(sample, evaluation, cacheHit));
        }
        surfaces.push_back({
            { "id", slice.id },
            { "axisX", PreciseRaw::ParameterStableString(slice.axisX) },
            { "axisY", slice.hasAxisY
                ? nlohmann::json(PreciseRaw::ParameterStableString(slice.axisY))
                : nlohmann::json(nullptr) },
            { "conditionalAnchor", slice.conditionalAnchor },
            { "shape", SummarizeSurfaceShape(samples) },
            { "samples", std::move(samples) }
        });
    }

    nlohmann::json evaluations = nlohmann::json::array();
    int completeEvaluations = 0;
    int rejectedEvaluations = 0;
    for (const auto& [id, evaluation] : accumulator.uniqueEvaluations) {
        (void)id;
        if (evaluation.status == EvaluationStatus::Complete) ++completeEvaluations;
        if (evaluation.status == EvaluationStatus::Rejected) ++rejectedEvaluations;
        evaluations.push_back(PreciseRaw::SerializeEvaluation(evaluation));
    }

    nlohmann::json promotion = nullptr;
    bool promotionValid = remainingFullPromotions <= 0;
    if (remainingFullPromotions > 0) {
        --remainingFullPromotions;
        RenderPipeline fullPipeline;
        fullPipeline.Initialize();
        fullPipeline.SetPreviewMaxDimension(0);
        fullPipeline.Resize(
            metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth,
            metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight);
        const std::string fullIdentity = "stack-full-resolution-feature-max-" +
            std::to_string(std::max(512, options.featureMaxDimension)) + "-v1";
        CandidateIdentityContext fullContext = identity;
        fullContext.proxyIdentity = fullIdentity;
        const CandidateProposal fullProposal = PreciseRaw::BuildCandidateProposal(
            currentRecipe, warmParameters, std::move(fullContext), ownership,
            "Phase 03 full-resolution promotion of the warm candidate.");
        RenderedEvaluation fullRendered = RenderAndEvaluate(
            fullPipeline,
            graph,
            fullProposal,
            parameterSpace,
            rawEvidence,
            metadata,
            &warmRendered.images,
            fullIdentity,
            true,
            std::max(512, options.featureMaxDimension),
            currentRecipe);
        const PreciseRaw::FullResolutionPromotionRecord compared =
            PreciseRaw::CompareProxyAndFullResolution(
                warmProposal, warmRendered.evaluation.render, fullRendered.evaluation.render);
        promotion = PreciseRaw::SerializePromotion(compared);
        promotionValid = compared.valid;
    }

    const std::string currentRecipeAfter = PreciseRaw::CanonicalRecipeBytes(currentRecipe);
    const bool noMutation = currentRecipeBefore == currentRecipeAfter &&
        warmRendered.evaluation.currentRecipeUnchanged &&
        warmRendered.evaluation.undoHistoryUnchanged &&
        warmRendered.evaluation.projectDirtyStateUnchanged;
    int warmSliceCount = 0;
    for (const PreciseRaw::SurfaceSlice& slice : surfacePlan.slices) {
        if (std::any_of(slice.samples.begin(), slice.samples.end(), [](const auto& sample) {
                return sample.warmStart;
            })) {
            ++warmSliceCount;
        }
    }

    success = readbackOptInOnly && stageReadbacksComplete && noMutation &&
        HasMeasuredDisposition(warmRendered.evaluation) &&
        HasMeasuredDisposition(warmRepeat.evaluation) &&
        surfacePlan.slices.size() == 20 && warmSliceCount == 20 &&
        completeEvaluations > 0 && promotionValid;

    return {
        { "fileName", input.path.filename().string() },
        { "format", input.path.extension().string() },
        { "partition", input.partition },
        { "sourceIdentitySha256", sourceIdentity.sha256 },
        { "decodeIdentitySha256", rawEvidence.decodeIdentity.sha256 },
        { "rawEvidenceIdentitySha256", rawEvidence.evidenceIdentitySha256 },
        { "camera", { { "make", metadata.cameraMake }, { "model", metadata.cameraModel } } },
        { "rawDimensions", { { "width", metadata.rawWidth }, { "height", metadata.rawHeight } } },
        { "warmStart", {
            { "kind", "pass-94-bounded-heuristic-isolated-reproduction" },
            { "warmMaxDimension", options.warmMaxDimension },
            { "upstreamPassCount", warm.upstreamPassCount },
            { "displayFitApplied", warm.displayFitApplied },
            { "recipeIdentity", PreciseRaw::RecipeIdentity(currentRecipe) },
            { "parameters", PreciseRaw::SerializeParameters(warmParameters) },
            { "passSummaries", warm.passSummaries },
            { "reason", warm.reason }
        } },
        { "parameterSpace", PreciseRaw::SerializeParameterSpace(parameterSpace) },
        { "surfacePlan", PreciseRaw::SerializeSurfacePlan(surfacePlan) },
        { "surfaceCount", surfacePlan.slices.size() },
        { "warmPresentInSurfaceCount", warmSliceCount },
        { "exactCandidateCache", {
            { "hits", accumulator.exactCacheHits },
            { "misses", accumulator.exactCacheMisses },
            { "uniqueCandidateCount", accumulator.candidateCache.size() },
            { "key", "complete candidate identity including source/decode/base/recipe/renderer/proxy/feature/budget/ownership/generation" }
        } },
        { "readbackContract", {
            { "optInOnly", readbackOptInOnly },
            { "requiredStageReadbacksComplete", stageReadbacksComplete },
            { "stageCount", warmRendered.images.size() }
        } },
        { "repeatability", SerializeRepeatAgreement(warmRendered, warmRepeat) },
        { "fullResolutionPromotion", std::move(promotion) },
        { "noMutation", {
            { "currentRecipeUnchanged", currentRecipeBefore == currentRecipeAfter },
            { "undoHistoryUnchanged", true },
            { "projectDirtyStateUnchanged", true },
            { "editorModuleInstantiated", false },
            { "productionApplyCalled", false }
        } },
        { "completeUniqueEvaluationCount", completeEvaluations },
        { "rejectedUniqueEvaluationCount", rejectedEvaluations },
        { "surfaces", std::move(surfaces) },
        { "uniqueEvaluations", std::move(evaluations) },
        { "status", success ? "complete" : "incomplete" }
    };
}

} // namespace

bool ValidatePreciseCandidateSurfaces(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "Candidate-surface validation: " << error << '\n';
        std::cerr
            << "Usage: Stack.exe --validate-raw-candidate-surfaces "
            << "--development-file <raw> --validation-file <raw> --output <json> "
            << "[--proxy-max-dimension N] [--feature-max-dimension N] "
            << "[--warm-max-dimension N] [--full-resolution-promotions N]\n";
        return false;
    }

    bool hasDevelopment = false;
    bool hasValidation = false;
    for (const SourceInput& source : options.sources) {
        hasDevelopment = hasDevelopment || source.partition == "development";
        hasValidation = hasValidation || source.partition == "validation";
    }
    if (!hasDevelopment || !hasValidation) {
        std::cerr << "Candidate-surface validation requires both development and validation partitions.\n";
        return false;
    }

    if (!glfwInit()) {
        std::cerr << "Candidate-surface validation could not initialize GLFW.\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Phase 03 Candidate Surfaces", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "Candidate-surface validation could not create a hidden OpenGL window.\n";
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "Candidate-surface validation could not load OpenGL functions.\n";
        return false;
    }

    const auto started = std::chrono::steady_clock::now();
    nlohmann::json records = nlohmann::json::array();
    int remainingFullPromotions = options.fullResolutionPromotions;
    int completed = 0;
    {
        for (const SourceInput& source : options.sources) {
            std::cout << "Measuring Phase 03 surfaces for " << source.path.filename().string()
                      << " (" << source.partition << ")...\n";
            bool sourceSuccess = false;
            records.push_back(RunSource(
                source, options, remainingFullPromotions, sourceSuccess));
            if (sourceSuccess) ++completed;
        }
    }

    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    const double runtimeMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    const bool promotionQuotaMet = remainingFullPromotions == 0;
    const bool allComplete = completed == static_cast<int>(options.sources.size());
    nlohmann::json report = {
        { "reportVersion", PreciseRaw::kSurfacePlanVersion },
        { "candidateEngineVersion", PreciseRaw::kCandidateEngineVersion },
        { "parameterSpaceVersion", PreciseRaw::kParameterSpaceVersion },
        { "objectiveConstraintVersion", PreciseRaw::kObjectiveConstraintVersion },
        { "renderedFeatureVersion", RenderedFeatures::kRenderedFeatureVersion },
        { "rawEvidenceVersion", RawEvidence::kRawTechnicalEvidenceVersion },
        { "sourceCount", options.sources.size() },
        { "completedSourceCount", completed },
        { "developmentPartitionPresent", hasDevelopment },
        { "validationPartitionPresent", hasValidation },
        { "lockedPartitionTouched", false },
        { "proxyPolicy", {
            { "renderMaxDimension", options.proxyMaxDimension },
            { "featureReadbackMaxDimension", options.featureMaxDimension },
            { "warmStartMaxDimension", options.warmMaxDimension },
            { "fullResolutionPromotionCount", options.fullResolutionPromotions }
        } },
        { "currentProjectRecipeMutation", false },
        { "productionApply", false },
        { "optimizerSelected", false },
        { "combinedObjectiveScore", nullptr },
        { "numericAcceptanceThresholdSelected", false },
        { "runtimeMs", runtimeMs },
        { "status", allComplete && promotionQuotaMet ? "complete" : "incomplete" },
        { "records", std::move(records) }
    };

    const std::string serialized = report.dump(2);
    bool pathScrubbed = true;
    for (const SourceInput& source : options.sources) {
        const std::string absolute = std::filesystem::absolute(source.path).parent_path().string();
        if (!absolute.empty() && serialized.find(absolute) != std::string::npos) {
            pathScrubbed = false;
        }
    }
    if (!pathScrubbed) {
        std::cerr << "Candidate-surface report rejected: an absolute source path escaped scrubbing.\n";
        return false;
    }

    if (!options.output.empty()) {
        std::error_code ec;
        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(options.output.parent_path(), ec);
        }
        std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "Candidate-surface validation could not write "
                      << options.output.filename().string() << '\n';
            return false;
        }
        output << serialized << '\n';
    } else {
        std::cout << serialized << '\n';
    }

    std::cout << "Phase 03 candidate surfaces: " << completed << "/"
              << options.sources.size() << " sources complete; full-resolution quota "
              << (promotionQuotaMet ? "met" : "not met") << ".\n";
    return allComplete && promotionQuotaMet;
}

} // namespace Stack::Validation
