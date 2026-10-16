#include "Project/RawCapturedMask.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "App/WorkspacePresentation.h"
#include "NodeMath/CompoundDefinition.h"
#include <algorithm>
#include <cstdio>

using namespace Stack;
using namespace Stack::Project;

bool EditorModule::RawCreatedAssetsEditable(std::string& error) const {
    const auto phase=m_Project->lifecycle.Phase();
    if (Workspace::IsPreview() || IsWorkspaceTransitionPending() || IsRawWorkspaceLockedByEditorProject() ||
        phase==ProjectLifecyclePhase::Loading || phase==ProjectLifecyclePhase::Importing ||
        phase==ProjectLifecyclePhase::Conflict || phase==ProjectLifecyclePhase::ReadOnlyRecovery ||
        Async::IsBusy(m_Project->files->load.state)) {
        error="The current project is not available for editing.";return false;
    }
    return true;
}

bool EditorModule::RawAssetCaptureInputMatches() const {
    const auto& capture=m_RawAssetCapture;
    const bool sameOwner=capture.target && capture.target->documentId==m_Project->documentId &&
        capture.target->loadGeneration==m_Project->files->load.generation && capture.frame==m_TimelineUi.currentFrame &&
        capture.sourceKey==m_Project->rawSourceKey && capture.sourceHash==GetActiveRawWorkspacePreviewSourceHash() &&
        capture.sourceRecipe==RawRecipe::SerializeWorkspaceSourceRecipe(RawRecipe::BuildTechnicalSourceRecipe(m_Project->rawRecipe));
    if(!sameOwner) return false;
    if(!capture.application) return capture.revision==m_Project->rawLayers.ProcessingRevision();
    if(capture.checkedRevision!=m_Project->rawLayers.Revision()) {
        capture.checkedInputMatches=capture.application->Matches(m_Project->rawLayers.State());
        capture.checkedRevision=m_Project->rawLayers.Revision();
    }
    return capture.checkedInputMatches;
}

Editor::RawOperationMaskAvailability EditorModule::QueryRawAssetCaptureAvailability() const {
    auto available = QueryRawOperationMaskAvailability();
    if (!available.target && !SelectedEditingNode())
        available.disabledReason = "Select a creative RAW operation. Camera preparation cannot capture range assets.";
    if (m_RawAssetCapture.pending) {
        available.target.reset(); available.disabledReason = "Wait for the current asset capture, or cancel it in Assets.";
    }
    return available;
}

