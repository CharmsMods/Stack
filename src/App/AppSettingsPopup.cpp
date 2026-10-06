#include "App/settings/PaletteWorkshop.h"
#include "AppSettingsPopup.h"
#include "Raw/RawViewportSettings.h"

#include "AppVersion.h"
#include "../Editor/EditorModule.h"
#include "../Utils/FileDialogs.h"
#include "../Utils/ImGuiExtras.h"
#include "App/Resources/EmbeddedTabIcons.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace AppSettingsPopup {
namespace {
void ReportActionResult(State& state, const std::string& key, const std::string& error) {
    auto& event = state.actionErrors[key];
    if (error.empty()) {
        state.notifier.Resolve(event);
        event = 0;
        return;
    }
    Stack::Notifications::NoticeSpec notice;
    notice.title = "Settings action failed";
    notice.message = error;
    notice.severity = Stack::Notifications::Severity::Error;
    notice.outcome = Stack::Notifications::Outcome::Failure;
    notice.dedupeKey = "settings-" + key;
    event = state.notifier.Post(std::move(notice));
}

void ReportPreferenceResult(State& state, const std::string& key, const std::string& setting,
    const StackAppearance::PreferenceMutationResult& result) {
    auto& event = state.actionErrors[key];
    if (result.persisted) {
        state.notifier.Resolve(event);
        event = 0;
        return;
    }
    Stack::Notifications::NoticeSpec notice;
    notice.title = result.applied ? "Setting not saved" : "Setting not changed";
    notice.message = result.applied
        ? "The change is active for now, but may be lost when Stack restarts."
        : "The setting could not be applied.";
    notice.context = setting;
    notice.details = result.error;
    notice.severity = result.applied ? Stack::Notifications::Severity::Warning : Stack::Notifications::Severity::Error;
    notice.outcome = result.applied ? Stack::Notifications::Outcome::Partial : Stack::Notifications::Outcome::Failure;
    notice.dedupeKey = "settings-" + key;
    event = state.notifier.Post(std::move(notice));
}

constexpr float kSettingsItemGap = 7.0f;
constexpr float kSettingsGroupGap = 14.0f;
constexpr float kSettingsControlWidth = 280.0f;

unsigned int GetArrowIconTexture() {
    static unsigned int s_Texture = 0;
    if (s_Texture == 0 && EmbeddedTabIcons::Arrow_png_data && EmbeddedTabIcons::Arrow_png_size > 0) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load_from_memory(
            EmbeddedTabIcons::Arrow_png_data,
            static_cast<int>(EmbeddedTabIcons::Arrow_png_size),
            &width,
            &height,
            &channels,
            4);
        if (pixels) {
            s_Texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
            stbi_image_free(pixels);
        }
    }
    return s_Texture;
}

void DrawRotatedImage(
    ImDrawList* drawList,
    ImTextureID texture,
    const ImVec2& centerPos,
    const ImVec2& size,
    float angleRad,
    ImU32 tintColor) {
    const float halfW = size.x * 0.5f;
    const float halfH = size.y * 0.5f;

    const float cosA = std::cos(angleRad);
    const float sinA = std::sin(angleRad);

    const auto rotatePoint = [&](float x, float y) -> ImVec2 {
        return ImVec2(
            centerPos.x + x * cosA - y * sinA,
            centerPos.y + x * sinA + y * cosA);
    };

    const ImVec2 p0 = rotatePoint(-halfW, -halfH);
    const ImVec2 p1 = rotatePoint(halfW, -halfH);
    const ImVec2 p2 = rotatePoint(halfW, halfH);
    const ImVec2 p3 = rotatePoint(-halfW, halfH);

    drawList->AddImageQuad(
        texture,
        p0, p1, p2, p3,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImVec2(0.0f, 1.0f),
        tintColor);
}

struct WheelArrowAnimationState {
    ImVec2 pos = ImVec2(0.0f, 0.0f);
    float angle = 0.0f;
    bool initialized = false;
};

