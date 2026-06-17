#include "Composite/Internal/CompositeModuleInternal.h"

#include "Composite/CompositeModule.h"
#include "Library/LibraryManager.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

void CompositeModule::RenderToolbar() {
    if (ImGui::BeginTable("CompositeToolbarTable", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Panels", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Button("New")) {
            NewProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Image")) {
            TriggerAddImage();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Project")) {
            TriggerAddProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add From Library")) {
            TriggerAddFromLibrary();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!HasLayers());
        if (ImGui::Button("Save To Library")) {
            TriggerSaveToLibrary();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export PNG")) {
            TriggerExportPng();
        }
        ImGui::EndDisabled();

        ImGui::TableSetColumnIndex(1);
        auto renderPanelButton = [&](const char* label, bool& open) {
            if (ImGui::SmallButton(label) && !open) {
                open = true;
                MarkDocumentDirty();
            }
        };

        renderPanelButton("Layers", m_ShowLayersWindow);
        ImGui::SameLine();
        renderPanelButton("Selected", m_ShowSelectedWindow);
        ImGui::SameLine();
        renderPanelButton("View", m_ShowViewWindow);
        ImGui::SameLine();
        renderPanelButton("Export", m_ShowExportWindow);

        ImGui::EndTable();
    }

    const std::string& saveStatus = LibraryManager::Get().GetSaveStatusText();
    if (!saveStatus.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", saveStatus.c_str());
        ImGui::PopTextWrapPos();
    }
}

