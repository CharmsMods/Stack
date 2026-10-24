// Focused parameter-edit regression using the production timeline catalog and
// edit/keyframe implementation. The live operation and display-name registries
// are unrelated boundaries; no UI, GPU, file data or image inputs are required.
#include "Editor/Timeline/TimelineParameterEdits.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace LayerRegistry {
const LayerDescriptor* GetDescriptor(LayerType) { return nullptr; }
}
namespace EditorNodeGraphDefinitions {
const LiveNodeDefinition* FindLiveNodeDefinition(const EditorNodeGraph::Node&) { return nullptr; }
}
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    using namespace Stack::Timeline;
    EditorNodeGraph::Node node;
    node.id=1; node.instanceUuid="first-node"; node.title="Chromatic Aberration";
    node.kind=EditorNodeGraph::NodeKind::Layer; node.layerType=LayerType::ChromaticAberration;
    const AnimatableParameterTarget x{1,"layer.chromaticCenterX","project","first-node"};
    const AnimatableParameterTarget y{1,"layer.chromaticCenterY","project","first-node"};
    TimelineAnimationState animation;
    SetOrReplaceKeyframe(animation,x,10,.5f);
    SetOrReplaceKeyframe(animation,y,10,.4f);
    const nlohmann::json before{{"center",{.5f,.4f}}};
    auto edited=PrepareParameterEditAnimation(animation,node,"project",10,before,
        nlohmann::json{{"center",{.8f,.7f}}});
    Require(FindTimelineTrack(edited.animation,x)->keyframes[0].value==.8f,"X keyframe not updated");
    Require(FindTimelineTrack(edited.animation,y)->keyframes[0].value==.7f,"Y keyframe not updated");
    Require(FindTimelineTrack(animation,x)->keyframes[0].value==.5f,"input animation mutated");
    Require(edited.livePreviewTargets.empty(),"existing keys incorrectly marked live-only");

    edited=PrepareParameterEditAnimation(animation,node,"project",11,before,
        nlohmann::json{{"center",{.8f,.4f}}});
    Require(edited.livePreviewTargets.size()==1 && SameTarget(edited.livePreviewTargets[0],x),
        "between-key edit lost component-specific preview");
    Require(FindTimelineTrack(edited.animation,x)->keyframes[0].value==.5f,"unrelated frame changed");
    edited=PrepareParameterEditAnimation(animation,node,"project",10,before,before);
    Require(edited.livePreviewTargets.empty(),"unchanged components generated previews");

    for (const auto& invalid : {nlohmann::json{{"center",.8f}},
            nlohmann::json{{"center",nlohmann::json::array()}},
            nlohmann::json{{"center",{"bad","bad"}}},
            nlohmann::json{{"center",{std::numeric_limits<float>::infinity(),
                                      std::numeric_limits<float>::quiet_NaN()}}}}) {
        edited=PrepareParameterEditAnimation(animation,node,"project",10,before,invalid);
        Require(FindTimelineTrack(edited.animation,x)->keyframes[0].value==.5f,"invalid X adopted");
        Require(FindTimelineTrack(edited.animation,y)->keyframes[0].value==.4f,"invalid Y adopted");
        Require(edited.livePreviewTargets.empty(),"invalid component generated preview");
    }
    edited=PrepareParameterEditAnimation(animation,node,"project",10,before,
        nlohmann::json{{"center",{.9f}}});
    Require(FindTimelineTrack(edited.animation,x)->keyframes[0].value==.9f,"valid first component dropped");
    Require(FindTimelineTrack(edited.animation,y)->keyframes[0].value==.4f,"missing second component changed");

    node.layerType=LayerType::Brightness;
    const AnimatableParameterTarget brightness{1,"layer.brightness","project","first-node"};
    TimelineAnimationState scalar;
    SetOrReplaceKeyframe(scalar,brightness,10,.1f);
    edited=PrepareParameterEditAnimation(scalar,node,"project",10,
        nlohmann::json{{"brightness",.1f}},nlohmann::json{{"brightness",.3f}});
    Require(FindTimelineTrack(edited.animation,brightness)->keyframes[0].value==.3f,"scalar edit regressed");
    node.layerType=LayerType::Flip;
    const AnimatableParameterTarget flip{1,"layer.flipHorizontal","project","first-node"};
    TimelineAnimationState boolean;
    SetOrReplaceKeyframe(boolean,flip,10,0.f);
    edited=PrepareParameterEditAnimation(boolean,node,"project",10,
        nlohmann::json{{"flipH",false}},nlohmann::json{{"flipH",true}});
    Require(FindTimelineTrack(edited.animation,flip)->keyframes[0].value==1.f,"boolean edit regressed");
    std::cout << "timeline array parameter edits: PASS\n";
}