static WheelArrowAnimationState s_ArrowState;

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
    std::string candidate = value;
    const std::string suffix = "...";
    while (candidate.size() > 1) {
        candidate.pop_back();
        std::string testStr = candidate + suffix;
        if (ImGui::CalcTextSize(testStr.c_str()).x <= maxWidth) {
            return testStr;
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
    if (selected || hovered)
        drawList->AddRectFilled(start, ImVec2(start.x + actualSize.x, start.y + actualSize.y),
            ImGui::GetColorU32(selected ? ImGuiCol_TabSelected : ImGuiCol_TabHovered), 4.0f);
    const ImU32 textColor = ImGui::GetColorU32(
        selected
            ? ImGuiCol_TabSelectedOverline
            : (hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    drawList->AddText(
        ImVec2(start.x + 4.0f, start.y + (actualSize.y - textSize.y) * 0.5f),
        textColor,
        label);
    return pressed;
}


void RenderAppearanceSection(StackAppearance::AppearanceManager* appearance, State& state, const float contentWidth) {
    if (!appearance) {
        ImGui::TextDisabled("Appearance settings are unavailable.");
        return;
    }

    StackAppearance::RenderPaletteWorkshop(*appearance, state.paletteWorkshop, state.notifier);
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

void RenderRawSection(EditorModule* editor, const float contentWidth) {
    RenderSectionIntro(
        "RAW",
        "Choose RAW editing and preview preferences.");
    if (editor == nullptr) {
        ImGui::TextDisabled("RAW settings are unavailable.");
        return;
    }

    ImGui::TextWrapped("Editing modules are selected from the left icon sidebar. Their controls appear beside it.");
    ImGui::Spacing();
    int targetFps = editor->GetRawViewportRequestedFps();
    ImGui::SetNextItemWidth(std::min(contentWidth, kSettingsControlWidth));
    if (ImGui::SliderInt("Target viewport FPS", &targetFps,
        Raw::kMinimumViewportTargetFps, std::max({240,editor->GetRawViewportMaximumFps(),targetFps}), "%d FPS",
        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
        editor->SetRawViewportTargetFps(targetFps);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) editor->FinishRawViewportTargetFpsEdit();
    ImGui::TextDisabled("Display limit: %d FPS", editor->GetRawViewportMaximumFps());
    const double nativeFps = editor->GetRawViewportNativeFps();
    if (nativeFps > 0.0) ImGui::Text("Whole image rebuild: approximately %.1f FPS", nativeFps);
    const double visibleFps = editor->GetRawViewportVisibleNativeFps();
    if (visibleFps > 0.0) ImGui::Text("Native visible area, cached input: approximately %.1f FPS", visibleFps);
    if (editor->GetRawViewportRequestedFps()>editor->GetRawViewportMaximumFps())
        ImGui::TextDisabled("Requested %d FPS; using %d FPS on this display",editor->GetRawViewportRequestedFps(),editor->GetRawViewportTargetFps());
    auto preferences=editor->GetRawViewportPreferences();
    int mode=static_cast<int>(preferences.mode);
    ImGui::SetNextItemWidth(std::min(contentWidth,kSettingsControlWidth));
    if (ImGui::Combo("During adjustments",&mode,"Target FPS\0Preserve detail\0")) {
        preferences.mode=static_cast<Raw::ViewportInteractionMode>(mode);
        editor->SetRawViewportPreferences(preferences);
    }
    ImGui::BeginDisabled(preferences.mode!=Raw::ViewportInteractionMode::PreserveDetail);
    ImGui::SetNextItemWidth(std::min(contentWidth,kSettingsControlWidth));
    if (ImGui::SliderInt("Minimum display detail",&preferences.minimumDetailPercent,25,100,"%d%%",ImGuiSliderFlags_AlwaysClamp))
        editor->SetRawViewportPreferences(preferences);
    ImGui::EndDisabled();
    ImGui::TextWrapped("100%% matches physical display detail at the current zoom, up to native source detail. Preserving detail can lower the achieved update rate.");
    ImGui::TextWrapped("%s",editor->GetRawViewportDecisionStatus().c_str());
    if (ImGui::Checkbox("Learn missing timings while idle",&preferences.backgroundLearning))
        editor->SetRawViewportPreferences(preferences);
    if (ImGui::Button("Reset learned timings for this hardware")) editor->ResetRawViewportLearnedTimings();
    bool smoothUpdates = editor->GetSmoothRawViewportUpdates();
    if (ImGui::Checkbox("Fade slower RAW preview updates", &smoothUpdates))
        editor->SetSmoothRawViewportUpdates(smoothUpdates);
    ImGui::BeginDisabled(!smoothUpdates);
    int fadeBelowFps = editor->GetRawViewportFadeBelowFps();
    ImGui::SetNextItemWidth(std::min(contentWidth, kSettingsControlWidth));
    if (ImGui::SliderInt("Fade below update rate", &fadeBelowFps,
        Raw::kMinimumViewportFadeBelowFps,
        editor->GetRawViewportMaximumFps(), "%d FPS",
        ImGuiSliderFlags_AlwaysClamp)) {
        editor->SetRawViewportFadeBelowFps(fadeBelowFps);
    }
    ImGui::TextWrapped("Higher values fade more updates. This does not change rendering speed or resolution. Fade length adapts to completed updates, including full-quality results after an adjustment.");
    ImGui::TextWrapped("When an adjustment ends, increases in preview resolution use a quick fade even above this rate.");
    ImGui::EndDisabled();
    const std::string calibrationStatus = editor->GetRawViewportCalibrationStatus();
    ImGui::TextWrapped("%s", calibrationStatus.c_str());
    ImGui::TextWrapped("During adjustments, resolution adapts toward this target. Lower FPS allows more detail; higher FPS favors responsiveness. After input settles, the visible area returns to native detail where memory permits.");
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
                state.lastActionError = errorMessage.empty() ? "Could not add the background image." : errorMessage;
            } else {
                state.lastActionError.clear();
            }
            ReportActionResult(state, "background-import", state.lastActionError);
        }
    }
    ImGui::PopStyleColor(3);

    float backgroundStrength = appearance->GetBackgroundImageStrength();
    ImGui::SetCursorPosX(controlX);
    ImGui::SetNextItemWidth(std::min(controlBlockWidth, kSettingsControlWidth));
    if (ImGui::SliderFloat("Background Image Strength", &backgroundStrength, 0.0f, 1.0f, "%.2f")) {
        appearance->SetBackgroundImageStrength(backgroundStrength);
    }

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
                        state.lastActionError = errorMessage.empty() ? "Could not remove the background image." : errorMessage;
                    } else {
                        state.lastActionError.clear();
                    }
                    ReportActionResult(state, "background-remove", state.lastActionError);
                }
            }
            ImGui::PopStyleColor(3);
        }
    }

    if (!state.lastActionError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::TextWrapped("%s", state.lastActionError.c_str());
        ImGui::PopStyleColor();
    }
}


