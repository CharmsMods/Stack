#include "AppSettingsPopup.h"

#include "AppVersion.h"
#include "../Editor/EditorModule.h"
#include "../Utils/FileDialogs.h"
#include "../Utils/ImGuiExtras.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace AppSettingsPopup {
namespace {

constexpr float kSettingsItemGap = 7.0f;
constexpr float kSettingsGroupGap = 14.0f;
constexpr float kSettingsControlWidth = 280.0f;

void RenderSectionIntro(const char* label, const char* description) {
    ImGuiExtras::RichSectionLabel(label);
    ImGui::TextWrapped("%s", description);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
}

void RenderSubsectionLabel(const char* label) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kSettingsGroupGap - kSettingsItemGap);
    ImGuiExtras::RichSectionLabel(label);
}

bool SeamlessSurfaceStylingEnabled(const StackAppearance::AppearanceManager* appearance) {
    return appearance && appearance->GetSeamlessSurfaceStylingEnabled();
}

std::string TruncateToWidth(const std::string& value, const float maxWidth) {
    if (ImGui::CalcTextSize(value.c_str()).x <= maxWidth) {
        return value;
    }
    constexpr const char* suffix = "...";
    std::string truncated = value;
    while (!truncated.empty()) {
        truncated.pop_back();
        while (!truncated.empty() &&
               (static_cast<unsigned char>(truncated.back()) & 0xC0u) == 0x80u) {
            truncated.pop_back();
        }
        const std::string candidate = truncated + suffix;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return suffix;
}

void ApplyTheme(StackAppearance::AppearanceManager* appearance) {
    if (!appearance) {
        return;
    }
    appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
}

bool RenderCategoryButton(
    StackAppearance::AppearanceManager* appearance,
    const char* label,
    const bool selected,
    const ImVec2& size) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 actualSize(
        size.x < 0.0f ? std::max(1.0f, ImGui::GetContentRegionAvail().x) : size.x,
        size.y);
    const bool pressed = ImGui::InvisibleButton(label, actualSize);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 textColor = ImGui::GetColorU32(
        selected
            ? ImGuiCol_CheckMark
            : (hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2(start.x + 4.0f, start.y + (actualSize.y - textSize.y) * 0.5f),
        textColor,
        label);
    return pressed;
}

void RenderThemeCards(StackAppearance::AppearanceManager* appearance, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Appearance settings are unavailable.");
        return;
    }

    const std::string activePresetId = appearance->GetActivePresetId();
    const std::vector<StackAppearance::ThemeDefinition>& factoryThemes = appearance->GetFactoryThemes();
    const std::vector<StackAppearance::ThemeDefinition>& customThemes = appearance->GetLibrary().customPresets;
    std::vector<const StackAppearance::ThemeDefinition*> themes;
    themes.reserve(factoryThemes.size() + customThemes.size());
    for (const auto& theme : factoryThemes) themes.push_back(&theme);
    for (const auto& theme : customThemes) themes.push_back(&theme);

    constexpr float columnGap = 48.0f;
    const int columns = contentWidth >= 420.0f ? 2 : 1;
    const float listWidth = std::min(contentWidth, columns == 2 ? 520.0f : 260.0f);
    const float cellWidth = columns == 2
        ? (listWidth - columnGap) * 0.5f
        : listWidth;
    const float startX = ImGui::GetCursorPosX() + std::max(0.0f, (contentWidth - listWidth) * 0.5f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (std::size_t index = 0; index < themes.size(); ++index) {
        if (columns == 2 && (index % 2) == 1) {
            ImGui::SameLine(0.0f, columnGap);
        } else {
            ImGui::SetCursorPosX(startX);
        }
        const auto& theme = *themes[index];
        const bool active = activePresetId == theme.id;
        ImGui::PushID(theme.id.c_str());
        const ImVec2 itemMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##ThemeChoice", ImVec2(cellWidth, 42.0f));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 textSize = ImGui::CalcTextSize(theme.displayName.c_str());
        drawList->AddText(
            ImVec2(
                itemMin.x + (cellWidth - textSize.x) * 0.5f,
                itemMin.y + (42.0f - textSize.y) * 0.5f),
            ImGui::GetColorU32(
                active
                    ? ImGuiCol_CheckMark
                    : (hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled)),
            theme.displayName.c_str());
        if (ImGui::IsItemClicked() && !active && appearance->SelectPresetById(theme.id)) {
            ApplyTheme(appearance);
        }
        ImGui::PopID();
    }
}

void RenderAppearanceSection(StackAppearance::AppearanceManager* appearance, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Appearance settings are unavailable.");
        return;
    }

    RenderThemeCards(appearance, contentWidth);
}

