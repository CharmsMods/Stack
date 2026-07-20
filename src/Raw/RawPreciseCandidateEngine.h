#pragma once

#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RenderedFeatureEvidence.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::PreciseRaw {

inline constexpr int kCandidateEvaluationSchemaVersion = 1;
inline constexpr const char* kCandidateEngineVersion = "raw-candidate-engine-v1";
inline constexpr const char* kParameterSpaceVersion = "raw-parameter-space-v1";
inline constexpr const char* kObjectiveConstraintVersion = "raw-objective-constraints-v1";
inline constexpr const char* kSurfacePlanVersion = "raw-objective-surfaces-v1";

enum class ParameterId : int {
    RawExposureEv = 0,
    LocalTargetEv,
    LocalDeltaEv,
    LocalWidthEv,
    LocalFeather,
    FinishY1,
    FinishY2,
    FinishY3,
    DisplayBlackEv,
    DisplayWhiteEv,
    DisplayMiddleGrey,
    DisplayShoulder,
    DisplayToe,
    Count
};

enum class EvaluationStatus {
    Pending,
    Complete,
    Rejected,
    Failed,
    Canceled,
    Stale
};

enum class ConstraintTier {
    Tier0IdentityState,
    Tier1RawArtifact
};

enum class ConstraintStatus {
    Passed,
    Failed,
    Unavailable
};

enum class ObjectiveTier {
    Tier2Technical,
    Tier3Display,
    Tier5TieBreaker
};

struct CandidateParameterVector {
    double rawExposureEv = 0.0;
    double localTargetEv = -2.0;
    double localDeltaEv = 0.0;
    double localWidthEv = 1.0;
    double localFeather = 0.35;
    double finishY1 = 0.25;
    double finishY2 = 0.50;
    double finishY3 = 0.75;
    double displayBlackEv = -8.0;
    double displayWhiteEv = 4.0;
    double displayMiddleGrey = 0.18;
    double displayShoulder = 0.45;
    double displayToe = 0.18;
};

struct ParameterRange {
    ParameterId id = ParameterId::RawExposureEv;
    double lower = 0.0;
    double upper = 0.0;
    double warm = 0.0;
    std::string units;
    bool active = true;
    std::string reason;
};

struct ParameterSpace {
    std::string version = kParameterSpaceVersion;
    std::array<ParameterRange, static_cast<std::size_t>(ParameterId::Count)> ranges;
};

struct CandidateOwnership {
    bool rawExposureUserOwned = false;
    bool localRangeUserOwned = false;
    bool finishToneUserOwned = false;
    bool displayFitUserOwned = false;
    std::string identity = "unowned-visible-controls-v1";
};

struct CandidateIdentityContext {
    std::string sourceIdentity;
    std::string decodeIdentity;
    std::string rawEvidenceIdentity;
    std::string baseRecipeIdentity;
    std::string rendererIdentity;
    std::string proxyIdentity;
    std::string featureVersion = RenderedFeatures::kRenderedFeatureVersion;
    std::string budgetIdentity;
    std::string ownershipIdentity;
    std::uint64_t generation = 0;
};

struct CandidateProposal {
    bool valid = false;
    std::string engineVersion = kCandidateEngineVersion;
    std::string parameterVersion = kParameterSpaceVersion;
    std::string candidateId;
    std::string candidateRecipeIdentity;
    CandidateIdentityContext identities;
    CandidateParameterVector parameters;
    CandidateOwnership ownership;
    RawRecipe::RawDevelopmentRecipe recipe;
    std::string proposalReason;
    std::vector<std::string> warnings;
};

struct StageFeatureEvidence {
    RawAutoStartPoint::RawAutoStartPointStage stage =
        RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
    RenderedFeatures::FeatureRecord features;
    RenderedFeatures::FeatureRecord comparisonToWarm;
};

struct CandidateRenderEvidence {
    bool attempted = false;
    bool success = false;
    bool canceled = false;
    bool stale = false;
    bool fullResolution = false;
    std::string renderIdentity;
    std::string proxyIdentity;
    int renderWidth = 0;
    int renderHeight = 0;
    int featureWidth = 0;
    int featureHeight = 0;
    double renderRuntimeMs = 0.0;
    double featureRuntimeMs = 0.0;
    int imageCacheHits = 0;
    int imageCacheMisses = 0;
    int rawStageCacheHits = 0;
    int rawStageCacheMisses = 0;
    std::vector<StageFeatureEvidence> stages;
    std::string error;
};

struct ConstraintResult {
    std::string id;
    ConstraintTier tier = ConstraintTier::Tier0IdentityState;
    ConstraintStatus status = ConstraintStatus::Unavailable;
    double value = 0.0;
    double limit = 0.0;
    std::string units;
    double uncertainty01 = 1.0;
    std::string reason;
};

struct ObjectiveTerm {
    std::string id;
    ObjectiveTier tier = ObjectiveTier::Tier2Technical;
    bool valid = false;
    double value = 0.0;
    std::string units;
    double uncertainty01 = 1.0;
    std::string stage;
    std::string role;
    std::string reason;
};

