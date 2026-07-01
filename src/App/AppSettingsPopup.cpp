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

bool SeamlessSurfaceStylingEnabled(const StackAppearance::AppearanceManager* appearance) {
    return appearance && appearance->GetSeamlessSurfaceStylingEnabled();
}

StackAppearance::RuntimeSurfacePalette GetSurfacePalette(const StackAppearance::AppearanceManager* appearance) {
    return appearance ? appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
}

int GetResponsiveCardColumnCount(const float contentWidth, const float minCardWidth, const float gap, const int maxColumns = 2) {
    const float availableWidth = std::max(0.0f, contentWidth);
    for (int columns = maxColumns; columns > 1; --columns) {
        const float totalGapWidth = gap * static_cast<float>(columns - 1);
        const float cardWidth = (availableWidth - totalGapWidth) / static_cast<float>(columns);
        if (cardWidth >= minCardWidth) {
            return columns;
        }
    }
    return 1;
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
    const bool wallpaperSurfaces = SeamlessSurfaceStylingEnabled(appearance);
    const StackAppearance::RuntimeSurfacePalette surfacePalette = GetSurfacePalette(appearance);
    const ImVec4 buttonColor = wallpaperSurfaces
        ? (selected ? surfacePalette.controlSurfaceActive : surfacePalette.controlSurface)
        : (selected
            ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)
            : ImGui::GetStyleColorVec4(ImGuiCol_Button));
    const ImVec4 hoveredColor = wallpaperSurfaces
        ? (selected ? surfacePalette.controlSurfaceActive : surfacePalette.controlSurfaceHovered)
        : (selected
            ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)
            : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    const ImVec4 activeColor = wallpaperSurfaces
        ? surfacePalette.controlSurfaceActive
        : ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);

    ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
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
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    constexpr float gap = 16.0f;
    const int cardColumns = GetResponsiveCardColumnCount(contentWidth, 220.0f, gap);
    const float cardWidth = cardColumns > 1
        ? std::max(1.0f, (std::max(0.0f, contentWidth) - gap) * 0.5f)
        : std::max(1.0f, contentWidth);
    constexpr float cardHeight = 128.0f;

    auto renderThemeGroup = [&](const std::vector<StackAppearance::ThemeDefinition>& themes) {
        for (size_t i = 0; i < themes.size(); ++i) {
            const auto& theme = themes[i];
            if (cardColumns > 1 && i > 0 && (i % cardColumns) != 0) {
                ImGui::SameLine(0.0f, gap);
            }

            const bool isActive = activePresetId == theme.id;
            ImGui::PushID(theme.id.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isActive ? 2.0f : 1.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                isActive
                    ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)
                    : ImVec4(1.0f, 1.0f, 1.0f, 0.10f));

            ImGui::BeginChild("##ThemeCard", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);

            ImGui::TextUnformatted(theme.displayName.c_str());
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            const ImGuiCol swatchCols[] = {
                ImGuiCol_WindowBg,
                ImGuiCol_ChildBg,
                ImGuiCol_FrameBg,
                ImGuiCol_Header,
                ImGuiCol_ButtonActive
            };
            const ImVec2 swatchStart = ImGui::GetCursorScreenPos();
            constexpr float radius = 7.0f;
            constexpr float swatchGap = 20.0f;
            for (int c = 0; c < IM_ARRAYSIZE(swatchCols); ++c) {
                const ImVec4 col = theme.colors[swatchCols[c]];
                const ImVec2 center(swatchStart.x + radius + c * swatchGap, swatchStart.y + radius);
                drawList->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(col));
                drawList->AddCircle(center, radius, IM_COL32(0, 0, 0, 40), 0, 1.0f);
            }
            ImGui::Dummy(ImVec2(0.0f, 22.0f));

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", isActive ? "Current theme" : "Click to apply");
            ImGui::PopStyleColor();

            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            ImGui::EndChild();

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (appearance->SelectPresetById(theme.id)) {
                    ApplyTheme(appearance);
                }
            }

            if (hovered && !isActive) {
                drawList->AddRect(
                    ImGui::GetItemRectMin(),
                    ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)),
                    10.0f,
                    0,
                    1.2f);
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            ImGui::PopID();
        }
    };

    renderThemeGroup(factoryThemes);
    if (!customThemes.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGuiExtras::RichSectionLabel("CUSTOM PRESETS", 4.0f);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        renderThemeGroup(customThemes);
    }
}