void RenderExperimentalSection(
    StackAppearance::AppearanceManager* appearance,
    const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Experimental settings are unavailable.");
        return;
    }

    bool islandEnabled = appearance->GetExperimentalIslandEnabled();
    const float checkboxWidth =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
        ImGui::CalcTextSize("Island").x;
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + std::max(0.0f, (contentWidth - checkboxWidth) * 0.5f));
    if (ImGui::Checkbox("Island", &islandEnabled)) {
        appearance->SetExperimentalIslandEnabled(islandEnabled);
    }
}

void RenderBackgroundSection(StackAppearance::AppearanceManager* appearance, State& state, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Background settings are unavailable.");
        return;
    }

    const float startX = ImGui::GetCursorPosX();
    const float controlBlockWidth = std::min(contentWidth, 520.0f);
    const float controlX = startX + std::max(0.0f, (contentWidth - controlBlockWidth) * 0.5f);
    bool backgroundImageEnabled = appearance->GetBackgroundImageEnabled();
    ImGui::SetCursorPosX(controlX);
    if (ImGui::Checkbox("Background image", &backgroundImageEnabled)) {
        appearance->SetBackgroundImageEnabled(backgroundImageEnabled);
    }

    const std::string managedPath = appearance->GetBackgroundImagePath();
    const bool hasManagedImage = !managedPath.empty();
    ImGui::SetCursorPosX(controlX);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Button("Add Image")) {
        const std::string path = FileDialogs::OpenImageFileDialog("Add Background Image");
        if (!path.empty()) {
            std::string errorMessage;
            if (!appearance->ImportBackgroundImageFromPath(path, &errorMessage)) {
                state.lastActionError = errorMessage;
            } else {
                state.lastActionError.clear();
            }
        }
    }
    ImGui::PopStyleColor(3);

    float backgroundStrength = appearance->GetBackgroundImageStrength();
    ImGui::SetCursorPosX(controlX);
    ImGui::SetNextItemWidth(std::min(controlBlockWidth, kSettingsControlWidth));
    if (ImGui::SliderFloat("Background Image Strength", &backgroundStrength, 0.0f, 1.0f, "%.2f")) {
        appearance->SetBackgroundImageStrength(backgroundStrength);
    }

    float uiSurfaceTransparency = appearance->GetUiSurfaceTransparency();
    ImGui::SetCursorPosX(controlX);
    ImGui::SetNextItemWidth(std::min(controlBlockWidth, kSettingsControlWidth));
    if (ImGui::SliderFloat("UI Surface Transparency", &uiSurfaceTransparency, 0.0f, 1.0f, "%.2f")) {
        if (appearance->SetUiSurfaceTransparency(uiSurfaceTransparency)) {
            ApplyTheme(appearance);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    const std::vector<StackAppearance::BackgroundImageEntry>& images = appearance->GetBackgroundImages();
    if (images.empty()) {
        ImGui::SetCursorPosX(controlX);
        ImGui::TextDisabled("No background images have been added yet.");
    } else {
        constexpr float columnGap = 16.0f;
        constexpr float rowHeight = 27.0f;
        const float listWidth = std::min(contentWidth, 520.0f);
        const float listX = startX + std::max(0.0f, (contentWidth - listWidth) * 0.5f);
        const float itemWidth = (listWidth - columnGap) * 0.5f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (std::size_t index = 0; index < images.size(); ++index) {
            const StackAppearance::BackgroundImageEntry& image = images[index];
            if ((index % 2) == 1) {
                ImGui::SameLine(0.0f, columnGap);
            } else {
                ImGui::SetCursorPosX(listX);
            }

            const bool selected = image.path == managedPath;
            ImGui::PushID(image.id.c_str());
            const ImVec2 itemMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##BackgroundChoice", ImVec2(itemWidth, rowHeight));
            const bool hovered = ImGui::IsItemHovered();
            const std::string label = image.displayName.empty()
                ? std::filesystem::path(image.path).filename().string()
                : image.displayName;
            const std::string visibleLabel = TruncateToWidth(label, itemWidth - 8.0f);
            drawList->AddText(
                ImVec2(itemMin.x + 4.0f, itemMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(
                    selected
                        ? ImGuiCol_CheckMark
                        : (hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled)),
                visibleLabel.c_str());
            if (ImGui::IsItemClicked() && appearance->SelectBackgroundImageById(image.id)) {
                state.lastActionError.clear();
            }
            if (hovered) {
                ImGui::SetTooltip(
                    "%s%s",
                    label.c_str(),
                    selected ? (backgroundImageEnabled ? "\nSelected" : "\nSelected, disabled") : "");
            }
            ImGui::PopID();
        }

        if (hasManagedImage) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::SetCursorPosX(controlX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            if (ImGui::Button("Remove Selected")) {
                const auto activeIt = std::find_if(
                    images.begin(),
                    images.end(),
                    [&](const StackAppearance::BackgroundImageEntry& entry) {
                        return entry.path == managedPath;
                    });
                if (activeIt != images.end()) {
                    std::string errorMessage;
                    if (!appearance->RemoveBackgroundImageById(activeIt->id, &errorMessage)) {
                        state.lastActionError = errorMessage;
                    } else {
                        state.lastActionError.clear();
                    }
                }
            }
            ImGui::PopStyleColor(3);
        }
    }

    if (!state.lastActionError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.48f, 0.48f, 1.0f));
        ImGui::TextWrapped("%s", state.lastActionError.c_str());
        ImGui::PopStyleColor();
    }
}