bool EditorModule::BeginRawAssetCapture(const Editor::RawOperationMaskTarget& target,
    RawAssetCapture::Shape shape, RawAssetCapture::Channels channels, std::string& error) {
    auto available = QueryRawAssetCaptureAvailability();
    if (!available.target || !(target == *available.target)) {
        error = available.target ? "The selected capture input changed. Open the capture menu again." : available.disabledReason;
        return false;
    }
    const auto* layer = FindRawAdjustmentLayer(m_Project->rawLayers.State(),target.layerId);
    const auto* operation = SelectedEditingNode();
    if (!layer || !operation || !layer->graph.FindInputLink(operation->id,"imageIn")) {
        error = "Connect the operation's image input before capturing values."; return false;
    }
    if ((shape!=RawAssetCapture::Shape::Line && shape!=RawAssetCapture::Shape::Circle && shape!=RawAssetCapture::Shape::Stroke) ||
        (channels!=RawAssetCapture::Channels::Luminance && channels!=RawAssetCapture::Channels::Chroma && channels!=RawAssetCapture::Channels::Both)) {
        error="Unsupported capture shape or channels.";return false;
    }
    m_Project->rawLayers.EndGesture();
    m_EditingRawLayerMask.reset(); m_RawLayerMaskDrag = -1;
    auto& ui = m_RawWorkspaceLabUi;
    ui.colorWarpPendingCircleActive = ui.colorWarpPendingCircleDrawing = false;
    ui.colorWarpPendingCircleRefining = ui.colorWarpPendingCircleCommitRequested = false;
    ++ui.colorWarpAreaAnalysisGeneration; ui.colorWarpAreaAnalysisState.reset();
    auto& capture = m_RawAssetCapture;
    if (capture.cancellation) capture.cancellation->store(true);
    capture.cancellation=std::make_shared<std::atomic<bool>>(false);
    const float previousRadius=capture.selection.strokeRadius,previousTolerance=capture.selection.outerFeather;
    capture.application.reset();capture.checkedRevision=0;capture.checkedInputMatches=false;
    capture.target = target; capture.selection = {}; capture.selection.shape = shape; capture.selection.channels = channels;
    if(capture.repeat) {capture.selection.strokeRadius=previousRadius;capture.selection.outerFeather=previousTolerance;}
    capture.actionMenu=capture.openMenu=capture.excludeStroke=capture.useCurve=false;capture.maskApplication=0;
    capture.armed = true; capture.drawing = capture.pending = false; capture.status.clear();
    ++capture.serial; capture.feedback.reset(); capture.feedbackNeeded = shape!=RawAssetCapture::Shape::Stroke; capture.feedbackDirty = false;
    capture.feedbackValid = false;
    capture.feedbackOpacity = 0; capture.lastActivity = capture.lastMeasurement = -1;
    capture.width = capture.height = 0; capture.operationId = operation->id;
    capture.revision = m_Project->rawLayers.ProcessingRevision(); capture.frame = m_TimelineUi.currentFrame;
    capture.sourceKey = m_Project->rawSourceKey; capture.sourceHash = GetActiveRawWorkspacePreviewSourceHash();
    capture.sourceRecipe = RawRecipe::SerializeWorkspaceSourceRecipe(RawRecipe::BuildTechnicalSourceRecipe(m_Project->rawRecipe));
    RequestOpenRawLabTab();
    error.clear(); return true;
}

bool EditorModule::SubmitRawAssetCapture(RawAssetCapture::Output output,bool applyMask,bool selectCurveTones) {
    auto& capture=m_RawAssetCapture;
    if(!capture.armed || capture.pending || capture.drawing || capture.selection.shape!=RawAssetCapture::Shape::Stroke ||
        capture.selection.path.empty() || int(output)<0 || int(output)>int(RawAssetCapture::Output::BothMatch)) return false;
    const bool mask=output==RawAssetCapture::Output::SpatialMask || output==RawAssetCapture::Output::LumaMatch ||
        output==RawAssetCapture::Output::ColorMatch || output==RawAssetCapture::Output::BothMatch;
    if((applyMask && !mask) || (selectCurveTones && output!=RawAssetCapture::Output::Samples)) return false;
    capture.maskApplication=capture.lastMaskApplication=applyMask?1:0;
    capture.selection.output=capture.lastOutput=output;capture.useCurve=capture.lastUseCurve=selectCurveTones;
    capture.actionMenu=capture.openMenu=false;
    return QueueRawAssetCapture();
}

bool EditorModule::QueueRawAssetCapture() {
    auto& capture=m_RawAssetCapture;
    if(!capture.target || !RawAssetCaptureInputMatches()) {
        capture.pending=false;capture.status="The capture input changed.";return false;
    }
    if(capture.maskApplication && !capture.application) {
        auto candidate=m_Project->rawLayers.State();
        Assets::FloatData black;black.width=black.height=black.channels=1;black.units="coverage";black.values={0};
        const auto created=ApplyRawCapturedMask(candidate,capture.target->layerId,capture.target->operationUuid,
            black,"Unfinished capture",capture.status);
        if(created.operationUuid.empty()) {capture.pending=false;return false;}
        auto* owner=FindRawAdjustmentLayer(candidate,created.layerId);
        const auto* published=FindRawMaskOutput(candidate,created.mask);
        const auto* producer=published?FindRawPublishedNode(*owner,*published):nullptr;
        if(!producer) {capture.pending=false;return false;}
        const auto token=NodeMath::GenerateCanonicalUuid();const auto uuid=producer->instanceUuid;
        owner->graph.FindNode(producer->id)->rawCoverage["captureToken"]=token;
        m_Project->rawLayers.EndGesture();
        if(!ApplyRawLayerStackEdit(std::move(candidate),true)) {capture.pending=false;capture.status=m_RawLayerStatus;return false;}
        capture.application=RawPendingMask{m_Project->rawLayers.State(),created.layerId,created.operationUuid,uuid,token};
        capture.checkedRevision=m_Project->rawLayers.Revision();capture.checkedInputMatches=true;
        SelectRawNodeEditor(created.layerId,created.operationUuid);
    }
    capture.armed=capture.drawing=capture.feedbackNeeded=false;
    capture.pending=true;++capture.serial;capture.status=capture.application?"Calculating mask. Your adjustment is ready to edit.":"Capturing native image values...";
    return true;
}

