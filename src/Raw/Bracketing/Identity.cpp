#include "Processor.h"
#include "LocalDetail.h"
#include "HdrColorFusion.h"
#include "Panorama/Panorama.h"
#include "LearnedAlignment.h"

namespace Raw::Bracketing {
std::string AnalysisIdentity(const ProcessingRequest& request) {
    if(request.recipe.reconstruction==ReconstructionMode::Panorama)return Panorama::PreparationIdentity(request);
    auto recipe=request.recipe;recipe.knots=EqualCurves(recipe.groups.size());recipe.automatic=true;
    // Output resolution does not alter source preparation, motion or the guide.
    recipe.reconstruction=ReconstructionMode::Standard;
    recipe.depth.focus=.5;recipe.depth.focusRange=.08;recipe.depth.blurRadius=0;
    for(auto& group:recipe.groups) {group.name.clear();group.colorSlot=-1;}
    auto j=Serialize(recipe);
    // Sampling and clipping decisions feed reliability, the guide and cached
    // group tiles. Keep their revisions together when these decisions change.
    j["alignmentProfile"]="automatic-local-v30-bright-groups-border-gradient";
    if(recipe.aiAlignment)j["learnedAlignmentProfile"]=LearnedAlignment::Profile;
    if(!request.alignmentProposals.empty()) {
        j["alignmentProposalProfile"]="external-flow-seed-raw-verification-v1";
        for(const auto& p:request.alignmentProposals)
            j["alignmentProposals"].push_back(AlignmentProposalIdentity(p));
    }
    j["samplingProfile"]="local-bayer-phase-coverage-v1";
    j["exposureProfile"]="overlap-fallback-v2-uncertainty-aware-refinement";
    j["groupMemoryProfile"]="adaptive-prepared-tiles-v1";
    j["detailProfile"]=kLocalDetailRevision;
    j["temporalProfile"]="double-precision-gpu-temporal-v3-border-gradient";
    j["hdrProfile"]=kHdrColorFusionRevision;
    if(recipe.depth.enabled)j["depthProfile"]="handheld-parallax-v6-gpu-guided-depth";
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