void RenderGraphModeButtons(StackAppearance::AppearanceManager* appearance, const float contentWidth) {
    StackAppearance::GraphVisualMode graphMode = appearance->GetGraphVisualMode();
    const StackAppearance::GraphVisualMode graphModes[] = {
        StackAppearance::GraphVisualMode::Classic,
        StackAppearance::GraphVisualMode::BlackNodes,
        StackAppearance::GraphVisualMode::SpotlightPrototype
    };

    const float buttonGap = 8.0f;
    constexpr float buttonWidth = 166.0f;
    const bool stackButtons = contentWidth < buttonWidth * 3.0f + buttonGap * 2.0f;
    for (int i = 0; i < IM_ARRAYSIZE(graphModes); ++i) {
        if (i > 0 && !stackButtons) {
            ImGui::SameLine(0.0f, buttonGap);
        }
        const StackAppearance::GraphVisualMode candidate = graphModes[i];
        const bool selected = graphMode == candidate;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }
        if (ImGui::Button(StackAppearance::GraphVisualModeLabel(candidate), ImVec2(buttonWidth, 0.0f))) {
            if (appearance->SetGraphVisualMode(candidate)) {
                graphMode = candidate;
            }
        }
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", StackAppearance::GraphVisualModeDescription(candidate));
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", StackAppearance::GraphVisualModeDescription(graphMode));
    ImGui::PopStyleColor();

    if (graphMode == StackAppearance::GraphVisualMode::SpotlightPrototype) {
        bool haloOutlines = appearance->GetGraphSpotlightHaloOutlines();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Checkbox("Halo edge outlines", &haloOutlines)) {
            appearance->SetGraphSpotlightHaloOutlines(haloOutlines);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Draw a faint edge halo around spotlight nodes.");
        }
    }
}