void EditorModule::AppendRawAssetCaptureRequests(std::vector<EditorRenderWorker::PreviewRequest>& requests) {
    auto& capture=m_RawAssetCapture;
    const bool feedback = capture.armed && capture.feedbackNeeded;
    if ((!capture.pending && !feedback) || !capture.target || capture.width <= 0 || capture.height <= 0) return;
    const auto available=QueryRawOperationMaskAvailability();
    if ((!capture.application && (!available.target || !(*available.target==*capture.target))) || !RawAssetCaptureInputMatches()) {
        if (capture.cancellation) capture.cancellation->store(true);
        capture.pending=capture.armed=capture.drawing=capture.feedbackNeeded=false;
        capture.application.reset();capture.status="Capture canceled because its image or operation changed.";return;
    }
    EditorRenderWorker::PreviewRequest request;
    request.assetCapture=capture.selection;request.assetLayerId=capture.target->layerId;
    request.assetCaptureInput=feedback;
    request.assetCaptureCancellation=capture.cancellation;
    request.assetOperationId=capture.operationId;request.assetWorkingSpace=capture.workingSpace;
    request.width=capture.width;request.height=capture.height;request.dirtyGeneration=capture.serial;
    request.sourceNodeId=ResolveRawWorkspaceStageOutputNodeId();
    request.sourceSocketId="imageOut";
    requests.push_back(std::move(request));
}

