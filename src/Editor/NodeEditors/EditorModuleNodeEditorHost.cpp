#include "Editor/NodeGraph/ChannelNodeNames.h"
#include "Editor/EditorModule.h"
#include "Editor/RawLayerTools.h"
#include "Project/RawNodeEditing.h"
#include "Persistence/ProjectStore.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Library/LibraryControls.h"
#include "App/WorkspacePresentation.h"
#include "Editor/NodeEditors/NodeEditorPanelPresentation.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Editor/NodeEditors/PhotographicPanelEditors.h"
#include "imgui_internal.h"
#include <unordered_set>
#include <cstdio>

using Stack::Project::NodeEditTarget;
using Stack::Project::kProjectGraphId;
using namespace Stack::Editor;

namespace {
// Scope hover windows at the host, preserving floating help and click popups.
class PanelHoverTextScope {
public:
    explicit PanelHoverTextScope(bool panel) : enabled(panel) {
        previous=GImGui->TooltipPreviousWindow;
        previousCount=previous ? previous->BeginCount : 0;
    }
    ~PanelHoverTextScope() {
        auto* tooltip=GImGui->TooltipPreviousWindow;
        if (enabled && tooltip && (tooltip!=previous || tooltip->BeginCount!=previousCount))
            ImGui::SetWindowHiddenAndSkipItemsForCurrentFrame(tooltip);
    }
private:
    bool enabled;
    ImGuiWindow* previous;
    int previousCount;
};
}


const EditorNodeGraph::Node* EditorModule::ResolveNodeEditorTarget(const NodeEditTarget& target) const {
    if (target.documentId!=GetProjectDocumentId()) return nullptr;
    if (target.layerId!=kProjectGraphId)
        return Stack::Project::ResolveRawNode(m_Project->rawLayers.State(),target,target.documentId);
    for (const auto& node:m_Project->graph.GetNodes())
        if (node.instanceUuid==target.nodeUuid) return &node;
    return nullptr;
}

bool EditorModule::BeginNodeEditorContext(const NodeEditTarget& target, RawWorkspaceEditContext& context) const {
    context={};
    const auto* node=ResolveNodeEditorTarget(target);
    if (!node) return false;
    context.nodeTarget=target;
    context.editorState=&m_NodeEditorState;
    context.recipe=Stack::RawRecipe::BuildWorkspaceSourceRecipe(m_Project->rawRecipe);
    context.resolvedMode=m_Project->rawMode;
    using Phase=Stack::Project::ProjectLifecyclePhase;
    const auto phase=m_Project->lifecycle.Phase();
    context.canEdit=phase!=Phase::Loading && phase!=Phase::Importing && phase!=Phase::Conflict &&
        phase!=Phase::ReadOnlyRecovery && (!m_Project->store || !m_Project->store->IsReadOnlyRecovery()) &&
        !IsWorkspaceTransitionPending() && !m_NodeOpenTransition.Active() && !Stack::Workspace::IsPreview();
    const auto fingerprint=NodeInputAnalysisIdentity(target,
        m_Project->editRevision+m_Project->rawLayers.ProcessingRevision(),CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame);
    const auto& analysis=m_NodeEditorState.Get<NodeEditorPanelAnalysis>(target);
    // Pixel revisions must not change the display mapping during an owned drag.
    if (analysis.acceptedFingerprint==fingerprint || m_NodeEditorPanel.gestureTarget==target) {
        context.inputAnalysis=analysis.readback;
        context.inputAnalysisRevision=analysis.revision;
    }
    BindRawOperationContext(context);
    // Fallback controls also edit a private node draft; the real owner is never
    // replaced with a temporary selected-node or canvas ID.
    if (!FindNodeEditor(*node).primary) {
        context.nodeDraft=*node;
        if(node->kind==EditorNodeGraph::NodeKind::Layer)context.nodeSettings=node->layerParameters;
    }
    return true;
}

