#pragma once
#include "Project/RawLayerStack.h"
#include "Raw/ReconstructionDepth.h"

namespace Stack::Project {
// Called on result adoption or single-master open. Coverage belongs to the
// result. The layer document owns only node identities, names and graph edits.
inline bool OfferBracketMasks(RawLayerStackState& state,const Raw::RawImageData& raw) {
    auto& layer=state.background;
    const auto depth=raw.reconstructionDepth;
    bool changed=false;
    for(auto& node:layer.graph.EditNodes()) {
        const auto key=node.rawCoverage.find("bracketDepthMask");
        if(key==node.rawCoverage.end())continue;
        const bool missing=!depth || std::none_of(depth->masks.begin(),depth->masks.end(),
            [&](const auto& m){return m.id==key->get_ref<const std::string&>();});
        if(missing!=node.rawCoverage.value("depthUnavailable",false)){
            node.rawCoverage["depthUnavailable"]=missing;changed=true;
        }
    }
    if(depth)for(const auto& m:depth->masks) {
        if(std::find(state.offeredDepthMasks.begin(),state.offeredDepthMasks.end(),m.id)!=state.offeredDepthMasks.end())continue;
        EditorNodeGraph::CustomMaskPayload payload;
        payload.width=m.width;payload.height=m.height;
        if(raw.metadata.orientation>=5 && raw.metadata.orientation<=8)std::swap(payload.width,payload.height);
        const auto n=state.offeredDepthMasks.size();
        auto* node=layer.graph.AddCustomMaskNode(std::move(payload),{-220.f+220.f*float(n/4),200.f+150.f*float(n%4)});
        if(!node)continue;
        node->title=m.name;node->rawCoverage={{"canvas","project"},{"bracketDepthMask",m.id}};
        state.offeredDepthMasks.push_back(m.id);changed=true;
    }
    if(changed)RegisterRawMaskOutputs(state,layer.id);
    return changed;
}
}
