#include "EditorSidebar.h"

#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Library/LibraryManager.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/FileDialogs.h"
#include <imgui.h>
#include <algorithm>

namespace {
    const char* ExportAspectPresetLabel(EditorModule::CompositeExportAspectPreset preset) {
        switch (preset) {
            case EditorModule::CompositeExportAspectPreset::Ratio4x3: return "4:3";
            case EditorModule::CompositeExportAspectPreset::Ratio3x2: return "3:2";
            case EditorModule::CompositeExportAspectPreset::Ratio16x9: return "16:9";
            case EditorModule::CompositeExportAspectPreset::Ratio9x16: return "9:16";
            case EditorModule::CompositeExportAspectPreset::Ratio2x3: return "2:3";
            case EditorModule::CompositeExportAspectPreset::Ratio5x4: return "5:4";
            case EditorModule::CompositeExportAspectPreset::Ratio21x9: return "21:9";
            case EditorModule::CompositeExportAspectPreset::Custom: return "Custom";
            case EditorModule::CompositeExportAspectPreset::Ratio1x1:
            default:
                return "1:1";
        }
    }

    const char* ChannelDisplayName(const std::string& channel) {
        if (channel == "r") return "R";
        if (channel == "g") return "G";
        if (channel == "b") return "B";
        if (channel == "a") return "A";
        return "";
    }

    ImVec4 ChannelPolicyColor(LayerChannelPolicy policy) {
        switch (policy) {
            case LayerChannelPolicy::ChannelSafe:
                return ImVec4(0.42f, 0.72f, 0.56f, 1.0f);
            case LayerChannelPolicy::ChannelUsefulWithWarning:
                return ImVec4(0.88f, 0.70f, 0.34f, 1.0f);
            case LayerChannelPolicy::FullImagePreferred:
            case LayerChannelPolicy::FullImageOnly:
            case LayerChannelPolicy::ReworkBeforeExpose:
                return ImVec4(0.95f, 0.55f, 0.38f, 1.0f);
        }
        return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    }

    void RenderLayerMetadataPanel(EditorModule* editor, const EditorNodeGraph::Node& node, float controlWidth) {
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node.typeId);
        if (!editor || !descriptor) {
            return;
        }

        const std::string channel = editor->GetNodeGraph().ResolveSocketChannel(node.id, EditorNodeGraph::kImageOutputSocketId);
        const bool hasChannel = !channel.empty();
        const bool lifecycleNeedsNote = descriptor->lifecycleStatus != LayerLifecycleStatus::Stable;
        if (!hasChannel && !lifecycleNeedsNote) {
            return;
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(120.0f, controlWidth));
        if (hasChannel) {
            ImGui::TextColored(
                ChannelPolicyColor(descriptor->channelPolicy),
                "Channel stream: %s (%s)",
                ChannelDisplayName(channel),
                LayerRegistry::ChannelPolicyLabel(descriptor->channelPolicy));
            if (descriptor->channelPolicy != LayerChannelPolicy::ChannelSafe &&
                descriptor->channelNote &&
                descriptor->channelNote[0] != '\0') {
                ImGui::TextDisabled("%s", descriptor->channelNote);
            }
        }
        if (lifecycleNeedsNote) {
            ImGui::TextColored(
                ImVec4(0.90f, 0.68f, 0.32f, 1.0f),
                "Status: %s",
                LayerRegistry::LifecycleStatusLabel(descriptor->lifecycleStatus));
            if (descriptor->lifecycleNote && descriptor->lifecycleNote[0] != '\0') {
                ImGui::TextDisabled("%s", descriptor->lifecycleNote);
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

}

EditorSidebar::EditorSidebar() {}
EditorSidebar::~EditorSidebar() {}

void EditorSidebar::Initialize() {
    m_NodeGraphUI.Initialize();
}

void EditorSidebar::Render(EditorModule* editor) {
    const std::string& saveStatus = LibraryManager::Get().GetSaveStatusText();
    const bool isNodeGraph = editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::NodeGraph;

    if (!isNodeGraph) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f)); // Top padding
        ImGui::Indent(18.0f); // Left margin
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 18.0f); // Right margin
    }

    if (!saveStatus.empty()) {
        ImGui::TextDisabled("%s", saveStatus.c_str());
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    } else if (isNodeGraph) {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }

    (void)editor->ConsumeSelectedTabFocusRequest();

    if (isNodeGraph) {
        m_NodeGraphUI.Render(editor);
    } else if (editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::Presets) {
        RenderPresets(editor);
    } else if (editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::ExportSettings) {
        RenderExportSettings(editor);
    } else if (editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::ComplexNode) {
        RenderComplexNodeSettings(editor);
    }

    if (!isNodeGraph) {
        ImGui::PopItemWidth();
        ImGui::Unindent(18.0f);
    }
}

