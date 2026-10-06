#include "Raw/RawPreciseCandidateEngine.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>

namespace {

using namespace Stack;
using namespace Stack::PreciseRaw;

int g_Failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_Failures;
    }
}

void Near(double actual, double expected, double tolerance, const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << '\n';
        ++g_Failures;
    }
}

RawEvidence::EvidenceMeasurement Measurement(double value, const char* units, double uncertainty = 0.05) {
    RawEvidence::EvidenceMeasurement result;
    result.valid = true;
    result.value = value;
    result.units = units;
    result.uncertainty01 = uncertainty;
    result.provenance = RawEvidence::EvidenceProvenance::Measured;
    result.reason = "candidate-engine-fixture";
    return result;
}

RawEvidence::RawTechnicalEvidenceRecord RawEvidenceFixture() {
    RawEvidence::RawTechnicalEvidenceRecord record;
    record.valid = true;
    record.sourceIdentity.valid = true;
    record.sourceIdentity.sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    record.decodeIdentity.valid = true;
    record.decodeIdentity.sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    record.evidenceIdentitySha256 = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    for (int c = 0; c < 3; ++c) {
        RawEvidence::PlaneEvidence plane;
        plane.plane = c;
        plane.planeName = c == 0 ? "R" : (c == 1 ? "G" : "B");
        plane.wbScaledHeadroomEv = Measurement(1.25 - 0.10 * c, "EV", 0.08);
        record.planes.push_back(plane);
    }
    record.clipping.valid = true;
    record.clipping.singleChannelClippedFraction = Measurement(0.01, "fraction");
    record.clipping.multiChannelClippedFraction = Measurement(0.002, "fraction");
    record.clipping.allChannelClippedFraction = Measurement(0.0, "fraction");
    RawEvidence::NoisePlaneEvidence noise;
    noise.plane = 0;
    noise.planeName = "R";
    noise.snrBySignal.push_back(Measurement(8.0, "linear-SNR", 0.12));
    record.noise.push_back(noise);
    return record;
}

RawRecipe::RawDevelopmentRecipe BaseRecipe() {
    RawRecipe::RawDevelopmentRecipe recipe = RawRecipe::MakeDefaultRecipe("C:/private/source-a.dng", "source-a.dng");
    recipe.source.fingerprint = "source-fingerprint";
    recipe.source.fileSizeBytes = 123456;
    recipe.source.modifiedTimeTicks = 999;
    return recipe;
}

CandidateIdentityContext Identity(
    const RawRecipe::RawDevelopmentRecipe& recipe,
    const RawEvidence::RawTechnicalEvidenceRecord& raw) {
    CandidateIdentityContext identity;
    identity.sourceIdentity = raw.sourceIdentity.sha256;
    identity.decodeIdentity = raw.decodeIdentity.sha256;
    identity.rawEvidenceIdentity = raw.evidenceIdentitySha256;
    identity.baseRecipeIdentity = RecipeIdentity(recipe);
    identity.rendererIdentity = "stack-raw-development-renderer-v1";
    identity.proxyIdentity = "proxy-256-linear-srgb-v1";
    identity.featureVersion = RenderedFeatures::kRenderedFeatureVersion;
    identity.budgetIdentity = "phase-03-surface-diagnostic-v1";
    identity.ownershipIdentity = "fixture-unowned-v1";
    identity.generation = 42;
    return identity;
}

RenderedFeatures::LinearRgbImage FixtureImage(int size, double scale) {
    RenderedFeatures::LinearRgbImage image;
    image.width = size;
    image.height = size;
    image.pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double gradient = 0.04 + 0.30 * static_cast<double>(x) / std::max(1, size - 1);
            const double region = (x < size / 2 && y > size / 3) ? 0.72 : 1.0;
            const std::size_t index = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 3u;
            image.pixels[index] = static_cast<float>(gradient * region * scale);
            image.pixels[index + 1] = static_cast<float>(gradient * scale);
            image.pixels[index + 2] = static_cast<float>(gradient * (2.0 - region) * scale);
        }
    }
    return image;
}

