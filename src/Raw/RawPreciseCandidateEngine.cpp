#include "Raw/RawPreciseCandidateEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace Stack::PreciseRaw {
namespace {

constexpr double kEpsilon = 1.0e-8;

const char* StageName(RawAutoStartPoint::RawAutoStartPointStage stage) {
    switch (stage) {
        case RawAutoStartPoint::RawAutoStartPointStage::RawTechnical: return "raw-technical";
        case RawAutoStartPoint::RawAutoStartPointStage::NeutralScene: return "neutral-scene";
        case RawAutoStartPoint::RawAutoStartPointStage::RawPlacement: return "raw-placement";
        case RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate: return "local-candidate";
        case RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate: return "finish-tone-candidate";
        case RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate: return "display-candidate";
    }
    return "unknown";
}

std::size_t Index(ParameterId id) {
    return static_cast<std::size_t>(id);
}

bool Finite(double value) {
    return std::isfinite(value);
}

std::string Sha256Text(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

bool ParametersFinite(const CandidateParameterVector& parameters) {
    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        if (!Finite(GetParameter(parameters, static_cast<ParameterId>(i)))) return false;
    }
    return true;
}

bool JsonFinite(const nlohmann::json& value) {
    if (value.is_number_float()) return std::isfinite(value.get<double>());
    if (value.is_array()) {
        return std::all_of(value.begin(), value.end(), [](const nlohmann::json& item) {
            return JsonFinite(item);
        });
    }
    if (value.is_object()) {
        return std::all_of(value.begin(), value.end(), [](const auto& item) {
            return JsonFinite(item);
        });
    }
    return true;
}

double JsonNumber(const nlohmann::json& value, const char* key, double fallback) {
    const auto found = value.find(key);
    return found != value.end() && found->is_number() ? found->get<double>() : fallback;
}

std::array<double, 3> FinishInterior(const RawRecipe::RawDevelopmentRecipe& recipe) {
    std::array<double, 3> result { 0.25, 0.50, 0.75 };
    const nlohmann::json& payload = recipe.finishTone.layerJson;
    if (!payload.is_object() || !payload.contains("points") || !payload["points"].is_array()) return result;
    const nlohmann::json& points = payload["points"];
    if (points.size() < 3) return result;
    auto sampleAt = [&](double target) {
        double bestDistance = std::numeric_limits<double>::infinity();
        double best = target;
        for (const nlohmann::json& point : points) {
            if (!point.is_object()) continue;
            const double x = JsonNumber(point, "x", target);
            const double y = JsonNumber(point, "y", target);
            const double distance = std::abs(x - target);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = y;
            }
        }
        return bestDistance < 0.30 ? best : target;
    };
    result[0] = sampleAt(0.25);
    result[1] = sampleAt(0.50);
    result[2] = sampleAt(0.75);
    return result;
}

nlohmann::json FinishPoints(const CandidateParameterVector& parameters) {
    return nlohmann::json::array({
        { { "x", 0.0 }, { "y", 0.0 }, { "shape", 1 } },
        { { "x", 0.25 }, { "y", parameters.finishY1 }, { "shape", 1 } },
        { { "x", 0.50 }, { "y", parameters.finishY2 }, { "shape", 1 } },
        { { "x", 0.75 }, { "y", parameters.finishY3 }, { "shape", 1 } },
        { { "x", 1.0 }, { "y", 1.0 }, { "shape", 1 } }
    });
}

double MinimumRawHeadroom(const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence) {
    if (!rawEvidence || !rawEvidence->valid) return std::numeric_limits<double>::quiet_NaN();
    double minimum = std::numeric_limits<double>::infinity();
    for (const RawEvidence::PlaneEvidence& plane : rawEvidence->planes) {
        if (plane.wbScaledHeadroomEv.valid) {
            minimum = std::min(minimum, plane.wbScaledHeadroomEv.value);
        }
    }
    return std::isfinite(minimum) ? minimum : std::numeric_limits<double>::quiet_NaN();
}

void SetRange(
    ParameterSpace& space,
    ParameterId id,
    double lower,
    double upper,
    double warm,
    const char* units,
    bool active,
    std::string reason) {
    ParameterRange& range = space.ranges[Index(id)];
    range.id = id;
    range.lower = lower;
    range.upper = upper;
    range.warm = warm;
    range.units = units;
    range.active = active;
    range.reason = std::move(reason);
}

bool Near(double a, double b, double tolerance = 1.0e-6) {
    return std::abs(a - b) <= tolerance;
}

bool FinishGraphValid(const RawRecipe::RawDevelopmentRecipe& recipe, std::string& reason) {
    const nlohmann::json& payload = recipe.finishTone.layerJson;
    if (!payload.is_object() || !payload.contains("points") || !payload["points"].is_array()) {
        reason = "Finish Tone points are missing.";
        return false;
    }
    const nlohmann::json& points = payload["points"];
    if (points.size() < 2 || points.size() > 12) {
        reason = "Finish Tone point count must be between 2 and 12.";
        return false;
    }
    double previousX = -std::numeric_limits<double>::infinity();
    double previousY = -std::numeric_limits<double>::infinity();
    for (const nlohmann::json& point : points) {
        if (!point.is_object()) {
            reason = "Finish Tone contains a non-object point.";
            return false;
        }
        const double x = JsonNumber(point, "x", std::numeric_limits<double>::quiet_NaN());
        const double y = JsonNumber(point, "y", std::numeric_limits<double>::quiet_NaN());
        if (!Finite(x) || !Finite(y) || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
            reason = "Finish Tone point is non-finite or outside [0,1].";
            return false;
        }
        if (x <= previousX + 1.0e-6) {
            reason = "Finish Tone x positions are not strictly ordered.";
            return false;
        }
        if (y + 1.0e-6 < previousY) {
            reason = "Finish Tone is not monotone.";
            return false;
        }
        previousX = x;
        previousY = y;
    }
    const nlohmann::json& first = points.front();
    const nlohmann::json& last = points.back();
    if (!Near(JsonNumber(first, "x", -1.0), 0.0) || !Near(JsonNumber(first, "y", -1.0), 0.0) ||
        !Near(JsonNumber(last, "x", -1.0), 1.0) || !Near(JsonNumber(last, "y", -1.0), 1.0)) {
        reason = "Finish Tone endpoints must remain (0,0) and (1,1).";
        return false;
    }
    reason = "Finite, ordered, monotone Finish Tone graph.";
    return true;
}

bool LocalGraphValid(const RawRecipe::RawDevelopmentRecipe& recipe, std::string& reason) {
    const RawRecipe::RawLocalRangeRecipe& local = recipe.localRange;
    if (!Finite(local.minEv) || !Finite(local.maxEv) || local.maxEv <= local.minEv ||
        local.points.size() < 2 || local.points.size() > 12) {
        reason = "Local Range domain or point count is invalid.";
        return false;
    }
    double previous = -std::numeric_limits<double>::infinity();
    for (const RawRecipe::RawLocalRangePoint& point : local.points) {
        if (!Finite(point.ev) || !Finite(point.deltaEv) || point.ev <= previous + 1.0e-6) {
            reason = "Local Range points are non-finite or not strictly ordered.";
            return false;
        }
        previous = point.ev;
    }
    if (!Near(local.points.front().ev, local.minEv, 1.0e-4) ||
        !Near(local.points.back().ev, local.maxEv, 1.0e-4) ||
        !Near(local.points.front().deltaEv, 0.0, 1.0e-4) ||
        !Near(local.points.back().deltaEv, 0.0, 1.0e-4)) {
        reason = "Local Range endpoints must remain neutral at the domain bounds.";
        return false;
    }
    reason = "Finite, ordered Local Range graph within capacity.";
    return true;
}