void RenderAppearanceSection(StackAppearance::AppearanceManager* appearance, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Appearance settings are unavailable.");
        return;
    }

    ImGuiExtras::RichSectionLabel("APPEARANCE", 4.0f);
    ImGui::TextWrapped("Choose the app theme. Theme changes ease between palettes instead of snapping.");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    RenderThemeCards(appearance, contentWidth);
}

void RenderBackgroundSection(StackAppearance::AppearanceManager* appearance, State& state, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Background settings are unavailable.");
        return;
    }

    ImGuiExtras::RichSectionLabel("BACKGROUND", 4.0f);
    ImGui::TextWrapped("Import wallpapers once, then switch between them from this library without reopening Explorer.");
    ImGui::Dummy(ImVec2(0.0f, 18.0f));

    bool backgroundImageEnabled = appearance->GetBackgroundImageEnabled();
    if (ImGui::Checkbox("Background image", &backgroundImageEnabled)) {
        appearance->SetBackgroundImageEnabled(backgroundImageEnabled);
    }

    const std::string managedPath = appearance->GetBackgroundImagePath();
    const bool hasManagedImage = !managedPath.empty();
    const bool stackImageActions = contentWidth < 350.0f;
    const float imageActionButtonWidth = stackImageActions ? std::max(1.0f, contentWidth) : 170.0f;

    if (ImGui::Button("Add Image", ImVec2(imageActionButtonWidth, 0.0f))) {
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
    if (stackImageActions) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    } else {
        ImGui::SameLine(0.0f, 10.0f);
    }
    ImGui::BeginDisabled(!hasManagedImage && !backgroundImageEnabled);
    if (ImGui::Button("Disable Background", ImVec2(imageActionButtonWidth, 0.0f))) {
        std::string errorMessage;
        if (!appearance->ClearBackgroundImage(&errorMessage)) {
            state.lastActionError = errorMessage;
        } else {
            state.lastActionError.clear();
        }
    }
    ImGui::EndDisabled();

    float backgroundStrength = appearance->GetBackgroundImageStrength();
    ImGui::SetNextItemWidth(std::min(contentWidth, 340.0f));
    if (ImGui::SliderFloat("Background Image Strength", &backgroundStrength, 0.0f, 1.0f, "%.2f")) {
        appearance->SetBackgroundImageStrength(backgroundStrength);
    }

    float uiSurfaceTransparency = appearance->GetUiSurfaceTransparency();
    ImGui::SetNextItemWidth(std::min(contentWidth, 340.0f));
    if (ImGui::SliderFloat("UI Surface Transparency", &uiSurfaceTransparency, 0.0f, 1.0f, "%.2f")) {
        if (appearance->SetUiSurfaceTransparency(uiSurfaceTransparency)) {
            ApplyTheme(appearance);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 14.0f));
    ImGuiExtras::RichSectionLabel("LIBRARY", 4.0f);
    const std::vector<StackAppearance::BackgroundImageEntry>& images = appearance->GetBackgroundImages();
    if (images.empty()) {
        ImGui::TextDisabled("No background images have been added yet.");
    } else {
        constexpr float gap = 10.0f;
        const int cardColumns = GetResponsiveCardColumnCount(contentWidth, 180.0f, gap);
        const float cardWidth = cardColumns > 1
            ? (std::max(0.0f, contentWidth) - gap) * 0.5f
            : std::max(1.0f, contentWidth);
        constexpr float cardHeight = 92.0f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (std::size_t index = 0; index < images.size(); ++index) {
            const StackAppearance::BackgroundImageEntry& image = images[index];
            if (cardColumns > 1 && index > 0 && (index % cardColumns) != 0) {
                ImGui::SameLine(0.0f, gap);
            }

            const bool selected = image.path == managedPath;
            ImGui::PushID(image.id.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, selected ? 2.0f : 1.0f);
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                selected
                    ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)
                    : ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
            ImGui::BeginChild("##BackgroundCard", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextUnformatted(image.displayName.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", std::filesystem::path(image.path).filename().string().c_str());
            ImGui::TextUnformatted(selected ? (backgroundImageEnabled ? "Active" : "Selected, disabled") : "Click to use");
            ImGui::PopStyleColor();
            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            ImGui::EndChild();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (appearance->SelectBackgroundImageById(image.id)) {
                    state.lastActionError.clear();
                }
            }
            if (hovered && !selected) {
                drawList->AddRect(
                    min,
                    max,
                    ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)),
                    12.0f,
                    0,
                    1.2f);
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::PopID();
        }

        if (hasManagedImage) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            if (ImGui::Button("Remove Selected From Library", ImVec2(std::min(220.0f, std::max(1.0f, contentWidth)), 0.0f))) {
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
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const std::string runtimeStatus = appearance->GetBackgroundImageRuntimeStatus();
    if (!runtimeStatus.empty()) {
        ImGui::TextWrapped("%s", runtimeStatus.c_str());
    } else if (!managedPath.empty()) {
        const std::string fileName = std::filesystem::path(managedPath).filename().string();
        ImGui::TextWrapped("Managed image: %s", fileName.c_str());
    } else {
        ImGui::TextUnformatted("No background image selected.");
    }
    ImGui::PopStyleColor();

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

    const float buttonGap = 10.0f;
    const float rowButtonWidth = (contentWidth - buttonGap * 2.0f) / 3.0f;
    const bool stackButtons = rowButtonWidth < 150.0f;
    const float buttonWidth = stackButtons ? std::max(1.0f, contentWidth) : rowButtonWidth;
    for (int i = 0; i < IM_ARRAYSIZE(graphModes); ++i) {
        if (i > 0) {
            if (stackButtons) {
                ImGui::Dummy(ImVec2(0.0f, 6.0f));
            } else {
                ImGui::SameLine(0.0f, buttonGap);
            }
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

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
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

    ImGuiExtras::RichSectionLabel("GRAPH", 4.0f);
    ImGui::TextWrapped("Choose how graph nodes render and which live graph diagnostics stay available while you work.");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::TextUnformatted("Graph Visual Mode");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    RenderGraphModeButtons(appearance, contentWidth);

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    bool dottedMaskLinks = appearance->GetGraphDottedMaskLinks();
    if (ImGui::Checkbox("Dotted mask-endpoint links", &dottedMaskLinks)) {
        appearance->SetGraphDottedMaskLinks(dottedMaskLinks);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Render links as dotted when either endpoint is a mask-typed graph socket.");
    }

    float graphLineOpacity = appearance->GetGraphLineOpacity();
    ImGui::SetNextItemWidth(std::min(contentWidth, 320.0f));
    if (ImGui::SliderFloat("Graph Line Opacity", &graphLineOpacity, 0.0f, 1.0f, "%.2f")) {
        appearance->SetGraphLineOpacity(graphLineOpacity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Adjust the graph grid line intensity without changing node or pane opacity.");
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

    ImGuiExtras::RichSectionLabel("VIEWPORT RENDERING", 4.0f);
    ImGui::TextWrapped("Control tile-first rendering for the main single-output viewport.");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

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
    if (ImGui::DragInt("Tile Halo", &haloPixels, 1.0f, 0, 256, "%d px")) {
        settings.haloPixels = haloPixels;
        appearance->SetViewportTilingSettings(settings);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reserved overlap for future halo-aware blur/detail nodes. Basic tile-safe nodes use 0.");
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
    ImGuiExtras::RichSectionLabel("CANVAS COMPOSITION", 4.0f);
    ImGui::TextWrapped("Control snapping and transform stepping for the composition canvas.");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

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
    ImGuiExtras::RichSectionLabel("APP UPDATES", 4.0f);
    ImGui::TextWrapped("Check GitHub Releases for new Stack installers, download them in the background, and hand off safely to the installer when you're ready.");
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

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

    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    const float topActionRowWidth = 170.0f + 190.0f + 220.0f + 20.0f;
    const bool stackTopActions = contentWidth < topActionRowWidth;

    ImGui::BeginDisabled(!updateManager->CanCheckForUpdates());
    if (ImGui::Button("Check for Updates", ImVec2(stackTopActions ? std::max(1.0f, contentWidth) : 170.0f, 0.0f))) {
        state.lastActionError.clear();
        updateManager->StartManualCheck();
    }
    ImGui::EndDisabled();

    if (stackTopActions) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    } else {
        ImGui::SameLine(0.0f, 10.0f);
    }
    if (ImGui::Button("Open GitHub Releases", ImVec2(stackTopActions ? std::max(1.0f, contentWidth) : 190.0f, 0.0f))) {
        state.lastActionError.clear();
        updateManager->OpenReleasesPage(&state.lastActionError);
    }

    if (stackTopActions) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    } else {
        ImGui::SameLine(0.0f, 10.0f);
    }
    if (ImGui::Button("Open Website Download Page", ImVec2(stackTopActions ? std::max(1.0f, contentWidth) : 220.0f, 0.0f))) {
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
    ImGui::Separator();
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
    const StackAppearance::RuntimeSurfacePalette surfacePalette = GetSurfacePalette(appearance);
    const float footerHeight = 42.0f;
    const float railWidth = 184.0f;
    const float contentHeight = std::max(120.0f, avail.y - footerHeight);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, wallpaperSurfaces ? 10.0f : ImGui::GetStyle().FrameRounding);
    ImGui::BeginChild("SettingsPopupBody", ImVec2(0.0f, contentHeight), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, wallpaperSurfaces ? 14.0f : ImGui::GetStyle().ChildRounding);
    if (wallpaperSurfaces) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, surfacePalette.drawerSurface);
        ImGui::PushStyleColor(ImGuiCol_Border, surfacePalette.border);
    }
    ImGui::BeginChild("SettingsPopupRail", ImVec2(railWidth, 0.0f), wallpaperSurfaces, ImGuiWindowFlags_NoScrollbar);
    if (RenderCategoryButton(appearance, "Appearance", state.activeCategory == Category::Appearance, ImVec2(-1.0f, 38.0f))) {
        state.activeCategory = Category::Appearance;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (RenderCategoryButton(appearance, "Background", state.activeCategory == Category::Background, ImVec2(-1.0f, 38.0f))) {
        state.activeCategory = Category::Background;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (RenderCategoryButton(appearance, "Graph", state.activeCategory == Category::Graph, ImVec2(-1.0f, 38.0f))) {
        state.activeCategory = Category::Graph;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (RenderCategoryButton(appearance, "Viewport", state.activeCategory == Category::Viewport, ImVec2(-1.0f, 38.0f))) {
        state.activeCategory = Category::Viewport;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (RenderCategoryButton(appearance, "Canvas Composition", state.activeCategory == Category::CanvasComposition, ImVec2(-1.0f, 44.0f))) {
        state.activeCategory = Category::CanvasComposition;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (RenderCategoryButton(appearance, "Updates", state.activeCategory == Category::Updates, ImVec2(-1.0f, 38.0f))) {
        state.activeCategory = Category::Updates;
    }
    ImGui::EndChild();
    if (wallpaperSurfaces) {
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar(2);

    ImGui::SameLine(0.0f, 20.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 0.0f));
    ImGui::BeginChild("SettingsPopupDetail", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 4.0f));
    if (wallpaperSurfaces) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    }
    ImGui::BeginChild("SettingsPopupScroll", ImVec2(0.0f, 0.0f), false);
    const float detailWidth = ImGui::GetContentRegionAvail().x;
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
    ImGui::PopStyleVar(2);
}

} // namespace AppSettingsPopup