RenderedFeatures::FeatureContext FeatureContextFor(
    const CandidateProposal& proposal,
    const RawEvidence::RawTechnicalEvidenceRecord& raw,
    const std::string& stage,
    int size) {
    RenderedFeatures::FeatureContext context;
    context.sourceIdentity = raw.sourceIdentity.sha256;
    context.recipeIdentity = proposal.candidateRecipeIdentity;
    context.stage = stage;
    context.colorSpace = "linear-sRGB";
    context.colorTransformIdentity = "IEC-61966-2-1-linear-sRGB-to-XYZ-D65-v1";
    context.transferFunction = "linear";
    context.workingToXyz = {
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041
    };
    context.referenceGrey = 0.18;
    context.rawEvidenceIdentity = raw.evidenceIdentitySha256;
    context.cropIdentity = "fixture-crop";
    context.orientationNormalized = true;
    context.sourceWidth = size;
    context.sourceHeight = size;
    context.globalLiftEv = proposal.parameters.rawExposureEv;
    context.maximumLocalLiftEv = std::max(0.0, proposal.parameters.localDeltaEv);
    return context;
}

StageFeatureEvidence SceneStage(
    const CandidateProposal& proposal,
    const RawEvidence::RawTechnicalEvidenceRecord& raw,
    RawAutoStartPoint::RawAutoStartPointStage stage,
    const char* stageId,
    int size,
    double scale) {
    const auto warm = FixtureImage(size, 1.0);
    const auto candidate = FixtureImage(size, scale);
    const auto context = FeatureContextFor(proposal, raw, stageId, size);
    StageFeatureEvidence result;
    result.stage = stage;
    result.features = RenderedFeatures::AnalyzeSceneLinear(candidate, context, &raw);
    result.comparisonToWarm = RenderedFeatures::CompareRenderedImages(warm, candidate, context);
    return result;
}

StageFeatureEvidence DisplayStage(
    const CandidateProposal& proposal,
    const RawEvidence::RawTechnicalEvidenceRecord& raw,
    int size,
    double scale) {
    const auto warm = FixtureImage(size, 1.0);
    const auto candidate = FixtureImage(size, scale);
    const auto context = FeatureContextFor(proposal, raw, "display-candidate/linear-display", size);
    StageFeatureEvidence result;
    result.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    result.features = RenderedFeatures::AnalyzeDisplayMapped(candidate, context);
    result.comparisonToWarm = RenderedFeatures::CompareRenderedImages(warm, candidate, context);
    return result;
}

CandidateRenderEvidence RenderFixture(
    const CandidateProposal& proposal,
    const RawEvidence::RawTechnicalEvidenceRecord& raw,
    int size = 128,
    bool full = false) {
    CandidateRenderEvidence render;
    render.attempted = true;
    render.success = true;
    render.fullResolution = full;
    render.renderIdentity = full ? "renderer-full-v1" : "renderer-proxy-v1";
    render.proxyIdentity = full ? "full-resolution-v1" : "proxy-128-v1";
    render.renderWidth = full ? 1024 : size;
    render.renderHeight = full ? 768 : size;
    render.featureWidth = size;
    render.featureHeight = size;
    render.renderRuntimeMs = 12.0;
    render.featureRuntimeMs = 5.0;
    const double scale = std::exp2(proposal.parameters.rawExposureEv);
    render.stages.push_back(SceneStage(proposal, raw, RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
        "neutral-scene/linear-sRGB", size, 1.0));
    render.stages.push_back(SceneStage(proposal, raw, RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
        "raw-placement/linear-sRGB", size, scale));
    render.stages.push_back(SceneStage(proposal, raw, RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
        "local-candidate/linear-sRGB", size, scale * std::exp2(proposal.parameters.localDeltaEv * 0.2)));
    render.stages.push_back(SceneStage(proposal, raw, RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
        "finish-tone-candidate/linear-sRGB", size, scale));
    render.stages.push_back(DisplayStage(proposal, raw, size, scale));
    return render;
}

const ConstraintResult* FindConstraint(const CandidateEvaluationRecord& record, const std::string& id) {
    const auto found = std::find_if(record.constraints.begin(), record.constraints.end(), [&](const ConstraintResult& item) {
        return item.id == id;
    });
    return found == record.constraints.end() ? nullptr : &*found;
}

