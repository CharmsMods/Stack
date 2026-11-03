#include "Processor.h"
#include "LocalDetail.h"
#include "HdrColorFusion.h"
#include "Panorama/Panorama.h"
#include "LearnedAlignment.h"

namespace Raw::Bracketing {
std::string AnalysisIdentity(const ProcessingRequest& request) {
    if(request.recipe.reconstruction==ReconstructionMode::Panorama)return Panorama::PreparationIdentity(request);
    auto recipe=request.recipe;
    const bool adaptive=recipe.reconstruction==ReconstructionMode::Adaptive;
    // Auto's choice of native group measurements can depend on authored exposure shares.
    if(!adaptive||recipe.automatic){recipe.knots=EqualCurves(recipe.groups.size());recipe.automatic=true;}
    const bool guide=NeedsBurstSpatialGuide(recipe);
    if(!adaptive)recipe.smoothing=guide?BurstSmoothing::All:BurstSmoothing::Off;
    recipe.reconstruction=ReconstructionMode::Standard;
    for(auto& group:recipe.groups) {group.name.clear();group.colorSlot=-1;}
    auto j=Serialize(recipe);
    if(adaptive)j["resolutionDependentGroups"]=true;
    if(guide)j["spatialGuide"]="independent-cfa-contrast-v1";
    // Sampling and clipping decisions feed reliability, the guide and cached
    // group tiles. Keep their revisions together when these decisions change.
    j["alignmentProfile"]="automatic-local-v40-valid-proxy-active-footprint";
    if(recipe.aiAlignment)j["learnedAlignmentProfile"]=LearnedAlignment::Profile;
    if(!request.alignmentProposals.empty()) {
        j["alignmentProposalProfile"]="external-flow-seed-raw-verification-v1";
        for(const auto& p:request.alignmentProposals)
            j["alignmentProposals"].push_back(AlignmentProposalIdentity(p));
    }
    j["samplingProfile"]="local-bayer-phase-coverage-v1";
    j["integerSaturationProfile"]="verified-code-spacing-v1";
    j["exposureProfile"]="image-only-overlap-v9-independent-channel-coverage";
    j["noiseProfile"]="temporal-burst-haar-v4-signed-signal";
    j["sourceVarianceProfile"]="resolved-single-quantization-v1";
    j["groupMemoryProfile"]="adaptive-prepared-tiles-v1";
    j["groupStorageProfile"]="finite-float-observations-v1";
    j["detailProfile"]=kLocalDetailRevision;
    j["temporalProfile"]="double-precision-gpu-temporal-v11-shared-radiance";
    j["srSupportProfile"]="categorical-reference-and-depth-v2";
    j["hdrProfile"]=kHdrColorFusionRevision;
    if(request.recipe.reconstruction==ReconstructionMode::Standard||adaptive)
        j["nativeHighlightProfile"]=kNativeHighlightRevision;
    if(recipe.alignmentMode!=AlignmentMode::FixedCoordinates)j["depthProfile"]="parallax-masks-v1";
    for(const auto& s:request.sources) j["sources"].push_back({s.frameId,s.sha256,s.bytes});
    // Native inspection uses the immutable prepared captures and does not
    // need to re-extract project assets merely to name the same sources.
    if(request.sources.empty()&&request.analysis) {
        const auto stored=nlohmann::json::parse(request.analysis->identity,nullptr,false);
        if(stored.is_object()&&stored.contains("sources"))j["sources"]=stored["sources"];
    }
    return j.dump();
}
}
