#include "Utils/UiBusyState.h"
#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphSelectionExport.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Library/LibraryManager.h"
#include "Persistence/StackBinaryFormat.h"
#include "Presets/PresetManager.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <algorithm>
#include <cstring>
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
                DuplicateSelectedNodes(editor);
            }
            if (ImGui::MenuItem("Save As Preset")) {
                if (!editor->GetNodeGraph().IsNodeSelected(node->id)) {
                    editor->GetNodeGraph().SelectNode(node->id, false);
                }
                const std::string defaultName = DefaultPresetName(editor);
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
            if (hasAdvancedEditor) {
                const char* openEditorLabel = node->kind == EditorNodeGraph::NodeKind::Image
                    ? "Open Slice Settings"
                    : "Open Advanced Editor";
                if (ImGui::MenuItem(openEditorLabel)) {
                    editor->SwitchToComplexNodeSubWindow(node->id);
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::Layer && editor->LayerUsesRichNodeSurface(node->layerIndex)) {
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
                            parameter.id) != node->exposedParameterIds.end();
                        if (ImGui::MenuItem(
                                parameter.label.c_str(), nullptr, exposed)) {
                            editor->SetFrequencyParameterExposed(
                                node->id, parameter.id, !exposed);
                        }
                    }
                    if (node->kind ==
                        EditorNodeGraph::NodeKind::FrequencyResponse) {
                        for (std::size_t notchIndex = 0;
                             notchIndex <
                                 node->frequencyResponseSettings.notches.size();
                             ++notchIndex) {
                            const EditorNodeGraph::FrequencyNotch& notch =
                                node->frequencyResponseSettings.notches[
                                    notchIndex];
                            for (const auto& field : {
                                     std::pair<const char*, const char*>{
                                         "frequency", "Frequency" },
                                     { "direction", "Direction" },
                                     { "width", "Width" } }) {
                                const std::string parameterId =
                                    EditorNodeGraph::FrequencyNotchParameterId(
                                        notch.id, field.first);
                                const bool exposed = std::find(
                                    node->exposedParameterIds.begin(),
                                    node->exposedParameterIds.end(),
                                    parameterId) !=
                                    node->exposedParameterIds.end();
                                const std::string label =
                                    "Notch " +
                                    std::to_string(notchIndex + 1) + " " +
                                    field.second;
                                if (ImGui::MenuItem(
                                        label.c_str(), nullptr, exposed)) {
                                    editor->SetFrequencyParameterExposed(
                                        node->id, parameterId, !exposed);
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::FrequencyFilter) {
                const bool hasResponse = editor->GetNodeGraph().FindAnyInputLink(
                    node->id,
                    EditorNodeGraph::kFrequencyResponseInputSocketId) != nullptr;
                ImGui::BeginDisabled(hasResponse);
                if (ImGui::MenuItem("Extract Response Node")) {
                    std::string error;
                    if (!editor->ExtractFrequencyResponseNode(node->id, &error)) {
                        PostNodeGraphContextNotification(
                            editor, UiNotificationSeverity::Error,
                            error, "frequency-extract-response");
                    } else {
                        node = editor->GetNodeGraph().FindNode(m_ContextNodeId);
                    }
                }
                ImGui::EndDisabled();
                if (ImGui::MenuItem("Expand to Advanced Nodes")) {
                    std::string error;
                    if (!editor->ExpandFrequencyFilterNode(node->id, &error)) {
                        PostNodeGraphContextNotification(
                            editor, UiNotificationSeverity::Error,
                            error, "frequency-expand-advanced");
                    } else {
                        ImGui::EndPopup();
                        ImGui::PopStyleVar();
                        return;
                    }
                }
            }
            if (editor->CanUndoFrequencyGraphAction() ||
                editor->CanRedoFrequencyGraphAction()) {
                ImGui::Separator();
                if (ImGui::MenuItem(
                        "Undo Frequency Action", "Ctrl+Z", false,
                        editor->CanUndoFrequencyGraphAction())) {
                    editor->UndoFrequencyGraphAction();
                    ImGui::EndPopup();
                    ImGui::PopStyleVar();
                    return;
                }
                if (ImGui::MenuItem(
                        "Redo Frequency Action", "Ctrl+Y", false,
                        editor->CanRedoFrequencyGraphAction())) {
                    editor->RedoFrequencyGraphAction();
                    ImGui::EndPopup();
                    ImGui::PopStyleVar();
                    return;
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::Output) {
                bool outputUsesChannel = false;
                if (const EditorNodeGraph::Link* input =
                        editor->GetNodeGraph().FindInputLink(
                            node->id,
                            EditorNodeGraph::kImageInputSocketId)) {
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
                if (ImGui::MenuItem(node->outputEnabled ? "Deactivate Output" : "Activate Output", "D")) {
                    editor->ToggleOutputNodeEnabled(node->id);
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::Layer) {
                if (ImGui::MenuItem("Split")) {
                    editor->SplitLayerNodeIntoChannels(node->id);
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::RawSource) {
                if (ImGui::MenuItem("Add Manual RAW Chain")) {
                    editor->AddFullRawTreeToSource(node->id);
                }
            }
            if (node->kind == EditorNodeGraph::NodeKind::RawDevelopment) {
                if (ImGui::MenuItem("Load Current RAW System")) {
                    editor->LoadActiveRawWorkspaceProjectInGraph();
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
                    editor->OpenRawLayerMaskGraph(Stack::Project::kRawBackgroundId);
                }
            }
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
                if (ImGui::MenuItem("Unpack")) {
                    std::string error;
                    if (!editor->UnpackCompoundNode(compoundNodeId, &error)) {
                        PostNodeGraphContextNotification(editor, UiNotificationSeverity::Error,
                            error, "compound-unpack");
                    }
                }
                if (ImGui::MenuItem("Edit As Unique Graph")) {
                    std::string error;
                    if (!editor->MakeCompoundNodeUnique(compoundNodeId, &error) ||
                        !editor->UnpackCompoundNode(compoundNodeId, &error)) {
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
            const std::string currentName = editor->GetCurrentProjectName().empty()
                ? "Untitled Project"
                : editor->GetCurrentProjectName();
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
                if (ImGui::MenuItem("Custom Mask")) {
                    editor->AddCustomMaskNodeAt(m_ContextGraphPos);
                }
                ImGui::Separator();
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
                const std::string defaultName = DefaultPresetName(editor);
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