void TestIdentityAndImmutableProposal() {
    const auto raw = RawEvidenceFixture();
    const RawRecipe::RawDevelopmentRecipe base = BaseRecipe();
    RawRecipe::RawDevelopmentRecipe relocated = base;
    relocated.source.sourcePath = "D:/different/location/source-a.dng";
    relocated.source.relativePathKey = "other";
    relocated.source.modifiedTimeTicks = 123456789;
    relocated.source.displayName = "renamed.dng";
    Check(RecipeIdentity(base) == RecipeIdentity(relocated),
        "candidate recipe identity excludes path, mtime, and UI label");
    relocated.preToneExposureEv = 0.25f;
    Check(RecipeIdentity(base) != RecipeIdentity(relocated),
        "candidate recipe identity includes visible RAW Exposure");

    const std::string before = CanonicalRecipeBytes(base);
    CandidateParameterVector parameters = ExtractParameters(base);
    parameters.rawExposureEv = 0.50;
    parameters.localDeltaEv = 0.35;
    CandidateProposal proposal = BuildCandidateProposal(
        base, parameters, Identity(base, raw), {}, "identity fixture");
    Check(proposal.valid && proposal.candidateId.size() == 64 && proposal.candidateRecipeIdentity.size() == 64,
        "proposal carries stable SHA-256 candidate and recipe identities");
    Check(CanonicalRecipeBytes(base) == before,
        "candidate proposal construction does not mutate the input recipe");
    Near(proposal.recipe.preToneExposureEv, 0.50, 1.0e-6,
        "candidate recipe carries requested visible exposure");
    Check(proposal.recipe.localRange.enabled && proposal.recipe.localRange.regionMaskEnabled,
        "active local parameter maps to visible Local Range graph and luminance mask");

    CandidateIdentityContext changed = Identity(base, raw);
    changed.generation++;
    const CandidateProposal other = BuildCandidateProposal(base, parameters, changed, {}, "identity fixture");
    Check(other.candidateId != proposal.candidateId,
        "generation participates in exact evaluation candidate identity");
}

void TestParameterSpaceAndSurfacePlan() {
    const auto raw = RawEvidenceFixture();
    const auto base = BaseRecipe();
    const ParameterSpace space = BuildParameterSpace(base, &raw);
    Near(space.ranges[static_cast<std::size_t>(ParameterId::RawExposureEv)].upper,
        1.05, 1.0e-9, "limiting blue-plane headroom caps positive exposure bound");
    Check(space.ranges.size() == static_cast<std::size_t>(ParameterId::Count),
        "frozen parameter space contains exactly 13 visible dimensions");
    const CandidateParameterVector warm = ExtractParameters(base);
    const ObjectiveSurfacePlan first = BuildObjectiveSurfacePlan(warm, space);
    const ObjectiveSurfacePlan second = BuildObjectiveSurfacePlan(warm, space);
    Check(first.slices.size() == 20,
        "surface plan contains 13 one-dimensional and seven interaction slices");
    Check(SerializeSurfacePlan(first).dump() == SerializeSurfacePlan(second).dump(),
        "surface enumeration is exactly reproducible");
    std::set<std::string> oneDimensionalAxes;
    for (const SurfaceSlice& slice : first.slices) {
        Check(std::any_of(slice.samples.begin(), slice.samples.end(), [](const SurfaceSample& sample) {
            return sample.warmStart;
        }), "every surface retains the exact warm-start candidate");
        if (!slice.hasAxisY) oneDimensionalAxes.insert(ParameterStableString(slice.axisX));
    }
    Check(oneDimensionalAxes.size() == 13,
        "every first-space dimension has a one-dimensional surface");
}