bool ControlChanged(
    const RawRecipe::RawDevelopmentRecipe& base,
    const RawRecipe::RawDevelopmentRecipe& candidate,
    ParameterId family) {
    switch (family) {
        case ParameterId::RawExposureEv:
            return !Near(base.preToneExposureEv, candidate.preToneExposureEv);
        case ParameterId::LocalTargetEv:
            return !RawRecipe::LocalRangeStateEquals(base, candidate);
        case ParameterId::FinishY1:
            return base.finishTone.layerJson != candidate.finishTone.layerJson;
        case ParameterId::DisplayBlackEv:
            return base.viewTransform.layerJson != candidate.viewTransform.layerJson;
        default:
            return false;
    }
}

const StageFeatureEvidence* FindStage(
    const CandidateRenderEvidence& render,
    RawAutoStartPoint::RawAutoStartPointStage stage) {
    const auto found = std::find_if(render.stages.begin(), render.stages.end(), [&](const StageFeatureEvidence& item) {
        return item.stage == stage;
    });
    return found == render.stages.end() ? nullptr : &*found;
}

ConstraintResult Constraint(
    std::string id,
    ConstraintTier tier,
    ConstraintStatus status,
    std::string reason,
    double value = 0.0,
    double limit = 0.0,
    std::string units = {},
    double uncertainty = 0.0) {
    ConstraintResult result;
    result.id = std::move(id);
    result.tier = tier;
    result.status = status;
    result.reason = std::move(reason);
    result.value = value;
    result.limit = limit;
    result.units = std::move(units);
    result.uncertainty01 = std::clamp(uncertainty, 0.0, 1.0);
    return result;
}

void AddFeatureTerm(
    std::vector<ObjectiveTerm>& terms,
    const std::string& prefix,
    const RenderedFeatures::FeatureRecord& record,
    const char* featureId,
    ObjectiveTier tier,
    const char* role) {
    const RenderedFeatures::FeatureValue* feature = RenderedFeatures::FindFeature(record, featureId);
    ObjectiveTerm term;
    term.id = prefix + "." + featureId;
    term.tier = tier;
    term.stage = record.stage;
    term.role = role;
    if (feature && feature->valid && feature->disposition == RenderedFeatures::FeatureDisposition::Accepted) {
        term.valid = true;
        term.value = feature->value;
        term.units = feature->units;
        term.uncertainty01 = feature->uncertainty01;
        term.reason = feature->reason;
    } else {
        term.reason = feature ? "Feature unavailable or not admitted for objective use." : "Feature missing.";
    }
    terms.push_back(std::move(term));
}

double FinishSmoothness(const RawRecipe::RawDevelopmentRecipe& recipe) {
    const auto values = FinishInterior(recipe);
    const std::array<double, 5> y { 0.0, values[0], values[1], values[2], 1.0 };
    double sum = 0.0;
    for (std::size_t i = 1; i + 1 < y.size(); ++i) {
        const double second = y[i + 1] - 2.0 * y[i] + y[i - 1];
        sum += second * second;
    }
    return sum;
}

std::vector<double> Triplet(const ParameterRange& range, double step) {
    const double center = std::clamp(range.warm, range.lower, range.upper);
    return {
        std::clamp(center - step, range.lower, range.upper),
        center,
        std::clamp(center + step, range.lower, range.upper)
    };
}

bool SameParameters(const CandidateParameterVector& a, const CandidateParameterVector& b) {
    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        if (!Near(GetParameter(a, static_cast<ParameterId>(i)),
                  GetParameter(b, static_cast<ParameterId>(i)), 1.0e-9)) return false;
    }
    return true;
}

void AddWarmIfMissing(SurfaceSlice& slice, const CandidateParameterVector& warm) {
    const bool found = std::any_of(slice.samples.begin(), slice.samples.end(), [&](const SurfaceSample& sample) {
        return SameParameters(sample.parameters, warm);
    });
    if (!found) {
        SurfaceSample sample;
        sample.sampleId = slice.id + ":warm";
        sample.parameters = warm;
        sample.warmStart = true;
        sample.axisX = GetParameter(warm, slice.axisX);
        sample.axisY = slice.hasAxisY ? GetParameter(warm, slice.axisY) : 0.0;
        slice.samples.insert(slice.samples.begin(), std::move(sample));
    } else {
        for (SurfaceSample& sample : slice.samples) {
            if (SameParameters(sample.parameters, warm)) sample.warmStart = true;
        }
    }
}

nlohmann::json SerializeConstraint(const ConstraintResult& constraint) {
    return {
        { "id", constraint.id },
        { "tier", ConstraintTierName(constraint.tier) },
        { "status", ConstraintStatusName(constraint.status) },
        { "value", constraint.value },
        { "limit", constraint.limit },
        { "units", constraint.units },
        { "uncertainty01", constraint.uncertainty01 },
        { "reason", constraint.reason }
    };
}

nlohmann::json SerializeTerm(const ObjectiveTerm& term) {
    return {
        { "id", term.id },
        { "tier", ObjectiveTierName(term.tier) },
        { "valid", term.valid },
        { "value", term.valid ? nlohmann::json(term.value) : nlohmann::json(nullptr) },
        { "units", term.units },
        { "uncertainty01", term.uncertainty01 },
        { "stage", term.stage },
        { "role", term.role },
        { "reason", term.reason }
    };
}

} // namespace

const char* ParameterStableString(ParameterId id) {
    switch (id) {
        case ParameterId::RawExposureEv: return "raw_exposure_ev";
        case ParameterId::LocalTargetEv: return "local_target_ev";
        case ParameterId::LocalDeltaEv: return "local_delta_ev";
        case ParameterId::LocalWidthEv: return "local_width_ev";
        case ParameterId::LocalFeather: return "local_feather";
        case ParameterId::FinishY1: return "finish_y_1";
        case ParameterId::FinishY2: return "finish_y_2";
        case ParameterId::FinishY3: return "finish_y_3";
        case ParameterId::DisplayBlackEv: return "display_black_ev";
        case ParameterId::DisplayWhiteEv: return "display_white_ev";
        case ParameterId::DisplayMiddleGrey: return "display_middle_grey";
        case ParameterId::DisplayShoulder: return "display_shoulder";
        case ParameterId::DisplayToe: return "display_toe";
        case ParameterId::Count: break;
    }
    return "unknown";
}

const char* EvaluationStatusName(EvaluationStatus status) {
    switch (status) {
        case EvaluationStatus::Pending: return "pending";
        case EvaluationStatus::Complete: return "complete";
        case EvaluationStatus::Rejected: return "rejected";
        case EvaluationStatus::Failed: return "failed";
        case EvaluationStatus::Canceled: return "canceled";
        case EvaluationStatus::Stale: return "stale";
    }
    return "failed";
}

const char* ConstraintTierName(ConstraintTier tier) {
    return tier == ConstraintTier::Tier0IdentityState ? "tier-0-identity-state" : "tier-1-raw-artifact";
}