void EditorModule::AdoptRawAssetCapture(EditorRenderWorker::PreviewResult& result) {
    auto& capture=m_RawAssetCapture;
    if (!capture.target || result.dirtyGeneration!=capture.serial ||
        (result.assetCaptureInput ? !capture.armed : !capture.pending)) return;
    const auto available=QueryRawOperationMaskAvailability();
    if ((!capture.application && (!available.target || !(*available.target==*capture.target))) || !RawAssetCaptureInputMatches()) {
        capture.pending=capture.armed=capture.drawing=capture.feedbackNeeded=false;
        capture.application.reset();capture.status="Capture discarded because its image changed.";return;
    }
    if (result.assetCaptureInput) {
        capture.feedbackNeeded=false;
        capture.feedback=std::move(result.assetFeedback);
        capture.feedbackDirty=true;
        if (!result.success || !capture.feedback) capture.status=result.error.empty()?"Live capture feedback is unavailable.":result.error;
        return;
    }
    // A newer preview may interrupt the worker while the same capture remains
    // valid. It will be included in that newer request; do not turn this into a
    // failed or unmasked adjustment.
    if(result.assetCaptureSuperseded) return;
    capture.pending=false;
    if (!result.success || !result.capturedAsset) {
        capture.application.reset();
        capture.status=result.error.empty()?"Capture failed.":result.error;
        PostNotification(UiNotificationSeverity::Error,capture.status,"raw-asset-capture"); return;
    }
    auto asset=std::move(*result.capturedAsset);
    if(capture.useCurve && asset.samples && m_Project->nodeEditor) {
        auto& state=m_NodeEditorState.Get<Editor::RawLabInternal::CurveCanvasState>(*m_Project->nodeEditor);
        const auto* layer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),capture.target->layerId);
        const auto* node=SelectedEditingNode();
        if(layer && node && node->kind==EditorNodeGraph::NodeKind::RawOperation) {
            const auto recipe=ReadRawLayerOperation(*layer,node->instanceUuid);
            const bool tone=node->rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve;
            const auto t=RawRecipe::ReadSceneTone(recipe.finishTone.layerJson.value("luminanceTone",nlohmann::json::object()));
            const auto points=tone?t.curve:RawRecipe::RawLocalRangeBezierPoints(recipe.localRange);
            const float low=tone?-16.f:recipe.localRange.minEv,high=tone?16.f:recipe.localRange.maxEv;
            state.samples.clear();state.selected.assign(points.size(),false);
            const auto& samples=*asset.samples;
            for(int i=0;i<samples.width;++i) {
                if(samples.values[(size_t(samples.width)*2+i)*4+3]==0)continue;
                const float ev=tone?samples.values[size_t(i)*4+3]:
                    std::log2(samples.values[size_t(i)*4+2]/recipe.localRange.middleGrey);
                const float x=(ev-low)/(high-low);
                state.samples.push_back(x);
                if(!points.empty()) {
                    const auto p=std::min_element(points.begin(),points.end(),[&](auto a,auto b){return std::abs(a.x-x)<std::abs(b.x-x);});
                    state.selected[size_t(p-points.begin())]=true;
                }
            }
            ++state.samplesRevision;
            state.mode=Editor::RawLabInternal::CurveGestureMode::Select;
        }
        capture.status="Sampled tones selected on the curve.";capture.useCurve=false;
        if(capture.repeat) {std::string error;const auto target=*capture.target;
            BeginRawAssetCapture(target,RawAssetCapture::Shape::Stroke,RawAssetCapture::Channels::Both,error);}
        return;
    }
    auto candidate=m_Project->rawLayers.State();
    asset.id=NodeMath::GenerateCanonicalUuid();
    const auto channels=capture.selection.channels;
    asset.name=std::string(channels==RawAssetCapture::Channels::Luminance?"Luminance":
        channels==RawAssetCapture::Channels::Chroma?"Chroma":"Luminance and chroma")+" "+std::to_string(candidate.assets.size()+1);
    if(capture.selection.shape==RawAssetCapture::Shape::Stroke) {
        const char* names[]={"Stroke samples","Luminance strip","Chroma strip","Color strip","Luminance texture","Chroma texture","Stroke mask","Brightness match","Color match","Brightness and color match"};
        asset.name=std::string(names[int(capture.selection.output)])+" "+std::to_string(candidate.assets.size()+1);
    }
    asset.provenance["operationUuid"]=capture.target->operationUuid;
    asset.provenance["graphId"]=capture.target->layerId;
    asset.provenance["documentId"]=capture.target->documentId;
    asset.provenance["documentRevision"]=capture.revision;
    std::string editedOperation;
    const bool attach=capture.maskApplication!=0 && asset.kind==Assets::Kind::Mask;
    if(attach && !capture.application) {
        const auto created=ApplyRawCapturedMask(candidate,capture.target->layerId,capture.target->operationUuid,
            *asset.data,asset.name,capture.status);
        if(created.operationUuid.empty())return;
        editedOperation=created.operationUuid;
    }
    bool applied=false;
    if(attach && capture.application) {
        const auto& pending=*capture.application;
        applied=m_Project->rawLayers.CompleteMaskCapture(pending.layerId,pending.maskUuid,pending.token,asset,m_RawLayerStatus);
        if(applied) {MarkDirty();MarkRenderRefreshDirty();}
    } else {
        candidate.assets.push_back(std::move(asset));
        m_Project->rawLayers.EndGesture();
        applied=ApplyRawLayerStackEdit(std::move(candidate),attach);
        if(applied && attach)SelectRawNodeEditor(capture.target->layerId,editedOperation);
    }
    capture.status=applied?"Asset saved.":m_RawLayerStatus;
    capture.application.reset();
    if(applied && capture.repeat && capture.selection.shape==RawAssetCapture::Shape::Stroke) {
        std::string error;const auto available=QueryRawAssetCaptureAvailability();
        if(available.target)BeginRawAssetCapture(*available.target,RawAssetCapture::Shape::Stroke,RawAssetCapture::Channels::Both,error);
    }
    if (capture.status=="Asset saved.") PostNotification(UiNotificationSeverity::Success,"Capture saved in Assets.","raw-asset-capture");
}

