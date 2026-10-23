#pragma once
#include "ProcessingInternal.h"
#include "ExposureGraph.h"

namespace Raw::Bracketing {
// Camera settings are an uncertain last-resort prior, not a measured RAW gain.
// In particular, reported ISO need not track the stored DNG signal exactly.
inline bool MetadataExposureRatio(const PreparedSource& a,const PreparedSource& b,double& ev) {
    const auto exposure=[](const RawMetadata& m,double& result) {
        if(!m.hasExposureTime||!m.hasIsoSpeed||!m.hasApertureFNumber||
            !std::isfinite(m.exposureTimeSeconds)||m.exposureTimeSeconds<=0||
            !std::isfinite(m.isoSpeed)||m.isoSpeed<=0||
            !std::isfinite(m.apertureFNumber)||m.apertureFNumber<=0) return false;
        result=std::log2(m.exposureTimeSeconds)+std::log2(m.isoSpeed)-2*std::log2(m.apertureFNumber);
        return std::isfinite(result);
    };
    double x=0,y=0;
    if(!exposure(a.metadata,x)||!exposure(b.metadata,y)) return false;
    ev=x-y;return std::isfinite(ev)&&std::abs(ev)<=32;
}

// Only bridge components without an anchor. Existing measured connections and
// manual EV anchors never acquire competing metadata constraints. Prefer the
// best available image bridge before considering any camera-settings bridge.
inline void ConnectExposureFallbacks(std::vector<ExposureEdge>& edges,
    std::vector<ExposureEdge> candidates,const std::vector<bool>& fixed,
    const PreparedDataset& data,const char* description,std::vector<std::string>& diagnostics) {
    auto reached=fixed;
    const auto propagate=[&] {
        for(std::size_t pass=0;pass<reached.size();++pass) {
            bool changed=false;
            for(const auto& edge:edges) if(reached[edge.a]!=reached[edge.b]) {
                reached[edge.a]=reached[edge.b]=true;changed=true;
            }
            if(!changed) break;
        }
    };
    propagate();
    std::stable_sort(candidates.begin(),candidates.end(),
        [](const auto& a,const auto& b){return a.variance<b.variance;});
    for(;;) {
        const auto bridge=std::find_if(candidates.begin(),candidates.end(),
            [&](const auto& edge){return reached[edge.a]!=reached[edge.b];});
        if(bridge==candidates.end()) break;
        edges.push_back(*bridge);
        diagnostics.push_back(data.sources[bridge->a].id+" / "+data.sources[bridge->b].id+
            ": "+description+"; continuing with "+std::to_string(std::sqrt(bridge->variance))+
            " EV uncertainty instead of blocking exposure calibration.");
        propagate();
    }
}
}