const char* ConstraintStatusName(ConstraintStatus status) {
    switch (status) {
        case ConstraintStatus::Passed: return "passed";
        case ConstraintStatus::Failed: return "failed";
        case ConstraintStatus::Unavailable: return "unavailable";
    }
    return "unavailable";
}

const char* ObjectiveTierName(ObjectiveTier tier) {
    switch (tier) {
        case ObjectiveTier::Tier2Technical: return "tier-2-technical";
        case ObjectiveTier::Tier3Display: return "tier-3-display";
        case ObjectiveTier::Tier5TieBreaker: return "tier-5-tie-breaker";
    }
    return "tier-2-technical";
}

double GetParameter(const CandidateParameterVector& p, ParameterId id) {
    switch (id) {
        case ParameterId::RawExposureEv: return p.rawExposureEv;
        case ParameterId::LocalTargetEv: return p.localTargetEv;
        case ParameterId::LocalDeltaEv: return p.localDeltaEv;
        case ParameterId::LocalWidthEv: return p.localWidthEv;
        case ParameterId::LocalFeather: return p.localFeather;
        case ParameterId::FinishY1: return p.finishY1;
        case ParameterId::FinishY2: return p.finishY2;
        case ParameterId::FinishY3: return p.finishY3;
        case ParameterId::DisplayBlackEv: return p.displayBlackEv;
        case ParameterId::DisplayWhiteEv: return p.displayWhiteEv;
        case ParameterId::DisplayMiddleGrey: return p.displayMiddleGrey;
        case ParameterId::DisplayShoulder: return p.displayShoulder;
        case ParameterId::DisplayToe: return p.displayToe;
        case ParameterId::Count: break;
    }
    return 0.0;
}

void SetParameter(CandidateParameterVector& p, ParameterId id, double value) {
    switch (id) {
        case ParameterId::RawExposureEv: p.rawExposureEv = value; break;
        case ParameterId::LocalTargetEv: p.localTargetEv = value; break;
        case ParameterId::LocalDeltaEv: p.localDeltaEv = value; break;
        case ParameterId::LocalWidthEv: p.localWidthEv = value; break;
        case ParameterId::LocalFeather: p.localFeather = value; break;
        case ParameterId::FinishY1: p.finishY1 = value; break;
        case ParameterId::FinishY2: p.finishY2 = value; break;
        case ParameterId::FinishY3: p.finishY3 = value; break;
        case ParameterId::DisplayBlackEv: p.displayBlackEv = value; break;
        case ParameterId::DisplayWhiteEv: p.displayWhiteEv = value; break;
        case ParameterId::DisplayMiddleGrey: p.displayMiddleGrey = value; break;
        case ParameterId::DisplayShoulder: p.displayShoulder = value; break;
        case ParameterId::DisplayToe: p.displayToe = value; break;
        case ParameterId::Count: break;
    }
}

std::string CanonicalRecipeBytes(const RawRecipe::RawDevelopmentRecipe& recipe) {
    nlohmann::json value = RawRecipe::SerializeRecipe(recipe);
    value.erase("sourceRef");
    return value.dump();
}

std::string RecipeIdentity(const RawRecipe::RawDevelopmentRecipe& recipe) {
    return Sha256Text(CanonicalRecipeBytes(recipe));
}

CandidateParameterVector ExtractParameters(const RawRecipe::RawDevelopmentRecipe& recipe) {
    CandidateParameterVector result;
    result.rawExposureEv = recipe.preToneExposureEv;
    const RawRecipe::RawLocalRangeRecipe local = RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange);
    if (local.points.size() > 2) {
        const auto best = std::max_element(local.points.begin() + 1, local.points.end() - 1,
            [](const auto& a, const auto& b) { return std::abs(a.deltaEv) < std::abs(b.deltaEv); });
        result.localTargetEv = best->ev;
        result.localDeltaEv = best->deltaEv;
    } else {
        result.localTargetEv = 0.5 * (local.minEv + local.maxEv);
        result.localDeltaEv = 0.0;
    }
    result.localWidthEv = std::clamp(
        0.5 * static_cast<double>(local.regionMaskHighEv - local.regionMaskLowEv), 0.02, 1.5);
    result.localFeather = local.regionMaskFeather;
    const auto finish = FinishInterior(recipe);
    result.finishY1 = finish[0];
    result.finishY2 = finish[1];
    result.finishY3 = finish[2];
    const nlohmann::json& display = recipe.viewTransform.layerJson;
    result.displayBlackEv = JsonNumber(display, "blackEv", -8.0);
    result.displayWhiteEv = JsonNumber(display, "whiteEv", 4.0);
    result.displayMiddleGrey = JsonNumber(display, "middleGrey", 0.18);
    result.displayShoulder = JsonNumber(display, "shoulder", 0.45);
    result.displayToe = JsonNumber(display, "toe", 0.18);
    return result;
}

ParameterSpace BuildParameterSpace(
    const RawRecipe::RawDevelopmentRecipe& warmRecipe,
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence) {
    ParameterSpace space;
    const CandidateParameterVector warm = ExtractParameters(warmRecipe);
    const double rawHeadroom = MinimumRawHeadroom(rawEvidence);
    double exposureUpper = std::min(8.0, warm.rawExposureEv + 2.0);
    std::string exposureReason = "UI range intersected with warm start +/-2EV.";
    if (std::isfinite(rawHeadroom)) {
        exposureUpper = std::min(exposureUpper, rawHeadroom);
        exposureUpper = std::max(exposureUpper, warm.rawExposureEv);
        exposureReason += " Positive expansion capped by limiting-plane WB-scaled raw headroom.";
    } else {
        exposureUpper = std::max(warm.rawExposureEv, std::min(0.0, exposureUpper));
        exposureReason += " Missing raw headroom blocks new positive expansion.";
    }
    SetRange(space, ParameterId::RawExposureEv, std::max(-8.0, warm.rawExposureEv - 2.0),
        exposureUpper, warm.rawExposureEv, "EV", true, exposureReason);
    SetRange(space, ParameterId::LocalTargetEv, warmRecipe.localRange.minEv, warmRecipe.localRange.maxEv,
        warm.localTargetEv, "EV", true, "Visible Local Range domain; conditional on an active local delta.");
    SetRange(space, ParameterId::LocalDeltaEv, -1.0, 1.0, warm.localDeltaEv, "EV", true,
        "Phase 00 one-point Local Range delta bound.");
    SetRange(space, ParameterId::LocalWidthEv, 0.02, 1.5, warm.localWidthEv, "EV half-width", true,
        "Visible luminance-range mask width bound.");
    SetRange(space, ParameterId::LocalFeather, 0.0, 1.0, warm.localFeather, "fraction", true,
        "Visible luminance-range mask feather bound.");
    SetRange(space, ParameterId::FinishY1, 0.0, 1.0, warm.finishY1, "normalized output", true,
        "Fixed x=0.25 Finish Tone output; graph constraints remain lexicographic.");
    SetRange(space, ParameterId::FinishY2, 0.0, 1.0, warm.finishY2, "normalized output", true,
        "Fixed x=0.50 Finish Tone output; graph constraints remain lexicographic.");
    SetRange(space, ParameterId::FinishY3, 0.0, 1.0, warm.finishY3, "normalized output", true,
        "Fixed x=0.75 Finish Tone output; graph constraints remain lexicographic.");
    SetRange(space, ParameterId::DisplayBlackEv, -16.0, -kEpsilon, warm.displayBlackEv, "EV", true,
        "Visible View Transform black EV range.");
    SetRange(space, ParameterId::DisplayWhiteEv, 0.0, 16.0, warm.displayWhiteEv, "EV", true,
        "Visible View Transform white EV range.");
    SetRange(space, ParameterId::DisplayMiddleGrey, 0.01, 1.0, warm.displayMiddleGrey, "linear", true,
        "Visible View Transform middle-grey range; no universal target imposed.");
    SetRange(space, ParameterId::DisplayShoulder, 0.05, 4.0, warm.displayShoulder, "control value", true,
        "Visible View Transform shoulder range.");
    SetRange(space, ParameterId::DisplayToe, 0.0, 1.0, warm.displayToe, "control value", true,
        "Visible View Transform toe range.");
    return space;
}

