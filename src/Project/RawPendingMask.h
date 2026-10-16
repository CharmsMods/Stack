#pragma once
#include "Project/RawLayerStack.h"

namespace Stack::Project {
// A capture may outlive edits to its receiving adjustment. Other document
// changes invalidate this conservative input snapshot. Pixel buffers are shared.
struct RawPendingMask {
    RawLayerStackState baseline;
    std::string layerId, operationUuid, maskUuid, token;
    bool Matches(const RawLayerStackState& current) const {
        auto expected=baseline;
        auto* owner=FindRawAdjustmentLayer(expected,layerId);
        const auto* live=FindRawAdjustmentLayer(current,layerId);
        if(!owner || !live) return false;
        auto find=[](const auto& layer,const std::string& uuid) -> const EditorNodeGraph::Node* {
            for(const auto& node:layer.graph.GetNodes()) if(node.instanceUuid==uuid) return &node;
            return nullptr;
        };
        const auto* original=find(*owner,operationUuid);const auto* edited=find(*live,operationUuid);
        const auto* mask=find(*live,maskUuid);
        if(!original || !edited || !mask || original->kind!=edited->kind || original->definitionId!=edited->definitionId ||
            mask->rawCoverage.value("captureToken",std::string{})!=token) return false;
        *owner->graph.FindNode(original->id)=*edited;
        return SameRawLayerStackState(expected,current);
    }
};
}
