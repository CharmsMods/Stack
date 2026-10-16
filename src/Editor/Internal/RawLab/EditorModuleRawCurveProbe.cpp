#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include <imgui_internal.h>
#include <cmath>

void EditorModule::UpdateRawCurveProbe(const ImVec2& minimum,const ImVec2& maximum,
    const Stack::RawRecipe::RawCropRotationRecipe& crop) {
    using namespace Stack;
    if(!m_Project->nodeEditor) return;
    auto& state=m_NodeEditorState.Get<Editor::RawLabInternal::CurveCanvasState>(*m_Project->nodeEditor);
    state.probe.reset();
    const auto* node=SelectedEditingNode();
    if(!node || node->kind!=EditorNodeGraph::NodeKind::RawOperation) return;
    const bool tone=node->rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve;
    if(!tone && node->rawOperation.kind!=RawRecipe::GraphOperationKind::LocalEv)return;
    const auto& scope=m_RawWorkspaceGraphScopeReadback;
    if(!scope.valid || !scope.sceneLinearBeforeViewTransform || scope.width<=0 || scope.height<=0 ||
       scope.stage!=(tone?RawDevelopmentGraphScopeStage::FinishToneInput:RawDevelopmentGraphScopeStage::LocalRangeInput) ||
       !ImGui::IsWindowHovered() || ImGui::IsAnyItemActive())return;
    const ImVec2 mouse=ImGui::GetIO().MousePos;
    if(!ImRect(minimum,maximum).Contains(mouse))return;
    const auto* layer=Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    const auto* input=layer?layer->graph.FindInputLink(node->id,"imageIn"):nullptr;
    if(!input)return;
    if(m_GraphOutputDescriptionRenderRevision!=m_RenderRevision)BuildGraphSnapshot();
    const auto found=m_GraphOutputDescriptions.find("raw/"+layer->id+"/"+EditorNodeGraph::GraphOutputIdentity(input->fromNodeId,input->fromSocketId));
    using namespace NodeMath;
    if(found==m_GraphOutputDescriptions.end() || found->second.descriptor.spatial.state!=KnowledgeState::Known ||
       m_LastGraphOutputSemanticDescriptor.spatial.state!=KnowledgeState::Known)return;
    const auto& descriptor=found->second.descriptor;
    // Current analysis pixels do not retain alpha. Do not claim a probe for
    // transparent inputs until their validity channel is available here.
    if(descriptor.alpha.state!=KnowledgeState::Known || descriptor.alpha.value!=AlphaMode::Opaque)return;
    const auto mapping=Editor::RawLabInternal::RawLabImageMapping::FromSpatial(descriptor.spatial.value,
        m_LastGraphOutputSemanticDescriptor.spatial.value,crop);
    if(!mapping || maximum.x<=minimum.x || maximum.y<=minimum.y)return;
    const auto uv=mapping->Local({(mouse.x-minimum.x)/(maximum.x-minimum.x),(mouse.y-minimum.y)/(maximum.y-minimum.y)});
    if(uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1)return;
    const int x=std::min(scope.width-1,int(uv.x*scope.width)),y=std::min(scope.height-1,int(uv.y*scope.height));
    const size_t index=size_t(y)*scope.width+x;
    if(scope.pixels.size()<size_t(scope.width)*scope.height*3)return;
    const auto recipe=Project::ReadRawLayerOperation(*layer,node->instanceUuid);
    const bool rec2020=descriptor.color.state==KnowledgeState::Known && descriptor.color.value.identity=="rec2020-d65";
    const float Y=rec2020?scope.pixels[index*3]*.2627002f+scope.pixels[index*3+1]*.6779981f+scope.pixels[index*3+2]*.0593017f:
        scope.pixels[index*3]*.2126729f+scope.pixels[index*3+1]*.7151522f+scope.pixels[index*3+2]*.0721750f;
    if(!(Y>0) || !std::isfinite(Y))return;
    const float low=tone?-16.f:recipe.localRange.minEv,high=tone?16.f:recipe.localRange.maxEv;
    const float ev=!tone && index<scope.controlSignal.size()?scope.controlSignal[index]:std::log2(Y/(tone?.18f:recipe.localRange.middleGrey));
    if(std::isfinite(ev))state.probe=(ev-low)/(high-low);
}
