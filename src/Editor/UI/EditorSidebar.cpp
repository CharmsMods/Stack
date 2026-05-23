#include "EditorSidebar.h"
#include "App/settings/AppearanceTheme.h"

#include "Editor/EditorModule.h"
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
    } else if (editor->GetActiveSubWindow() == EditorModule::EditorSubWindow::ExportSettings) {
        RenderExportSettings(editor);
    } else {
        RenderSettings(editor);
    }

    if (!isNodeGraph) {
        ImGui::PopItemWidth();
        ImGui::Unindent(18.0f);
    }
}

static const char* CompositeSnapPresetLabel(EditorModule::CompositeSnapModePreset preset) {
    switch (preset) {
        case EditorModule::CompositeSnapModePreset::ObjectOnly: return "Object Only";
        case EditorModule::CompositeSnapModePreset::Full: return "Full";
        case EditorModule::CompositeSnapModePreset::Custom: return "Custom";
        case EditorModule::CompositeSnapModePreset::Off:
        default:
            return "Off";
    }
}

void EditorSidebar::RenderSettings(EditorModule* editor) {
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    static int activeCategory = 0; // 0: General, 1: Canvas Composition
    
    ImGui::Columns(2, "##SettingsColumns", false);
    ImGui::SetColumnWidth(0, 95.0f * ImGui::GetIO().FontGlobalScale);
    
    // Left column: Category selection
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(50, 50, 60, 200));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(70, 70, 85, 230));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(90, 90, 110, 255));
    
    if (ImGui::Selectable("General", activeCategory == 0, 0, ImVec2(0, 26.0f))) {
        activeCategory = 0;
    }
    
    const bool hasComposite = editor->GetCompletedChainCount() >= 2;
    if (hasComposite) {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Selectable("Canvas\nComposition", activeCategory == 1, 0, ImVec2(0, 36.0f))) {
            activeCategory = 1;
        }
    } else {
        if (activeCategory == 1) {
            activeCategory = 0;
        }
    }
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    
    ImGui::NextColumn();
    
    // Right column: Content for category
    const float rightColWidth = ImGui::GetContentRegionAvail().x;
    
    if (activeCategory == 0) {
        ImGuiExtras::RichSectionLabel("THEME & GENERAL", 4.0f);
        ImGui::TextWrapped("Grid & Theme options are listed below.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        
        auto* appearance = editor->GetAppearance();
        if (appearance != nullptr) {
            const std::string activePresetId = appearance->GetActivePresetId();
            const StackAppearance::ThemeDefinition* activePreset = appearance->GetActivePreset();
            std::string currentPresetName = activePreset ? activePreset->displayName : "Custom";

            ImGui::Text("Theme Preset");
            ImGui::SetNextItemWidth(rightColWidth);
            if (ImGui::BeginCombo("##EditorThemePresetCombo", currentPresetName.c_str())) {
                // Draw factory presets
                for (const auto& preset : appearance->GetFactoryThemes()) {
                    const bool selected = activePresetId == preset.id;
                    if (ImGui::Selectable(preset.displayName.c_str(), selected)) {
                        appearance->SelectPresetById(preset.id);
                        appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
                    }
                }
                ImGui::EndCombo();
            }
        }
        
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Text("Grid Spacing: 32 px");
        ImGui::Text("Renderer: OpenGL 4.3 Core");
        
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::TextDisabled("Stack Image Editor");
        ImGui::TextDisabled("Version 1.0.0");
    } else if (activeCategory == 1 && hasComposite) {
        ImGuiExtras::RichSectionLabel("CANVAS COMPOSITION", 4.0f);
        
        // Snap Mode
        EditorModule::CompositeSnapModePreset snapPreset = editor->GetCompositeSnapModePreset();
        static const EditorModule::CompositeSnapModePreset snapPresets[] = {
            EditorModule::CompositeSnapModePreset::Off,
            EditorModule::CompositeSnapModePreset::ObjectOnly,
            EditorModule::CompositeSnapModePreset::Full,
            EditorModule::CompositeSnapModePreset::Custom
        };
        
        ImGui::SetNextItemWidth(rightColWidth);
        if (ImGui::BeginCombo("##SnapMode", CompositeSnapPresetLabel(snapPreset))) {
            for (const auto preset : snapPresets) {
                const bool selected = preset == snapPreset;
                if (ImGui::Selectable(CompositeSnapPresetLabel(preset), selected)) {
                    editor->ApplyCompositeSnapModePreset(preset);
                    snapPreset = editor->GetCompositeSnapModePreset();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        
        // Snap settings checkboxes
        auto& snapSettings = editor->GetMutableCompositeSnapSettings();
        ImGui::TextDisabled("Advanced snap tuning:");
        bool tuningChanged = false;
        
        tuningChanged |= ImGui::Checkbox("Snap To Objects", &snapSettings.snapToObjects);
        tuningChanged |= ImGui::Checkbox("Snap To Centers", &snapSettings.snapToCenters);
        tuningChanged |= ImGui::Checkbox("Snap To Canvas Center", &snapSettings.snapToCanvasCenter);
        tuningChanged |= ImGui::Checkbox("Snap To Export Bounds", &snapSettings.snapToExportBounds);
        
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        
        // Input Floats for Step Tuning
        ImGui::SetNextItemWidth(rightColWidth * 0.7f);
        if (ImGui::DragFloat("Rotate Step", &snapSettings.rotateSnapStep, 1.0f, 0.0f, 180.0f, "%.0f deg")) {
            snapSettings.rotateSnapStep = std::clamp(snapSettings.rotateSnapStep, 0.0f, 180.0f);
            if (snapSettings.rotateSnapStep > 0.0f) {
                snapSettings.lastNonZeroRotateSnapStep = snapSettings.rotateSnapStep;
            }
            tuningChanged = true;
        }
        
        ImGui::SetNextItemWidth(rightColWidth * 0.7f);
        if (ImGui::DragFloat("Scale Step", &snapSettings.scaleSnapStep, 0.01f, 0.0f, 1.0f, "%.2f")) {
            snapSettings.scaleSnapStep = std::clamp(snapSettings.scaleSnapStep, 0.0f, 1.0f);
            if (snapSettings.scaleSnapStep > 0.0f) {
                snapSettings.lastNonZeroScaleSnapStep = snapSettings.scaleSnapStep;
            }
            tuningChanged = true;
        }
        
        if (tuningChanged) {
            snapSettings.enabled =
                snapSettings.snapToObjects ||
                snapSettings.snapToCenters ||
                snapSettings.snapToCanvasCenter ||
                snapSettings.snapToExportBounds ||
                snapSettings.rotateSnapStep > 0.0f ||
                snapSettings.scaleSnapStep > 0.0f;
        }
        
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        
        // Info about modes
        ImGui::TextDisabled("Resize Mode: %s", editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Stretch ? "Stretch" : "Scale");
        ImGui::TextDisabled("Origin Mode: %s", editor->GetCompositeScaleOriginMode() == EditorModule::CompositeScaleOriginMode::Center ? "Center" : "Opposite");
    }
    
    ImGui::Columns(1);
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
