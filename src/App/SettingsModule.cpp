#include "SettingsModule.h"

#include "Utils/FileDialogs.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>

namespace {

struct ColorBinding {
    ImGuiCol index;
    const char* label;
};

constexpr ColorBinding kFoundationColors[] = {
    { ImGuiCol_Text, "Text" },
    { ImGuiCol_TextDisabled, "Text Disabled" },
    { ImGuiCol_WindowBg, "Window Background" },
    { ImGuiCol_ChildBg, "Child Background" },
    { ImGuiCol_PopupBg, "Popup Background" },
    { ImGuiCol_Border, "Border" },
    { ImGuiCol_BorderShadow, "Border Shadow" },
};

constexpr ColorBinding kFrameColors[] = {
    { ImGuiCol_FrameBg, "Frame Background" },
    { ImGuiCol_FrameBgHovered, "Frame Hovered" },
    { ImGuiCol_FrameBgActive, "Frame Active" },
    { ImGuiCol_CheckMark, "Check Mark" },
    { ImGuiCol_SliderGrab, "Slider Grab" },
    { ImGuiCol_SliderGrabActive, "Slider Active" },
};

constexpr ColorBinding kControlColors[] = {
    { ImGuiCol_Button, "Button" },
    { ImGuiCol_ButtonHovered, "Button Hovered" },
    { ImGuiCol_ButtonActive, "Button Active" },
    { ImGuiCol_Header, "Header" },
    { ImGuiCol_HeaderHovered, "Header Hovered" },
    { ImGuiCol_HeaderActive, "Header Active" },
};

constexpr ColorBinding kBarColors[] = {
    { ImGuiCol_TitleBg, "Title Background" },
    { ImGuiCol_TitleBgActive, "Title Active" },
    { ImGuiCol_TitleBgCollapsed, "Title Collapsed" },
    { ImGuiCol_MenuBarBg, "Menu Bar Background" },
    { ImGuiCol_Tab, "Tab" },
    { ImGuiCol_TabHovered, "Tab Hovered" },
    { ImGuiCol_TabActive, "Tab Active" },
    { ImGuiCol_TabUnfocused, "Tab Unfocused" },
    { ImGuiCol_TabUnfocusedActive, "Tab Unfocused Active" },
};

constexpr ColorBinding kScrollAndSeparatorColors[] = {
    { ImGuiCol_ScrollbarBg, "Scrollbar Background" },
    { ImGuiCol_ScrollbarGrab, "Scrollbar Grab" },
    { ImGuiCol_ScrollbarGrabHovered, "Scrollbar Grab Hovered" },
    { ImGuiCol_ScrollbarGrabActive, "Scrollbar Grab Active" },
    { ImGuiCol_Separator, "Separator" },
    { ImGuiCol_SeparatorHovered, "Separator Hovered" },
    { ImGuiCol_SeparatorActive, "Separator Active" },
    { ImGuiCol_ResizeGrip, "Resize Grip" },
    { ImGuiCol_ResizeGripHovered, "Resize Grip Hovered" },
    { ImGuiCol_ResizeGripActive, "Resize Grip Active" },
};

constexpr ColorBinding kDockAndDataColors[] = {
    { ImGuiCol_DockingPreview, "Docking Preview" },
    { ImGuiCol_DockingEmptyBg, "Docking Empty Background" },
    { ImGuiCol_PlotLines, "Plot Lines" },
    { ImGuiCol_PlotLinesHovered, "Plot Lines Hovered" },
    { ImGuiCol_PlotHistogram, "Plot Histogram" },
    { ImGuiCol_PlotHistogramHovered, "Plot Histogram Hovered" },
    { ImGuiCol_TableHeaderBg, "Table Header" },
    { ImGuiCol_TableBorderStrong, "Table Border Strong" },
    { ImGuiCol_TableBorderLight, "Table Border Light" },
    { ImGuiCol_TableRowBg, "Table Row" },
    { ImGuiCol_TableRowBgAlt, "Table Row Alt" },
    { ImGuiCol_TextSelectedBg, "Text Selected" },
    { ImGuiCol_DragDropTarget, "Drag and Drop Target" },
    { ImGuiCol_NavHighlight, "Nav Highlight" },
    { ImGuiCol_NavWindowingHighlight, "Windowing Highlight" },
    { ImGuiCol_NavWindowingDimBg, "Windowing Dim Background" },
    { ImGuiCol_ModalWindowDimBg, "Modal Dim Background" },
};

bool RenderColorGroup(StackAppearance::ThemeDefinition& theme, const char* title, const ColorBinding* bindings, std::size_t count) {
    bool changed = false;
    ImGui::SeparatorText(title);
    for (std::size_t index = 0; index < count; ++index) {
        ImVec4& color = theme.colors[static_cast<std::size_t>(bindings[index].index)];
        changed |= ImGui::ColorEdit4(bindings[index].label, &color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    }
    return changed;
}

bool DragFloatControl(const char* label, float& value, float speed, float minValue, float maxValue, const char* format = "%.3f") {
    return ImGui::DragFloat(label, &value, speed, minValue, maxValue, format);
}

bool DragVec2Control(const char* label, ImVec2& value, float speed, float minValue, float maxValue, const char* format = "%.3f") {
    return ImGui::DragFloat2(label, &value.x, speed, minValue, maxValue, format);
}

bool CheckboxControl(const char* label, bool& value) {
    return ImGui::Checkbox(label, &value);
}

std::string BuildPresetExportName(const std::string& displayName) {
    std::string stem;
    stem.reserve(displayName.size());
    for (unsigned char ch : displayName) {
        if (std::isalnum(ch)) {
            stem.push_back(static_cast<char>(ch));
        } else if (!stem.empty() && stem.back() != '_') {
            stem.push_back('_');
        }
    }

    while (!stem.empty() && stem.back() == '_') {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "theme_preset";
    }
    return stem + ".stacktheme.json";
}

} // namespace

SettingsModule::SettingsModule() = default;

void SettingsModule::Initialize(StackAppearance::AppearanceManager* appearanceManager) {
    m_Appearance = appearanceManager;
    m_StatusMessage.clear();
    m_StatusIsError = false;
    SyncPresetNameBuffer();
}

void SettingsModule::Shutdown() {
    m_Appearance = nullptr;
    m_PresetNameBuffer[0] = '\0';
    m_StatusMessage.clear();
    m_StatusIsError = false;
}

void SettingsModule::SyncPresetNameBuffer() {
    if (m_Appearance == nullptr) {
        m_PresetNameBuffer[0] = '\0';
        return;
    }

    const std::string displayName = m_Appearance->GetWorkingTheme().displayName;
    std::snprintf(m_PresetNameBuffer, sizeof(m_PresetNameBuffer), "%s", displayName.c_str());
}

void SettingsModule::ApplyCurrentTheme() {
    if (m_Appearance == nullptr) {
        return;
    }

    m_Appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
}

void SettingsModule::SetStatus(const std::string& message, bool isError) {
    m_StatusMessage = message;
    m_StatusIsError = isError;
}

void SettingsModule::RenderPresetLibrary() {
    if (m_Appearance == nullptr) {
        return;
    }

    const StackAppearance::AppearanceLibrary& library = m_Appearance->GetLibrary();
    const std::string activePresetId = m_Appearance->GetActivePresetId();

    ImGui::TextUnformatted("Preset Library");
    ImGui::TextDisabled("Select a preset to load it into the live preview.");
    ImGui::Spacing();

    ImGui::TextDisabled("Factory presets");
    for (const StackAppearance::ThemeDefinition& preset : m_Appearance->GetFactoryThemes()) {
        ImGui::PushID(preset.id.c_str());
        const bool selected = activePresetId == preset.id;
        if (ImGui::Selectable(preset.displayName.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f))) {
            if (m_Appearance->SelectPresetById(preset.id)) {
                SyncPresetNameBuffer();
                ApplyCurrentTheme();
                SetStatus("Loaded preset \"" + preset.displayName + "\".");
            } else {
                SetStatus("Unable to select the preset.", true);
            }
        }
        ImGui::TextDisabled("Read-only factory preset");
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (library.customPresets.empty()) {
        ImGui::TextDisabled("No custom presets yet.");
    } else {
        for (const StackAppearance::ThemeDefinition& preset : library.customPresets) {
            ImGui::PushID(preset.id.c_str());
            const bool selected = activePresetId == preset.id;
            if (ImGui::Selectable(preset.displayName.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f))) {
                if (m_Appearance->SelectPresetById(preset.id)) {
                    SyncPresetNameBuffer();
                    ApplyCurrentTheme();
                    SetStatus("Loaded preset \"" + preset.displayName + "\".");
                } else {
                    SetStatus("Unable to load the selected preset.", true);
                }
            }
            if (selected && !m_Appearance->HasUnsavedChanges()) {
                ImGui::SameLine();
                ImGui::TextDisabled("Active");
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Live edits are not saved until you press Save or Save As New.");
}

void SettingsModule::RenderThemeEditor() {
    if (m_Appearance == nullptr) {
        ImGui::TextDisabled("Appearance manager is not available.");
        return;
    }

    StackAppearance::ThemeDefinition& theme = m_Appearance->EditWorkingTheme();
    bool previewNeedsApply = false;

    ImGui::TextUnformatted("Appearance");
    ImGui::TextDisabled("Edit the current theme live. Save writes back to the preset library.");
    ImGui::Spacing();

    if (!m_StatusMessage.empty()) {
        const ImVec4 statusColor = m_StatusIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.68f, 0.86f, 1.0f, 1.0f);
        ImGui::TextColored(statusColor, "%s", m_StatusMessage.c_str());
    }

    const bool isDirty = m_Appearance->HasUnsavedChanges();
    if (isDirty) {
        ImGui::TextColored(ImVec4(0.92f, 0.72f, 0.35f, 1.0f), "Unsaved changes");
    } else {
        ImGui::TextDisabled("No unsaved changes");
    }

    ImGui::Spacing();
    ImGui::InputText("Preset Name", m_PresetNameBuffer, sizeof(m_PresetNameBuffer));
    if (theme.displayName != m_PresetNameBuffer) {
        theme.displayName = m_PresetNameBuffer;
        previewNeedsApply = true;
    }

    ImGui::TextDisabled("Preset ID: %s", theme.id.c_str());
    ImGui::TextDisabled("%s", theme.readOnly
        ? "Factory presets are read-only. Use Save As New to create an editable copy."
        : "Custom preset.");

    ImGui::Spacing();

    const bool canSave = !m_Appearance->ActivePresetIsFactory();
    if (ImGui::Button("Apply", ImVec2(110.0f, 0.0f))) {
        ApplyCurrentTheme();
        SetStatus("Applied the current preview.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canSave || !isDirty);
    if (ImGui::Button("Save", ImVec2(110.0f, 0.0f))) {
        if (m_Appearance->SaveWorkingTheme()) {
            SyncPresetNameBuffer();
            ApplyCurrentTheme();
            SetStatus("Preset saved.");
        } else {
            SetStatus("Unable to save the active preset.", true);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Save As New", ImVec2(130.0f, 0.0f))) {
        if (m_Appearance->SaveWorkingThemeAsNew(m_PresetNameBuffer)) {
            SyncPresetNameBuffer();
            ApplyCurrentTheme();
            SetStatus("Saved a new preset.");
        } else {
            SetStatus("Unable to create a new preset.", true);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Duplicate", ImVec2(110.0f, 0.0f))) {
        if (m_Appearance->DuplicateWorkingTheme()) {
            SyncPresetNameBuffer();
            ApplyCurrentTheme();
            SetStatus("Duplicated the current preset.");
        } else {
            SetStatus("Unable to duplicate the preset.", true);
        }
    }

    ImGui::Spacing();

    if (ImGui::Button("Reset", ImVec2(110.0f, 0.0f))) {
        if (m_Appearance->ResetWorkingTheme()) {
            SyncPresetNameBuffer();
            ApplyCurrentTheme();
            SetStatus("Reset the live preview.");
        } else {
            SetStatus("Unable to reset the preview.", true);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Export", ImVec2(110.0f, 0.0f))) {
        const std::string defaultFileName = BuildPresetExportName(theme.displayName);
        const std::string path = FileDialogs::SaveThemePresetFileDialog("Export Theme Preset", defaultFileName.c_str());
        if (!path.empty()) {
            std::string errorMessage;
            if (m_Appearance->ExportWorkingTheme(path, &errorMessage)) {
                SetStatus("Exported preset to " + path + ".");
            } else {
                SetStatus(errorMessage.empty() ? "Unable to export the preset." : errorMessage, true);
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Import", ImVec2(110.0f, 0.0f))) {
        const std::string path = FileDialogs::OpenThemePresetFileDialog("Import Theme Preset");
        if (!path.empty()) {
            std::string errorMessage;
            if (m_Appearance->ImportPreset(path, &errorMessage)) {
                SyncPresetNameBuffer();
                ApplyCurrentTheme();
                SetStatus("Imported preset from " + path + ".");
            } else {
                SetStatus(errorMessage.empty() ? "Unable to import the preset." : errorMessage, true);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    previewNeedsApply |= RenderColorGroup(theme, "Foundations", kFoundationColors, std::size(kFoundationColors));
    previewNeedsApply |= RenderColorGroup(theme, "Frames", kFrameColors, std::size(kFrameColors));
    previewNeedsApply |= RenderColorGroup(theme, "Controls", kControlColors, std::size(kControlColors));
    previewNeedsApply |= RenderColorGroup(theme, "Bars and Tabs", kBarColors, std::size(kBarColors));
    previewNeedsApply |= RenderColorGroup(theme, "Scroll and Separators", kScrollAndSeparatorColors, std::size(kScrollAndSeparatorColors));
    previewNeedsApply |= RenderColorGroup(theme, "Docking and Data", kDockAndDataColors, std::size(kDockAndDataColors));

    ImGui::Spacing();
    ImGui::SeparatorText("Layout and Typography");

    if (DragFloatControl("Alpha", theme.style.alpha, 0.01f, 0.0f, 1.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Window Padding", theme.style.windowPadding, 0.25f, 0.0f, 48.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Frame Padding", theme.style.framePadding, 0.20f, 0.0f, 40.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Cell Padding", theme.style.cellPadding, 0.20f, 0.0f, 40.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Item Spacing", theme.style.itemSpacing, 0.20f, 0.0f, 48.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Item Inner Spacing", theme.style.itemInnerSpacing, 0.20f, 0.0f, 48.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Indent Spacing", theme.style.indentSpacing, 0.25f, 0.0f, 80.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Scrollbar Size", theme.style.scrollbarSize, 0.10f, 4.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Grab Min Size", theme.style.grabMinSize, 0.10f, 4.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Borders and Rounding");

    if (DragFloatControl("Window Border Size", theme.style.windowBorderSize, 0.05f, 0.0f, 4.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Child Border Size", theme.style.childBorderSize, 0.05f, 0.0f, 4.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Popup Border Size", theme.style.popupBorderSize, 0.05f, 0.0f, 4.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Frame Border Size", theme.style.frameBorderSize, 0.05f, 0.0f, 4.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Tab Border Size", theme.style.tabBorderSize, 0.05f, 0.0f, 4.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Window Rounding", theme.style.windowRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Child Rounding", theme.style.childRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Frame Rounding", theme.style.frameRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Popup Rounding", theme.style.popupRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Scrollbar Rounding", theme.style.scrollbarRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Grab Rounding", theme.style.grabRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Tab Rounding", theme.style.tabRounding, 0.10f, 0.0f, 24.0f, "%.1f")) {
        previewNeedsApply = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Alignment and Motion");

    if (DragVec2Control("Window Title Align", theme.style.windowTitleAlign, 0.01f, 0.0f, 1.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Button Text Align", theme.style.buttonTextAlign, 0.01f, 0.0f, 1.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (DragVec2Control("Selectable Text Align", theme.style.selectableTextAlign, 0.01f, 0.0f, 1.0f, "%.2f")) {
        previewNeedsApply = true;
    }
    if (CheckboxControl("Anti-Aliased Lines", theme.style.antiAliasedLines)) {
        previewNeedsApply = true;
    }
    if (CheckboxControl("Anti-Aliased Fill", theme.style.antiAliasedFill)) {
        previewNeedsApply = true;
    }
    if (DragFloatControl("Curve Tessellation Tol", theme.style.curveTessellationTol, 0.01f, 0.20f, 4.00f, "%.2f")) {
        previewNeedsApply = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Typography");
    if (ImGui::SliderFloat("Text Scale", &theme.textScale, 0.75f, 1.60f, "%.2f")) {
        previewNeedsApply = true;
    }
    ImGui::TextDisabled("The bundled Roboto font stays fixed. Text scale changes the live font scale only.");

    if (previewNeedsApply) {
        ApplyCurrentTheme();
    }
}

void SettingsModule::RenderAppearancePanel() {
    if (m_Appearance == nullptr) {
        ImGui::TextDisabled("Appearance manager is not available.");
        return;
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float libraryWidth = std::clamp(available.x * 0.30f, 260.0f, 360.0f);

    ImGui::BeginChild("AppearancePresetLibrary", ImVec2(libraryWidth, 0.0f), true);
    RenderPresetLibrary();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("AppearanceThemeEditor", ImVec2(0.0f, 0.0f), true);
    RenderThemeEditor();
    ImGui::EndChild();
}

void SettingsModule::RenderUI() {
    ImGui::TextUnformatted("Settings");
    ImGui::TextDisabled("Global application preferences.");
    ImGui::Spacing();

    if (ImGui::BeginTabBar("SettingsPanels", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Appearance")) {
            RenderAppearancePanel();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