CandidateProposal BuildCandidateProposal(
    const RawRecipe::RawDevelopmentRecipe& baseRecipe,
    const CandidateParameterVector& parameters,
    CandidateIdentityContext identities,
    CandidateOwnership ownership,
    std::string proposalReason) {
    CandidateProposal proposal;
    proposal.parameters = parameters;
    proposal.ownership = std::move(ownership);
    proposal.identities = std::move(identities);
    proposal.recipe = baseRecipe;
    proposal.proposalReason = std::move(proposalReason);
    const CandidateParameterVector baseParameters = ExtractParameters(baseRecipe);
    const std::string computedBaseIdentity = RecipeIdentity(baseRecipe);
    if (proposal.identities.baseRecipeIdentity.empty()) {
        proposal.identities.baseRecipeIdentity = computedBaseIdentity;
    }
    if (proposal.identities.ownershipIdentity.empty()) {
        proposal.identities.ownershipIdentity = proposal.ownership.identity;
    }

    if (ParametersFinite(parameters)) {
        proposal.recipe.preToneExposureEv = static_cast<float>(parameters.rawExposureEv);
        const bool localChanged =
            !Near(parameters.localTargetEv, baseParameters.localTargetEv) ||
            !Near(parameters.localDeltaEv, baseParameters.localDeltaEv) ||
            !Near(parameters.localWidthEv, baseParameters.localWidthEv) ||
            !Near(parameters.localFeather, baseParameters.localFeather);
        if (localChanged) {
            RawRecipe::RawLocalRangeRecipe& local = proposal.recipe.localRange;
            local.points = {
                { local.minEv, 0.0f },
                { static_cast<float>(parameters.localTargetEv), static_cast<float>(parameters.localDeltaEv) },
                { local.maxEv, 0.0f }
            };
            local.enabled = std::abs(parameters.localDeltaEv) > kEpsilon;
            local.regionMaskEnabled = local.enabled;
            local.regionMaskMode = "luminance-range";
            local.regionMaskLowEv = static_cast<float>(parameters.localTargetEv - parameters.localWidthEv);
            local.regionMaskHighEv = static_cast<float>(parameters.localTargetEv + parameters.localWidthEv);
            local.regionMaskFeather = static_cast<float>(parameters.localFeather);
        }
        const bool finishChanged =
            !Near(parameters.finishY1, baseParameters.finishY1) ||
            !Near(parameters.finishY2, baseParameters.finishY2) ||
            !Near(parameters.finishY3, baseParameters.finishY3);
        if (finishChanged) {
            nlohmann::json finish = proposal.recipe.finishTone.layerJson.is_object()
                ? proposal.recipe.finishTone.layerJson
                : RawRecipe::DefaultFinishToneJson();
            finish["type"] = "ToneCurve";
            finish["domain"] = 0;
            finish["points"] = FinishPoints(parameters);
            finish["preparedPoints"] = finish["points"];
            proposal.recipe.finishTone.layerJson = std::move(finish);
        }
        nlohmann::json display = proposal.recipe.viewTransform.layerJson.is_object()
            ? proposal.recipe.viewTransform.layerJson
            : RawRecipe::DefaultViewTransformJson();
        display["type"] = "ViewTransform";
        // These fields are production float controls. Store their exact float
        // representation so canonical serialize/deserialize identity does not
        // reject a candidate solely for double-to-float JSON normalization.
        display["blackEv"] = static_cast<float>(parameters.displayBlackEv);
        display["whiteEv"] = static_cast<float>(parameters.displayWhiteEv);
        display["middleGrey"] = static_cast<float>(parameters.displayMiddleGrey);
        display["shoulder"] = static_cast<float>(parameters.displayShoulder);
        display["toe"] = static_cast<float>(parameters.displayToe);
        proposal.recipe.viewTransform.layerJson = std::move(display);
    }

    proposal.candidateRecipeIdentity = RecipeIdentity(proposal.recipe);
    const std::string identityText =
        std::string(kCandidateEngineVersion) + "|" + kParameterSpaceVersion + "|" +
        proposal.identities.sourceIdentity + "|" + proposal.identities.decodeIdentity + "|" +
        proposal.identities.rawEvidenceIdentity + "|" + proposal.identities.baseRecipeIdentity + "|" +
        proposal.candidateRecipeIdentity + "|" + proposal.identities.rendererIdentity + "|" +
        proposal.identities.proxyIdentity + "|" + proposal.identities.featureVersion + "|" +
        proposal.identities.budgetIdentity + "|" + proposal.identities.ownershipIdentity + "|" +
        std::to_string(proposal.identities.generation) + "|" + SerializeParameters(parameters).dump();
    proposal.candidateId = Sha256Text(identityText);
    proposal.valid = ParametersFinite(parameters) &&
        proposal.identities.baseRecipeIdentity == computedBaseIdentity &&
        !proposal.identities.sourceIdentity.empty() &&
        !proposal.identities.decodeIdentity.empty() &&
        !proposal.identities.rawEvidenceIdentity.empty() &&
        !proposal.identities.rendererIdentity.empty() &&
        !proposal.identities.proxyIdentity.empty() &&
        proposal.identities.featureVersion == RenderedFeatures::kRenderedFeatureVersion &&
        !proposal.identities.budgetIdentity.empty() &&
        !proposal.identities.ownershipIdentity.empty() &&
        proposal.candidateId.size() == 64;
    if (!proposal.valid) {
        proposal.warnings.push_back("Candidate identity, parameter, or base-recipe contract is incomplete.");
    }
    return proposal;
}

