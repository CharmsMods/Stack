#pragma once

#include "Raw/RawPreciseCandidateEngine.h"
#include "Renderer/MaskRenderTypes.h"
#include "Renderer/RenderPipeline.h"

#include <functional>
#include <string>
#include <vector>

namespace Stack::Validation::PreciseCandidateRuntime {

struct WarmStartResult {
    bool valid = false;
    RawRecipe::RawDevelopmentRecipe recipe;
    int upstreamPassCount = 0;
    bool displayFitApplied = false;
    std::vector<std::string> passSummaries;
    std::string reason;
};

struct RenderedEvaluation {
    PreciseRaw::CandidateEvaluationRecord evaluation;
    std::vector<RawAutoStartPoint::RawAutoStartPointStageImage> images;
};

RenderGraphSnapshot BuildGraph(const RawRecipe::RawDevelopmentRecipe& recipe);

WarmStartResult BuildPass94WarmStart(
    const RawRecipe::RawDevelopmentRecipe& baseRecipe,
    const Raw::RawMetadata& metadata,
    const std::string& sourceKey,
    int maxDimension,
    const std::function<bool()>& shouldCancel = {});

RenderedEvaluation RenderAndEvaluate(
    RenderPipeline& pipeline,
    const RenderGraphSnapshot& graph,
    const PreciseRaw::CandidateProposal& proposal,
    const PreciseRaw::ParameterSpace& parameterSpace,
    const RawEvidence::RawTechnicalEvidenceRecord& rawEvidence,
    const Raw::RawMetadata& metadata,
    const std::vector<RawAutoStartPoint::RawAutoStartPointStageImage>* warmImages,
    const std::string& proxyIdentity,
    bool fullResolution,
    int featureMaxDimension,
    const RawRecipe::RawDevelopmentRecipe& solverBaseRecipe,
    const std::string& requestReason,
    const std::string& renderRequestTag = {});

bool HasRequiredStages(const RenderedEvaluation& rendered);

} // namespace Stack::Validation::PreciseCandidateRuntime
