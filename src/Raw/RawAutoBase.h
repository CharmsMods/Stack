#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawImageAnalysis.h"

#include <array>
#include <string>
#include <vector>

namespace Stack::RawAutoBase {

enum class RecommendationKind {
    RawExposure,
    WhiteBalance,
    HighlightProtection,
    HighlightReconstruction
};

enum class RecommendationAction {
    None,
    ApplyVisibleRecipeValue,
    CreateSuggestionOnly
};

struct ViewTransformFit {
    bool valid = false;
    float exposure = 0.0f;
    float blackEv = -8.0f;
    float whiteEv = 4.0f;
    float middleGrey = 0.18f;
    float shoulder = 0.45f;
    float toe = 0.18f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool preserveHue = true;

    float highlightRisk01 = 0.0f;
    float shadowCompressionRisk01 = 0.0f;
    float whiteMarginEv = 0.35f;
    float blackMarginEv = 0.30f;
    float medianEv = 0.0f;
    float blackAnchorEv = 0.0f;
    float whiteAnchorEv = 0.0f;
    std::string reason;
};

struct ViewFitDecision {
    bool valid = false;
    bool canApply = false;
    ViewTransformFit fit;
    std::string summary;
    std::string reason;
};

struct RawExposureRecommendation {
    bool valid = false;
    RecommendationAction action = RecommendationAction::CreateSuggestionOnly;
    float currentEv = 0.0f;
    float suggestedEv = 0.0f;
    float deltaEv = 0.0f;
    float confidence = 0.0f;
    bool autoApplyAllowed = false;
    bool blockedByHighlightRisk = false;
    bool usedCurrentFrameFallback = false;
    std::string rationale;
};

struct WhiteBalanceRecommendation {
    enum class Method {
        CameraAsShot,
        GrayWorld,
        ShadesOfGray,
        GreyEdge
    };

    bool valid = false;
    RecommendationAction action = RecommendationAction::CreateSuggestionOnly;
    Method method = Method::CameraAsShot;
    float gainsR = 1.0f;
    float gainsG = 1.0f;
    float gainsB = 1.0f;
    float confidence = 0.0f;
    float eligiblePixelPercent = 0.0f;
    float neutralResidualBefore = 0.0f;
    float neutralResidualAfter = 0.0f;
    bool autoApplyAllowed = false;
    bool cameraWhiteBalanceAvailable = false;
    bool manualWhiteBalanceProtected = false;
    bool alternateCandidateAvailable = false;
    std::string rationale;
};

struct HighlightRecommendation {
    bool valid = false;
    RecommendationAction protectionAction = RecommendationAction::CreateSuggestionOnly;
    RecommendationAction reconstructionAction = RecommendationAction::CreateSuggestionOnly;
    bool recommendProtectiveViewShoulder = false;
    bool recommendNoPositiveRawExposure = false;
    bool recommendReconstruction = false;
    bool recommendAchromaticClip = false;
    float confidence = 0.0f;
    std::string rationale;
};

enum class SuggestedLocalAdjustmentKind {
    OpenShadows,
    ProtectSky,
    OpenBacklitSubject,
    RecoverHighlights,
    BrightenFoliage
};

struct SuggestedLocalAdjustment {
    bool valid = false;
    SuggestedLocalAdjustmentKind kind = SuggestedLocalAdjustmentKind::OpenShadows;

    float targetEv = 0.0f;
    float deltaEv = 0.0f;
    float widthEv = 2.0f;
    float feather = 0.6f;

    bool protectHighlights = true;

    bool colorQualifierEnabled = false;
    float targetSceneR = 0.0f;
    float targetSceneG = 1.0f;
    float targetSceneB = 0.0f;
    float colorWidth = 0.32f;
    float colorFeather = 0.35f;
    float neutralGuard = 0.08f;

    float confidence = 0.0f;
    float affectedAreaPercent = 0.0f;
    std::string label;
    std::string rationale;
};

struct LocalSuggestionPixel {
    bool valid = true;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct LocalSuggestionAnalysisImage {
    bool valid = false;
    bool sceneLinearBeforeLocalRange = true;
    int width = 0;
    int height = 0;
    std::vector<LocalSuggestionPixel> pixels;
    std::string statusMessage;
};

struct LocalSuggestionComponentReport {
    bool valid = false;
    float validPixelPercent = 0.0f;

    float skyAreaPercent = 0.0f;
    float skyMedianEv = 0.0f;
    float skyP70Ev = 0.0f;
    float skyMedianR = 0.0f;
    float skyMedianG = 0.0f;
    float skyMedianB = 0.0f;

    float foliageAreaPercent = 0.0f;
    float foliageMedianEv = 0.0f;
    float foliageMedianR = 0.0f;
    float foliageMedianG = 0.0f;
    float foliageMedianB = 0.0f;
    float foliageChromaMedian = 0.0f;