void RenderGraphSection(StackAppearance::AppearanceManager* appearance, EditorModule* editor, State& state,
    const float contentWidth) {
    if (!appearance || !editor) {
        ImGui::TextDisabled("Graph settings are unavailable.");
        return;
    }

    RenderSectionIntro("GRAPH", "Choose how graph nodes render and which live graph diagnostics stay available while you work.");


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
        const auto result = appearance->SetGraphPanSensitivity(graphPanSensitivity);
        ReportPreferenceResult(state, "graph-pan-sensitivity", "Middle-mouse pan sensitivity", result);
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

void RenderUpdatesSection(AppUpdate::UpdateManager* updateManager, State& state, const float contentWidth) {
    RenderSectionIntro("APP UPDATES", "Check GitHub Releases for new Stack installers, download them in the background, and hand off safely to the installer when you're ready.");

    if (updateManager == nullptr) {
        ImGui::TextDisabled("Update services are unavailable.");
        return;
    }

    const AppUpdate::Snapshot& snapshot = updateManager->GetSnapshot();
    bool automaticStartupCheck = updateManager->IsAutomaticStartupCheckEnabled();
    if (ImGui::Checkbox("Check automatically when Stack starts", &automaticStartupCheck)) {
        updateManager->SetAutomaticStartupCheckEnabled(automaticStartupCheck);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("When enabled, Stack requests the official GitHub Releases API after legal acceptance. Stack does not send images, projects, or intentional product telemetry with this check.");
    if (snapshot.isLocalTestBuild) {
        ImGui::TextWrapped("Automatic and manual update checks are disabled for local-test builds so they cannot upgrade into or impersonate a public release.");
    }
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
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
        if (!updateManager->OpenReleasesPage(&state.lastActionError) && state.lastActionError.empty())
            state.lastActionError = "Could not open the releases page.";
        ReportActionResult(state, "open-releases", state.lastActionError);
    }

    if (!stackTopActions) {
        ImGui::SameLine(0.0f, 10.0f);
    }
    if (ImGui::Button("Open Website Download Page", ImVec2(204.0f, 0.0f))) {
        state.lastActionError.clear();
        if (!updateManager->OpenWebsiteDownloadPage(&state.lastActionError) && state.lastActionError.empty())
            state.lastActionError = "Could not open the download page.";
        ReportActionResult(state, "open-download-page", state.lastActionError);
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
            if (!updateManager->RevealDownloadedUpdate(&state.lastActionError) && state.lastActionError.empty())
                state.lastActionError = "Could not show the downloaded update.";
            ReportActionResult(state, "reveal-update", state.lastActionError);
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
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::TextWrapped("%s", state.lastActionError.c_str());
        ImGui::PopStyleColor();
    }


}

void RenderLegalSection(AppLegal::Manager* legalManager, State& state, const float contentWidth) {
    RenderSectionIntro(
        "ABOUT & LEGAL",
        "Stack is published by Darynn Ho. Stack, Charm, and CharmsMods are product or publishing brands.");

    ImGui::Text("Product: Stack Image Editor");
    ImGui::Text("Version: %s", AppVersion::kVersionString);
    ImGui::Text("Publisher and copyright owner: %s", AppVersion::kPublisher);
    ImGui::TextWrapped("%s", AppVersion::kCopyright);

    if (legalManager == nullptr) {
        ImGui::TextDisabled("Legal documents are unavailable.");
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextWrapped("%s", legalManager->GetStatusMessage().c_str());
    ImGui::TextWrapped("Acceptance record: %s", legalManager->GetAcceptanceLocationLabel().c_str());

    struct LegalAction {
        const char* label;
        AppLegal::Document document;
    };
    const LegalAction actions[] = {
        { "Open EULA", AppLegal::Document::Eula },
        { "Open Privacy Notice", AppLegal::Document::Privacy },
        { "Open Source License", AppLegal::Document::SourceLicense },
        { "Open Third-Party Notices", AppLegal::Document::ThirdPartyNotices },
        { "Open Third-Party Licenses", AppLegal::Document::ThirdPartyDirectory },
        { "Open Legal Folder", AppLegal::Document::LegalDirectory }
    };

    RenderSubsectionLabel("DOCUMENTS");
    const float buttonWidth = std::min(240.0f, std::max(1.0f, contentWidth));
    if (ImGui::Button("View Initial Legal Agreement Surface", ImVec2(buttonWidth, 0.0f))) {
        state.requestShowLegalGate = true;
    }
    for (const LegalAction& action : actions) {
        if (ImGui::Button(action.label, ImVec2(buttonWidth, 0.0f))) {
            state.lastActionError.clear();
            if (!legalManager->OpenDocument(action.document, &state.lastActionError) && state.lastActionError.empty())
                state.lastActionError = "Could not open the legal document.";
            ReportActionResult(state, "open-legal-" + std::to_string(static_cast<int>(action.document)), state.lastActionError);
        }
    }

    if (!state.lastActionError.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::TextWrapped("%s", state.lastActionError.c_str());
        ImGui::PopStyleColor();
    }
}

void RenderFooter() {
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("Stack Image Editor by Darynn Ho");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::Text("Version %s", AppVersion::kVersionString);
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextUnformatted("Renderer: OpenGL 4.3 Core");
    ImGui::PopStyleColor();
}

} // namespace

void UpdateNotifications(AppUpdate::UpdateManager* updateManager, State& state) {
    using namespace Stack::Notifications;
    if (state.installOperation && (!updateManager || updateManager->GetSnapshot().downloadedFilePath != state.installTarget)) {
        state.notifier.InvalidateOperation(state.installOperation);
        state.installOperation = 0;
    }
    if (!state.showInstallConfirmPopup || !updateManager) return;
    state.showInstallConfirmPopup = false;
    if (!updateManager->CanInstallUpdate()) return;
    if (state.installOperation) state.notifier.InvalidateOperation(state.installOperation);
    state.installOperation = state.notifier.NewOperation();
    state.installTarget = updateManager->GetSnapshot().downloadedFilePath;
    const auto scope = state.notifier;
    const auto operation = state.installOperation;
    const auto target = state.installTarget;
    NoticeSpec spec;
    spec.title = "Install update?";
    spec.message = "Stack will close to install the update. Save your work before continuing.";
    spec.details = "Windows may ask for administrator permission.";
    if (!updateManager->GetSnapshot().isInstalledBuild)
        spec.details += " This portable copy will stay in place. The installer creates an installed copy.";
    spec.severity = Severity::Warning; spec.operationId = operation; spec.foreground = true;
    const auto current = [scope, operation, target, updateManager] {
        return scope.IsOperationCurrent(operation) && updateManager->CanInstallUpdate() &&
            updateManager->GetSnapshot().downloadedFilePath == target;
    };
    ActionSpec cancel;
    cancel.label = "Cancel"; cancel.safeCancel = true;
    cancel.invoke = [] { return ActionResult::Success(); };
    spec.actions.push_back(std::move(cancel));
    ActionSpec install;
    install.label = "Install and restart"; install.destructive = true; install.canInvoke = current;
    install.invoke = [current, updateManager] {
        if (!current()) return ActionResult::Failure("The update changed. Review it again in Settings.");
        std::string error;
        return updateManager->InstallAndRestart(&error) ? ActionResult::Success() :
            ActionResult::Failure(error.empty() ? "The installer could not be started. Try again." : error);
    };
    spec.actions.push_back(std::move(install));
    scope.RequestDecision(std::move(spec));
}

void RenderContents(
    StackAppearance::AppearanceManager* appearance,
    EditorModule* editor,
    AppUpdate::UpdateManager* updateManager,
    AppLegal::Manager* legalManager,
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
    constexpr float categoryHeights[] = { 32.0f, 32.0f, 32.0f, 32.0f, 32.0f, 36.0f, 32.0f, 32.0f, 32.0f };
    float categoryListHeight = 0.0f;
    for (float height : categoryHeights) categoryListHeight += height;
    categoryListHeight += kSettingsItemGap * 8.0f;
    ImGui::SetCursorPosY(std::max(
        ImGui::GetCursorPosY(),
        (ImGui::GetWindowHeight() - categoryListHeight) * 0.5f));
    if (RenderCategoryButton(appearance, "Appearance", state.activeCategory == Category::Appearance, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Appearance;
    }
    if (RenderCategoryButton(appearance, "Background", state.activeCategory == Category::Background, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Background;
    }
    if (RenderCategoryButton(appearance, "RAW", state.activeCategory == Category::Raw, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Raw;
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
    if (RenderCategoryButton(appearance, "Legal", state.activeCategory == Category::Legal, ImVec2(-1.0f, 32.0f))) {
        state.activeCategory = Category::Legal;
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
        constexpr float wheelHeight = 244.0f;
        ImGui::SetCursorPosY(std::max(
            ImGui::GetCursorPosY(),
            (ImGui::GetWindowHeight() - wheelHeight) * 0.5f));
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
        RenderAppearanceSection(appearance, state, detailWidth);
        break;
    case Category::Background:
        RenderBackgroundSection(appearance, state, detailWidth);
        break;
    case Category::Raw:
        RenderRawSection(editor, detailWidth);
        break;
    case Category::Graph:
        RenderGraphSection(appearance, editor, state, detailWidth);
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
    case Category::Legal:
        RenderLegalSection(legalManager, state, detailWidth);
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
