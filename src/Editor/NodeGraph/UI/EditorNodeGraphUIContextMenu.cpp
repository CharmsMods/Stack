#include "Utils/UiBusyState.h"
#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "NodeMath/ChannelImageSemantics.h"
#include "Editor/NodeGraph/EditorNodeGraphSelectionExport.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Library/LibraryManager.h"
#include "Persistence/StackBinaryFormat.h"
#include "Presets/PresetManager.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/Utf8Text.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <utility>

namespace {

void PostNodeGraphContextNotification(
    EditorModule* editor,
    UiNotificationSeverity severity,
    const std::string& message,
    const char* dedupeKey) {
    if (!editor || message.empty()) {
        return;
    }
    editor->ShowUiNotification(severity, message, dedupeKey ? dedupeKey : "");
}

std::string DefaultPresetName(const EditorModule* editor) {
    if (!editor) {
        return "Node Preset";
    }
    const auto& selected = editor->GetNodeGraph().GetSelectedNodeIds();
    if (selected.size() == 1) {
        if (const EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(selected.front())) {
            return node->title.empty() ? "Node Preset" : node->title + " Preset";
        }
    }
    return std::to_string(selected.size()) + " Node Preset";
}

} // namespace

void EditorNodeGraphUI::RenderContextMenu(EditorModule* editor) {
    std::optional<std::pair<int, bool>> pendingDelete;
    std::function<void()> pendingNodeAction;
    if (m_OpenRenameProjectPopup) {
        ImGui::OpenPopup("Rename Project##EditorNodeGraph");
        m_OpenRenameProjectPopup = false;
    }
    if (m_OpenSavePresetPopup) {
        ImGui::OpenPopup("Save Preset##EditorNodeGraph");
        m_OpenSavePresetPopup = false;
    }

    float popupAlpha = 1.0f;
    if (m_ContextMenuFadeActive) {
        const float elapsed = static_cast<float>(ImGui::GetTime() - m_ContextMenuOpenedAt);
        const float appearT = std::clamp(elapsed / 0.11f, 0.0f, 1.0f);
        popupAlpha = ImGuiExtras::EaseOutCubic(appearT);
        if (appearT >= 1.0f) {
            m_ContextMenuFadeActive = false;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
    const bool contextMenuOpen = ImGui::BeginPopup("EditorNodeGraphContextMenu");

    if (contextMenuOpen && m_ContextTarget == ContextTarget::Node) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(m_ContextNodeId);
        if (node) {
            ImGui::TextDisabled("%s", node->title.c_str());
            const auto nodeSockets=editor->GetNodeGraph().GetSockets(*node,false);
            const bool hasImageOutput=std::any_of(nodeSockets.begin(),nodeSockets.end(),[](const auto& socket) {
                return socket.direction==EditorNodeGraph::SocketDirection::Output && socket.logicalType==Stack::NodeMath::LogicalValueType::ColorImage;
            });
            if(hasImageOutput && ImGui::BeginMenu("Channel outputs")) {
                for(const auto& socket:nodeSockets) {
                    if(socket.direction!=EditorNodeGraph::SocketDirection::Output ||
                        socket.logicalType!=Stack::NodeMath::LogicalValueType::ColorImage)continue;
                    const auto* described=editor->GetGraphOutputDescription(node->id,socket.id);
                    const bool image=!described || described->descriptor.logicalType==Stack::NodeMath::LogicalValueType::ColorImage;
                    ImGui::PushID(socket.id.c_str());
                    for(const auto& entry:std::initializer_list<std::pair<const char*,const char*>>{{"r","Red"},{"g","Green"},{"b","Blue"},{"a","Alpha"}}) {
                        bool available=image;
                        if(described && described->descriptor.presentImageComponents.state==Stack::NodeMath::KnowledgeState::Known) {
                            const auto component=Stack::NodeMath::ParseImageComponentToken(std::string(1,static_cast<char>(std::toupper(entry.first[0]))));
                            available &= component && Stack::NodeMath::HasImageComponent(described->descriptor.presentImageComponents.value,*component);
                        }
                        const auto id=EditorNodeGraph::ChannelOutputId(socket.id,entry.first);
                        const bool exists=editor->GetNodeGraph().FindSocket(node->id,id,nullptr);
                        const std::string label=socket.label+" / "+entry.second;
                        if(ImGui::MenuItem(label.c_str(),nullptr,exists,available && !exists))
                            pendingNodeAction=[this,editor,nodeId=node->id,base=socket.id,component=std::string(entry.first)] {
                                ExposeChannelSocket(editor,nodeId,base,component);
                            };
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            const bool protectedNode = node->role == Stack::GraphModel::NodeRole::OriginalImage ||
                node->role == Stack::GraphModel::NodeRole::CurrentImage || node->role == Stack::GraphModel::NodeRole::LayerResult;
            const bool canDeleteNode = !protectedNode && (node->kind != EditorNodeGraph::NodeKind::Output ||
                editor->GetNodeGraph().GetOutputNodeIds().size() > 1);
            if (canDeleteNode && ImGui::MenuItem("Delete Node")) {
                pendingDelete = {{node->id, true}};
            }
            if (canDeleteNode && ImGui::MenuItem("Delete and disconnect")) pendingDelete = {{node->id, false}};
            if (node->kind == EditorNodeGraph::NodeKind::RawOperation && ImGui::MenuItem("Edit photo controls"))
                editor->GetGraphEditorContext().inspectParameters(node->id);
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !protectedNode)) {
                if (!editor->GetNodeGraph().IsNodeSelected(node->id)) {
                    editor->GetNodeGraph().SelectNode(node->id, false);
                }
                pendingNodeAction = [this, editor] { DuplicateSelectedNodes(editor); };
            }
            if (ImGui::MenuItem("Save As Preset")) {
                if (!editor->GetNodeGraph().IsNodeSelected(node->id)) {
                    editor->GetNodeGraph().SelectNode(node->id, false);
                }
                std::string defaultName = DefaultPresetName(editor);
                Stack::Utf8Text::TruncateToByteLimit(defaultName, sizeof(m_SavePresetNameBuffer) - 1u);
                strncpy_s(m_SavePresetNameBuffer, defaultName.c_str(), sizeof(m_SavePresetNameBuffer) - 1);
                m_SavePresetNameBuffer[sizeof(m_SavePresetNameBuffer) - 1] = '\0';
                m_OpenSavePresetPopup = true;
            }
            if (node->kind == EditorNodeGraph::NodeKind::Image) {
                if (ImGui::BeginMenu("Rotate")) {
                    if (ImGui::MenuItem("90 CW")) {
                        editor->RotateImageNode(node->id, 1);
                    }
                    if (ImGui::MenuItem("90 CCW")) {
                        editor->RotateImageNode(node->id, -1);
                    }
                    if (ImGui::MenuItem("180")) {
                        editor->RotateImageNode(node->id, 2);
                    }
                    ImGui::EndMenu();
                }
            }
            const bool hasAdvancedEditor = editor->NodeHasDedicatedComplexEditor(node->id);
            if (editor->IsRawProjectCanvas()) {
                if (ImGui::MenuItem("Open in image editor")) {
                    pendingNodeAction = [editor, nodeId = node->id] { editor->OpenRawProjectNodeEditor(nodeId); };
                }
            } else if (hasAdvancedEditor) {
                const char* openEditorLabel = node->kind == EditorNodeGraph::NodeKind::Image
                    ? "Open Slice Settings"
                    : "Open Advanced Editor";
                if (ImGui::MenuItem(openEditorLabel)) {
                    editor->SwitchToComplexNodeSubWindow(node->id);
                }
            }
            if (editor->IsRawProjectCanvas()) {
                // Creative nodes remain visible and expanded in the project graph.
            } else if (node->kind == EditorNodeGraph::NodeKind::Layer && editor->LayerUsesRichNodeSurface(node->layerIndex)) {
                if (ImGui::MenuItem(node->expanded ? "Collapse Controls" : "Expand Controls")) {
                    node->expanded = !node->expanded;
                    if (!node->expanded) {
                        editor->CancelCanvasTool();
                        editor->ClearGraphAutoFocusIfTrackedNode(node->id);
                    }
                }
            } else if (ImGui::MenuItem(node->expanded ? "Collapse" : "Expand")) {
                node->expanded = !node->expanded;
                if (!node->expanded) {
                    editor->ClearGraphAutoFocusIfTrackedNode(node->id);
                }
            }
            if (const EditorNodeGraphDefinitions::LiveNodeDefinition* definition =
                    EditorNodeGraphDefinitions::FindLiveNodeDefinition(*node)) {
                const bool hasExposableParameter = std::any_of(
                    definition->parameters.begin(),
                    definition->parameters.end(),
                    [](const EditorNodeGraphDefinitions::LiveParameterDefinition& parameter) {
                        return parameter.graphInputCapable;
                    });
                if (hasExposableParameter && ImGui::BeginMenu("Expose as Input")) {
                    for (const EditorNodeGraphDefinitions::LiveParameterDefinition& parameter :
                         definition->parameters) {
                        if (!parameter.graphInputCapable) continue;
                        const bool exposed = std::find(
                            node->exposedParameterIds.begin(),
                            node->exposedParameterIds.end(),
                            parameter.id) != node->exposedParameterIds.end() ||
                            editor->GetNodeGraph().FindInputLink(node->id, EditorNodeGraph::ParameterInputSocketId(parameter.id));
                        if (ImGui::MenuItem(
                                parameter.label.c_str(), nullptr, exposed)) {
                            pendingNodeAction = [editor, nodeId = node->id, parameterId = parameter.id, exposed] {
                                editor->SetNodeParameterExposed(nodeId, parameterId, !exposed);
                            };
                        }
                    }
                    
                    ImGui::EndMenu();
                }
            }
            
            if (editor->CanUndoProjectGraphEdit() ||
                editor->CanRedoProjectGraphEdit()) {
                ImGui::Separator();
                if (ImGui::MenuItem(
                        "Undo", "Ctrl+Z", false,
                        editor->CanUndoProjectGraphEdit())) {
                    editor->UndoProjectGraphEdit();
                    ImGui::EndPopup();
                    ImGui::PopStyleVar();
                    return;
                }
                if (ImGui::MenuItem(
                        "Redo", "Ctrl+Y", false,
                        editor->CanRedoProjectGraphEdit())) {
                    editor->RedoProjectGraphEdit();
                    ImGui::EndPopup();
                    ImGui::PopStyleVar();
                    return;
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::Output) {
                bool outputUsesChannel = false;
                bool outputHasInput = false;
                if (const EditorNodeGraph::Link* input =
                        editor->GetNodeGraph().FindInputLink(
                            node->id,
                            EditorNodeGraph::kImageInputSocketId)) {
                    outputHasInput = true;
                    outputUsesChannel =
                        editor->GetNodeGraph().IsScalarSocketStream(
                            input->fromNodeId,
                            input->fromSocketId);
                }
                ImGui::BeginDisabled(
                    !editor->GetNodeGraph().IsOutputConnected() ||
                    outputUsesChannel ||
                    editor->IsExportBusy());
                if (ImGui::MenuItem("Export")) {
                    const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
                    if (!path.empty()) {
                        editor->RequestExportImage(path);
                    }
                }
                if (outputUsesChannel &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(
                        "PNG export requires an Image. Connect this Channel "
                        "to Image Combine first.");
                }
                ImGui::EndDisabled();
                std::string queueReason;
                const bool canQueueProject = editor->CanAddProjectOutputToQueue(node->id, &queueReason);
                const bool canQueueOutput = canQueueProject && outputHasInput && !outputUsesChannel;
                if (ImGui::MenuItem("Add to queue", nullptr, false, canQueueOutput)) {
                    pendingNodeAction = [editor, nodeId = node->id] { editor->AddProjectOutputToQueue(nodeId); };
                }
                if (!canQueueOutput && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", outputUsesChannel
                        ? "Queue export requires an Image. Connect this Channel to Image Combine first."
                        : !outputHasInput ? "Connect an image to this Output before adding it to Queue."
                        : queueReason.c_str());
                }
                if (ImGui::MenuItem(node->outputEnabled ? "Deactivate Output" : "Activate Output", "D")) {
                    pendingNodeAction = [editor, nodeId = node->id] { editor->ToggleOutputNodeEnabled(nodeId); };
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::ChannelSplit) {
                if (ImGui::MenuItem("Missing alpha is opaque",nullptr,node->splitMissingAlphaIsOpaque)) {
                    node->splitMissingAlphaIsOpaque=!node->splitMissingAlphaIsOpaque;
                    editor->MarkGraphEdited(node->id);
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::Layer) {
                if (ImGui::MenuItem("Split")) {
                    pendingNodeAction = [editor, nodeId = node->id] { editor->SplitLayerNodeIntoChannels(nodeId); };
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::RawSource) {
                if (ImGui::MenuItem("Add Manual RAW Chain")) {
                    pendingNodeAction = [editor, nodeId = node->id] { editor->AddFullRawTreeToSource(nodeId); };
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::RawDevelopment) {
                if (ImGui::MenuItem("Load Current RAW System")) {
                    pendingNodeAction = [editor] { editor->LoadActiveRawWorkspaceProjectInGraph(); };
                }
                if (ImGui::MenuItem("Edit In RAW Tab")) {
                    const std::string sourceKey = !node->rawDevelopment.recipe.source.relativePathKey.empty()
                        ? node->rawDevelopment.recipe.source.relativePathKey
                        : node->rawDevelopment.recipe.source.sourcePath;
                    if (!sourceKey.empty()) {
                        editor->SelectRawWorkspaceSourceForPreview(sourceKey);
                    }
                    editor->RequestOpenRawWorkspaceTab();
                }
                if (ImGui::MenuItem("Open Background graph")) {
                    pendingNodeAction = [editor] { editor->OpenRawLayerMaskGraph(Stack::Project::kRawBackgroundId); };
                }
            }
            for (const auto& group : editor->GetNodeGraph().GetGroups()) {
                if (!group.compoundBoundary.is_object() ||
                    std::find(group.compoundMembers.begin(),group.compoundMembers.end(),node->id)==group.compoundMembers.end()) continue;
                const std::string label="Repack edited "+group.title+"##"+std::to_string(group.id);
                if (ImGui::MenuItem(label.c_str())) {
                    const int groupId=group.id;
                    pendingNodeAction=[editor,groupId] {
                        std::string error;
                        if(!editor->RepackCompoundGroup(groupId,&error))
                            PostNodeGraphContextNotification(editor,UiNotificationSeverity::Error,error,"compound-repack");
                    };
                }
            }
            const auto decomposition=EditorNodeGraphDefinitions::GetNodeDecompositionCapability(editor->GetNodeGraph(),*node);
            ImGui::Separator();
            if (ImGui::MenuItem("Decompile one level",nullptr,false,decomposition.canDecompile)) {
                const int nodeId=node->id;
                pendingNodeAction=[this,editor,nodeId] {
                    std::string error;
                    if (!DecompileNode(editor,nodeId,&error))
                        PostNodeGraphContextNotification(editor,UiNotificationSeverity::Error,error,"node-decompile");
                };
            }
            if (!decomposition.canDecompile && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s",decomposition.reason.c_str());
            if (node->kind == EditorNodeGraph::NodeKind::Compound) {
                const int compoundNodeId = node->id;
                ImGui::Separator();
                if (ImGui::MenuItem("Make Unique")) {
                    std::string error;
                    if (!editor->MakeCompoundNodeUnique(compoundNodeId, &error)) {
                        PostNodeGraphContextNotification(editor, UiNotificationSeverity::Error,
                            error, "compound-make-unique");
                    }
                }
                if (ImGui::MenuItem("Edit As Unique Graph")) {
                    std::string error;
                    if (!editor->MakeCompoundNodeUnique(compoundNodeId, &error) ||
                        !DecompileNode(editor,compoundNodeId, &error)) {
                        PostNodeGraphContextNotification(editor, UiNotificationSeverity::Error,
                            error, "compound-edit-unique");
                    }
                }
                if (ImGui::MenuItem("Update To Newer Embedded Version")) {
                    std::string error;
                    if (!editor->UpdateCompoundNodeToLatestEmbeddedVersion(compoundNodeId, &error)) {
                        PostNodeGraphContextNotification(editor, UiNotificationSeverity::Warning,
                            error, "compound-update");
                    }
                }
            }
        }
        ImGui::EndPopup();
    }
    else if (contextMenuOpen && m_ContextTarget == ContextTarget::Group) {
        if (const auto* group = editor->GetNodeGraph().FindGroup(m_ContextGroupId)) {
            ImGui::TextDisabled("%s", group->title.c_str());
            if (group->compoundBoundary.is_object() && ImGui::MenuItem("Recompile")) {
                pendingNodeAction = [editor, groupId = group->id] {
                    std::string error;
                    if (!editor->RepackCompoundGroup(groupId, &error))
                        PostNodeGraphContextNotification(editor, UiNotificationSeverity::Error, error, "compound-repack");
                };
            }
        }
        ImGui::EndPopup();
    }
    else if (contextMenuOpen && m_ContextTarget == ContextTarget::Link) {
        if (ImGui::MenuItem("Delete Link")) {
            editor->RemoveGraphLink(m_ContextLink.fromNodeId, m_ContextLink.fromSocketId, m_ContextLink.toNodeId, m_ContextLink.toSocketId);
        }
        ImGui::EndPopup();
    }
    else if (contextMenuOpen) {
        const bool canRenameProject = editor &&
            (!editor->GetCurrentProjectName().empty() || !editor->GetCurrentProjectFileName().empty());
        const bool saveBusy = (editor && (editor->IsProjectFileSaveBusy() || editor->IsRawWorkspaceProjectSaveBusy()));

        Stack::UiActivity::BeginDisabledForWork(saveBusy, !canRenameProject);
        if (ImGui::MenuItem("Rename Project")) {
            std::string currentName = editor->GetCurrentProjectName().empty()
                ? "Untitled Project"
                : editor->GetCurrentProjectName();
            Stack::Utf8Text::TruncateToByteLimit(currentName, sizeof(m_RenameProjectBuffer) - 1u);
            strncpy_s(m_RenameProjectBuffer, currentName.c_str(), sizeof(m_RenameProjectBuffer) - 1);
            m_RenameProjectBuffer[sizeof(m_RenameProjectBuffer) - 1] = '\0';
            m_OpenRenameProjectPopup = true;
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        if (ImGui::BeginMenu("Add")) {
            if (ImGui::MenuItem("Import Slice")) {
                editor->RequestPromptAddImageNodeAt(m_ContextGraphPos);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Add Output")) {
                editor->AddOutputNodeAt(m_ContextGraphPos);
            }

            if (ImGui::MenuItem("Add Node...")) {
                OpenNodeBrowser(NodeBrowserMode::GeneralAdd, m_ContextGraphPos);
            }
            if (ImGui::BeginMenu("Scopes")) {
                if (ImGui::MenuItem("Histogram")) {
                    editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::Histogram, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Vectorscope")) {
                    editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::Vectorscope, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("RGB Parade")) {
                    editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::RGBParade, m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Analysis")) {
                if (ImGui::MenuItem("Preview")) {
                    editor->AddPreviewNodeAt(m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Mask")) {
                if (ImGui::MenuItem("Solid Mask")) {
                    editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::Solid, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Linear Gradient Mask")) {
                    editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::LinearGradient, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Radial Gradient Mask")) {
                    editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::RadialGradient, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Noise Mask")) {
                    editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::Noise, m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Mask / Math")) {
                if (ImGui::MenuItem("Add Mask")) {
                    editor->AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode::Add, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Subtract Mask")) {
                    editor->AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode::Subtract, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Intersect Mask")) {
                    editor->AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode::Intersect, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Difference Mask")) {
                    editor->AddMaskCombineNodeAt(EditorNodeGraph::MaskCombineMode::Exclude, m_ContextGraphPos);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Invert Mask")) {
                    editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Invert, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Remap Mask")) {
                    editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Levels, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Threshold Mask")) {
                    editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Threshold, m_ContextGraphPos);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clamp")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Clamp, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Add")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Add, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Subtract")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Subtract, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Multiply")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Multiply, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Divide")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Divide, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Average")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Average, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Minimum")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Min, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Maximum")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Max, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Difference")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Difference, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Remap")) {
                    editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::Remap, m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Average Images")) {
                editor->AddDataMathNodeAt(EditorNodeGraph::DataMathMode::ImageAverage, m_ContextGraphPos);
            }
            if (ImGui::BeginMenu("Generator")) {
                if (ImGui::MenuItem("Luminance Mask")) {
                    editor->AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind::Luminance, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Sampled Range Mask")) {
                    editor->AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind::SampledRange, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Solid Color Image")) {
                    editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::SolidColor, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Color Gradient Image")) {
                    editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::ColorGradient, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Square")) {
                    editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::Square, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Circle")) {
                    editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::Circle, m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Text")) {
                    editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::Text, m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Merge")) {
                if (ImGui::MenuItem("Blend Images")) {
                    editor->AddMixNodeAt(m_ContextGraphPos);
                }
                if (ImGui::MenuItem("HDR Merge")) {
                    editor->AddHdrMergeNodeAt(m_ContextGraphPos);
                }
                if (ImGui::MenuItem("MFSR")) {
                    editor->AddMfsrNodeAt(m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Image Operations")) {
                if (ImGui::MenuItem("LUT")) {
                    editor->AddLutNodeAt(m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Blend Images")) {
                    editor->AddMixNodeAt(m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Channels")) {
                if (ImGui::MenuItem("Channel Split")) {
                    editor->AddChannelSplitNodeAt(m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Image Combine")) {
                    editor->AddChannelCombineNodeAt(m_ContextGraphPos);
                }
                if (ImGui::MenuItem("Constant Channel")) {
                    editor->AddConstantChannelNodeAt(
                        m_ContextGraphPos);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Auto Layout")) {
                editor->AutoLayoutGraph();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Graph")) {
            const bool hasSelection = !editor->GetNodeGraph().GetSelectedNodeIds().empty();
            ImGui::BeginDisabled(!hasSelection);
            if (ImGui::MenuItem("Create Compound From Selection")) {
                std::string error;
                if (!editor->CreateCompoundFromSelection("Custom Compound", &error)) {
                    PostNodeGraphContextNotification(editor, UiNotificationSeverity::Error,
                        error, "compound-create");
                }
            }
            if (ImGui::MenuItem("Save Selection As Preset")) {
                std::string defaultName = DefaultPresetName(editor);
                Stack::Utf8Text::TruncateToByteLimit(defaultName, sizeof(m_SavePresetNameBuffer) - 1u);
                strncpy_s(m_SavePresetNameBuffer, defaultName.c_str(), sizeof(m_SavePresetNameBuffer) - 1);
                m_SavePresetNameBuffer[sizeof(m_SavePresetNameBuffer) - 1] = '\0';
                m_OpenSavePresetPopup = true;
            }
            ImGui::EndDisabled();

            if (ImGui::BeginMenu("Copy Graph Info")) {
                ImGui::BeginDisabled(!hasSelection);
                if (ImGui::MenuItem("Current Selection / Tree Only")) {
                    CopyGraphInfo(editor, false, false);
                }
                if (ImGui::MenuItem("Current Selection / Tree + State")) {
                    CopyGraphInfo(editor, false, true);
                }
                ImGui::EndDisabled();
                if (ImGui::MenuItem("Whole Graph / Tree Only")) {
                    CopyGraphInfo(editor, true, false);
                }
                if (ImGui::MenuItem("Whole Graph / Tree + State")) {
                    CopyGraphInfo(editor, true, true);
                }
                if (ImGui::MenuItem("Whole Graph / Ordered Structure (No Layout)")) {
                    CopyGraphInfo(editor, true, true, false);
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Paste Graph")) {
                PasteGraphInfo(editor);
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Preset##EditorNodeGraph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Preset name");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText("##SavePresetName", m_SavePresetNameBuffer, sizeof(m_SavePresetNameBuffer));
        ImGui::Spacing();

        const bool canSave = editor && !editor->GetNodeGraph().GetSelectedNodeIds().empty();
        ImGui::BeginDisabled(!canSave);
        if (ImGui::Button("Save Preset", ImVec2(130.0f, 0.0f))) {
            const auto selectedIds = editor->GetNodeGraph().GetSelectedNodeIds();
            auto exportResult = EditorNodeGraphSelectionExport::BuildExport(editor, selectedIds, true, false);
            std::vector<StackBinaryFormat::NodePresetBoundarySocket> boundarySockets;
            boundarySockets.reserve(exportResult.boundarySockets.size());
            for (const auto& socket : exportResult.boundarySockets) {
                StackBinaryFormat::NodePresetBoundarySocket out;
                out.nodeTitle = socket.nodeTitle;
                out.socketLabel = socket.socketLabel;
                out.direction = socket.direction;
                out.type = socket.type;
                boundarySockets.push_back(std::move(out));
            }

            std::string error;
            if (PresetManager::Get().SaveUserPreset(
                    m_SavePresetNameBuffer,
                    exportResult.clipboardPayload.value("payload", nlohmann::json::object()),
                    {},
                    boundarySockets,
                    exportResult.nodeCount,
                    &error)) {
                PostNodeGraphContextNotification(editor, UiNotificationSeverity::Success, "Preset saved.", "editor-node-graph-preset");
                editor->SwitchToSubWindow(EditorModule::EditorSubWindow::Presets);
                ImGui::CloseCurrentPopup();
            } else {
                PostNodeGraphContextNotification(
                    editor,
                    UiNotificationSeverity::Error,
                    error.empty() ? "Preset save failed." : error,
                    "editor-node-graph-preset");
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (!ImGui::IsPopupOpen("EditorNodeGraphContextMenu")) {
        m_ContextMenuFadeActive = false;
    }
    ImGui::PopStyleVar();
    if (pendingDelete) editor->RemoveGraphNode(pendingDelete->first, pendingDelete->second);
    // Graph commands may replace node storage. Finish all reads of the menu's
    // node before applying an action that invalidates it.
    if (pendingNodeAction) pendingNodeAction();

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Rename Project##EditorNodeGraph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Project name");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##RenameProjectName", m_RenameProjectBuffer, sizeof(m_RenameProjectBuffer));
        ImGui::Spacing();

        if (ImGui::Button("Rename", ImVec2(120.0f, 0.0f))) {
            std::string newName = m_RenameProjectBuffer;
            if (newName.empty()) {
                newName = "Untitled Project";
            }
            editor->RequestSaveCurrentProject(newName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
