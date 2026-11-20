#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphEditorCommands.h"
#include "Project/AuthoredMask.h"
#include "Assets/MaskDisplayPreview.h"
#include "Project/GraphImageContracts.h"
#include "App/WorkspacePresentation.h"

using namespace Stack;
using namespace Stack::Project;
using Phase=Editor::MaskAuthoringSession::Phase;

bool EditorModule::PickingMaskDestination() const {return m_MaskSession.phase==Phase::Destination;}
bool EditorModule::MaskAuthoringActive() const {return m_MaskSession.phase!=Phase::Idle;}

void EditorModule::CancelMaskAuthoring() {
    auto& s=m_MaskSession;
    if(s.phase==Phase::Idle)return;
    if(s.cancel)s.cancel->store(true);
    if(s.work)s.work->canceled.store(true);
    if(PickingMaskDestination())m_ViewSectionTab=s.previousTab;
    const auto serial=s.generation+1;
    s={};s.generation=serial;
}

bool EditorModule::MaskOwnerCurrent() const {
    const auto& s=m_MaskSession;
    return s.document==m_Project->documentId && s.load==m_Project->files->load.generation &&
        s.frame==m_TimelineUi.currentFrame && s.edit==m_Project->editRevision &&
        s.rawEdit==m_Project->rawLayers.ProcessingRevision() && !Workspace::IsPreview();
}

void EditorModule::StartMaskTool(MaskAuthoring::Tool tool) {
    auto& s=m_MaskSession;
    if(s.phase==Phase::Applying || s.dragging)return;
    if(s.phase==Phase::Idle) {
        std::string error;
        if(!RawCreatedAssetsEditable(error) || !FinishWorkspaceInteraction()) {
            if(!error.empty())PostNotification(UiNotificationSeverity::Warning,error,"mask-authoring");
            return;
        }
        if(m_RawAssetCapture.pending || m_RawAssetCapture.drawing)return;
        m_RawAssetCapture.armed=false;
        s.phase=Phase::Destination;s.document=m_Project->documentId;s.load=m_Project->files->load.generation;
        s.frame=m_TimelineUi.currentFrame;s.edit=m_Project->editRevision;s.rawEdit=m_Project->rawLayers.ProcessingRevision();
        s.previousTab=m_ViewSectionTab;m_ViewSectionTab=1;s.acceptAfter=ImGui::GetFrameCount();
        s.cancel=std::make_shared<std::atomic<bool>>(false);
        m_WhiteBalancePickerArmed=false;m_RawWorkspaceLabUi.ResetColorWarpAreaRequest();
        m_EditingRawLayerMask.reset();
    }
    s.tool=tool;
}

void EditorModule::ChooseMaskDestination(const NodeEditTarget& target) {
    auto& s=m_MaskSession;
    if(!PickingMaskDestination() || !MaskOwnerCurrent())return;
    s.target=target;s.layerId=target.layerId;
    if(s.layerId.empty())s.layerId=IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()? kRawBackgroundId:kProjectGraphId;
    m_ViewSectionTab=s.previousTab;s.phase=Phase::Drawing;s.acceptAfter=ImGui::GetFrameCount();
    s.pendingSource=true;++s.generation;
}

bool EditorModule::ReopenAuthoredMask(const EditorNodeGraph::Node& node,const std::string& layer) {
    if(node.kind!=EditorNodeGraph::NodeKind::CustomMask || !node.customMask.authoring)return false;
    // A project canvas may be rebuilt by FinishWorkspaceInteraction.
    auto recipe=node.customMask.authoring;const auto uuid=node.instanceUuid;
    const auto source=node.rawCoverage.value("sampleAfter",std::string{});
    auto owner=layer;
    auto saved=Assets::MaskDisplayPreview(node.customMask.rasterLayer,node.customMask.width,node.customMask.height,768);
    try {
        auto draft=MaskAuthoring::Deserialize(*recipe);
        CancelMaskAuthoring();
        StartMaskTool(draft.marks.empty()?MaskAuthoring::Tool::Brush:draft.marks.front().tool);
        if(!PickingMaskDestination())return true;
        ChooseMaskDestination({m_Project->documentId,owner,source});
        auto& s=m_MaskSession;s.maskUuid=uuid;s.recipe=std::move(draft);s.preview=std::move(saved);s.textureDirty=true;
        if(owner==kProjectGraphId)SetWorkspaceMode(WorkspaceMode::View);else RequestOpenRawLabTab();
    } catch(const std::exception& e){PostNotification(UiNotificationSeverity::Error,e.what(),"mask-authoring");}
    return true;
}

