#include "NodeMath/ImageOperationInput.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Editor/NodeEditors/ToneCurveEditorState.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include <imgui_internal.h>
#include <cmath>

void EditorModule::UpdateRawCurveProbe(const ImVec2& minimum,const ImVec2& maximum,
    const Stack::RawRecipe::RawCropRotationRecipe& crop) {
    using namespace Stack;
    if(!m_Project->nodeEditor) return;
    const auto* node=SelectedEditingNode();
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawOperation) return;
    const bool tone = node->rawOperation.kind == RawRecipe::GraphOperationKind::ToneCurve;
    if (!tone && node->rawOperation.kind != RawRecipe::GraphOperationKind::LocalEv) return;
    const auto& parameters = node->rawOperation.parameters;
    int channel = -1;
    Editor::RawLabInternal::CurveCanvasState* canvas;
    if (tone) {
        auto& editorState = m_NodeEditorState.Get<Editor::ToneCurveEditorState>(*m_Project->nodeEditor);
        const auto mode = NodeMath::ToneCurveMode(parameters);
        if (mode != "luminance" && mode != "components") return;
        channel = mode == "luminance" ? editorState.photoChannel : editorState.component;
        if (mode == "luminance" && channel == 0) channel = 4;
        canvas = &editorState.canvas[channel];
    } else {
        canvas = &m_NodeEditorState.Get<Editor::RawLabInternal::CurveCanvasState>(*m_Project->nodeEditor);
    }
    canvas->probe.reset();
    const auto& scope=m_RawWorkspaceGraphScopeReadback;
    if(!scope.valid || !scope.sceneLinearBeforeViewTransform || scope.width<=0 || scope.height<=0 ||
       scope.stage!=(tone?RawDevelopmentGraphScopeStage::FinishToneInput:RawDevelopmentGraphScopeStage::LocalRangeInput) ||
       !ImGui::IsWindowHovered() || ImGui::IsAnyItemActive())return;
    const ImVec2 mouse=ImGui::GetIO().MousePos;
    if(!ImRect(minimum,maximum).Contains(mouse))return;
    const auto* layer=Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),m_SelectedRawAdjustmentLayer);
    const auto* input=layer?layer->graph.FindInputLink(node->id,"imageIn"):nullptr;
    if(!input)return;
    if (m_GraphOutputDescriptionRenderRevision != m_RenderRevision) return;
    const auto found=m_GraphOutputDescriptions.find("raw/"+layer->id+"/"+EditorNodeGraph::GraphOutputIdentity(input->fromNodeId,input->fromSocketId));
    using namespace NodeMath;
    if(found==m_GraphOutputDescriptions.end() || found->second.descriptor.spatial.state!=KnowledgeState::Known ||
       m_LastGraphOutputSemanticDescriptor.spatial.state!=KnowledgeState::Known)return;
    const auto& descriptor=found->second.descriptor;
    // Current analysis pixels do not retain alpha. Do not claim a probe for
    // transparent inputs until their validity channel is available here.
    if (descriptor.alpha.state != KnowledgeState::Known ||
        (descriptor.alpha.value != AlphaMode::Opaque && descriptor.alpha.value != AlphaMode::Absent)) return;
    const auto mapping=Editor::RawLabInternal::RawLabImageMapping::FromSpatial(descriptor.spatial.value,
        m_LastGraphOutputSemanticDescriptor.spatial.value,crop);
    if(!mapping || maximum.x<=minimum.x || maximum.y<=minimum.y)return;
    const auto uv=mapping->Local({(mouse.x-minimum.x)/(maximum.x-minimum.x),(mouse.y-minimum.y)/(maximum.y-minimum.y)});
    if(uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1)return;
    const int x=std::min(scope.width-1,int(uv.x*scope.width)),y=std::min(scope.height-1,int(uv.y*scope.height));
    const size_t index=size_t(y)*scope.width+x;
    if(scope.pixels.size()<size_t(scope.width)*scope.height*3)return;
    if (tone && channel != 4) {
        if (channel == 0 && !NodeMath::DescribeImageOperationInput(descriptor).scalar) return;
        float coordinate = scope.pixels[index*3 + (channel ? channel-1 : 0)];
        if (parameters.value("domain",0)==1) {
            if (!(coordinate > 0)) return;
            const float lo=parameters.value("logMinEv",-10.f), hi=parameters.value("logMaxEv",6.f);
            coordinate=(std::log2(coordinate/parameters.value("middleGrey",.18f))-lo)/(hi-lo);
        }
        if (std::isfinite(coordinate)) canvas->probe=coordinate;
        return;
    }
    const auto weights=NodeMath::DescribeImageOperationInput(descriptor).lumaWeights;
    const float luminance=weights[0]*scope.pixels[index*3]+weights[1]*scope.pixels[index*3+1]+weights[2]*scope.pixels[index*3+2];
    if (!(luminance>0) || !std::isfinite(luminance)) return;
    if (tone) { canvas->probe=(std::log2(luminance/.18f)+16.f)/32.f; return; }
    const auto& local=parameters.at("localRange");
    const float low=local.value("minEv",-8.f), high=local.value("maxEv",6.f);
    const float ev=index<scope.controlSignal.size() ? scope.controlSignal[index] : std::log2(luminance/local.value("middleGrey",.18f));
    if (std::isfinite(ev) && low<high) canvas->probe=(ev-low)/(high-low);
}