void EditorModule::BindNodeEditorPrimaryState(const NodeEditTarget& target) {
    if (m_NodePrimaryUiTarget==target) return;
    auto& live=static_cast<Stack::EditorModuleTypes::RawNodePrimaryUiState&>(m_RawWorkspaceLabUi);
    const bool liveCloud=live.colorWarpLiveCloud;
    live.EndInteraction();
    if (m_NodePrimaryUiTarget && m_NodePrimaryUiTarget->documentId==target.documentId)
        std::swap(live,m_NodeEditorState.Get<Stack::EditorModuleTypes::RawNodePrimaryUiState>(*m_NodePrimaryUiTarget));
    else live={};
    std::swap(live,m_NodeEditorState.Get<Stack::EditorModuleTypes::RawNodePrimaryUiState>(target));
    live.colorWarpLiveCloud=liveCloud;
    m_NodePrimaryUiTarget=target;
}

NodeEditorDrawResult EditorModule::DrawNodeEditorPrimary(RawWorkspaceEditContext& context) {
    PanelHoverTextScope hoverText(context.presentation==NodeEditorPresentation::Panel);
    NodeEditorDrawResult result;
    if (!context.nodeTarget) return result;
    const auto* node=ResolveNodeEditorTarget(*context.nodeTarget);
    if (!node) return result;
    const auto& definition=FindNodeEditor(*node);
    const bool selected=m_Project->nodeEditor==context.nodeTarget;
    if (selected) BindNodeEditorPrimaryState(*context.nodeTarget);
    auto& live=static_cast<Stack::EditorModuleTypes::RawNodePrimaryUiState&>(m_RawWorkspaceLabUi);
    auto* borrowed=selected ? nullptr : &m_NodeEditorState.Get<Stack::EditorModuleTypes::RawNodePrimaryUiState>(*context.nodeTarget);
    const bool liveCloud=live.colorWarpLiveCloud;
    if (borrowed) std::swap(live,*borrowed);
    live.colorWarpLiveCloud=liveCloud;
    const auto previousTool=m_RawWorkspaceLabUi.activeTool;
    const int previousTone=m_RawWorkspaceLabUi.sceneToneView;
    auto* previousContext=m_NodeEditorDrawingContext;
    m_NodeEditorDrawingContext=&context;
    m_RawWorkspaceLabUi.activeTool=definition.legacyTool.value_or(RawLabTool::GraphNode);
    if (definition.toneView>=0) m_RawWorkspaceLabUi.sceneToneView=definition.toneView;
    const auto* window=ImGui::GetCurrentWindow();
    const auto oldActive=ImGui::GetActiveID();
    const bool panel=context.presentation==NodeEditorPresentation::Panel && definition.panelPrimary;
    if(panel)ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,2.f*ImGui::GetFontSize()/13.f});
    ImGui::PushID(context.nodeTarget->Key().c_str());
    ImGui::BeginDisabled(!context.canEdit);
    const auto primary=panel ? definition.panelPrimary : definition.primary;
    if (primary) result.changed=primary(*this,context);
    else if (context.nodeDraft) {
        const auto* layer=Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),context.nodeTarget->layerId);
        auto& graph=context.nodeTarget->layerId==kProjectGraphId ? m_Project->graph :
            const_cast<EditorNodeGraph::Graph&>(layer->graph);
        m_Sidebar.GetNodeGraphUI().RenderOperationDraftControls(this,graph,*context.nodeDraft);
        result.changed=context.draftChanged;
    }
    if(panel) {
        auto& state=context.State<PanelControlState>();
        const auto settings=definition.panelSettings ? definition.panelSettings : definition.settings;
        if(settings) {
            ImGui::Spacing();
            if(Stack::Editor::RawLabInternal::BareTextButton(state.settingsOpen ? "v  Settings" : ">  Settings"))
                state.settingsOpen=!state.settingsOpen;
            if(state.settingsOpen) {
                ImGui::PushID("PanelSettings");
                result.changed|=settings(*this,context);
                ImGui::PopID();
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    if(panel)ImGui::PopStyleVar();
    auto& gui=*ImGui::GetCurrentContext();
    result.interactionActive=gui.ActiveId && gui.ActiveIdWindow &&
        ImGui::IsWindowChildOf(gui.ActiveIdWindow,const_cast<ImGuiWindow*>(window),true,true);
    result.activated=result.changed || (result.interactionActive && oldActive!=gui.ActiveId);
    m_NodeEditorDrawingContext=previousContext;
    m_RawWorkspaceLabUi.activeTool=previousTool;
    m_RawWorkspaceLabUi.sceneToneView=previousTone;
    const bool editedLiveCloud=live.colorWarpLiveCloud;
    if (borrowed) std::swap(live,*borrowed);
    live.colorWarpLiveCloud=editedLiveCloud;
    return result;
}

void EditorModule::RefreshNodeEditorPanel() {
    auto& panel=m_NodeEditorPanel;
    const auto document=GetProjectDocumentId();
    // Compare a small owner/revision list, never hash node payloads in drawing.
    const auto& state=m_Project->rawLayers.State();
    // Ordinary documents can retain unused RAW defaults. Match the renderer's
    // lowering rule so these never masquerade as connected editing controls.
    const bool ownsRawLayers=IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive();
    const std::size_t ownerCount=ownsRawLayers?state.layers.size()+2:1;
    bool current=panel.document==document && panel.structure.size()==ownerCount;
    std::size_t index=0;
    const auto matches=[&](const std::string& id,const EditorNodeGraph::Graph& graph) {
        if (current && (panel.structure[index].first!=id ||
            panel.structure[index].second!=graph.GetStructureRevision())) current=false;
        ++index;
    };
    if(ownsRawLayers) {
        matches(state.background.id,state.background.graph);
        for (const auto& layer:state.layers) matches(layer.id,layer.graph);
    }
    matches(kProjectGraphId,m_Project->graph);
    if (current) {
        if (m_Project->nodeEditor && panel.lastTarget!=m_Project->nodeEditor) {
            for (auto& group:panel.groups) for (const auto& instance:group.instances)
                if (instance.target==*m_Project->nodeEditor) {
                    group.selectedKey=instance.key;group.measuredHeight=0.f;
                    panel.lastTarget=m_Project->nodeEditor;
                }
        }
        return;
    }
    if (panel.document!=document) {
        const bool visible=panel.visible;
        panel={};panel.document=document;panel.visible=visible;
        m_NodePrimaryUiTarget.reset();
    }
    auto old=std::move(panel.groups);
    panel.structure.clear();panel.groups.clear();
    for(const auto* definition:PhotographicPanelDefinitions()) {
        NodeEditorPanelGroup group;group.definitionId=definition->definitionId;group.label=definition->panelLabel;
        const auto previous=std::find_if(old.begin(),old.end(),[&](const auto& value){return value.definitionId==group.definitionId;});
        if(previous!=old.end()) {group=*previous;group.instances.clear();}
        panel.groups.push_back(std::move(group));
    }
    const auto append=[&](const std::string& owner,const EditorNodeGraph::Graph& graph) {
        panel.structure.emplace_back(owner,graph.GetStructureRevision());
        // Traverse the image ancestry, excluding coverage/parameter branches.
        // This catalog is rebuilt only when graph structure changes.
        std::vector<const EditorNodeGraph::Node*> ordered;
        std::unordered_set<int> visited;
        const auto visit=[&](const auto& self,int id)->void {
            if (!visited.insert(id).second) return;
            const auto* node=graph.FindNode(id);
            if (!node) return;
            for (const auto& link:graph.GetLinks()) if (link.toNodeId==id) {
                EditorNodeGraph::SocketDefinition socket;
                if (graph.FindSocket(id,link.toSocketId,&socket) &&
                    (socket.type==EditorNodeGraph::SocketType::Image || socket.type==EditorNodeGraph::SocketType::ImageOrChannel || socket.type==EditorNodeGraph::SocketType::Raw))
                    self(self,link.fromNodeId);
            }
            ordered.push_back(node);
        };
        for (int output:graph.GetOutputNodeIds()) {
            const auto* node=graph.FindNode(output);
            if (node && !node->outputSettings.maskOutput) visit(visit,output);
        }
        for (const auto* node:ordered) {
            if (node->role!=Stack::GraphModel::NodeRole::Ordinary) continue;
            const auto resolved=EditorNodeGraphDefinitions::ResolveNodeDefinition(graph,*node);
            if (!resolved || !resolved.Identity()) continue;
            const auto& editor=FindNodeEditor(*node);
            // Serialized payload/inspection metadata is not an editable tool.
            const bool fallback=resolved.compound ? resolved.ParameterCount()>0 :
                resolved.builtin && std::any_of(resolved.builtin->parameters.begin(),resolved.builtin->parameters.end(),
                    [](const auto& parameter) {
                        return parameter.storageKey!="node-payload" && parameter.id!="inspection";
                    });
            if (!editor.primary && !fallback) continue;
            const auto id=resolved.Identity()->id;
            auto group=std::find_if(panel.groups.begin(),panel.groups.end(),[&](const auto& value){return value.definitionId==id;});
            if (group==panel.groups.end()) {
                NodeEditorPanelGroup item;item.definitionId=id;
                item.label=resolved.builtin ? EditorNodeGraph::NodeDisplayLabel(node->kind, resolved.builtin->label) : node->title;
                const auto previous=std::find_if(old.begin(),old.end(),[&](const auto& value){return value.definitionId==id;});
                if (previous!=old.end()) {
                    item.selectedKey=previous->selectedKey;
                    item.measuredHeight=previous->measuredHeight;
                    item.measuredWidth=previous->measuredWidth;
                    item.measuredFont=previous->measuredFont;
                }
                panel.groups.push_back(std::move(item));group=panel.groups.end()-1;
            }
            NodeEditTarget target{document,owner,node->instanceUuid};
            group->instances.push_back({target,target.Key(),std::to_string(group->instances.size()+1)});
        }
    };
    if(ownsRawLayers) {
        append(state.background.id,state.background.graph);
        for (const auto& layer:state.layers) append(layer.id,layer.graph);
    }
    append(kProjectGraphId,m_Project->graph);
    for (auto& group:panel.groups) {
        if(group.instances.empty()) {group.selectedKey.clear();continue;}
        const auto chosen=std::find_if(group.instances.begin(),group.instances.end(),[&](const auto& value){return value.key==group.selectedKey;});
        if (chosen==group.instances.end()) {
            const auto active=std::find_if(group.instances.begin(),group.instances.end(),[&](const auto& value){return m_Project->nodeEditor==value.target;});
            group.selectedKey=(active!=group.instances.end()?active:group.instances.begin())->key;
        }
    }
    std::unordered_set<std::string> live;
    for (const auto& group:panel.groups) for (const auto& instance:group.instances) live.insert(instance.key);
    for (const auto& group:old) for (const auto& instance:group.instances)
        if (!live.count(instance.key)) m_NodeEditorState.Get<NodeEditorPanelAnalysis>(instance.target)={};
    if (panel.lastTarget && !ResolveNodeEditorTarget(*panel.lastTarget)) panel.lastTarget.reset();
    if(panel.focusTarget && !live.count(panel.focusTarget->Key()))ExitFocusedNodeEditor();
}

bool EditorModule::ActivateNodeEditorPanelTarget(const NodeEditTarget& target) {
    if (m_NodeOpenTransition.Active() || !ResolveNodeEditorTarget(target) || target.documentId!=GetProjectDocumentId()) return false;
    if (m_Project->nodeEditor!=target) {
        // Finish the outgoing gesture before changing its stable edit identity.
        m_Project->rawLayers.EndGesture();m_Project->graphHistory.EndGesture();
        if (target.layerId==kProjectGraphId) {
            m_EditingRawLayerMask.reset();m_RawLayerMaskGenerator=-1;m_RawLayerMaskDrag=-1;
            m_Project->nodeEditor=target;
            const auto& definition=FindNodeEditor(*ResolveNodeEditorTarget(target));
            m_RawWorkspaceLabUi.activeTool=definition.legacyTool.value_or(RawLabTool::GraphNode);
            if (definition.toneView>=0) m_RawWorkspaceLabUi.sceneToneView=definition.toneView;
        } else SelectRawNodeEditor(target.layerId,target.nodeUuid);
        BindNodeEditorPrimaryState(target);
    }
    if(m_NodeEditorPanel.lastTarget!=target) {
        for(auto& group:m_NodeEditorPanel.groups)for(const auto& instance:group.instances)
            if(instance.target==target && group.selectedKey!=instance.key) {
                group.selectedKey=instance.key;group.measuredHeight=0.f;
            }
        m_NodeEditorPanel.lastTarget=target;
    }
    return true;
}

void EditorModule::RenderNodeEditorPanel() {
    RefreshNodeEditorPanel();
    if (m_NodeEditorPanel.groups.empty()) { ImGui::TextWrapped("This image has no editable graph tools.");return; }
    std::optional<NodeEditTarget> nextAnalysis;
    bool queuedAnalysisVisible=!m_NodeEditorPanel.analysisTarget;
    bool firstSection=true;
    for (auto& group:m_NodeEditorPanel.groups) {
        const auto* panelDefinition=FindPhotographicPanelEditor(group.definitionId);
        if(!panelDefinition)continue;
        if (!firstSection) {
            ImGui::Dummy(ImVec2(0,2.f));
        }
        firstSection=false;
        ImGui::PushID(group.definitionId.c_str());
        const float width=ImGui::GetContentRegionAvail().x;
        const auto selected=std::find_if(group.instances.begin(),group.instances.end(),
            [&](const auto& value){return value.key==group.selectedKey;});
        if (selected==group.instances.end()) {
            ImGui::TextDisabled("%s",group.label.c_str());DrawMissingPanelControls(panelDefinition->panelOrder);
            ImGui::PopID();continue;
        }
        auto target=selected->target;
        const auto* node=ResolveNodeEditorTarget(target);
        using Kind=EditorNodeGraph::NodeKind;
        const auto enabledState=[](const EditorNodeGraph::Node* n)->std::optional<bool> {
            if (!n) return {};
            if (n->kind==Kind::RawOperation) return n->rawOperation.enabled;
            if (n->kind==Kind::Layer) return n->layerParameters.value("enabled",true);
            if (n->kind==Kind::MaskUtility && n->maskUtilityKind==EditorNodeGraph::MaskUtilityKind::Invert)
                return n->maskUtilitySettings.enabled;
            return {};
        };
        auto enabled=enabledState(node);
        bool toggle=false;
        const auto headingMinimum=ImGui::GetCursorScreenPos();
        const int activated=DrawControlsPanelHeader(group,width,enabled ? &*enabled : nullptr,toggle);
        const auto headingMaximum=ImGui::GetItemRectMax();
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) &&
            ImGui::GetIO().MousePos.y>=headingMinimum.y && ImGui::GetIO().MousePos.y<=headingMaximum.y && !ImGui::IsAnyItemActive())
            EnterFocusedNodeEditor(target);
        if (toggle && enabled && FinishWorkspaceInteraction()) {
            RawWorkspaceEditContext edit;
            if (BeginNodeEditorContext(target,edit) && edit.canEdit) {
                node=ResolveNodeEditorTarget(target);
                if (node) {
                    edit.nodeDraft=*node;
                    if (node->kind==Kind::Layer) {
                        edit.nodeSettings=node->layerParameters;
                        edit.nodeSettings["enabled"]=!*enabled;
                    } else if (node->kind==Kind::RawOperation) edit.nodeDraft->rawOperation.enabled=!*enabled;
                    else edit.nodeDraft->maskUtilitySettings.enabled=!*enabled;
                    CommitRawWorkspaceEditContext(edit,true,false);
                }
            }
            node=ResolveNodeEditorTarget(target);
            enabled=enabledState(node);
        }
        if (activated>=0 && activated<static_cast<int>(group.instances.size())) {
            const auto& instance=group.instances[activated];
            if (FinishWorkspaceInteraction() && ActivateNodeEditorPanelTarget(instance.target)) {
                if (group.selectedKey!=instance.key) {
                    m_NodeEditorState.Get<NodeEditorPanelAnalysis>(target)={};
                    group.selectedKey=instance.key;group.measuredHeight=0.f;
                }
                target=instance.target;
            }
            node=ResolveNodeEditorTarget(target);
            enabled=enabledState(node);
        }
        const auto* definition=node ? &FindNodeEditor(*node) : nullptr;
        if (definition) {
            const float font=ImGui::GetFontSize();
            const bool measured=group.measuredHeight>0 && group.measuredWidth==width && group.measuredFont==font;
            // Separate the drawing budget from the measured outer slot. Feeding
            // last frame's extent back into available-space-based plots drifts.
            const float layoutHeight=definition->panelHeight*ImGui::GetFontSize()/13.f;
            // Width changes invalidate measurement, not the last useful slot.
            // Falling back to the preferred height on every drag frame makes
            // all following sections jump between estimated and actual sizes.
            const float estimated=group.measuredHeight>0 && group.measuredFont==font
                ? group.measuredHeight : layoutHeight;
            const auto origin=ImGui::GetCursorScreenPos();
            const auto* parent=ImGui::GetCurrentWindow();
            const bool onScreen=origin.y+estimated>=parent->ClipRect.Min.y && origin.y<=parent->ClipRect.Max.y;
            const bool ownsGesture=m_NodeEditorPanel.gestureTarget==target;
            if (onScreen && enabled.value_or(true) && m_NodeEditorPanel.analysisTarget==target) queuedAnalysisVisible=true;
            if (onScreen || ownsGesture || !measured) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
                ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,0.f);
                ImGui::SetNextWindowContentSize(ImVec2(0,layoutHeight));
                ImGui::SetNextWindowScroll(ImVec2(0,0));
                const bool visible=ImGui::BeginChild("Primary",ImVec2(width,estimated),ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
                auto* primary=ImGui::GetCurrentWindow();
                const bool skipItems=primary->SkipItems;
                // Submit the captured item until release even outside the body;
                // its drawing remains clipped to the real panel.
                if (ownsGesture) primary->SkipItems=false;
                if (visible || ownsGesture) {
                    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        ActivateNodeEditorPanelTarget(target);
                    RawWorkspaceEditContext context;
                    if (BeginNodeEditorContext(target,context)) {
                        context.presentation=NodeEditorPresentation::Panel;
                        context.canEdit &= enabled.value_or(true);
                        const bool needsAnalysis=definition->panelAnalysis!=RawDevelopmentGraphScopeStage::None ||
                            (context.State<PanelControlState>().settingsOpen && definition->inputAnalysis!=RawDevelopmentGraphScopeStage::None);
                        if (onScreen && context.canEdit && needsAnalysis && !nextAnalysis) {
                            const auto identity=NodeInputAnalysisIdentity(target,
                                m_Project->editRevision+m_Project->rawLayers.ProcessingRevision(),CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame);
                            const auto& analysis=m_NodeEditorState.Get<NodeEditorPanelAnalysis>(target);
                            if (analysis.acceptedFingerprint!=identity && analysis.attemptedFingerprint!=identity)
                                nextAnalysis=target;
                        }
                        const auto result=DrawNodeEditorPrimary(context);
                        if(context.canEdit && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) &&
                            !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))
                            EnterFocusedNodeEditor(target);
                        if (result.activated) ActivateNodeEditorPanelTarget(target);
                        if (result.changed || ownsGesture || result.interactionActive) {
                            CommitRawWorkspaceEditContext(context,result.changed,result.interactionActive);
                            m_NodeEditorPanel.gestureTarget=result.interactionActive ? std::optional<NodeEditTarget>(target) : std::nullopt;
                        }
                        if (!context.error.empty()) ImGui::TextWrapped("%s",context.error.c_str());
                    }
                    if (!measured || !m_NodeEditorPanel.gestureTarget) {
                        group.measuredHeight=std::max(24.f,ImGui::GetCursorPosY());
                        group.measuredWidth=width;group.measuredFont=font;
                    }
                }
                // EndChild advances the parent using this frame's measured
                // extent, so lower sections never inherit a stale height.
                if (group.measuredHeight>0 && group.measuredWidth==width && group.measuredFont==font)
                    ImGui::SetWindowSize(ImVec2(width,group.measuredHeight));
                primary->SkipItems=skipItems;
                ImGui::EndChild();ImGui::PopStyleVar(2);
            } else ImGui::Dummy(ImVec2(width,estimated));
        }
        ImGui::Dummy(ImVec2(0,4.f));
        ImGui::PopID();
    }
    if (!queuedAnalysisVisible && !m_RawWorkspaceAnalysisRequested) {
        m_NodeEditorPanel.analysisTarget.reset();m_RawWorkspaceAnalysisPending=false;
    }
    if (nextAnalysis && !m_RawWorkspaceAnalysisRequested && !m_RawWorkspaceAnalysisPending &&
        !m_NodeEditorPanel.analysisTarget && !ImGui::IsAnyItemActive()) {
        m_NodeEditorPanel.analysisTarget=nextAnalysis;
        m_RawWorkspaceAnalysisPending=true;
        m_RawWorkspaceAnalysisScopeRetryCount=0;
        m_RawWorkspaceAnalysisQuietUntilTime=ImGui::GetTime()+.1;
    }

}

