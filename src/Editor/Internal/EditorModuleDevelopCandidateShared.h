#pragma once

#include "Raw/RawImageData.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Stack::Editor::DevelopCandidate {

inline constexpr const char* kDevelopAdaptiveRenderBudgetVersion = "AdaptiveRenderBudgetV1";
inline constexpr std::size_t kDefaultDevelopCandidateRenderRequestsPerNode = 4;
inline constexpr std::size_t kMaxDevelopCandidateRenderRequestsPerNode = 6;
inline constexpr std::size_t kMaxDevelopCandidateRenderRequestsTotal = 20;

struct DevelopStageCacheValidation {
    bool evaluated = false;
    bool met = true;
    bool expectedRawBaseReuse = false;
    bool expectedPreFinishReuse = false;
    std::string expectedBoundary = "none";
    std::string status = "notRequired";
    std::string reason;
};

bool IsSubjectIntentProbeCandidateIdForRenderRequest(const std::string& candidateId);
bool IsFinishToneProbeCandidateIdForRenderRequest(const std::string& candidateId);
bool TryResolveWhiteBalanceProbeCandidateModeForRenderRequest(
    const std::string& candidateId,
    Raw::WhiteBalanceMode& outMode);
bool IsWhiteBalanceProbeCandidateIdForRenderRequest(const std::string& candidateId);
std::string DevelopRenderedRevisionStageForRefineIntent(const std::string& refineIntent);
std::string DevelopRenderedRevisionStageForCandidateId(const std::string& candidateId);
std::string DevelopExpectedDirtyBoundaryForCandidateStage(const std::string& candidateRevisionStage);
std::string DevelopObservedDirtyBoundaryFromCacheHits(
    bool rawBaseCacheHit,
    bool preFinishCacheHit);
DevelopStageCacheValidation EvaluateDevelopStageCacheValidation(
    const std::string& candidateRevisionStage,
    bool rawBaseCacheHit,
    bool preFinishCacheHit);

} // namespace Stack::Editor::DevelopCandidate
