#pragma once
#include "ProcessingInternal.h"
#include "ExposureGraph.h"

namespace Raw::Bracketing {
// Only bridge components without an anchor. Existing measured connections and
// manual EV anchors never acquire competing low-confidence constraints.
inline void ConnectExposureFallbacks(std::vector<ExposureEdge>& edges,
    std::vector<ExposureEdge> candidates,const std::vector<bool>& fixed,
    const PreparedDataset& data,const char* description,std::vector<std::string>& diagnostics) {
    auto reached=ConnectedExposureNodes(edges,fixed);
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
        reached=ConnectedExposureNodes(edges,fixed);
    }
}
}