bool EditorModule::ResolveMaskSource(const RawRecipe::RawCropRotationRecipe& crop) {
    auto& s=m_MaskSession;
    if(s.sourceReady || !s.pendingSource)return s.sourceReady;
    const auto unavailable=[&](const char* reason){
        s.pendingSource=false;s.status=reason;return false;
    };
    if(m_GraphOutputDescriptionRenderRevision!=m_RenderRevision)BuildGraphSnapshot();
    auto descriptor=m_LastGraphOutputSemanticDescriptor;
    // A saved mask outlives its sampling adjustment. It can use the current
    // image on a deliberate edit while retaining the previously sampled colors.
    if(!s.maskUuid.empty() && !s.target.nodeUuid.empty() && !ResolveNodeEditorTarget(s.target))s.target.nodeUuid.clear();
    if(!s.target.nodeUuid.empty()) {
        const auto* node=ResolveNodeEditorTarget(s.target);
        if(!node)return unavailable("The source adjustment is unavailable.");
        s.sourceNode=node->id;
        const auto key=(s.layerId==kProjectGraphId?std::string{}:"raw/"+s.layerId+"/")+
            EditorNodeGraph::GraphOutputIdentity(node->id,"imageOut");
        const auto found=m_GraphOutputDescriptions.find(key);
        if(found==m_GraphOutputDescriptions.end())return unavailable("The adjustment image is unavailable.");
        descriptor=found->second.descriptor;
    } else if(IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive()) {
        const auto it=m_GraphOutputDescriptions.find("raw/$raw-output/imageIn");
        if(it!=m_GraphOutputDescriptions.end())descriptor=it->second.descriptor;
    }
    s.workingSpace=RawOperationInputWorkingSpace(descriptor).value_or(
        IsRawWorkspaceProjectActive()?m_Project->rawRecipe.technical.workingSpace:Raw::RawWorkingSpace::LinearSrgbD65);
    if(descriptor.spatial.state==NodeMath::KnowledgeState::Known &&
        m_LastGraphOutputSemanticDescriptor.spatial.state==NodeMath::KnowledgeState::Known) {
        auto mapping=Editor::RawLabInternal::RawLabImageMapping::FromSpatial(descriptor.spatial.value,
            m_LastGraphOutputSemanticDescriptor.spatial.value,crop,false);
        if(!mapping)return unavailable("This image has no usable coordinate mapping.");
        s.mapping=*mapping;
    } else if(!s.target.nodeUuid.empty())return unavailable("The adjustment image geometry is unavailable.");
    else if(crop.cropEnabled){
        NodeMath::SpatialDescriptor spatial{};
        auto mapping=Editor::RawLabInternal::RawLabImageMapping::FromSpatial(spatial,spatial,crop,false);
        if(!mapping)return unavailable("Crop coordinates are unavailable.");
        s.mapping=*mapping;
    }
    if(descriptor.spatial.state==NodeMath::KnowledgeState::Known){
        const auto& extent=descriptor.spatial.value.fullWindow;
        s.width=int(extent.width);s.height=int(extent.height);
    }
    s.sourceReady=true;s.status.clear();return true;
}

void EditorModule::AppendMaskAuthoringRequest(std::vector<EditorRenderWorker::PreviewRequest>& requests) {
    const auto& s=m_MaskSession;
    if(!s.pendingSource || !s.sourceReady || !MaskOwnerCurrent())return;
    EditorRenderWorker::PreviewRequest request;
    request.assetCapture.emplace();request.maskAuthoring=true;request.assetCaptureInput=s.phase!=Phase::Applying;
    if(!request.assetCaptureInput)request.maskRecipe=std::make_shared<const MaskAuthoring::Recipe>(s.recipe);
    request.maskNativeWidth=s.width;request.maskNativeHeight=s.height;
    request.assetLayerId=s.layerId;request.assetOperationId=s.sourceNode;request.assetWorkingSpace=s.workingSpace;
    request.assetCaptureCancellation=s.cancel;request.dirtyGeneration=s.generation;
    request.sourceNodeId=ResolveRawWorkspaceStageOutputNodeId();request.sourceSocketId="imageOut";
    // RAW nodes own their source and can start from a transparent reference canvas.
    if(IsRawWorkspaceProjectActive() && s.width>0 && s.height>0){request.width=s.width;request.height=s.height;}
    requests.push_back(std::move(request));
}