bool EditorModule::ViewSectionPanelOpen() const { return m_ViewSectionOpen; }

void EditorModule::SetViewSectionPanelOpen(bool open) {
    if (m_ViewSectionOpen==open || Stack::Workspace::IsPreview()) return;
    if (!FinishWorkspaceInteraction()) return;
    m_ViewSectionOpen=open;
    m_Project->workspace.viewPanelOpen=open;
    UpdateNodeEditorPanelVisibility();
    SaveRawWorkspaceAppState();
}

void EditorModule::SetViewSectionTab(int tab) {
    tab=std::clamp(tab,0,3);
    if (tab==m_ViewSectionTab || Stack::Workspace::IsPreview()) return;
    if (!FinishWorkspaceInteraction()) return;
    m_ViewSectionTab=tab;
    m_RawWorkspaceLabUi.settingsTabActive=tab==2;
    m_RawAssetCapture.assetsTab=tab==3;
    UpdateNodeEditorPanelVisibility();
    SaveRawWorkspaceAppState();
}

void EditorModule::UpdateNodeEditorPanelVisibility() {
    const bool visible=m_ViewSectionTab==1 && m_ViewSectionOpen &&
        !IsFocusedNodeEditor() &&
        !m_PermanentGalleryWorkspace && !m_RawWorkspaceLabUi.galleryWorkspaceOpen &&
        IsViewMode() && !IsBracketingPresentationActive();
    if (visible==m_NodeEditorPanel.visible) return;
    m_NodeEditorPanel.visible=visible;
    m_Project->rawLayers.EndGesture();m_Project->graphHistory.EndGesture();
    if (m_NodeEditorPanel.gestureTarget) m_NodeEditorState.EndInteraction(*m_NodeEditorPanel.gestureTarget);
    if (m_Project->nodeEditor) m_NodeEditorState.EndInteraction(*m_Project->nodeEditor);
    static_cast<Stack::EditorModuleTypes::RawNodePrimaryUiState&>(m_RawWorkspaceLabUi).EndInteraction();
    auto& gui=*ImGui::GetCurrentContext();
    if (gui.ActiveId && gui.ActiveIdWindow) {
        char floating[80],section[80];
        std::snprintf(floating,sizeof(floating),"##RawFloatingControls_%p",static_cast<void*>(this));
        std::snprintf(section,sizeof(section),"##RawWorkspaceSection_%p",static_cast<void*>(this));
        auto* oldHost=ImGui::FindWindowByName(visible ? floating : section);
        if (oldHost && ImGui::IsWindowChildOf(gui.ActiveIdWindow,oldHost,true,true)) ImGui::ClearActiveID();
    }
    m_NodeEditorPanel.gestureTarget.reset();
    if (!visible && m_NodeEditorPanel.analysisTarget) {
        if (!m_RawWorkspaceAnalysisRequested) m_RawWorkspaceAnalysisPending=false;
        m_NodeEditorPanel.analysisTarget.reset();
    }
    m_RawFloatingSurface.input={};m_RawFloatingSurface.frame=-2;
    m_RawActiveControlFrame=-2;
    if (!visible) {
        if (m_NodeEditorPanel.lastTarget) ActivateNodeEditorPanelTarget(*m_NodeEditorPanel.lastTarget);
        m_NodeEditorPanel.floatingRevealStarted=ImGui::GetTime();
    }
}