void CompositeModule::RenderLayerPane() {
    ImGui::Text("Project: %s", m_ProjectName.c_str());
    ImGui::TextDisabled("Right click a layer for actions. Drag rows to reorder.");
    ImGui::Separator();

    if (ImGui::BeginTable("CompositeLayerTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(-1, 0))) {
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Vis", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableHeadersRow();

        std::vector<CompositeLayer*> sorted;
        sorted.reserve(m_Layers.size());
        for (CompositeLayer& layer : m_Layers) {
            sorted.push_back(&layer);
        }
        std::sort(sorted.begin(), sorted.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
            return a->z > b->z;
        });

        for (int displayIndex = 0; displayIndex < static_cast<int>(sorted.size()); ++displayIndex) {
            CompositeLayer* layer = sorted[displayIndex];
            ImGui::PushID(layer->id.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const bool selected = (layer->id == m_SelectedId);
            if (ImGui::Selectable(layer->name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                m_SelectedId = layer->id;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("COMPOSITE_LAYER_MOVE", &displayIndex, sizeof(int));
                ImGui::Text("Move %s", layer->name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMPOSITE_LAYER_MOVE")) {
                    const int sourceIndex = *static_cast<const int*>(payload->Data);
                    if (sourceIndex != displayIndex &&
                        sourceIndex >= 0 &&
                        sourceIndex < static_cast<int>(sorted.size())) {
                        CompositeLayer* movingLayer = sorted[sourceIndex];
                        sorted.erase(sorted.begin() + sourceIndex);
                        sorted.insert(sorted.begin() + displayIndex, movingLayer);
                        ReassignZFromTopOrder(sorted);
                        MarkDocumentDirty();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem("CompositeLayerRowContext")) {
                m_SelectedId = layer->id;
                RenderLayerContextMenu(layer);
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", LayerKindBadge(layer->kind));

            ImGui::TableSetColumnIndex(2);
            bool visible = layer->visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                layer->visible = visible;
                MarkDocumentDirty();
            }

            ImGui::TableSetColumnIndex(3);
            bool locked = layer->locked;
            if (ImGui::Checkbox("##locked", &locked)) {
                layer->locked = locked;
                MarkDocumentDirty();
            }

            ImGui::PopID();
        }

        ImGui::EndTable();

        const bool layersFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool layersHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if ((layersFocused || layersHovered || m_CanvasFocused) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && !m_SelectedId.empty()) {
            int selectedIdx = -1;
            for (int i = 0; i < static_cast<int>(sorted.size()); ++i) {
                if (sorted[i]->id == m_SelectedId) {
                    selectedIdx = i;
                    break;
                }
            }

            if (selectedIdx != -1) {
                bool ZOrderMoved = false;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    if (selectedIdx > 0) {
                        CompositeLayer* movingLayer = sorted[selectedIdx];
                        sorted.erase(sorted.begin() + selectedIdx);
                        sorted.insert(sorted.begin() + (selectedIdx - 1), movingLayer);
                        ZOrderMoved = true;
                    }
                } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                    if (selectedIdx < static_cast<int>(sorted.size()) - 1) {
                        CompositeLayer* movingLayer = sorted[selectedIdx];
                        sorted.erase(sorted.begin() + selectedIdx);
                        sorted.insert(sorted.begin() + (selectedIdx + 1), movingLayer);
                        ZOrderMoved = true;
                    }
                }

                if (ZOrderMoved) {
                    ReassignZFromTopOrder(sorted);
                    MarkDocumentDirty();
                }
            }
        }
    }
}

void CompositeModule::RenderSelectedInspector(CompositeLayer* selectedLayer) {
    if (!selectedLayer) {
        ImGui::TextDisabled("Select a layer to inspect it.");
        return;
    }

    const bool layerLocked = selectedLayer->locked;
    const int displayWidth = std::max(1, static_cast<int>(std::round(LayerWorldWidth(*selectedLayer))));
    const int displayHeight = std::max(1, static_cast<int>(std::round(LayerWorldHeight(*selectedLayer))));

    ImGui::Text("Name: %s", selectedLayer->name.c_str());
    ImGui::TextDisabled("Kind: %s", LayerKindBadge(selectedLayer->kind));
    ImGui::TextDisabled("Display Size: %d x %d", displayWidth, displayHeight);
    if (selectedLayer->kind == LayerKind::Text) {
        ImGui::TextDisabled("Render Size: %d x %d", selectedLayer->imgW, selectedLayer->imgH);
    }
    ImGui::TextDisabled("Visible: %s", selectedLayer->visible ? "Yes" : "No");
    ImGui::TextDisabled("Locked: %s", selectedLayer->locked ? "Yes" : "No");
    if (selectedLayer->kind == LayerKind::EditorProject) {
        if (!selectedLayer->linkedProjectName.empty()) {
            ImGui::TextDisabled("Linked Project: %s", selectedLayer->linkedProjectName.c_str());
        } else if (selectedLayer->generatedFromImage) {
            ImGui::TextDisabled("Generated from an image layer inside Composite.");
        } else {
            ImGui::TextDisabled("Embedded editor project layer.");
        }
    }
    ImGui::Separator();

    ImGui::BeginDisabled(layerLocked);
    ImGui::TextUnformatted("Grouping");
    const CompositeLayer* currentParent = FindLayerById(m_Layers, selectedLayer->parentId);
    const char* parentLabel = selectedLayer->parentId.empty()
        ? "None"
        : (currentParent ? currentParent->name.c_str() : "(Missing Parent)");
    if (ImGui::BeginCombo("Parent", parentLabel)) {
        if (ImGui::Selectable("None", selectedLayer->parentId.empty())) {
            if (SetLayerParentPreserveWorld(m_Layers, *selectedLayer, std::string())) {
                MarkDocumentDirty();
            }
        }

        for (CompositeLayer& candidate : m_Layers) {
            if (candidate.id == selectedLayer->id || WouldCreateParentCycle(m_Layers, *selectedLayer, candidate.id)) {
                continue;
            }

            ImGui::PushID(candidate.id.c_str());
            const bool candidateSelected = candidate.id == selectedLayer->parentId;
            if (ImGui::Selectable(candidate.name.c_str(), candidateSelected)) {
                if (SetLayerParentPreserveWorld(m_Layers, *selectedLayer, candidate.id)) {
                    MarkDocumentDirty();
                }
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    if (!selectedLayer->parentId.empty()) {
        if (const CompositeLayer* parent = FindLayerById(m_Layers, selectedLayer->parentId)) {
            ImGui::TextDisabled("Parented to: %s", parent->name.c_str());
        } else {
            ImGui::TextDisabled("Parented to: missing layer");
        }

        if (ImGui::Button("Detach Parent", ImVec2(-1, 0))) {
            if (SetLayerParentPreserveWorld(m_Layers, *selectedLayer, std::string())) {
                MarkDocumentDirty();
            }
        }
    }

    std::size_t childCount = 0;
    for (const CompositeLayer& candidate : m_Layers) {
        if (candidate.parentId == selectedLayer->id) {
            ++childCount;
        }
    }
    if (childCount > 0) {
        ImGui::TextDisabled("Children: %zu", childCount);
    }

    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::TextUnformatted("Transform");
    ImGui::BeginDisabled(layerLocked);
    bool changed = false;
    bool scaleChanged = false;
    changed |= ImGui::DragFloat2("Position", &selectedLayer->x, 1.0f);
    changed |= ImGui::SliderAngle("Rotation", &selectedLayer->rotation, -180.0f, 180.0f);
    if (ImGui::Checkbox("Preserve Aspect Ratio", &selectedLayer->preserveAspectRatio)) {
        changed = true;
        scaleChanged = true;
        if (selectedLayer->preserveAspectRatio) {
            const float unifiedScale = std::max(selectedLayer->scaleX, selectedLayer->scaleY);
            selectedLayer->scaleX = unifiedScale;
            selectedLayer->scaleY = unifiedScale;
        }
    }
    if (selectedLayer->preserveAspectRatio) {
        float uniformScale = selectedLayer->scaleX;
        if (ImGui::DragFloat("Scale", &uniformScale, 0.01f, 0.01f, 32.0f, "%.2f")) {
            selectedLayer->scaleX = uniformScale;
            selectedLayer->scaleY = uniformScale;
            changed = true;
            scaleChanged = true;
        }
    } else {
        scaleChanged |= ImGui::DragFloat("Scale X", &selectedLayer->scaleX, 0.01f, 0.01f, 32.0f, "%.2f");
        scaleChanged |= ImGui::DragFloat("Scale Y", &selectedLayer->scaleY, 0.01f, 0.01f, 32.0f, "%.2f");
        changed |= scaleChanged;
    }
    if (changed) {
        selectedLayer->scaleX = std::max(0.01f, selectedLayer->scaleX);
        selectedLayer->scaleY = std::max(0.01f, selectedLayer->scaleY);
        if (scaleChanged && selectedLayer->kind == LayerKind::Text) {
            RegenerateGeneratedLayerTexture(*selectedLayer);
        }
        MarkDocumentDirty();
    }
    if (ImGui::Button("Reset Transform", ImVec2(-1, 0))) {
        selectedLayer->rotation = 0.0f;
        selectedLayer->scaleX = 1.0f;
        selectedLayer->scaleY = 1.0f;
        if (selectedLayer->kind == LayerKind::Text) {
            RegenerateGeneratedLayerTexture(*selectedLayer);
        }
        MarkDocumentDirty();
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextUnformatted("Appearance");
    ImGui::BeginDisabled(layerLocked);
    bool appearanceChanged = false;
    appearanceChanged |= ImGui::SliderFloat("Opacity", &selectedLayer->opacity, 0.0f, 1.0f);
    int blendModeIndex = static_cast<int>(selectedLayer->blendMode);
    const char* blendLabels[] = {
        "Normal",
        "Multiply",
        "Screen",
        "Add",
        "Overlay",
        "Soft Light",
        "Hard Light",
        "Hue",
        "Color"
    };
    if (ImGui::Combo("Blend Mode", &blendModeIndex, blendLabels, IM_ARRAYSIZE(blendLabels))) {
        selectedLayer->blendMode = static_cast<CompositeBlendMode>(blendModeIndex);
        appearanceChanged = true;
    }
    appearanceChanged |= ImGui::Checkbox("Flip X", &selectedLayer->flipX);
    appearanceChanged |= ImGui::Checkbox("Flip Y", &selectedLayer->flipY);
    if (selectedLayer->kind == LayerKind::ShapeRect ||
        selectedLayer->kind == LayerKind::ShapeCircle ||
        selectedLayer->kind == LayerKind::Text) {
        float fillColor[4] = {
            selectedLayer->fillColor[0],
            selectedLayer->fillColor[1],
            selectedLayer->fillColor[2],
            selectedLayer->fillColor[3]
        };
        if (ImGui::ColorEdit4("Fill Color", fillColor)) {
            for (int channel = 0; channel < 4; ++channel) {
                selectedLayer->fillColor[channel] = Clamp01(fillColor[channel]);
            }
            if (RegenerateGeneratedLayerTexture(*selectedLayer)) {
                appearanceChanged = true;
            }
        }
    }
    if (appearanceChanged) {
        MarkDocumentDirty();
    }
    ImGui::EndDisabled();

    if (selectedLayer->kind == LayerKind::Text) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Text");
        ImGui::BeginDisabled(layerLocked);
        char textBuffer[4096];
        std::snprintf(textBuffer, sizeof(textBuffer), "%s", selectedLayer->textContent.c_str());
        bool textChanged = false;
        if (ImGui::InputTextMultiline("Content", textBuffer, sizeof(textBuffer), ImVec2(-1.0f, 120.0f))) {
            selectedLayer->textContent = textBuffer;
            textChanged = true;
        }
        textChanged |= ImGui::DragFloat("Font Size", &selectedLayer->textFontSize, 1.0f, 1.0f, 0.0f, "%.0f px");
        if (textChanged) {
            selectedLayer->textFontSize = std::max(1.0f, selectedLayer->textFontSize);
            if (RegenerateGeneratedLayerTexture(*selectedLayer)) {
                MarkDocumentDirty();
            }
        }
        ImGui::EndDisabled();
    }
}

void CompositeModule::RenderViewInspector() {
    if (ImGui::SliderFloat("Zoom", &m_ViewZoom, 0.05f, 16.0f, "%.2f")) {
        MarkStageDirty();
    }
    if (ImGui::Button("Reset View", ImVec2(-1, 0))) {
        m_ViewZoom = 1.0f;
        m_ViewPanX = 0.0f;
        m_ViewPanY = 0.0f;
        MarkStageDirty();
    }

    if (ImGui::Checkbox("Show Checker", &m_ShowChecker)) {
        MarkDocumentDirty();
    }
    if (ImGui::Checkbox("Limit Project Preview Resolution To 4K", &m_LimitProjectResolution)) {
        MarkDocumentDirty();
    }

    ImGui::Separator();
    if (ImGui::Checkbox("Enable Snapping", &m_SnapEnabled)) {
        MarkDocumentDirty();
    }
    ImGui::BeginDisabled(!m_SnapEnabled);
    bool snapChanged = false;
    int snapModeIndex = static_cast<int>(GetSnapModePreset());
    const char* snapModeLabels[] = { "Full", "Object-Only", "Off", "Custom" };
    if (ImGui::Combo("Snap Mode", &snapModeIndex, snapModeLabels, IM_ARRAYSIZE(snapModeLabels))) {
        ApplySnapModePreset(static_cast<CompositeSnapModePreset>(snapModeIndex));
        snapChanged = true;
    }
    snapChanged |= ImGui::Checkbox("Snap To Objects", &m_SnapToObjects);
    snapChanged |= ImGui::Checkbox("Snap To Centers", &m_SnapToCenters);
    snapChanged |= ImGui::Checkbox("Snap To Canvas Center", &m_SnapToCanvasCenter);
    snapChanged |= ImGui::Checkbox("Snap To Export Bounds", &m_SnapToExportBounds);
    snapChanged |= ImGui::Checkbox("Snap To Spacing", &m_SnapToSpacing);
    ImGui::Separator();
    snapChanged |= ImGui::DragFloat("Grid Size", &m_GridSize, 1.0f, 0.0f, 512.0f, "%.0f px");
    snapChanged |= ImGui::DragFloat("Rotate Step", &m_RotateSnapStep, 1.0f, 0.0f, 180.0f, "%.0f deg");
    snapChanged |= ImGui::DragFloat("Scale Step", &m_ScaleSnapStep, 0.01f, 0.0f, 1.0f, "%.2f");
    if (snapChanged) {
        m_GridSize = std::max(0.0f, m_GridSize);
        m_RotateSnapStep = std::clamp(m_RotateSnapStep, 0.0f, 180.0f);
        m_ScaleSnapStep = std::clamp(m_ScaleSnapStep, 0.0f, 1.0f);
        RememberSnapStepDefaults();
        MarkDocumentDirty();
    }
    ImGui::EndDisabled();
}

void CompositeModule::RenderExportInspector() {
    ImGui::BeginDisabled(!HasLayers());
    const bool autoBounds = m_ExportSettings.boundsMode == CompositeExportBoundsMode::Auto;
    if (ImGui::RadioButton("Auto Bounds", autoBounds)) {
        m_ExportSettings.boundsMode = CompositeExportBoundsMode::Auto;
        MarkDocumentDirty();
    }
    if (ImGui::RadioButton("Custom Bounds", !autoBounds)) {
        m_ExportSettings.boundsMode = CompositeExportBoundsMode::Custom;
        MarkDocumentDirty();
    }

    static const CompositeExportAspectPreset aspectPresets[] = {
        CompositeExportAspectPreset::Ratio1x1,
        CompositeExportAspectPreset::Ratio4x3,
        CompositeExportAspectPreset::Ratio3x2,
        CompositeExportAspectPreset::Ratio16x9,
        CompositeExportAspectPreset::Ratio9x16,
        CompositeExportAspectPreset::Ratio2x3,
        CompositeExportAspectPreset::Ratio5x4,
        CompositeExportAspectPreset::Ratio21x9,
        CompositeExportAspectPreset::Custom
    };

    if (ImGui::BeginCombo("Aspect Ratio", ExportAspectPresetToLabel(m_ExportSettings.aspectPreset))) {
        for (int index = 0; index < IM_ARRAYSIZE(aspectPresets); ++index) {
            const bool selected = aspectPresets[index] == m_ExportSettings.aspectPreset;
            if (ImGui::Selectable(ExportAspectPresetToLabel(aspectPresets[index]), selected)) {
                m_ExportSettings.aspectPreset = aspectPresets[index];
                if (m_ExportSettings.aspectPreset == CompositeExportAspectPreset::Custom) {
                    UpdateCustomExportAspectFromBounds();
                } else if (m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
                    const float ratio = ExportAspectRatioValue(m_ExportSettings);
                    const float centerY = m_ExportSettings.customY + m_ExportSettings.customHeight * 0.5f;
                    m_ExportSettings.customHeight = std::max(1.0f, m_ExportSettings.customWidth / std::max(0.0001f, ratio));
                    m_ExportSettings.customY = centerY - m_ExportSettings.customHeight * 0.5f;
                }
                SyncExportResolutionFromWidth();
                MarkDocumentDirty();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        ImGui::TextDisabled("Adjust the export bounds directly on the canvas while the Export panel or tab is active.");
        ImGui::Text("Bounds: %.1f x %.1f", m_ExportSettings.customWidth, m_ExportSettings.customHeight);
        ImGui::TextDisabled("Origin: %.1f, %.1f", m_ExportSettings.customX, m_ExportSettings.customY);

        if (ImGui::Button("Use View As Export", ImVec2(-1, 0))) {
            const FloatRect viewRect = ComputeViewWorldRect(m_CanvasW, m_CanvasH, m_ViewZoom, m_ViewPanX, m_ViewPanY);
            m_ExportSettings.boundsMode = CompositeExportBoundsMode::Custom;
            m_ExportSettings.customX = viewRect.x;
            m_ExportSettings.customY = viewRect.y;
            m_ExportSettings.customWidth = viewRect.width;
            m_ExportSettings.customHeight = viewRect.height;
            m_ExportSettings.aspectPreset = CompositeExportAspectPreset::Custom;
            UpdateCustomExportAspectFromBounds();
            m_ExportSettings.outputWidth = std::max(1, static_cast<int>(std::round(std::max(1.0f, m_CanvasW))));
            SyncExportResolutionFromWidth();
            MarkDocumentDirty();
        }
    }

    ImGui::Separator();
    if (ImGui::RadioButton("Transparent Background", m_ExportSettings.backgroundMode == CompositeExportBackgroundMode::Transparent)) {
        m_ExportSettings.backgroundMode = CompositeExportBackgroundMode::Transparent;
        MarkDocumentDirty();
    }
    if (ImGui::RadioButton("Solid Background", m_ExportSettings.backgroundMode == CompositeExportBackgroundMode::Solid)) {
        m_ExportSettings.backgroundMode = CompositeExportBackgroundMode::Solid;
        MarkDocumentDirty();
    }
    if (m_ExportSettings.backgroundMode == CompositeExportBackgroundMode::Solid) {
        float color[3] = {
            m_ExportSettings.backgroundColor[0],
            m_ExportSettings.backgroundColor[1],
            m_ExportSettings.backgroundColor[2]
        };
        if (ImGui::ColorEdit3("Background Color", color)) {
            m_ExportSettings.backgroundColor[0] = Clamp01(color[0]);
            m_ExportSettings.backgroundColor[1] = Clamp01(color[1]);
            m_ExportSettings.backgroundColor[2] = Clamp01(color[2]);
            m_ExportSettings.backgroundColor[3] = 1.0f;
            MarkDocumentDirty();
        }
    }

    ImGui::Separator();
    bool resolutionChanged = false;
    const bool widthChanged = ImGui::InputInt("Output Width", &m_ExportSettings.outputWidth);
    const bool heightChanged = ImGui::InputInt("Output Height", &m_ExportSettings.outputHeight);
    if (widthChanged) {
        SyncExportResolutionFromWidth();
        resolutionChanged = true;
    } else if (heightChanged) {
        SyncExportResolutionFromHeight();
        resolutionChanged = true;
    }
    if (resolutionChanged) {
        MarkDocumentDirty();
    }

    if (ImGui::Button("Export PNG", ImVec2(-1, 0))) {
        TriggerExportPng();
    }
    ImGui::EndDisabled();

    if (!HasLayers()) {
        ImGui::TextDisabled("Add at least one layer to export the composite.");
    }
}

void CompositeModule::RenderLayerContextMenu(CompositeLayer* targetLayer) {
    if (targetLayer == nullptr) {
        return;
    }

    m_SelectedId = targetLayer->id;

    ImGui::TextDisabled("Layer");
    if (ImGui::MenuItem("Rename")) {
        BeginRenameSelectedLayer();
    }
    if (ImGui::MenuItem("Duplicate")) {
        DuplicateSelectedLayer();
    }
    const bool visible = targetLayer->visible;
    if (ImGui::MenuItem("Visible", nullptr, visible)) {
        targetLayer->visible = !visible;
        MarkDocumentDirty();
    }
    const bool locked = targetLayer->locked;
    if (ImGui::MenuItem("Locked", nullptr, locked)) {
        targetLayer->locked = !locked;
        MarkDocumentDirty();
    }
    ImGui::BeginDisabled(targetLayer->locked);
    if (ImGui::MenuItem("Delete")) {
        RemoveSelectedLayers();
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (IsEditorBridgeLayer(*targetLayer)) {
        ImGui::TextDisabled("Replace");
        if (ImGui::MenuItem("Replace With Image")) {
            const std::string path = FileDialogs::OpenImageFileDialog("Replace composite layer with image");
            if (!path.empty()) {
                ReplaceSelectedLayerWithImageFile(path);
            }
        }
        if (ImGui::MenuItem("Replace With Library Project")) {
            m_LibraryPickerMode = LibraryPickerMode::ReplaceSelectedWithProject;
            m_LibraryPickerTargetLayerId = targetLayer->id;
            m_ShowLibraryPicker = true;
        }

        ImGui::Separator();
        ImGui::TextDisabled("Bridge");
        if (ImGui::MenuItem("Open in Editor")) {
            OpenSelectedLayerInEditor();
        }
        if (targetLayer->kind == LayerKind::EditorProject && !targetLayer->linkedProjectFileName.empty()) {
            if (ImGui::MenuItem("Update Linked Project")) {
                UpdateLinkedProjectFromSelectedLayer();
            }
        }
    }
}

void CompositeModule::RenderCanvasContextMenu() {
    if (ImGui::MenuItem("Add Image")) {
        TriggerAddImage();
    }
    if (ImGui::MenuItem("Add Project")) {
        TriggerAddProject();
    }
    if (ImGui::MenuItem("Add From Library")) {
        TriggerAddFromLibrary();
    }
    if (ImGui::MenuItem("Add Square")) {
        AddShapeLayer(LayerKind::ShapeRect);
    }
    if (ImGui::MenuItem("Add Circle")) {
        AddShapeLayer(LayerKind::ShapeCircle);
    }
    if (ImGui::MenuItem("Add Text")) {
        AddTextLayer();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Composite Layout")) {
        ResetWorkspaceLayout(true);
    }
}

void CompositeModule::RenderSavePopup() {
    if (m_OpenSavePopup) {
        ImGui::OpenPopup("SaveCompositeToLibrary");
        m_OpenSavePopup = false;
    }

    if (ImGui::BeginPopupModal("SaveCompositeToLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char nameBuffer[256] = "Composite Project";
        if (ImGui::IsWindowAppearing()) {
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", m_ProjectName.c_str());
        }

        ImGui::InputText("Project name", nameBuffer, sizeof(nameBuffer));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            LibraryManager::Get().RequestSaveCompositeProject(nameBuffer, this, "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void CompositeModule::RenderRenamePopup() {
    if (m_OpenRenamePopup) {
        ImGui::OpenPopup("RenameCompositeLayer");
        m_OpenRenamePopup = false;
    }

    if (ImGui::BeginPopupModal("RenameCompositeLayer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Layer name", m_RenameBuffer, sizeof(m_RenameBuffer));
        if (ImGui::Button("Apply", ImVec2(120, 0))) {
            CompositeLayer* layer = FindLayerById(m_Layers, m_RenameLayerId);
            if (layer) {
                const std::string trimmedName = TrimWhitespace(m_RenameBuffer);
                if (!trimmedName.empty()) {
                    layer->name = trimmedName;
                    MarkDocumentDirty();
                }
            }
            m_RenameLayerId.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_RenameLayerId.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void CompositeModule::RenderLibraryPicker() {
    if (!m_ShowLibraryPicker) {
        return;
    }

    const char* windowTitle = (m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject)
        ? "Replace Layer With Library Project"
        : "Select Library Project";

    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(windowTitle, &m_ShowLibraryPicker, ImGuiWindowFlags_NoCollapse)) {
        const auto& projects = LibraryManager::Get().GetProjects();
        if (projects.empty()) {
            ImGui::TextDisabled("No editor projects were found in the Library.");
        } else {
            ImGui::TextUnformatted(
                m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject
                    ? "Choose a project to replace the selected layer."
                    : "Choose a project to add it as a new layer.");
            ImGui::Separator();

            if (ImGui::BeginChild("CompositeLibraryPickerList")) {
                const float thumbSize = 84.0f;
                const float padding = 12.0f;

                for (const auto& project : projects) {
                    if (!project || project->projectKind != StackBinaryFormat::kEditorProjectKind) {
                        continue;
                    }

                    ImGui::PushID(project->fileName.c_str());
                    ImGui::BeginGroup();

                    if (project->thumbnailTex != 0) {
                        float aspect = 1.0f;
                        if (project->sourceWidth > 0 && project->sourceHeight > 0) {
                            aspect = static_cast<float>(project->sourceWidth) / static_cast<float>(project->sourceHeight);
                        }
                        float thumbW = thumbSize;
                        float thumbH = thumbSize;
                        if (aspect > 1.0f) {
                            thumbH = thumbSize / aspect;
                        } else {
                            thumbW = thumbSize * aspect;
                        }
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (thumbSize - thumbH) * 0.5f);
                        ImGui::Image(
                            (ImTextureID)(intptr_t)project->thumbnailTex,
                            ImVec2(thumbW, thumbH),
                            ImVec2(0, 1),
                            ImVec2(1, 0));
                    } else {
                        ImGui::Dummy(ImVec2(thumbSize, thumbSize));
                        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32_WHITE);
                    }

                    ImGui::SameLine(thumbSize + padding);
                    ImGui::BeginGroup();
                    ImGui::Text("%s", project->projectName.c_str());
                    ImGui::TextDisabled("%s", project->timestamp.c_str());
                    const char* actionLabel = (m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject)
                        ? "Replace Layer"
                        : "Add To Composite";
                    if (ImGui::Button(actionLabel)) {
                        const std::filesystem::path fullPath = LibraryManager::Get().GetLibraryPath() / project->fileName;
                        if (m_LibraryPickerMode == LibraryPickerMode::ReplaceSelectedWithProject) {
                            ReplaceSelectedLayerWithProjectFile(fullPath.string());
                        } else {
                            AddProjectLayerFromFile(fullPath.string());
                        }
                        m_ShowLibraryPicker = false;
                    }
                    ImGui::EndGroup();

                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }

                ImGui::EndChild();
            }
        }

        ImGui::End();
    }
}

void CompositeModule::RenderUI() {
    if (!m_Initialized) {
        Initialize();
    }

    ImGui::BeginChild("CompositeToolbarRegion", ImVec2(0.0f, kCompositeToolbarHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    RenderToolbar();
    ImGui::EndChild();

    const ImGuiWindowFlags workspaceFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("CompositeWorkspaceRegion", ImVec2(0.0f, 0.0f), false, workspaceFlags);
    ImGui::PopStyleVar();

    m_WorkspaceDockId = ImGui::GetID(kCompositeDockSpaceName);
    if (m_PendingWorkspaceLayoutLoad && !m_WorkspaceLayoutIni.empty()) {
        ImGui::DockBuilderRemoveNode(m_WorkspaceDockId);
        ImGui::LoadIniSettingsFromMemory(m_WorkspaceLayoutIni.c_str(), m_WorkspaceLayoutIni.size());
        m_PendingWorkspaceLayoutLoad = false;
    }

    ImGui::DockSpace(m_WorkspaceDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (m_PendingWorkspaceLayoutReset) {
        BuildDefaultWorkspaceLayout(m_WorkspaceDockId);
        m_PendingWorkspaceLayoutReset = false;
    }

    bool panelVisibilityChanged = false;
    m_ExportPanelActive = false;

    const bool layersOpenBefore = m_ShowLayersWindow;
    if (m_ShowLayersWindow) {
        if (ImGui::Begin(kCompositeLayersWindowName, &m_ShowLayersWindow)) {
            RenderLayerPane();
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (layersOpenBefore != m_ShowLayersWindow);

    const bool selectedOpenBefore = m_ShowSelectedWindow;
    if (m_ShowSelectedWindow) {
        if (ImGui::Begin(kCompositeSelectedWindowName, &m_ShowSelectedWindow)) {
            RenderSelectedInspector(GetSelectedLayer());
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (selectedOpenBefore != m_ShowSelectedWindow);

    const bool viewOpenBefore = m_ShowViewWindow;
    if (m_ShowViewWindow) {
        if (ImGui::Begin(kCompositeViewWindowName, &m_ShowViewWindow)) {
            RenderViewInspector();
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (viewOpenBefore != m_ShowViewWindow);

    const bool exportOpenBefore = m_ShowExportWindow;
    if (m_ShowExportWindow) {
        if (ImGui::Begin(kCompositeExportWindowName, &m_ShowExportWindow)) {
            m_ExportPanelActive = true;
            RenderExportInspector();
        }
        ImGui::End();
    }
    panelVisibilityChanged |= (exportOpenBefore != m_ShowExportWindow);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(kCompositeCanvasWindowName, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        RenderStage();
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (panelVisibilityChanged) {
        MarkDocumentDirty();
    }

    CaptureWorkspaceLayout();

    ImGui::EndChild();

    RenderLibraryPicker();
    RenderSavePopup();
    RenderRenamePopup();

    if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    }
}