void TestConstraintEvaluationAndTerms() {
    const auto raw = RawEvidenceFixture();
    const auto base = BaseRecipe();
    const ParameterSpace space = BuildParameterSpace(base, &raw);
    const CandidateParameterVector warm = ExtractParameters(base);
    const CandidateProposal proposal = BuildCandidateProposal(base, warm, Identity(base, raw), {}, "warm start");
    const CandidateEvaluationRecord record = EvaluateCandidate(
        proposal, space, &raw, RenderFixture(proposal, raw), base, base, 7, 7, false, false);
    Check(record.status == EvaluationStatus::Complete,
        "fully identified safe warm candidate evaluates complete");
    Check(record.currentRecipeUnchanged && record.undoHistoryUnchanged && record.projectDirtyStateUnchanged,
        "candidate evaluation preserves recipe, undo, and dirty state assertions");
    Check(FindConstraint(record, "raw.wb_scaled_headroom") &&
          FindConstraint(record, "raw.wb_scaled_headroom")->status == ConstraintStatus::Passed,
        "raw headroom is a Tier 1 hard constraint");
    Check(!record.terms.empty() && std::any_of(record.terms.begin(), record.terms.end(), [](const ObjectiveTerm& term) {
        return term.id == "display.display.linear_clip_high_fraction" && term.valid;
    }), "evaluation retains individual accepted display terms");
    const nlohmann::json serialized = SerializeEvaluation(record);
    Check(serialized.at("combinedTotalScore").is_null(),
        "Phase 03 evaluation has no hidden combined total score");
    Check(serialized.at("proposal").at("completeVisibleRecipe").find("sourceRef") ==
          serialized.at("proposal").at("completeVisibleRecipe").end(),
        "durable candidate record scrubs local source paths");

    CandidateParameterVector floatDisplay = warm;
    floatDisplay.displayBlackEv = -7.37;
    floatDisplay.displayWhiteEv = 3.63;
    floatDisplay.displayMiddleGrey = 0.173;
    floatDisplay.displayShoulder = 0.317;
    floatDisplay.displayToe = 0.193;
    const CandidateProposal floatDisplayProposal = BuildCandidateProposal(
        base, floatDisplay, Identity(base, raw), {}, "production float round-trip fixture");
    const RawRecipe::RawDevelopmentRecipe floatDisplayRoundTrip =
        RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(floatDisplayProposal.recipe));
    Check(CanonicalRecipeBytes(floatDisplayProposal.recipe) == CanonicalRecipeBytes(floatDisplayRoundTrip),
        "production float display controls retain exact canonical round-trip identity");
    const CandidateEvaluationRecord floatDisplayRecord = EvaluateCandidate(
        floatDisplayProposal, space, &raw, RenderFixture(floatDisplayProposal, raw),
        base, base, 7, 7, false, false);
    Check(floatDisplayRecord.status == EvaluationStatus::Complete &&
          FindConstraint(floatDisplayRecord, "recipe.finite_serializable") &&
          FindConstraint(floatDisplayRecord, "recipe.finite_serializable")->status == ConstraintStatus::Passed,
        "non-binary display control values are not falsely rejected as recipe mutation");

    CandidateOwnership owned;
    owned.rawExposureUserOwned = true;
    owned.identity = "raw-exposure-user-owned-v1";
    CandidateIdentityContext ownedIdentity = Identity(base, raw);
    ownedIdentity.ownershipIdentity = owned.identity;
    CandidateParameterVector exposure = warm;
    exposure.rawExposureEv = 0.5;
    const CandidateProposal ownedProposal = BuildCandidateProposal(base, exposure, ownedIdentity, owned, "ownership fixture");
    const CandidateEvaluationRecord ownedRecord = EvaluateCandidate(
        ownedProposal, space, &raw, RenderFixture(ownedProposal, raw), base, base, 7, 7, false, false);
    Check(ownedRecord.status == EvaluationStatus::Rejected &&
          FindConstraint(ownedRecord, "ownership.visible_controls") &&
          FindConstraint(ownedRecord, "ownership.visible_controls")->status == ConstraintStatus::Failed,
        "user-owned visible control change is rejected before ranking");

    CandidateParameterVector unsafe = warm;
    unsafe.rawExposureEv = 1.5;
    const CandidateProposal unsafeProposal = BuildCandidateProposal(base, unsafe, Identity(base, raw), {}, "headroom fixture");
    const CandidateEvaluationRecord unsafeRecord = EvaluateCandidate(
        unsafeProposal, space, &raw, RenderFixture(unsafeProposal, raw), base, base, 7, 7, false, false);
    Check(unsafeRecord.status == EvaluationStatus::Rejected &&
          FindConstraint(unsafeRecord, "raw.wb_scaled_headroom") &&
          FindConstraint(unsafeRecord, "raw.wb_scaled_headroom")->status == ConstraintStatus::Failed,
        "positive exposure beyond limiting raw headroom is rejected");
}