ObjectiveSurfacePlan BuildObjectiveSurfacePlan(
    const CandidateParameterVector& warm,
    const ParameterSpace& space) {
    ObjectiveSurfacePlan plan;
    plan.warmStart = warm;
    const std::array<double, static_cast<std::size_t>(ParameterId::Count)> steps {
        0.75, 2.0, 0.50, 0.35, 0.25, 0.12, 0.12, 0.12, 2.0, 2.0, 0.06, 0.25, 0.18
    };

    auto addOne = [&](ParameterId axis, bool conditionalLocal) {
        SurfaceSlice slice;
        slice.id = std::string("one-d/") + ParameterStableString(axis);
        slice.axisX = axis;
        CandidateParameterVector anchor = warm;
        if (conditionalLocal && std::abs(anchor.localDeltaEv) < 0.05) {
            anchor.localDeltaEv = 0.35;
            slice.conditionalAnchor = "Local dimensions sampled with +0.35EV visible local delta; exact warm start retained separately.";
        }
        const std::vector<double> values = Triplet(space.ranges[Index(axis)], steps[Index(axis)]);
        for (std::size_t i = 0; i < values.size(); ++i) {
            SurfaceSample sample;
            sample.sampleId = slice.id + "/" + std::to_string(i);
            sample.parameters = anchor;
            SetParameter(sample.parameters, axis, values[i]);
            sample.axisX = values[i];
            slice.samples.push_back(std::move(sample));
        }
        AddWarmIfMissing(slice, warm);
        plan.slices.push_back(std::move(slice));
    };
    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        const ParameterId id = static_cast<ParameterId>(i);
        const bool conditional = id == ParameterId::LocalTargetEv || id == ParameterId::LocalWidthEv || id == ParameterId::LocalFeather;
        addOne(id, conditional);
    }

    auto addTwo = [&](const char* id, ParameterId x, ParameterId y, bool conditionalLocal) {
        SurfaceSlice slice;
        slice.id = std::string("two-d/") + id;
        slice.axisX = x;
        slice.hasAxisY = true;
        slice.axisY = y;
        CandidateParameterVector anchor = warm;
        if (conditionalLocal && std::abs(anchor.localDeltaEv) < 0.05 &&
            x != ParameterId::LocalDeltaEv && y != ParameterId::LocalDeltaEv) {
            anchor.localDeltaEv = 0.35;
            slice.conditionalAnchor = "Conditional Local Range surface uses +0.35EV visible delta; exact warm start retained separately.";
        }
        const auto xs = Triplet(space.ranges[Index(x)], steps[Index(x)]);
        const auto ys = Triplet(space.ranges[Index(y)], steps[Index(y)]);
        for (std::size_t iy = 0; iy < ys.size(); ++iy) {
            for (std::size_t ix = 0; ix < xs.size(); ++ix) {
                SurfaceSample sample;
                sample.sampleId = slice.id + "/" + std::to_string(iy * xs.size() + ix);
                sample.parameters = anchor;
                SetParameter(sample.parameters, x, xs[ix]);
                SetParameter(sample.parameters, y, ys[iy]);
                sample.axisX = xs[ix];
                sample.axisY = ys[iy];
                slice.samples.push_back(std::move(sample));
            }
        }
        AddWarmIfMissing(slice, warm);
        plan.slices.push_back(std::move(slice));
    };
    addTwo("raw-exposure_x_local-delta", ParameterId::RawExposureEv, ParameterId::LocalDeltaEv, false);
    addTwo("local-target_x_width", ParameterId::LocalTargetEv, ParameterId::LocalWidthEv, true);
    addTwo("local-delta_x_feather", ParameterId::LocalDeltaEv, ParameterId::LocalFeather, false);
    addTwo("finish-shadow_x_highlight", ParameterId::FinishY1, ParameterId::FinishY3, false);
    addTwo("display-white_x_shoulder", ParameterId::DisplayWhiteEv, ParameterId::DisplayShoulder, false);
    addTwo("display-black_x_toe", ParameterId::DisplayBlackEv, ParameterId::DisplayToe, false);
    addTwo("raw-exposure_x_display-white", ParameterId::RawExposureEv, ParameterId::DisplayWhiteEv, false);
    return plan;
}

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
    bool projectDirtyAfter) {
    CandidateEvaluationRecord record;
    record.proposal = proposal;
    record.render = std::move(render);
    record.currentRecipeUnchanged = CanonicalRecipeBytes(currentRecipeBefore) == CanonicalRecipeBytes(currentRecipeAfter);
    record.undoHistoryUnchanged = undoDepthBefore == undoDepthAfter;
    record.projectDirtyStateUnchanged = projectDirtyBefore == projectDirtyAfter;
    record.evaluationId = Sha256Text(
        proposal.candidateId + "|" + record.render.renderIdentity + "|" + record.render.proxyIdentity + "|" +
        kObjectiveConstraintVersion + "|" + (record.render.fullResolution ? "full" : "proxy"));

    record.constraints.push_back(Constraint("identity.proposal", ConstraintTier::Tier0IdentityState,
        proposal.valid ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        proposal.valid ? "Candidate proposal identity is complete." : "Candidate proposal identity is incomplete."));
    const bool recipeIdentityMatches = proposal.candidateRecipeIdentity == RecipeIdentity(proposal.recipe);
    record.constraints.push_back(Constraint("identity.candidate_recipe", ConstraintTier::Tier0IdentityState,
        recipeIdentityMatches ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        recipeIdentityMatches ? "Canonical candidate recipe identity matches." : "Candidate recipe changed after identity construction."));
    const bool baseIdentityMatches = proposal.identities.baseRecipeIdentity == RecipeIdentity(currentRecipeBefore);
    record.constraints.push_back(Constraint("identity.base_recipe", ConstraintTier::Tier0IdentityState,
        baseIdentityMatches ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        baseIdentityMatches ? "Base recipe identity matches the immutable current-recipe capture." : "Base recipe identity is stale."));
    record.constraints.push_back(Constraint("state.current_recipe_unchanged", ConstraintTier::Tier0IdentityState,
        record.currentRecipeUnchanged ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        record.currentRecipeUnchanged ? "Candidate evaluation did not mutate the current recipe." : "Current recipe changed during candidate evaluation."));
    record.constraints.push_back(Constraint("state.undo_history_unchanged", ConstraintTier::Tier0IdentityState,
        record.undoHistoryUnchanged ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        record.undoHistoryUnchanged ? "Undo depth is unchanged." : "Undo history changed during diagnostic evaluation."));
    record.constraints.push_back(Constraint("state.project_dirty_unchanged", ConstraintTier::Tier0IdentityState,
        record.projectDirtyStateUnchanged ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        record.projectDirtyStateUnchanged ? "Project dirty state is unchanged." : "Project dirty state changed during diagnostic evaluation."));

    const nlohmann::json serialized = RawRecipe::SerializeRecipe(proposal.recipe);
    const bool finiteSerializable = JsonFinite(serialized);
    bool roundTrip = false;
    if (finiteSerializable) {
        const RawRecipe::RawDevelopmentRecipe loaded = RawRecipe::DeserializeRecipe(serialized);
        roundTrip = CanonicalRecipeBytes(loaded) == CanonicalRecipeBytes(proposal.recipe);
    }
    record.constraints.push_back(Constraint("recipe.finite_serializable", ConstraintTier::Tier0IdentityState,
        finiteSerializable && roundTrip ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        finiteSerializable && roundTrip ? "Finite canonical recipe round-trip is exact." : "Recipe is non-finite or canonical round-trip changed it."));

    std::string localReason;
    const bool localValid = LocalGraphValid(proposal.recipe, localReason);
    record.constraints.push_back(Constraint("graph.local_order_capacity", ConstraintTier::Tier0IdentityState,
        localValid ? ConstraintStatus::Passed : ConstraintStatus::Failed, localReason));
    std::string finishReason;
    const bool finishValid = FinishGraphValid(proposal.recipe, finishReason);
    record.constraints.push_back(Constraint("graph.finish_monotone", ConstraintTier::Tier0IdentityState,
        finishValid ? ConstraintStatus::Passed : ConstraintStatus::Failed, finishReason));

    const bool ownershipOk =
        (!proposal.ownership.rawExposureUserOwned || !ControlChanged(currentRecipeBefore, proposal.recipe, ParameterId::RawExposureEv)) &&
        (!proposal.ownership.localRangeUserOwned || !ControlChanged(currentRecipeBefore, proposal.recipe, ParameterId::LocalTargetEv)) &&
        (!proposal.ownership.finishToneUserOwned || !ControlChanged(currentRecipeBefore, proposal.recipe, ParameterId::FinishY1)) &&
        (!proposal.ownership.displayFitUserOwned || !ControlChanged(currentRecipeBefore, proposal.recipe, ParameterId::DisplayBlackEv));
    record.constraints.push_back(Constraint("ownership.visible_controls", ConstraintTier::Tier0IdentityState,
        ownershipOk ? ConstraintStatus::Passed : ConstraintStatus::Failed,
        ownershipOk ? "User-owned controls remain unchanged." : "Candidate changes a user-owned visible control."));

    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        const ParameterId id = static_cast<ParameterId>(i);
        const ParameterRange& range = parameterSpace.ranges[Index(id)];
        const double value = GetParameter(proposal.parameters, id);
        const bool inRange = Finite(value) && value >= range.lower - kEpsilon && value <= range.upper + kEpsilon;
        record.constraints.push_back(Constraint(
            std::string("parameter.") + ParameterStableString(id),
            ConstraintTier::Tier0IdentityState,
            inRange ? ConstraintStatus::Passed : ConstraintStatus::Failed,
            inRange ? range.reason : "Parameter is non-finite or outside the frozen Phase 03 bound.",
            value,
            value < range.lower ? range.lower : range.upper,
            range.units));
    }

    const bool rawIdentityMatches = rawEvidence && rawEvidence->valid &&
        rawEvidence->sourceIdentity.sha256 == proposal.identities.sourceIdentity &&
        rawEvidence->decodeIdentity.sha256 == proposal.identities.decodeIdentity &&
        rawEvidence->evidenceIdentitySha256 == proposal.identities.rawEvidenceIdentity;
    record.constraints.push_back(Constraint("raw.identity", ConstraintTier::Tier1RawArtifact,
        rawIdentityMatches ? ConstraintStatus::Passed : ConstraintStatus::Unavailable,
        rawIdentityMatches ? "Exact Phase 01 source/decode/evidence identity matches." : "Matching Phase 01 evidence is unavailable."));
    const double headroom = MinimumRawHeadroom(rawEvidence);
    if (rawIdentityMatches && std::isfinite(headroom)) {
        const bool safe = proposal.parameters.rawExposureEv <= headroom + kEpsilon;
        double uncertainty = 0.0;
        for (const RawEvidence::PlaneEvidence& plane : rawEvidence->planes) {
            if (plane.wbScaledHeadroomEv.valid) uncertainty = std::max(uncertainty, plane.wbScaledHeadroomEv.uncertainty01);
        }
        record.constraints.push_back(Constraint("raw.wb_scaled_headroom", ConstraintTier::Tier1RawArtifact,
            safe ? ConstraintStatus::Passed : ConstraintStatus::Failed,
            safe ? "Candidate exposure remains within limiting-plane WB-scaled headroom." : "Candidate exposure exceeds raw headroom.",
            proposal.parameters.rawExposureEv, headroom, "EV", uncertainty));
    } else {
        const double baseExposure = ExtractParameters(currentRecipeBefore).rawExposureEv;
        const bool noPositiveExpansion = proposal.parameters.rawExposureEv <= baseExposure + kEpsilon;
        record.constraints.push_back(Constraint("raw.wb_scaled_headroom", ConstraintTier::Tier1RawArtifact,
            noPositiveExpansion ? ConstraintStatus::Unavailable : ConstraintStatus::Failed,
            noPositiveExpansion ? "Raw headroom unavailable; candidate does not expand positive exposure." : "Raw headroom unavailable; positive expansion is forbidden.",
            proposal.parameters.rawExposureEv, baseExposure, "EV", 1.0));
    }
    if (rawIdentityMatches && rawEvidence->clipping.allChannelClippedFraction.valid) {
        record.constraints.push_back(Constraint("raw.all_channel_clip_increase", ConstraintTier::Tier1RawArtifact,
            ConstraintStatus::Passed,
            "Visible recipe evaluation cannot alter capture-domain all-channel clipping; baseline fraction retained.",
            rawEvidence->clipping.allChannelClippedFraction.value,
            rawEvidence->clipping.allChannelClippedFraction.value,
            "fraction",
            rawEvidence->clipping.allChannelClippedFraction.uncertainty01));
    } else {
        record.constraints.push_back(Constraint("raw.all_channel_clip_increase", ConstraintTier::Tier1RawArtifact,
            ConstraintStatus::Unavailable, "CFA-aligned all-channel clipping evidence unavailable."));
    }

    if (record.render.canceled) {
        record.status = EvaluationStatus::Canceled;
        record.rejectionReason = record.render.error.empty() ? "Candidate render canceled." : record.render.error;
        return record;
    }
    if (record.render.stale) {
        record.status = EvaluationStatus::Stale;
        record.rejectionReason = record.render.error.empty() ? "Candidate render identity became stale." : record.render.error;
        return record;
    }
    if (!record.render.attempted || !record.render.success) {
        record.status = EvaluationStatus::Failed;
        record.rejectionReason = record.render.error.empty() ? "Candidate render or feature extraction failed." : record.render.error;
        return record;
    }

    bool featureIdentityOk = !record.render.stages.empty();
    for (const StageFeatureEvidence& stage : record.render.stages) {
        const bool match = stage.features.valid &&
            stage.features.sourceIdentity == proposal.identities.sourceIdentity &&
            stage.features.recipeIdentity == proposal.candidateRecipeIdentity &&
            stage.features.featureVersion == proposal.identities.featureVersion &&
            stage.features.rawEvidenceIdentity == proposal.identities.rawEvidenceIdentity;
        record.constraints.push_back(Constraint(
            std::string("feature.identity.") + StageName(stage.stage),
            ConstraintTier::Tier0IdentityState,
            match ? ConstraintStatus::Passed : ConstraintStatus::Failed,
            match ? "Stage feature identity matches candidate/source/raw evidence." : "Stage feature record is missing or stale."));
        featureIdentityOk = featureIdentityOk && match;
    }
    for (const RawAutoStartPoint::RawAutoStartPointStage required : {
             RawAutoStartPoint::RawAutoStartPointStage::NeutralScene,
             RawAutoStartPoint::RawAutoStartPointStage::RawPlacement,
             RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate,
             RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate,
             RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate }) {
        if (!FindStage(record.render, required)) {
            record.constraints.push_back(Constraint(
                std::string("feature.required.") + StageName(required),
                ConstraintTier::Tier0IdentityState,
                ConstraintStatus::Failed,
                "Required stage feature record is missing."));
            featureIdentityOk = false;
        }
    }

    const StageFeatureEvidence* rawPlacement = FindStage(record.render, RawAutoStartPoint::RawAutoStartPointStage::RawPlacement);
    const StageFeatureEvidence* local = FindStage(record.render, RawAutoStartPoint::RawAutoStartPointStage::LocalCandidate);
    const StageFeatureEvidence* finish = FindStage(record.render, RawAutoStartPoint::RawAutoStartPointStage::FinishToneCandidate);
    const StageFeatureEvidence* display = FindStage(record.render, RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate);
    if (rawPlacement) {
        AddFeatureTerm(record.terms, "raw-placement", rawPlacement->features, "scene.ev_p50", ObjectiveTier::Tier2Technical, "scene placement observation; no universal target");
        AddFeatureTerm(record.terms, "raw-placement", rawPlacement->features, "scene.occupied_range_ev", ObjectiveTier::Tier2Technical, "scene occupied range");
        AddFeatureTerm(record.terms, "raw-placement", rawPlacement->features, "noise.rendered_luma_residual_rms", ObjectiveTier::Tier2Technical, "rendered noise evidence");
        AddFeatureTerm(record.terms, "raw-placement", rawPlacement->features, "noise.raw_predicted_snr_min", ObjectiveTier::Tier2Technical, "Phase 01 signal-dependent noise prior");
        AddFeatureTerm(record.terms, "raw-placement", rawPlacement->features, "color.gamut_pressure_fraction", ObjectiveTier::Tier2Technical, "working-space gamut pressure");
    }
    if (local) {
        AddFeatureTerm(record.terms, "local", local->features, "region.bright_border_center_conflict_ev", ObjectiveTier::Tier2Technical, "regional conflict observation");
        AddFeatureTerm(record.terms, "local", local->comparisonToWarm, "halo.gradient_reversal_energy", ObjectiveTier::Tier2Technical, "artifact penalty candidate; no threshold selected");
        AddFeatureTerm(record.terms, "local", local->comparisonToWarm, "halo.adjacent_band_energy", ObjectiveTier::Tier2Technical, "artifact penalty candidate; no threshold selected");
        AddFeatureTerm(record.terms, "local", local->comparisonToWarm, "structure.detail_energy_ratio_r2", ObjectiveTier::Tier2Technical, "local contrast preservation ratio");
    }
    if (finish) {
        AddFeatureTerm(record.terms, "finish", finish->features, "scene.ev_p50", ObjectiveTier::Tier2Technical, "pre-display tone placement");
        AddFeatureTerm(record.terms, "finish", finish->comparisonToWarm, "structure.detail_energy_ratio_r4", ObjectiveTier::Tier2Technical, "tone-stage structure ratio");
        AddFeatureTerm(record.terms, "finish", finish->comparisonToWarm, "halo.adjacent_band_energy", ObjectiveTier::Tier2Technical, "tone-stage band artifact evidence");
    }
    if (display) {
        AddFeatureTerm(record.terms, "display", display->features, "display.linear_clip_high_fraction", ObjectiveTier::Tier3Display, "display high clipping");
        AddFeatureTerm(record.terms, "display", display->features, "display.linear_clip_low_fraction", ObjectiveTier::Tier3Display, "display low clipping");
        AddFeatureTerm(record.terms, "display", display->features, "display.linear_p50", ObjectiveTier::Tier3Display, "relative display middle placement");
        AddFeatureTerm(record.terms, "display", display->comparisonToWarm, "color.hue_shift_degrees", ObjectiveTier::Tier3Display, "before/after hue stability");
    }

    const CandidateParameterVector warm = ExtractParameters(currentRecipeBefore);
    double editDistance = 0.0;
    int changedCount = 0;
    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        const ParameterId id = static_cast<ParameterId>(i);
        const double change = std::abs(GetParameter(proposal.parameters, id) - GetParameter(warm, id));
        const ParameterRange& range = parameterSpace.ranges[Index(id)];
        editDistance += change / std::max(kEpsilon, range.upper - range.lower);
        if (change > 1.0e-6) ++changedCount;
    }
    ObjectiveTerm edit;
    edit.id = "edit.normalized_l1";
    edit.tier = ObjectiveTier::Tier5TieBreaker;
    edit.valid = true;
    edit.value = editDistance;
    edit.units = "normalized-parameter-L1";
    edit.uncertainty01 = 0.0;
    edit.stage = "visible-recipe";
    edit.role = "tie-breaker only";
    record.terms.push_back(edit);
    edit.id = "edit.changed_parameter_count";
    edit.value = changedCount;
    edit.units = "count";
    record.terms.push_back(edit);
    edit.id = "curve.finish_second_difference_energy";
    edit.tier = ObjectiveTier::Tier2Technical;
    edit.value = FinishSmoothness(proposal.recipe);
    edit.units = "normalized-squared-curvature";
    edit.role = "curve smoothness observation; graph validity remains a hard constraint";
    record.terms.push_back(edit);

    const bool anyFailed = std::any_of(record.constraints.begin(), record.constraints.end(), [](const ConstraintResult& constraint) {
        return constraint.status == ConstraintStatus::Failed;
    });
    if (anyFailed || !featureIdentityOk) {
        record.status = EvaluationStatus::Rejected;
        const auto first = std::find_if(record.constraints.begin(), record.constraints.end(), [](const ConstraintResult& constraint) {
            return constraint.status == ConstraintStatus::Failed;
        });
        record.rejectionReason = first != record.constraints.end() ? first->id + ": " + first->reason : "Feature identity rejected.";
    } else {
        record.status = EvaluationStatus::Complete;
    }
    return record;
}