void RenderGraphSection(StackAppearance::AppearanceManager* appearance, EditorModule* editor, const float contentWidth) {
    if (!appearance || !editor) {
        ImGui::TextDisabled("Graph settings are unavailable.");
        return;
    }

    RenderSectionIntro("GRAPH", "Choose how graph nodes render and which live graph diagnostics stay available while you work.");

    ImGuiExtras::RichSectionLabel("VISUAL MODE");
    RenderGraphModeButtons(appearance, contentWidth);

    RenderSubsectionLabel("CONNECTIONS");

    bool dottedMaskLinks = appearance->GetGraphDottedMaskLinks();
    if (ImGui::Checkbox("Dotted mask-endpoint links", &dottedMaskLinks)) {
        appearance->SetGraphDottedMaskLinks(dottedMaskLinks);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Render links as dotted when either endpoint is a mask-typed graph socket.");
    }

    bool straightLinks = appearance->GetGraphStraightLinks();
    if (ImGui::Checkbox("Straight connection lines", &straightLinks)) {
        appearance->SetGraphStraightLinks(straightLinks);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Render graph links as direct straight lines instead of curved connections.");
    }

    ImGui::TextDisabled("Hold F to reveal connection labels.");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The informational text above and below each connection fades in while F is held, then quickly fades out when it is released.");
    }

    StackAppearance::GraphConnectionTextLayout connectionTextLayout =
        appearance->GetGraphConnectionTextLayout();
    const char* connectionLayoutNames[] = { "Floating", "Break Line" };
    int connectionLayoutIndex = static_cast<int>(connectionTextLayout);
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::Combo(
            "Connection text layout",
            &connectionLayoutIndex,
            connectionLayoutNames,
            IM_ARRAYSIZE(connectionLayoutNames))) {
        appearance->SetGraphConnectionTextLayout(
            static_cast<StackAppearance::GraphConnectionTextLayout>(connectionLayoutIndex));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Floating places plain text over the wire. Break Line opens a safe centered gap when the wire is long enough.");
    }

    float connectionTextSize = appearance->GetGraphConnectionTextSize();
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::SliderFloat(
            "Connection text size",
            &connectionTextSize,
            StackAppearance::kGraphConnectionTextSizeMin,
            StackAppearance::kGraphConnectionTextSizeMax,
            "%.0f px")) {
        appearance->SetGraphConnectionTextSize(connectionTextSize);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Set the base screen size for the text drawn above and below graph connections.");
    }

    StackAppearance::GraphConnectionTextSizing connectionTextSizing =
        appearance->GetGraphConnectionTextSizing();
    const char* connectionSizingNames[] = { "Zoom-Aware", "Fixed" };
    int connectionSizingIndex = static_cast<int>(connectionTextSizing);
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::Combo(
            "Connection text sizing",
            &connectionSizingIndex,
            connectionSizingNames,
            IM_ARRAYSIZE(connectionSizingNames))) {
        appearance->SetGraphConnectionTextSizing(
            static_cast<StackAppearance::GraphConnectionTextSizing>(connectionSizingIndex));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Zoom-Aware scales text moderately with the graph. Fixed keeps the selected screen-pixel size.");
    }

    bool connectionTextOutline = appearance->GetGraphConnectionTextOutline();
    if (ImGui::Checkbox("Connection text outline", &connectionTextOutline)) {
        appearance->SetGraphConnectionTextOutline(connectionTextOutline);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw a subtle dark readability edge around connection text.");
    }

    RenderSubsectionLabel("INTERACTION & DIAGNOSTICS");

    float graphLineOpacity = appearance->GetGraphLineOpacity();
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::SliderFloat("Graph Line Opacity", &graphLineOpacity, 0.0f, 1.0f, "%.2f")) {
        appearance->SetGraphLineOpacity(graphLineOpacity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Adjust the graph grid line intensity without changing node or pane opacity.");
    }

    float graphPanSensitivity = appearance->GetGraphPanSensitivity();
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::SliderFloat("Middle-Mouse Pan Sensitivity", &graphPanSensitivity, 0.10f, 1.0f, "%.2f")) {
        appearance->SetGraphPanSensitivity(graphPanSensitivity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Adjust how far the graph moves while panning with the middle mouse button.");
    }

    float nodeSliderDragSensitivity = appearance->GetGraphNodeSliderDragSensitivity();
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::SliderFloat(
            "Node Slider Drag Sensitivity",
            &nodeSliderDragSensitivity,
            StackAppearance::kGraphNodeSliderDragSensitivityMin,
            StackAppearance::kGraphNodeSliderDragSensitivityMax,
            "%.2f",
            ImGuiSliderFlags_Logarithmic)) {
        appearance->SetGraphNodeSliderDragSensitivity(nodeSliderDragSensitivity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Adjust on-node slider drag speed. 1.00 is standard; lower values provide finer control. Graph zoom does not change this sensitivity.");
    }

    bool showGraphPerf = editor->GetGraphPerformancePopupEnabled();
    if (ImGui::Checkbox("Graph performance popup", &showGraphPerf)) {
        editor->SetGraphPerformancePopupEnabled(showGraphPerf);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show live graph render invalidation, queue, and cache stats over the graph.");
    }
}