    float shadowAreaPercent = 0.0f;
    float shadowMedianEv = 0.0f;
    float shadowP25Ev = 0.0f;

    float brightTopAreaPercent = 0.0f;
    float centerMedianEv = 0.0f;
    float backlitContrastEv = 0.0f;

    std::string statusMessage;
};

struct NoiseDetailRecommendation {
    bool valid = false;

    float iso = 0.0f;
    float baselineNoise = 1.0f;
    float effectiveNoiseScore = 0.0f;
    float shadowLiftEv = 0.0f;

    bool suggestChromaDenoise = false;
    bool suggestLumaDenoise = false;
    bool suggestReduceSharpening = false;
    bool autoApplyMinimalChromaDenoise = false;

    float chromaDenoiseAmount = 0.0f;
    float lumaDenoiseAmount = 0.0f;
    float sharpeningScale = 1.0f;

    float confidence = 0.0f;
    std::string rationale;
};

struct AutoBaseRecommendations {
    RawExposureRecommendation exposure;
    WhiteBalanceRecommendation whiteBalance;
    HighlightRecommendation highlight;
    NoiseDetailRecommendation noiseDetail;
    std::vector<SuggestedLocalAdjustment> localAdjustments;
    LocalSuggestionComponentReport localReport;
    std::string localSuggestionRationale;
};

struct WhiteBalanceSample {
    bool valid = true;
    bool clipped = false;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float luma = 0.0f;
};

struct WhiteBalanceCandidateEvidence {
    bool valid = false;
    WhiteBalanceRecommendation::Method method =
        WhiteBalanceRecommendation::Method::GrayWorld;
    float gainsR = 1.0f;
    float gainsG = 1.0f;
    float gainsB = 1.0f;
    float eligiblePixelPercent = 0.0f;
    float neutralResidualBefore = 0.0f;
    float neutralResidualAfter = 0.0f;
    bool sceneLooksStylized = false;
    bool candidateGainsAreExtreme = false;
    std::string rationale;
};

float Remap01(float value, float low, float high);
float ComputeHighlightRisk01(const Stack::RawAnalysis::RawImageAnalysis& analysis);
float ComputeShadowCompressionRisk01(const Stack::RawAnalysis::RawImageAnalysis& analysis);
ViewTransformFit FitViewTransformFromAnalysis(const Stack::RawAnalysis::RawImageAnalysis& analysis);
ViewFitDecision BuildAutoBaseViewFitDecision(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
void ApplyViewTransformFitToRecipe(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const ViewTransformFit& fit);

WhiteBalanceCandidateEvidence BuildWhiteBalanceCandidateEvidence(
    const std::vector<WhiteBalanceSample>& samples,
    const Stack::RawAnalysis::PercentileStats& stats,
    WhiteBalanceRecommendation::Method method);
RawExposureRecommendation BuildRawExposureRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe);
WhiteBalanceRecommendation BuildWhiteBalanceRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const WhiteBalanceCandidateEvidence* alternateEvidence = nullptr);
HighlightRecommendation BuildHighlightRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const ViewTransformFit* fit = nullptr);
float EstimateShadowLiftEvForNoiseDetail(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::vector<SuggestedLocalAdjustment>* localSuggestions = nullptr);
NoiseDetailRecommendation BuildNoiseDetailRecommendation(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::vector<SuggestedLocalAdjustment>* localSuggestions = nullptr,
    bool visibleRawDenoiseControlsAvailable = false);
AutoBaseRecommendations BuildAutoBaseRecommendations(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const WhiteBalanceCandidateEvidence* alternateWhiteBalanceEvidence = nullptr,
    const LocalSuggestionAnalysisImage* localSuggestionImage = nullptr);
void ApplyWhiteBalanceRecommendationToRecipe(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const WhiteBalanceRecommendation& recommendation);
void ApplyHighlightProtectionToRecipe(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const HighlightRecommendation& recommendation,
    const ViewTransformFit& fit);
LocalSuggestionComponentReport AnalyzeLocalSuggestionComponents(
    const LocalSuggestionAnalysisImage& image,
    const Stack::RawAnalysis::RawImageAnalysis& analysis);
std::vector<SuggestedLocalAdjustment> BuildSuggestedLocalAdjustments(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const LocalSuggestionAnalysisImage& image,
    LocalSuggestionComponentReport* outReport = nullptr);
bool ApplySuggestedLocalAdjustment(
    const SuggestedLocalAdjustment& suggestion,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe);
const char* SuggestedLocalAdjustmentKindLabel(SuggestedLocalAdjustmentKind kind);

} // namespace Stack::RawAutoBase