FullResolutionPromotionRecord CompareProxyAndFullResolution(
    const CandidateProposal& proposal,
    const CandidateRenderEvidence& proxy,
    const CandidateRenderEvidence& full) {
    FullResolutionPromotionRecord record;
    record.requested = true;
    record.candidateId = proposal.candidateId;
    record.candidateRecipeIdentity = proposal.candidateRecipeIdentity;
    record.proxyIdentity = proxy.proxyIdentity;
    record.fullResolutionIdentity = full.proxyIdentity;
    record.proxyWidth = proxy.renderWidth;
    record.proxyHeight = proxy.renderHeight;
    record.fullWidth = full.renderWidth;
    record.fullHeight = full.renderHeight;
    if (!proposal.valid || !proxy.success || !full.success || !full.fullResolution) {
        record.reason = "Candidate, proxy, or full-resolution render is incomplete.";
        return record;
    }
    for (const StageFeatureEvidence& proxyStage : proxy.stages) {
        const StageFeatureEvidence* fullStage = FindStage(full, proxyStage.stage);
        if (!fullStage) continue;
        StagePromotionAgreement agreement;
        agreement.stage = proxyStage.stage;
        agreement.agreement = RenderedFeatures::CompareFeatureRecords(fullStage->features, proxyStage.features);
        record.stages.push_back(std::move(agreement));
    }
    record.valid = !record.stages.empty() && std::all_of(record.stages.begin(), record.stages.end(), [](const StagePromotionAgreement& stage) {
        return stage.agreement.valid;
    });
    record.reason = record.valid
        ? "Proxy/full feature disagreement recorded; no acceptance threshold selected in Phase 03."
        : "No comparable proxy/full stage features were available.";
    return record;
}