void TestGraphStateFailureAndCancellation() {
    const auto raw = RawEvidenceFixture();
    const auto base = BaseRecipe();
    const auto space = BuildParameterSpace(base, &raw);
    const auto warm = ExtractParameters(base);
    CandidateProposal broken = BuildCandidateProposal(base, warm, Identity(base, raw), {}, "broken graph");
    broken.recipe.finishTone.layerJson["points"] = nlohmann::json::array({
        { { "x", 0.0 }, { "y", 0.0 } },
        { { "x", 0.5 }, { "y", 0.8 } },
        { { "x", 0.5 }, { "y", 0.2 } },
        { { "x", 1.0 }, { "y", 1.0 } }
    });
    const CandidateEvaluationRecord brokenRecord = EvaluateCandidate(
        broken, space, &raw, RenderFixture(broken, raw), base, base, 1, 1, false, false);
    Check(brokenRecord.status == EvaluationStatus::Rejected &&
          FindConstraint(brokenRecord, "graph.finish_monotone") &&
          FindConstraint(brokenRecord, "graph.finish_monotone")->status == ConstraintStatus::Failed,
        "duplicate/reversing Finish Tone graph is rejected");

    CandidateProposal nonfinite = BuildCandidateProposal(base, warm, Identity(base, raw), {}, "NaN fixture");
    nonfinite.recipe.preToneExposureEv = std::numeric_limits<float>::quiet_NaN();
    const CandidateEvaluationRecord nonfiniteRecord = EvaluateCandidate(
        nonfinite, space, &raw, RenderFixture(nonfinite, raw), base, base, 1, 1, false, false);
    Check(nonfiniteRecord.status == EvaluationStatus::Rejected &&
          FindConstraint(nonfiniteRecord, "recipe.finite_serializable") &&
          FindConstraint(nonfiniteRecord, "recipe.finite_serializable")->status == ConstraintStatus::Failed,
        "NaN recipe state is rejected");

    const CandidateProposal proposal = BuildCandidateProposal(base, warm, Identity(base, raw), {}, "state fixture");
    CandidateRenderEvidence canceled;
    canceled.attempted = true;
    canceled.canceled = true;
    canceled.error = "generation canceled";
    Check(EvaluateCandidate(proposal, space, &raw, canceled, base, base, 1, 1, false, false).status ==
          EvaluationStatus::Canceled, "canceled render publishes canceled status only");
    CandidateRenderEvidence stale;
    stale.attempted = true;
    stale.stale = true;
    stale.error = "source changed";
    Check(EvaluateCandidate(proposal, space, &raw, stale, base, base, 1, 1, false, false).status ==
          EvaluationStatus::Stale, "source/recipe generation change publishes stale status only");
    CandidateRenderEvidence failed;
    failed.attempted = true;
    failed.error = "render failed";
    Check(EvaluateCandidate(proposal, space, &raw, failed, base, base, 1, 1, false, false).status ==
          EvaluationStatus::Failed, "failed render cannot publish partial objective evidence");

    RawRecipe::RawDevelopmentRecipe changedCurrent = base;
    changedCurrent.preToneExposureEv = 0.1f;
    const CandidateEvaluationRecord mutated = EvaluateCandidate(
        proposal, space, &raw, RenderFixture(proposal, raw), base, changedCurrent, 1, 2, false, true);
    Check(mutated.status == EvaluationStatus::Rejected &&
          !mutated.currentRecipeUnchanged && !mutated.undoHistoryUnchanged && !mutated.projectDirtyStateUnchanged,
        "recipe/undo/dirty mutation is detected and rejected");
}

void TestCacheAndFullPromotion() {
    const auto raw = RawEvidenceFixture();
    const auto base = BaseRecipe();
    const auto space = BuildParameterSpace(base, &raw);
    const auto proposal = BuildCandidateProposal(base, ExtractParameters(base), Identity(base, raw), {}, "cache fixture");
    const CandidateEvaluationRecord record = EvaluateCandidate(
        proposal, space, &raw, RenderFixture(proposal, raw), base, base, 0, 0, false, false);
    CandidateEvaluationCache cache;
    cache.Store(record);
    CandidateEvaluationRecord cached;
    Check(cache.Find(record.evaluationId, cached) && cached.cacheHit && cache.Size() == 1,
        "exact evaluation identity produces a cache hit");
    Check(!cache.Find(record.evaluationId + "-different", cached),
        "changed evaluation identity misses cache");
    CandidateEvaluationRecord canceled = record;
    canceled.status = EvaluationStatus::Canceled;
    canceled.evaluationId = "canceled-record";
    cache.Store(canceled);
    Check(cache.Size() == 1, "canceled records are not permanent cache evidence");

    const CandidateRenderEvidence proxy = RenderFixture(proposal, raw, 128, false);
    const CandidateRenderEvidence full = RenderFixture(proposal, raw, 256, true);
    const FullResolutionPromotionRecord promotion = CompareProxyAndFullResolution(proposal, proxy, full);
    Check(promotion.valid && promotion.stages.size() == 5 && promotion.fullWidth > promotion.proxyWidth,
        "full-resolution promotion records per-stage proxy disagreement without applying");
    const nlohmann::json serialized = SerializePromotion(promotion);
    Check(serialized.at("valid").get<bool>() && serialized.at("stages").size() == 5,
        "promotion serialization retains each stage agreement");
}

} // namespace

int main() {
    TestIdentityAndImmutableProposal();
    TestParameterSpaceAndSurfacePlan();
    TestConstraintEvaluationAndTerms();
    TestGraphStateFailureAndCancellation();
    TestCacheAndFullPromotion();
    if (g_Failures != 0) {
        std::cerr << g_Failures << " Phase 03 candidate fixture assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Phase 03 candidate engine fixtures passed.\n";
    return 0;
}