void EditorModule::AdoptMaskAuthoringResult(EditorRenderWorker::PreviewResult& result) {
    auto& s=m_MaskSession;
    if(!s.pendingSource || s.generation!=result.dirtyGeneration || result.assetCaptureSuperseded)return;
    if(!MaskOwnerCurrent()){CancelMaskAuthoring();return;}
    s.pendingSource=false;
    if(!result.success) {
        s.phase=Phase::Drawing;s.status=result.error.empty()?"Could not read the mask source.":result.error;
        PostNotification(UiNotificationSeverity::Error,s.status,"mask-authoring");return;
    }
    if(result.assetCaptureInput){s.field=std::move(result.assetFeedback);s.previewDirty=s.edited;return;}
    if(!result.capturedAsset || !result.capturedAsset->data){s.phase=Phase::Drawing;s.status="Mask calculation returned no coverage.";return;}
    try {
        std::string error,adjustment;
        if(!RawCreatedAssetsEditable(error))throw std::runtime_error(error);
        AuthoredMaskEdit edit{s.target.nodeUuid,s.maskUuid,"Mask",std::move(result.capturedAsset->provenance.at("authoring")),result.capturedAsset->data};
        auto layer=s.layerId;
        if(layer==kProjectGraphId) {
            auto proposal=GraphModel::ProposeEdit(m_Project->graph,m_Project->graph.GetStructureRevision(),[&](auto& graph){adjustment=WriteAuthoredMask(graph,edit);});
            if(!ApplyProjectGraphDocumentEdit(s.document,s.edit,std::move(proposal),m_Project->timeline,false,error))throw std::runtime_error(error);
        } else {
            auto candidate=m_Project->rawLayers.State();
            auto* owner=FindRawAdjustmentLayer(candidate,layer);
            if(!owner)throw std::runtime_error("The destination layer has been removed.");
            edit.name="Mask "+std::to_string(owner->maskOutputs.size()+1);
            adjustment=WriteAuthoredMask(owner->graph,edit);
            RegisterRawMaskOutputs(candidate,layer);
            if(!ValidateRawLayerStack(candidate,error,&m_Project->graph,ResolveRawWorkspaceStageOutputNodeId()))throw std::runtime_error(error);
            m_Project->rawLayers.EndGesture();
            if(!ApplyRawLayerStackEdit(std::move(candidate)))throw std::runtime_error(m_RawLayerStatus);
        }
        CancelMaskAuthoring();
        if(!adjustment.empty()) {
            ActivateNodeEditorPanelTarget({m_Project->documentId,layer,adjustment});
            SetViewSectionTab(1);m_RawSettingsPanelRequested=true;
        }
    } catch(const std::exception& e){s.phase=Phase::Drawing;s.status=e.what();PostNotification(UiNotificationSeverity::Error,s.status,"mask-authoring");}
}

void EditorModule::UpdateMaskPreview() {
    auto& s=m_MaskSession;
    if(s.work && s.work->ready.load()) {
        if(!s.work->canceled.load()) {
            if(s.work->coverage){s.preview=s.work->coverage;s.textureDirty=true;}
            else s.status=s.work->error;
        }
        s.work.reset();
    }
    if(!s.field || !s.previewDirty || s.work || s.dragging || s.phase!=Phase::Drawing)return;
    s.previewDirty=false;
    if(s.recipe.marks.empty()){s.preview.reset();s.texture.Reset();return;}
    auto job=std::make_shared<Editor::MaskPreviewWork>();
    if(ProjectTasks().Submit("Previewing mask",[job,field=s.field,recipe=s.recipe] {
        try {job->coverage=MaskAuthoring::Evaluate(*field,recipe,[&]{return job->canceled.load();}).data;}
        catch(const std::exception& e){job->error=e.what();}
        job->ready.store(true);
    }))s.work=std::move(job);
    else s.status="Mask preview worker is unavailable.";
}