bool CandidateEvaluationCache::Find(const std::string& evaluationId, CandidateEvaluationRecord& out) const {
    const auto found = m_Records.find(evaluationId);
    if (found == m_Records.end()) return false;
    out = found->second;
    out.cacheHit = true;
    return true;
}

void CandidateEvaluationCache::Store(const CandidateEvaluationRecord& record) {
    if (!record.evaluationId.empty() && record.status != EvaluationStatus::Canceled && record.status != EvaluationStatus::Stale) {
        m_Records[record.evaluationId] = record;
    }
}

void CandidateEvaluationCache::Clear() { m_Records.clear(); }
std::size_t CandidateEvaluationCache::Size() const { return m_Records.size(); }

nlohmann::json SerializeParameters(const CandidateParameterVector& p) {
    nlohmann::json result = nlohmann::json::object();
    for (int i = 0; i < static_cast<int>(ParameterId::Count); ++i) {
        const ParameterId id = static_cast<ParameterId>(i);
        result[ParameterStableString(id)] = GetParameter(p, id);
    }
    return result;
}

nlohmann::json SerializeParameterSpace(const ParameterSpace& space) {
    nlohmann::json ranges = nlohmann::json::array();
    for (const ParameterRange& range : space.ranges) {
        ranges.push_back({
            { "id", ParameterStableString(range.id) }, { "lower", range.lower },
            { "upper", range.upper }, { "warm", range.warm }, { "units", range.units },
            { "active", range.active }, { "reason", range.reason }
        });
    }
    return { { "version", space.version }, { "ranges", std::move(ranges) } };
}

