#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "App/WorkspacePresentation.h"
#include <imgui_internal.h>

bool EditorModule::StartInterpretationWhiteBalance(std::optional<std::array<float,2>> sample) {
    if(m_InterpretationWhiteBalance.Busy() || !FinishWorkspaceInteraction())return false;
    RawWorkspaceEditContext context;
    if(!BeginRawWorkspaceEditContext(FindRawWorkspaceSourceByKey(m_Project->rawSourceKey),context,false)||!context.canEdit)return false;
    std::shared_ptr<const Raw::RawImageData> raw;
    if(context.multiFrameResult) {
        if(m_HdrAdoptedRawResult)raw=m_HdrAdoptedRawResult->rawData;
        else if(m_MfdAdoptedRawResult)raw=m_MfdAdoptedRawResult->rawData;
        if(!raw){m_WhiteBalanceStatus="Wait for the merged image before measuring white balance.";return false;}
    }
    m_WhiteBalanceRequestSource=GetProjectDocumentId()+GetActiveRawWorkspacePreviewIdentity();
    m_WhiteBalanceRequestRevision=m_Project->editRevision+m_Project->rawLayers.ProcessingRevision();
    m_WhiteBalanceAutoRequest=!sample;
    m_WhiteBalanceStatus.clear();
    return m_InterpretationWhiteBalance.Start(raw,context.recipe,sample);
}
void EditorModule::UpdateInterpretationWhiteBalance() {
    if(!IsRawWorkspaceProjectActive()&&!IsMultiFrameRawProjectActive())return;
    const auto revision=m_Project->editRevision+m_Project->rawLayers.ProcessingRevision();
    if(m_InterpretationWhiteBalance.Busy()) {
        if(m_WhiteBalanceRequestSource!=GetProjectDocumentId()+GetActiveRawWorkspacePreviewIdentity() ||
            revision!=m_WhiteBalanceRequestRevision) {
            m_InterpretationWhiteBalance.Cancel();m_WhiteBalanceStatus="White balance canceled because the image changed.";return;
        }
        std::array<float,3> gains;
        if(m_InterpretationWhiteBalance.Poll(gains,m_WhiteBalanceStatus)&&m_WhiteBalanceStatus.empty()) {
            RawWorkspaceEditContext context;
            if(!BeginRawWorkspaceEditContext(FindRawWorkspaceSourceByKey(m_Project->rawSourceKey),context,false)||!context.canEdit)return;
            const bool initialPreview=GetProjectSessionKind()==ProjectSessionKind::RawPreview &&
                context.recipe.whiteBalance.mode==Stack::RawRecipe::WhiteBalanceMode::Auto &&
                !context.recipe.whiteBalance.hasMultipliers;
            if(!context.multiFrameResult)context.recipe=ReadRawControlRecipe();
            context.recipe.whiteBalance.mode=m_WhiteBalanceAutoRequest?Stack::RawRecipe::WhiteBalanceMode::Auto:Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
            context.recipe.whiteBalance.hasMultipliers=true;context.recipe.whiteBalance.multipliers=gains;
            if(initialPreview) {
                // Establish the new preview baseline without materializing a
                // project merely because the user opened an original.
                const int sourceId=ResolveRawWorkspaceStageOutputNodeId();
                auto* node=m_Project->graph.FindNode(sourceId);
                if(node && m_Project->rawLayers.ApplySourceEdit(m_Project->rawLayers.State(),
                    m_Project->rawRecipe,context.recipe,m_WhiteBalanceStatus,&m_Project->graph,sourceId)) {
                    node->rawDevelopment.recipe=m_Project->rawRecipe;
                    ++m_Project->editRevision;MarkRenderRefreshDirty();
                }
            } else CommitRawWorkspaceEditContext(context,true,false);
        }
        return;
    }
    if(!m_WhiteBalanceRequestSource.empty())return;
    // Resolve the source once after loading. Merged recipes belong to their
    // source-set snapshot, so do not infer their mode from the single-RAW cache.
    if(!IsRawWorkspaceProjectLoadBusy() && !Stack::Workspace::IsPreview()) {
        const auto identity=GetProjectDocumentId()+GetActiveRawWorkspacePreviewIdentity();
        if(m_WhiteBalanceRequestSource==identity)return;
        RawWorkspaceEditContext context;
        if(!BeginRawWorkspaceEditContext(FindRawWorkspaceSourceByKey(m_Project->rawSourceKey),context,false)||!context.canEdit)return;
        if(context.multiFrameResult && !m_HdrAdoptedRawResult && !m_MfdAdoptedRawResult)return;
        m_WhiteBalanceRequestSource=identity;
        if(context.recipe.whiteBalance.mode==Stack::RawRecipe::WhiteBalanceMode::Auto && !context.recipe.whiteBalance.hasMultipliers)
            StartInterpretationWhiteBalance(std::nullopt);
    }
}
void EditorModule::RenderWhiteBalancePicker(const ImVec2& minimum,const ImVec2& maximum,
    const Stack::RawRecipe::RawCropRotationRecipe& crop) {
    if(!m_WhiteBalancePickerArmed)return;
    if(m_ViewSectionTab!=0 || !m_ViewSectionOpen || ImGui::GetIO().AppFocusLost ||
        ImGui::IsKeyPressed(ImGuiKey_Escape) || Stack::Workspace::IsPreview()) {
        m_WhiteBalancePickerArmed=false;return;
    }
    if(!ImGui::IsWindowHovered() || !ImRect(minimum,maximum).Contains(ImGui::GetIO().MousePos))return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const auto mouse=ImGui::GetIO().MousePos;
    ImGui::GetWindowDrawList()->AddCircle(mouse,7,ImGui::GetColorU32(ImGuiCol_Text),24,1.5f);
    auto* draw=ImGui::GetWindowDrawList();
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddLine({mouse.x-11,mouse.y},{mouse.x+11,mouse.y},ink);
    draw->AddLine({mouse.x,mouse.y-11},{mouse.x,mouse.y+11},ink);
    if(!ImGui::IsMouseClicked(0) || ImGui::IsAnyItemActive())return;
    const auto found=m_GraphOutputDescriptions.find("raw/$raw-source/technical/imageOut");
    using namespace Stack::NodeMath;
    if(m_GraphOutputDescriptionRenderRevision!=m_RenderRevision || found==m_GraphOutputDescriptions.end() ||
       found->second.descriptor.spatial.state!=KnowledgeState::Known ||
       m_LastGraphOutputSemanticDescriptor.spatial.state!=KnowledgeState::Known) {
        m_WhiteBalanceStatus="Wait for the image to settle before sampling.";return;
    }
    const auto mapping=Stack::Editor::RawLabInternal::RawLabImageMapping::FromSpatial(
        found->second.descriptor.spatial.value,m_LastGraphOutputSemanticDescriptor.spatial.value,crop,false);
    if(!mapping){m_WhiteBalanceStatus="This graph's geometry cannot be mapped to the RAW source.";return;}
    const auto uv=mapping->Local({(mouse.x-minimum.x)/(maximum.x-minimum.x),(mouse.y-minimum.y)/(maximum.y-minimum.y)});
    if(uv.x<0||uv.y<0||uv.x>1||uv.y>1)return;
    if(StartInterpretationWhiteBalance(std::array<float,2>{uv.x,uv.y}))m_WhiteBalancePickerArmed=false;
}