bool EditorModule::AddRawAssetToGraph(const std::string& assetId) {
    if (!RawCreatedAssetsEditable(m_RawAssetCapture.status)) return false;
    if (!CommitRawLayerMaskGraph()) return false;
    auto candidate=m_Project->rawLayers.State();
    const auto asset=std::find_if(candidate.assets.begin(),candidate.assets.end(),[&](const auto& a){return a.id==assetId;});
    auto* layer=FindRawAdjustmentLayer(candidate,m_SelectedRawAdjustmentLayer);
    if (asset==candidate.assets.end() || !layer || asset->kind!=Assets::Kind::Mask || !asset->data) return false;
    auto payload=CapturedMaskPayload(asset->data);
    const auto layerId=layer->id;
    auto* node=layer->graph.AddCustomMaskNode(std::move(payload),{0,-480.f});
    if (!node) return false;
    node->title=asset->name;node->rawCoverage={{"canvas","project"}}; const auto uuid=node->instanceUuid;
    RegisterRawMaskOutputs(candidate,layerId);
    m_Project->rawLayers.EndGesture();
    if (!ApplyRawLayerStackEdit(std::move(candidate))) return false;
    OpenRawLayerMaskGraph(layerId);
    for (const auto& projected:GetNodeGraph().GetNodes())
        if (projected.rawLayerOwnerId==layerId && projected.instanceUuid==uuid) {
            GetNodeGraph().SelectNode(projected.id); break;
        }
    m_RawAssetCapture.status="Added an independent mask texture to Graph.";
    return true;
}