nlohmann::json SerializeProposal(const CandidateProposal& proposal) {
    return {
        { "valid", proposal.valid }, { "engineVersion", proposal.engineVersion },
        { "parameterVersion", proposal.parameterVersion }, { "candidateId", proposal.candidateId },
        { "candidateRecipeIdentity", proposal.candidateRecipeIdentity },
        { "identities", {
            { "source", proposal.identities.sourceIdentity }, { "decode", proposal.identities.decodeIdentity },
            { "rawEvidence", proposal.identities.rawEvidenceIdentity }, { "baseRecipe", proposal.identities.baseRecipeIdentity },
            { "renderer", proposal.identities.rendererIdentity }, { "proxy", proposal.identities.proxyIdentity },
            { "feature", proposal.identities.featureVersion }, { "budget", proposal.identities.budgetIdentity },
            { "ownership", proposal.identities.ownershipIdentity }, { "generation", proposal.identities.generation }
        } },
        { "parameters", SerializeParameters(proposal.parameters) },
        { "completeVisibleRecipe", nlohmann::json::parse(CanonicalRecipeBytes(proposal.recipe)) },
        { "proposalReason", proposal.proposalReason }, { "warnings", proposal.warnings }
    };
}

nlohmann::json SerializeEvaluation(const CandidateEvaluationRecord& record) {
    nlohmann::json constraints = nlohmann::json::array();
    for (const ConstraintResult& constraint : record.constraints) constraints.push_back(SerializeConstraint(constraint));
    nlohmann::json terms = nlohmann::json::array();
    for (const ObjectiveTerm& term : record.terms) terms.push_back(SerializeTerm(term));
    nlohmann::json stages = nlohmann::json::array();
    for (const StageFeatureEvidence& stage : record.render.stages) {
        stages.push_back({
            { "stage", StageName(stage.stage) },
            { "features", RenderedFeatures::SerializeFeatureRecord(stage.features) },
            { "comparisonToWarm", RenderedFeatures::SerializeFeatureRecord(stage.comparisonToWarm) }
        });
    }
    return {
        { "schemaVersion", record.schemaVersion }, { "engineVersion", record.engineVersion },
        { "objectiveConstraintVersion", record.objectiveConstraintVersion },
        { "evaluationId", record.evaluationId }, { "status", EvaluationStatusName(record.status) },
        { "proposal", SerializeProposal(record.proposal) },
        { "render", {
            { "attempted", record.render.attempted }, { "success", record.render.success },
            { "canceled", record.render.canceled }, { "stale", record.render.stale },
            { "fullResolution", record.render.fullResolution }, { "renderIdentity", record.render.renderIdentity },
            { "proxyIdentity", record.render.proxyIdentity }, { "renderWidth", record.render.renderWidth },
            { "renderHeight", record.render.renderHeight }, { "featureWidth", record.render.featureWidth },
            { "featureHeight", record.render.featureHeight }, { "renderRuntimeMs", record.render.renderRuntimeMs },
            { "featureRuntimeMs", record.render.featureRuntimeMs }, { "imageCacheHits", record.render.imageCacheHits },
            { "imageCacheMisses", record.render.imageCacheMisses }, { "rawStageCacheHits", record.render.rawStageCacheHits },
            { "rawStageCacheMisses", record.render.rawStageCacheMisses }, { "error", record.render.error },
            { "stages", std::move(stages) }
        } },
        { "constraints", std::move(constraints) }, { "terms", std::move(terms) },
        { "cacheHit", record.cacheHit }, { "currentRecipeUnchanged", record.currentRecipeUnchanged },
        { "undoHistoryUnchanged", record.undoHistoryUnchanged },
        { "projectDirtyStateUnchanged", record.projectDirtyStateUnchanged },
        { "combinedTotalScore", nullptr }, { "rejectionReason", record.rejectionReason }
    };
}

nlohmann::json SerializeSurfacePlan(const ObjectiveSurfacePlan& plan) {
    nlohmann::json slices = nlohmann::json::array();
    for (const SurfaceSlice& slice : plan.slices) {
        nlohmann::json samples = nlohmann::json::array();
        for (const SurfaceSample& sample : slice.samples) {
            samples.push_back({
                { "sampleId", sample.sampleId }, { "warmStart", sample.warmStart },
                { "axisX", sample.axisX },
                { "axisY", slice.hasAxisY ? nlohmann::json(sample.axisY) : nlohmann::json(nullptr) },
                { "parameters", SerializeParameters(sample.parameters) }
            });
        }
        slices.push_back({
            { "id", slice.id }, { "axisX", ParameterStableString(slice.axisX) },
            { "hasAxisY", slice.hasAxisY },
            { "axisY", slice.hasAxisY ? nlohmann::json(ParameterStableString(slice.axisY)) : nlohmann::json(nullptr) },
            { "conditionalAnchor", slice.conditionalAnchor }, { "samples", std::move(samples) }
        });
    }
    return {
        { "version", plan.version }, { "parameterVersion", plan.parameterVersion },
        { "warmStart", SerializeParameters(plan.warmStart) }, { "slices", std::move(slices) }
    };
}

nlohmann::json SerializePromotion(const FullResolutionPromotionRecord& promotion) {
    nlohmann::json stages = nlohmann::json::array();
    for (const StagePromotionAgreement& stage : promotion.stages) {
        stages.push_back({
            { "stage", StageName(stage.stage) },
            { "agreement", RenderedFeatures::SerializeFeatureAgreement(stage.agreement) }
        });
    }
    return {
        { "requested", promotion.requested }, { "valid", promotion.valid },
        { "candidateId", promotion.candidateId }, { "candidateRecipeIdentity", promotion.candidateRecipeIdentity },
        { "proxyIdentity", promotion.proxyIdentity }, { "fullResolutionIdentity", promotion.fullResolutionIdentity },
        { "proxyWidth", promotion.proxyWidth }, { "proxyHeight", promotion.proxyHeight },
        { "fullWidth", promotion.fullWidth }, { "fullHeight", promotion.fullHeight },
        { "stages", std::move(stages) }, { "reason", promotion.reason }
    };
}

} // namespace Stack::PreciseRaw