struct CandidateEvaluationRecord {
    int schemaVersion = kCandidateEvaluationSchemaVersion;
    std::string engineVersion = kCandidateEngineVersion;
    std::string objectiveConstraintVersion = kObjectiveConstraintVersion;
    std::string evaluationId;
    EvaluationStatus status = EvaluationStatus::Pending;
    CandidateProposal proposal;
    CandidateRenderEvidence render;
    std::vector<ConstraintResult> constraints;
    std::vector<ObjectiveTerm> terms;
    bool cacheHit = false;
    bool currentRecipeUnchanged = false;
    bool undoHistoryUnchanged = false;
    bool projectDirtyStateUnchanged = false;
    std::string rejectionReason;
};

struct SurfaceSample {
    std::string sampleId;
    CandidateParameterVector parameters;
    bool warmStart = false;
    double axisX = 0.0;
    double axisY = 0.0;
};

struct SurfaceSlice {
    std::string id;
    ParameterId axisX = ParameterId::RawExposureEv;
    bool hasAxisY = false;
    ParameterId axisY = ParameterId::RawExposureEv;
    std::string conditionalAnchor;
    std::vector<SurfaceSample> samples;
};

struct ObjectiveSurfacePlan {
    std::string version = kSurfacePlanVersion;
    std::string parameterVersion = kParameterSpaceVersion;
    CandidateParameterVector warmStart;
    std::vector<SurfaceSlice> slices;
};

struct StagePromotionAgreement {
    RawAutoStartPoint::RawAutoStartPointStage stage =
        RawAutoStartPoint::RawAutoStartPointStage::RawPlacement;
    RenderedFeatures::FeatureAgreement agreement;
};

struct FullResolutionPromotionRecord {
    bool requested = false;
    bool valid = false;
    std::string candidateId;
    std::string candidateRecipeIdentity;
    std::string proxyIdentity;
    std::string fullResolutionIdentity;
    int proxyWidth = 0;
    int proxyHeight = 0;
    int fullWidth = 0;
    int fullHeight = 0;
    std::vector<StagePromotionAgreement> stages;
    std::string reason;
};

class CandidateEvaluationCache {
public:
    bool Find(const std::string& evaluationId, CandidateEvaluationRecord& out) const;
    void Store(const CandidateEvaluationRecord& record);
    void Clear();
    std::size_t Size() const;

private:
    std::unordered_map<std::string, CandidateEvaluationRecord> m_Records;
};

const char* ParameterStableString(ParameterId id);
const char* EvaluationStatusName(EvaluationStatus status);
const char* ConstraintTierName(ConstraintTier tier);
const char* ConstraintStatusName(ConstraintStatus status);
const char* ObjectiveTierName(ObjectiveTier tier);

double GetParameter(const CandidateParameterVector& parameters, ParameterId id);
void SetParameter(CandidateParameterVector& parameters, ParameterId id, double value);

std::string CanonicalRecipeBytes(const RawRecipe::RawDevelopmentRecipe& recipe);
std::string RecipeIdentity(const RawRecipe::RawDevelopmentRecipe& recipe);
CandidateParameterVector ExtractParameters(const RawRecipe::RawDevelopmentRecipe& recipe);
ParameterSpace BuildParameterSpace(
    const RawRecipe::RawDevelopmentRecipe& warmRecipe,
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence);
CandidateProposal BuildCandidateProposal(
    const RawRecipe::RawDevelopmentRecipe& baseRecipe,
    const CandidateParameterVector& parameters,
    CandidateIdentityContext identities,
    CandidateOwnership ownership,
    std::string proposalReason);
ObjectiveSurfacePlan BuildObjectiveSurfacePlan(
    const CandidateParameterVector& warmStart,
    const ParameterSpace& parameterSpace);

CandidateEvaluationRecord EvaluateCandidate(
    const CandidateProposal& proposal,
    const ParameterSpace& parameterSpace,
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence,
    CandidateRenderEvidence render,
    const RawRecipe::RawDevelopmentRecipe& currentRecipeBefore,
    const RawRecipe::RawDevelopmentRecipe& currentRecipeAfter,
    int undoDepthBefore,
    int undoDepthAfter,
    bool projectDirtyBefore,
    bool projectDirtyAfter);

FullResolutionPromotionRecord CompareProxyAndFullResolution(
    const CandidateProposal& proposal,
    const CandidateRenderEvidence& proxy,
    const CandidateRenderEvidence& full);

nlohmann::json SerializeParameters(const CandidateParameterVector& parameters);
nlohmann::json SerializeParameterSpace(const ParameterSpace& parameterSpace);
nlohmann::json SerializeProposal(const CandidateProposal& proposal);
nlohmann::json SerializeEvaluation(const CandidateEvaluationRecord& record);
nlohmann::json SerializeSurfacePlan(const ObjectiveSurfacePlan& plan);
nlohmann::json SerializePromotion(const FullResolutionPromotionRecord& promotion);

} // namespace Stack::PreciseRaw
