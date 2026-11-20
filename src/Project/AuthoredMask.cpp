#include "Project/AuthoredMask.h"
#include <stdexcept>

namespace Stack::Project {
std::string WriteAuthoredMask(EditorNodeGraph::Graph& graph,const AuthoredMaskEdit& edit) {
    auto payload=CapturedMaskPayload(edit.coverage);
    payload.authoring=std::make_shared<const nlohmann::json>(edit.recipe);
    std::string adjustment,error;
    if(!edit.existing.empty()) {
        for(auto& node:graph.EditNodes())if(node.instanceUuid==edit.existing &&
            node.kind==EditorNodeGraph::NodeKind::CustomMask && node.customMask.authoring) {
            node.customMask=std::move(payload);
            return {};
        }
        throw std::runtime_error("The edited mask is no longer available.");
    }
    EditorNodeGraph::Vec2 position{0,240};
    if(!edit.destination.empty()) {
        adjustment=InsertMaskAdjustment(graph,edit.destination,error,true);
        if(adjustment.empty())throw std::runtime_error(error);
    }
    int target=0;
    for(const auto& n:graph.GetNodes()) {
        if(adjustment.empty())position.y=std::max(position.y,n.position.y+180.f);
        else if(n.instanceUuid==adjustment){target=n.id;position={n.position.x,n.position.y+220.f};break;}
    }
    auto* mask=graph.AddCustomMaskNode(std::move(payload),position);
    if(!mask)throw std::runtime_error("Could not create mask texture.");
    mask->title=edit.name;mask->rawCoverage={{"canvas","project"},{"sampleAfter",edit.destination}};
    if(target && !graph.TryConnectSockets(mask->id,"maskOut",target,"maskIn",&error))throw std::runtime_error(error);
    return adjustment;
}
}