void EditorSidebar::RenderPresets(EditorModule* editor) {
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGuiExtras::RichSectionLabel("PRESETS", 6.0f);
    ImGui::TextDisabled("Ctrl+Tab toggles this panel.");
    ImGui::TextDisabled("Hover a preset name to preview it on the right.");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::BeginChild("PresetsScrollRegion", ImVec2(0.0f, 0.0f), false);
    m_NodeGraphUI.RenderPresetsPanel(editor, std::max(220.0f, fullWidth));
    ImGui::EndChild();
}

void EditorSidebar::RenderExportSettings(EditorModule* editor) {
    auto& settings = editor->GetMutableCompositeExportSettings();
    const bool hasCompositeContent = !editor->GetCompositeSceneItems().empty();
    const float controlWidth = ImGui::GetContentRegionAvail().x;

    ImGui::BeginDisabled(!hasCompositeContent);

    // Bounds Mode Section
    ImGuiExtras::RichSectionLabel("BOUNDS MODE", 4.0f);
    const bool autoBounds = settings.boundsMode == EditorModule::CompositeExportBoundsMode::Auto;
    
    if (ImGui::RadioButton("Auto Bounds", autoBounds)) {
        settings.boundsMode = EditorModule::CompositeExportBoundsMode::Auto;
        editor->SyncCompositeExportResolutionFromWidth();
    }
    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::RadioButton("Custom Bounds", !autoBounds)) {
        settings.boundsMode = EditorModule::CompositeExportBoundsMode::Custom;
        if (settings.aspectPreset == EditorModule::CompositeExportAspectPreset::Custom) {
            editor->UpdateCompositeCustomExportAspectFromBounds();
        } else {
            const float ratio = editor->GetCurrentCompositeExportAspectRatio();
            const float centerY = settings.customY + settings.customHeight * 0.5f;
            settings.customHeight = std::max(1.0f, settings.customWidth / std::max(0.0001f, ratio));
            settings.customY = centerY - settings.customHeight * 0.5f;
            editor->SyncCompositeExportResolutionFromWidth();
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    // Aspect Ratio Combo
    static const EditorModule::CompositeExportAspectPreset presets[] = {
        EditorModule::CompositeExportAspectPreset::Ratio1x1,
        EditorModule::CompositeExportAspectPreset::Ratio4x3,
        EditorModule::CompositeExportAspectPreset::Ratio3x2,
        EditorModule::CompositeExportAspectPreset::Ratio16x9,
        EditorModule::CompositeExportAspectPreset::Ratio9x16,
        EditorModule::CompositeExportAspectPreset::Ratio2x3,
        EditorModule::CompositeExportAspectPreset::Ratio5x4,
        EditorModule::CompositeExportAspectPreset::Ratio21x9,
        EditorModule::CompositeExportAspectPreset::Custom
    };
    
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::BeginCombo("##Aspect Ratio", ExportAspectPresetLabel(settings.aspectPreset))) {
        for (const auto preset : presets) {
            const bool selected = preset == settings.aspectPreset;
            if (ImGui::Selectable(ExportAspectPresetLabel(preset), selected)) {
                settings.aspectPreset = preset;
                if (settings.aspectPreset == EditorModule::CompositeExportAspectPreset::Custom) {
                    editor->UpdateCompositeCustomExportAspectFromBounds();
                } else if (settings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom) {
                    const float ratio = editor->GetCurrentCompositeExportAspectRatio();
                    const float centerY = settings.customY + settings.customHeight * 0.5f;
                    settings.customHeight = std::max(1.0f, settings.customWidth / std::max(0.0001f, ratio));
                    settings.customY = centerY - settings.customHeight * 0.5f;
                }
                editor->SyncCompositeExportResolutionFromWidth();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (settings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom) {
        ImGui::TextDisabled("Toggle boundary edit with F.");
        ImGui::Text("Bounds: %.1f x %.1f", settings.customWidth, settings.customHeight);
        ImGui::TextDisabled("Origin: %.1f, %.1f", settings.customX, settings.customY);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGuiExtras::RichFullWidthButton("Use View As Export", controlWidth)) {
            editor->UseCompositeViewAsExportBounds(editor->GetLastCompositeCanvasSize());
        }
    } else {
        ImGui::TextDisabled("Auto bounds follow the visible composite.");
    }

    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    // Background Mode Section
    ImGuiExtras::RichSectionLabel("BACKGROUND MODE", 4.0f);
    if (ImGui::RadioButton("Transparent", settings.backgroundMode == EditorModule::CompositeExportBackgroundMode::Transparent)) {
        settings.backgroundMode = EditorModule::CompositeExportBackgroundMode::Transparent;
    }
    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::RadioButton("Solid Color", settings.backgroundMode == EditorModule::CompositeExportBackgroundMode::Solid)) {
        settings.backgroundMode = EditorModule::CompositeExportBackgroundMode::Solid;
    }

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (settings.backgroundMode == EditorModule::CompositeExportBackgroundMode::Solid) {
        float color[3] = {
            settings.backgroundColor[0],
            settings.backgroundColor[1],
            settings.backgroundColor[2]
        };
        if (ImGuiExtras::NodeColorEdit3("Background Color", "##BackgroundColor", color, 0, controlWidth)) {
            settings.backgroundColor[0] = std::clamp(color[0], 0.0f, 1.0f);
            settings.backgroundColor[1] = std::clamp(color[1], 0.0f, 1.0f);
            settings.backgroundColor[2] = std::clamp(color[2], 0.0f, 1.0f);
            settings.backgroundColor[3] = 1.0f;
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Output Resolution Section
    ImGuiExtras::RichSectionLabel("OUTPUT RESOLUTION", 4.0f);
    const bool widthChanged = ImGuiExtras::NodeInputInt("Width", "##OutputWidth", &settings.outputWidth, 1, 100, controlWidth);
    const bool heightChanged = ImGuiExtras::NodeInputInt("Height", "##OutputHeight", &settings.outputHeight, 1, 100, controlWidth);
    if (widthChanged) {
        editor->SyncCompositeExportResolutionFromWidth();
    } else if (heightChanged) {
        editor->SyncCompositeExportResolutionFromHeight();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));

    // Export Action
    if (ImGuiExtras::RichFullWidthButton("Export PNG", controlWidth, 36.0f)) {
        const std::string path = FileDialogs::SavePngFileDialog("Export Composite Image", "composite_output.png");
        if (!path.empty()) {
            editor->RequestExportImage(path);
        }
    }

    ImGui::EndDisabled();

    if (!hasCompositeContent) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("Add at least two completed chains to export the composite.");
        ImGui::PopStyleColor();
     }
}

void EditorSidebar::RenderComplexNodeSettings(EditorModule* editor) {
    const float fullWidth = ImGui::GetContentRegionAvail().x;

    int nodeId = editor->GetActiveComplexNodeId();
    EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(nodeId);
    if (!node) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("Selected complex node is no longer available.");
        return;
    }
    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    std::string displayName = node->title;
    if (displayName.empty()) {
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node->typeId);
        displayName = descriptor && descriptor->displayName ? descriptor->displayName : "Advanced Node";
    }
    ImGuiExtras::RichSectionLabel(displayName.c_str(), 6.0f);
    
    const float controlWidth = std::min(450.0f * ImGui::GetIO().FontGlobalScale, fullWidth);
    ImGui::BeginChild("ComplexNodeScrollRegion", ImVec2(0.0f, 0.0f), false);
    RenderLayerMetadataPanel(editor, *node, controlWidth);
    
    if (node->kind == EditorNodeGraph::NodeKind::RawSource) {
        editor->RenderRawSourceControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
        editor->RenderRawNeuralDenoiseControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawDecode) {
        editor->RenderRawDecodeControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        editor->RenderRawDevelopControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        editor->RenderRawDetailAutoMaskControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawDetailFusion) {
        editor->RenderRawDetailFusionControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::HdrMerge) {
        editor->RenderHdrMergeControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::Mfsr) {
        ImGui::TextDisabled("Status");
        if (!node->mfsr.errorMessage.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "%s", node->mfsr.errorMessage.c_str());
        } else {
            ImGui::TextWrapped("%s", node->mfsr.placeholderStatus.c_str());
        }
        ImGui::TextDisabled("%s", node->mfsr.hasPlaceholderCachedOutput
            ? "Placeholder cache marked"
            : "No MFSR render cache");
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::Lut) {
        editor->RenderLutControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }
    if (node->kind == EditorNodeGraph::NodeKind::CustomMask) {
        editor->RenderCustomMaskControls(*node, controlWidth, true);
        ImGui::EndChild();
        return;
    }

    auto& layers = editor->GetLayers();
    if (node->layerIndex >= 0 && node->layerIndex < static_cast<int>(layers.size())) {
        editor->RenderLayerControlsWithDirtyTracking(*node, [&](LayerBase& layer) {
            NodeSurfaceContext surfaceContext;
            surfaceContext.nodeId = node->id;
            surfaceContext.availableWidth = controlWidth;
            surfaceContext.safeContentWidth = controlWidth;
            surfaceContext.logicalAvailableWidth = controlWidth / ImGui::GetIO().FontGlobalScale;
            surfaceContext.logicalSafeContentWidth = controlWidth / ImGui::GetIO().FontGlobalScale;
            surfaceContext.layoutScale = ImGui::GetIO().FontGlobalScale;
            surfaceContext.contentScale = ImGui::GetIO().FontGlobalScale;
            surfaceContext.itemGap = 6.0f;
            surfaceContext.sectionGap = 10.0f;
            surfaceContext.focused = true;
            surfaceContext.density = NodeSurfaceDensity::Dense;
            surfaceContext.canvasToolActive = editor->GetCanvasToolOwnerNodeId() == node->id;
            surfaceContext.canvasToolStatusText = editor->GetCanvasToolStatusText().empty()
                ? nullptr
                : editor->GetCanvasToolStatusText().c_str();

            const float uiScale = ImGui::GetIO().FontGlobalScale;
            const float densityScale = 0.85f;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::max(2.0f, 6.0f * uiScale * densityScale), std::max(2.0f, 4.0f * uiScale * densityScale)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::max(2.0f, 6.0f * uiScale * 0.75f * densityScale), std::max(2.0f, 6.0f * uiScale * 0.58f * densityScale)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(std::max(2.0f, 5.0f * uiScale * densityScale), std::max(2.0f, 4.0f * uiScale * densityScale)));
            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, std::max(2.5f, 6.5f * uiScale * densityScale));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(0.8f, 1.0f * uiScale));

            layer.RenderExpandedNodeSurface(editor, surfaceContext);

            ImGui::PopStyleVar(7);
        });
    } else {
        ImGui::TextDisabled("Layer controls are not available.");
    }
    ImGui::EndChild();
}