void EditorModule::RenderRawAssetsPanel() {
    using namespace Editor::RawLabInternal;
    auto& capture=m_RawAssetCapture;
    m_RawAssetPreviews.Synchronize(GetProjectDocumentId(),m_Project->rawLayers.State().assets);
    std::string editingError;
    const bool editable=RawCreatedAssetsEditable(editingError) && !capture.pending;
    ImGui::BeginDisabled(!editable);
    if (BareTextButton("Undo")) UndoRawLayerEdit();
    ImGui::SameLine(); if (BareTextButton("Redo")) RedoRawLayerEdit();
    ImGui::EndDisabled();
    if (capture.armed || capture.pending) {
        ImGui::TextWrapped("%s",capture.pending?"Capturing native image values...":"Draw on the image to capture values. Escape cancels.");
        if (capture.armed && capture.selection.shape!=RawAssetCapture::Shape::Stroke)
            ImGui::TextWrapped("Hold the button and scroll to soften the mask. Shift adjusts inner falloff; Ctrl adjusts outer falloff.");
        if (BareTextButton("Cancel capture")) {
            if (capture.cancellation) capture.cancellation->store(true);
            capture.pending=capture.armed=capture.drawing=capture.feedbackNeeded=false;
            capture.application.reset();capture.feedback.reset();++capture.serial;capture.status="Capture canceled.";
        }
    }
    if (!capture.status.empty()) ImGui::TextWrapped("%s",capture.status.c_str());
    if (m_Project->rawLayers.State().assets.empty()) {
        ImGui::TextWrapped("Hold C and draw on the image, or use the mask icon in the top bar. Choose what to save or apply when you release."); return;
    }
    ImGui::TextDisabled("Saved captures keep their original values.");
    for (const auto& asset:m_Project->rawLayers.State().assets) {
        const std::string id=asset.id;
        ImGui::PushID(id.c_str());
        const bool open=ImGui::TreeNodeEx("Asset",ImGuiTreeNodeFlags_DefaultOpen,"%s",asset.name.c_str());
        bool changed=false;
        if (open) {
            const auto& data=*asset.data;
            ImGui::TextDisabled("%s  %d x %d",data.units.c_str(),data.width,data.height);
            if (asset.samples) ImGui::TextDisabled("%d sampled pixels",asset.samples->width);
            if (asset.provenance.contains("innerFeather") && asset.provenance.contains("outerFeather") &&
                asset.provenance.at("innerFeather").is_number() && asset.provenance.at("outerFeather").is_number())
                ImGui::TextDisabled("Inner %.0f%%  Outer %.0f%%",asset.provenance.at("innerFeather").get<float>()*100,
                    asset.provenance.at("outerFeather").get<float>()*100);
            if (asset.provenance.contains("rangeMinimum") && asset.provenance.contains("rangeMaximum")) {
                const auto& low=asset.provenance.at("rangeMinimum");const auto& high=asset.provenance.at("rangeMaximum");
                const int channels=asset.provenance.contains("channels") && asset.provenance.at("channels").is_number_integer()?
                    asset.provenance.at("channels").get<int>():-1;
                const auto numeric=[](const auto& array) {return array.is_array() && array.size()==3 &&
                    std::all_of(array.begin(),array.end(),[](const auto& value){return value.is_number();});};
                if (numeric(low) && numeric(high) && channels>=0 && channels<=static_cast<int>(RawAssetCapture::Channels::Both)) {
                    if (channels!=static_cast<int>(RawAssetCapture::Channels::Chroma))
                        ImGui::TextDisabled("Luminance Y  %.5g to %.5g",low[0].get<float>(),high[0].get<float>());
                    if (channels!=static_cast<int>(RawAssetCapture::Channels::Luminance)) {
                        ImGui::TextDisabled("Chroma a  %.4f to %.4f",low[1].get<float>(),high[1].get<float>());
                        ImGui::TextDisabled("Chroma b  %.4f to %.4f",low[2].get<float>(),high[2].get<float>());
                    }
                }
            }
            if(asset.kind!=Assets::Kind::DataRange) {
                const auto origin=ImGui::GetCursorScreenPos();
                const float w=std::max(1.f,ImGui::GetContentRegionAvail().x),h=std::clamp(w*data.height/data.width,32.f,160.f);
                if (ImGui::IsRectVisible({w,h}))
                    if (const auto texture=m_RawAssetPreviews.Texture(asset))
                        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture),origin,{origin.x+w,origin.y+h},
                            ImVec2(0,0), ImVec2(1,1), ImGui::GetColorU32(ImVec4(1,1,1,1)));
                ImGui::Dummy({w,h});
                if(data.channels==2)ImGui::TextDisabled("Chroma preview uses fixed lightness.");
            } else ImGui::TextDisabled("Ordered positions, RGB, alpha, luminance and chroma.");
            char name[256];std::snprintf(name,sizeof(name),"%s",asset.name.c_str());
            ImGui::BeginDisabled(!editable);
            if (ImGui::InputText("Name",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue) && *name) {
                auto candidate=m_Project->rawLayers.State();
                for (auto& a:candidate.assets) if (a.id==asset.id) a.name=name;
                changed=ApplyRawLayerStackEdit(std::move(candidate),false);
            }
            if (!changed && asset.kind==Assets::Kind::Mask && BareTextButton("Add to Graph")) changed=AddRawAssetToGraph(id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds an independent texture at the capture's native resolution. Edit its result with Graph math nodes.");
            ImGui::SameLine();
            if (!changed && BareTextButton("Delete")) {
                auto candidate=m_Project->rawLayers.State();
                candidate.assets.erase(std::remove_if(candidate.assets.begin(),candidate.assets.end(),[&](const auto& a){return a.id==id;}),candidate.assets.end());
                changed=ApplyRawLayerStackEdit(std::move(candidate),false);
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::PopID(); if (changed) break;
    }
}