void RenderViewportSection(StackAppearance::AppearanceManager* appearance, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Viewport settings are unavailable.");
        return;
    }

    RenderSectionIntro("VIEWPORT RENDERING", "Control tile-first rendering for the main single-output viewport.");

    ViewportTilingSettings settings = appearance->GetViewportTilingSettings();
    ViewportTilingMode mode = settings.mode;
    ImGui::SetNextItemWidth(std::min(contentWidth, 260.0f));
    if (ImGui::BeginCombo("Tiled Rendering", RenderTiling::ViewportTilingModeLabel(mode))) {
        const ViewportTilingMode modes[] = {
            ViewportTilingMode::Off,
            ViewportTilingMode::Auto,
            ViewportTilingMode::Always
        };
        for (ViewportTilingMode candidate : modes) {
            const bool selected = candidate == mode;
            if (ImGui::Selectable(RenderTiling::ViewportTilingModeLabel(candidate), selected)) {
                settings.mode = candidate;
                mode = candidate;
                appearance->SetViewportTilingSettings(settings);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto uses tile-first rendering for large tile-safe graphs. Unsupported graphs fall back to the standard renderer.");
    }

    int tileSize = settings.tileSize;
    ImGui::SetNextItemWidth(std::min(contentWidth, 220.0f));
    if (ImGui::DragInt("Tile Size", &tileSize, 64.0f, 256, 4096, "%d px")) {
        settings.tileSize = tileSize;
        appearance->SetViewportTilingSettings(settings);
    }

    int haloPixels = settings.haloPixels;
    ImGui::SetNextItemWidth(std::min(contentWidth, 220.0f));
    if (ImGui::DragInt("Extra Tile Halo", &haloPixels, 1.0f, 0, 256, "%d px")) {
        settings.haloPixels = haloPixels;
        appearance->SetViewportTilingSettings(settings);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Optional extra overlap. Stack now derives the required halo from supported graph operations automatically.");
    }

    int threshold = settings.autoPixelThresholdMegapixels;
    ImGui::SetNextItemWidth(std::min(contentWidth, 220.0f));
    if (ImGui::DragInt("Auto Threshold", &threshold, 1.0f, 1, 512, "%d MP")) {
        settings.autoPixelThresholdMegapixels = threshold;
        appearance->SetViewportTilingSettings(settings);
    }

    bool progressive = settings.progressive;
    if (ImGui::Checkbox("Progressive priority", &progressive)) {
        settings.progressive = progressive;
        appearance->SetViewportTilingSettings(settings);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Tile jobs are planned independently so viewport-priority ordering can be expanded without changing settings.");
    }

    bool debugOverlay = settings.debugOverlay;
    if (ImGui::Checkbox("Debug tile overlay", &debugOverlay)) {
        settings.debugOverlay = debugOverlay;
        appearance->SetViewportTilingSettings(settings);
    }
}

const char* CompositeSnapPresetLabel(EditorModule::CompositeSnapModePreset preset) {
    switch (preset) {
    case EditorModule::CompositeSnapModePreset::ObjectOnly: return "Object Only";
    case EditorModule::CompositeSnapModePreset::Full: return "Full";
    case EditorModule::CompositeSnapModePreset::Custom: return "Custom";
    case EditorModule::CompositeSnapModePreset::Off:
    default:
        return "Off";
    }
}

void RenderCanvasCompositionSection(EditorModule* editor, const float contentWidth) {
    RenderSectionIntro("CANVAS COMPOSITION", "Control snapping and transform stepping for the composition canvas.");

    if (!editor || editor->GetCompletedChainCount() < 2) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Canvas Composition settings become active when the editor has at least two completed chains available for composition.");
        ImGui::PopStyleColor();
        return;
    }

    EditorModule::CompositeSnapModePreset snapPreset = editor->GetCompositeSnapModePreset();
    static const EditorModule::CompositeSnapModePreset snapPresets[] = {
        EditorModule::CompositeSnapModePreset::Off,
        EditorModule::CompositeSnapModePreset::ObjectOnly,
        EditorModule::CompositeSnapModePreset::Full,
        EditorModule::CompositeSnapModePreset::Custom
    };

    ImGui::SetNextItemWidth(std::min(contentWidth, 280.0f));
    if (ImGui::BeginCombo("Snap Preset", CompositeSnapPresetLabel(snapPreset))) {
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

    auto& snapSettings = editor->GetMutableCompositeSnapSettings();
    bool tuningChanged = false;
    tuningChanged |= ImGui::Checkbox("Snap To Objects", &snapSettings.snapToObjects);
    tuningChanged |= ImGui::Checkbox("Snap To Centers", &snapSettings.snapToCenters);
    tuningChanged |= ImGui::Checkbox("Snap To Canvas Center", &snapSettings.snapToCanvasCenter);
    tuningChanged |= ImGui::Checkbox("Snap To Export Bounds", &snapSettings.snapToExportBounds);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    ImGui::SetNextItemWidth(std::min(contentWidth, 220.0f));
    if (ImGui::DragFloat("Rotate Step", &snapSettings.rotateSnapStep, 1.0f, 0.0f, 180.0f, "%.0f deg")) {
        snapSettings.rotateSnapStep = std::clamp(snapSettings.rotateSnapStep, 0.0f, 180.0f);
        if (snapSettings.rotateSnapStep > 0.0f) {
            snapSettings.lastNonZeroRotateSnapStep = snapSettings.rotateSnapStep;
        }
        tuningChanged = true;
    }

    ImGui::SetNextItemWidth(std::min(contentWidth, 220.0f));
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

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("Resize Mode: %s", editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Stretch ? "Stretch" : "Scale");
    ImGui::Text("Origin Mode: %s", editor->GetCompositeScaleOriginMode() == EditorModule::CompositeScaleOriginMode::Center ? "Center" : "Opposite");
    ImGui::PopStyleColor();
}

void RenderUpdateInstallPopup(AppUpdate::UpdateManager* updateManager, State& state) {
    if (state.showInstallConfirmPopup) {
        ImGui::OpenPopup("Install Update##Stack");
        state.showInstallConfirmPopup = false;
    }

    if (updateManager == nullptr) {
        return;
    }

    if (ImGui::BeginPopupModal("Install Update##Stack", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const AppUpdate::Snapshot& snapshot = updateManager->GetSnapshot();
        ImGui::TextWrapped("Stack needs to close to finish installing the update. Save your work before continuing.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextWrapped("Windows may ask for administrator permission to continue.");
        if (!snapshot.isInstalledBuild) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextWrapped("Portable build detected. This will launch the installer for a normal installed copy instead of overwriting the current folder.");
        }
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        const float popupWidth = ImGui::GetContentRegionAvail().x;
        const bool stackButtons = popupWidth < 270.0f;
        if (ImGui::Button("Install and Restart", ImVec2(stackButtons ? std::max(1.0f, popupWidth) : 160.0f, 0.0f))) {
            std::string errorMessage;
            if (!updateManager->InstallAndRestart(&errorMessage)) {
                state.lastActionError = errorMessage;
            } else {
                state.lastActionError.clear();
            }
            ImGui::CloseCurrentPopup();
        }

        if (stackButtons) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        } else {
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel", ImVec2(stackButtons ? std::max(1.0f, popupWidth) : 100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void RenderUpdatesSection(AppUpdate::UpdateManager* updateManager, State& state, const float contentWidth) {
    RenderSectionIntro("APP UPDATES", "Check GitHub Releases for new Stack installers, download them in the background, and hand off safely to the installer when you're ready.");

    if (updateManager == nullptr) {
        ImGui::TextDisabled("Update services are unavailable.");
        return;
    }

    const AppUpdate::Snapshot& snapshot = updateManager->GetSnapshot();
    ImGui::Text("Install Mode: %s", snapshot.isInstalledBuild ? "Installed" : "Portable");
    ImGui::Text("Current Version: %s", snapshot.currentVersion.c_str());
    ImGui::Text("Latest Version: %s", snapshot.latestVersion.empty() ? "Unknown" : snapshot.latestVersion.c_str());
    ImGui::Text("Last Update Check: %s", snapshot.lastCheckDisplay.empty() ? "Never" : snapshot.lastCheckDisplay.c_str());
    ImGui::Text("State: %s", AppUpdate::UpdateStateLabel(snapshot.state));

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (!snapshot.statusMessage.empty()) {
        ImGui::TextWrapped("%s", snapshot.statusMessage.c_str());
    }
    if (!snapshot.releaseName.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextWrapped("Release: %s", snapshot.releaseName.c_str());
    }
    if (!snapshot.releaseSummary.empty()) {
        ImGui::TextWrapped("%s", snapshot.releaseSummary.c_str());
    }
    if (!snapshot.selectedAssetName.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextWrapped("Selected Asset: %s", snapshot.selectedAssetName.c_str());
    }
    ImGui::PopStyleColor();

    RenderSubsectionLabel("ACTIONS");
    const float topActionRowWidth = 150.0f + 174.0f + 204.0f + 20.0f;
    const bool stackTopActions = contentWidth < topActionRowWidth;

    ImGui::BeginDisabled(!updateManager->CanCheckForUpdates());
    if (ImGui::Button("Check for Updates", ImVec2(150.0f, 0.0f))) {
        state.lastActionError.clear();
        updateManager->StartManualCheck();
    }
    ImGui::EndDisabled();

    if (!stackTopActions) {
        ImGui::SameLine(0.0f, 10.0f);
    }
    if (ImGui::Button("Open GitHub Releases", ImVec2(174.0f, 0.0f))) {
        state.lastActionError.clear();
        updateManager->OpenReleasesPage(&state.lastActionError);
    }

    if (!stackTopActions) {
        ImGui::SameLine(0.0f, 10.0f);
    }
    if (ImGui::Button("Open Website Download Page", ImVec2(204.0f, 0.0f))) {
        state.lastActionError.clear();
        updateManager->OpenWebsiteDownloadPage(&state.lastActionError);
    }

    if (updateManager->CanDownloadUpdate()) {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (ImGui::Button("Download Update", ImVec2(std::min(170.0f, std::max(1.0f, contentWidth)), 0.0f))) {
            state.lastActionError.clear();
            updateManager->DownloadUpdate();
        }
    }

    if (!snapshot.downloadedFilePath.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        const float downloadActionRowWidth = 170.0f + 190.0f + 10.0f;
        const bool stackDownloadActions = contentWidth < downloadActionRowWidth;
        if (updateManager->CanInstallUpdate()) {
            if (ImGui::Button("Install and Restart", ImVec2(stackDownloadActions ? std::max(1.0f, contentWidth) : 170.0f, 0.0f))) {
                state.showInstallConfirmPopup = true;
            }
            if (stackDownloadActions) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            } else {
                ImGui::SameLine(0.0f, 10.0f);
            }
        }
        if (ImGui::Button("Show Downloaded File", ImVec2(stackDownloadActions ? std::max(1.0f, contentWidth) : 190.0f, 0.0f))) {
            state.lastActionError.clear();
            updateManager->RevealDownloadedUpdate(&state.lastActionError);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (!snapshot.isInstalledBuild) {
        ImGui::TextWrapped("Portable copies stay conservative: Stack downloads the installer and lets you choose whether to switch to a normal installed version.");
    } else if (snapshot.verificationAvailable) {
        ImGui::TextWrapped("%s", snapshot.verificationPassed
            ? "The most recently downloaded update was verified against a published SHA-256 value."
            : "Verification is available when the release publishes a digest or SHA256SUMS file.");
    } else {
        ImGui::TextWrapped("Verification uses a published digest or SHA256SUMS file when the release provides one.");
    }
    ImGui::PopStyleColor();

    if (!state.lastActionError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.48f, 0.48f, 1.0f));
        ImGui::TextWrapped("%s", state.lastActionError.c_str());
        ImGui::PopStyleColor();
    }

    RenderUpdateInstallPopup(updateManager, state);
}

void RenderFooter() {
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("Stack Image Editor");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("Version %s", AppVersion::kVersionString);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextUnformatted("Renderer: OpenGL 4.3 Core");
    ImGui::PopStyleColor();
}

} // namespace

void RenderContents(
    StackAppearance::AppearanceManager* appearance,
    EditorModule* editor,
    AppUpdate::UpdateManager* updateManager,
    State& state) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool wallpaperSurfaces = SeamlessSurfaceStylingEnabled(appearance);
    const float footerHeight = 32.0f;
    const float railWidth = 176.0f;
    const float contentHeight = std::max(120.0f, avail.y - footerHeight);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, kSettingsItemGap));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, wallpaperSurfaces ? 8.0f : ImGui::GetStyle().FrameRounding);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("SettingsPopupBody", ImVec2(0.0f, contentHeight), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    if (wallpaperSurfaces) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    }
    ImGui::BeginChild(
        "SettingsPopupRail",
        ImVec2(railWidth, 0.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);
    constexpr float categoryHeights[] = { 32.0f, 32.0f, 32.0f, 32.0f, 36.0f, 32.0f, 32.0f };
    float categoryListHeight = 0.0f;
    for (float height : categoryHeights) categoryListHeight += height;
    categoryListHeight += kSettingsItemGap * 6.0f;
    ImGui::SetCursorPosY(std::max(
        ImGui::GetCursorPosY(),
        (ImGui::GetWindowHeight() - categoryListHeight) * 0.5f));
    if (RenderCategoryButton(appearance, "Appearance", state.activeCategory == Category::Appearance, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Appearance;
    }
    if (RenderCategoryButton(appearance, "Background", state.activeCategory == Category::Background, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Background;
    }
    if (RenderCategoryButton(appearance, "Graph", state.activeCategory == Category::Graph, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Graph;
    }
    if (RenderCategoryButton(appearance, "Viewport", state.activeCategory == Category::Viewport, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Viewport;
    }
    if (RenderCategoryButton(appearance, "Canvas Composition", state.activeCategory == Category::CanvasComposition, ImVec2(-1.0f, 36.0f))) {
        state.activeCategory = Category::CanvasComposition;
    }
    if (RenderCategoryButton(appearance, "Experimental", state.activeCategory == Category::Experimental, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Experimental;
    }
    if (RenderCategoryButton(appearance, "Updates", state.activeCategory == Category::Updates, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Updates;
    }
    ImGui::EndChild();
    if (wallpaperSurfaces) {
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);

    ImGui::SameLine(0.0f, 24.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("SettingsPopupDetail", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    if (wallpaperSurfaces) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    }
    ImGui::BeginChild(
        "SettingsPopupScroll",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_None);
    const float fullDetailWidth = ImGui::GetContentRegionAvail().x;
    const float detailWidth = std::min(fullDetailWidth, 720.0f);
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + std::max(0.0f, (fullDetailWidth - detailWidth) * 0.5f));
    if (state.activeCategory == Category::Appearance) {
        const std::size_t themeCount = appearance
            ? appearance->GetFactoryThemes().size() +
                appearance->GetLibrary().customPresets.size()
            : 0;
        const int columns = detailWidth >= 420.0f ? 2 : 1;
        const std::size_t rows = columns > 1 ? (themeCount + 1) / 2 : themeCount;
        const float listHeight = static_cast<float>(rows) * 42.0f +
            (rows > 0 ? static_cast<float>(rows - 1) * kSettingsItemGap : 0.0f);
        ImGui::SetCursorPosY(std::max(
            ImGui::GetCursorPosY(),
            (ImGui::GetWindowHeight() - listHeight) * 0.5f));
    } else if (state.activeCategory == Category::Background && appearance) {
        const std::size_t imageCount = appearance->GetBackgroundImages().size();
        const std::size_t rows = (imageCount + 1) / 2;
        const float estimatedControlsHeight = 150.0f;
        const float estimatedListHeight = static_cast<float>(rows) *
            (27.0f + kSettingsItemGap);
        const float estimatedHeight = estimatedControlsHeight + estimatedListHeight;
        ImGui::SetCursorPosY(std::max(
            ImGui::GetCursorPosY(),
            (ImGui::GetWindowHeight() - estimatedHeight) * 0.5f));
    } else if (state.activeCategory == Category::Experimental) {
        ImGui::SetCursorPosY(std::max(
            ImGui::GetCursorPosY(),
            (ImGui::GetWindowHeight() - ImGui::GetFrameHeight()) * 0.5f));
    }
    switch (state.activeCategory) {
    case Category::Appearance:
        RenderAppearanceSection(appearance, detailWidth);
        break;
    case Category::Background:
        RenderBackgroundSection(appearance, state, detailWidth);
        break;
    case Category::Graph:
        RenderGraphSection(appearance, editor, detailWidth);
        break;
    case Category::Viewport:
        RenderViewportSection(appearance, detailWidth);
        break;
    case Category::CanvasComposition:
        RenderCanvasCompositionSection(editor, detailWidth);
        break;
    case Category::Experimental:
        RenderExperimentalSection(appearance, detailWidth);
        break;
    case Category::Updates:
        RenderUpdatesSection(updateManager, state, detailWidth);
        break;
    }
    ImGui::EndChild();
    if (wallpaperSurfaces) {
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::EndChild();

    RenderFooter();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

} // namespace AppSettingsPopup
