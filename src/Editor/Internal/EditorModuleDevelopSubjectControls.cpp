#include "Editor/Internal/EditorModuleDevelopSubjectControls.h"

#include "Editor/EditorModule.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cstddef>
#include <imgui.h>

namespace Stack::Editor::DevelopSubjectControls {

SubjectImportanceControlResult RenderDevelopSubjectImportanceControls(
    EditorNodeGraph::DevelopSubjectImportanceMap& importance,
    float controlWidth,
    float buttonGap) {
    SubjectImportanceControlResult result;
    bool subjectImportanceChanged = false;
    bool subjectImportanceActive = false;
    auto markSubjectImportanceEdit = [&](bool localChanged) {
        if (localChanged) {
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
    };

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGuiExtras::RichSectionLabel("SUBJECT IMPORTANCE", 3.0f);
    markSubjectImportanceEdit(ImGui::Checkbox("Use Importance Guidance", &importance.enabled));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Marked regions and brush strokes bias Auto candidate scoring. They are not hard masks and do not replace the Finish Mask input.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d region%s  |  %d stroke%s",
        static_cast<int>(importance.regions.size()),
        importance.regions.size() == 1 ? "" : "s",
        static_cast<int>(importance.strokes.size()),
        importance.strokes.size() == 1 ? "" : "s");

    if (ImGui::Checkbox("Show Overlay", &importance.showOverlay)) {
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the Develop subject-importance region overlay in the image viewport.");
    }
    if (ImGuiExtras::NodeSliderFloat("Overlay Opacity", "##SubjectRegionOverlayOpacity",
        &importance.overlayOpacity, 0.05f, 1.0f, "%.2f", controlWidth)) {
        result.changed = true;
    }

    if (ImGui::Checkbox("Show Interpreted Map", &importance.showInterpretedMapOverlay)) {
        if (importance.showInterpretedMapOverlay) {
            importance.showOverlay = true;
        }
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the compact 5x5 SubjectImportanceMapV1 diagnostic derived from current regions and brush strokes. This is solver interpretation, not the future edge-aware map.");
    }
    if (ImGuiExtras::NodeSliderFloat("Map Opacity", "##SubjectInterpretedMapOpacity",
        &importance.interpretedMapOpacity, 0.05f, 1.0f, "%.2f", controlWidth)) {
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Adjust only the diagnostic map display opacity. This does not change Auto solving.");
    }

    if (ImGui::Checkbox("Show Refined Map", &importance.showRefinedMapOverlay)) {
        if (importance.showRefinedMapOverlay) {
            importance.showOverlay = true;
        }
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the SubjectRefinedMapV1 diagnostic confidence map derived from current marks and solved subject/readability/protection/mood evidence. It is not AI detection or final edge-aware refinement.");
    }
    if (ImGuiExtras::NodeSliderFloat("Refined Opacity", "##SubjectRefinedMapOpacity",
        &importance.refinedMapOpacity, 0.05f, 1.0f, "%.2f", controlWidth)) {
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Adjust only the refined diagnostic map display opacity. This does not change Auto solving.");
    }

    bool brushEnabled = importance.brushEnabled;
    if (ImGui::Checkbox("Brush Edit", &brushEnabled)) {
        importance.brushEnabled = brushEnabled;
        if (brushEnabled) {
            importance.enabled = true;
            importance.showOverlay = true;
        }
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Paint Develop subject-importance strokes in the image viewport.");
    }
    if (ImGui::IsItemActive()) {
        subjectImportanceActive = true;
    }
    ImGui::SameLine();
    bool brushSubtract = importance.brushSubtract;
    if (ImGui::Checkbox("Reduce", &brushSubtract)) {
        importance.brushSubtract = brushSubtract;
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Paint strokes that reduce or de-prioritize marked importance.");
    }
    if (ImGui::IsItemActive()) {
        subjectImportanceActive = true;
    }

    int brushMode = static_cast<int>(importance.brushMode);
    const char* subjectImportanceModeLabels[] = {
        "Important",
        "Reveal",
        "Protect",
        "Preserve Mood",
        "Ignore / Low Priority"
    };
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Brush Mode", &brushMode, subjectImportanceModeLabels, IM_ARRAYSIZE(subjectImportanceModeLabels))) {
        importance.brushMode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(
            std::clamp(brushMode, 0, IM_ARRAYSIZE(subjectImportanceModeLabels) - 1));
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", EditorNodeGraph::DevelopSubjectImportanceModeDescription(importance.brushMode));
    }
    if (ImGui::IsItemActive()) {
        subjectImportanceActive = true;
    }
    if (ImGuiExtras::NodeSliderFloat("Brush Size", "##SubjectBrushRadius", &importance.brushRadius, 0.005f, 0.25f, "%.3f", controlWidth)) {
        result.changed = true;
    }
    if (ImGui::IsItemActive()) {
        subjectImportanceActive = true;
    }
    if (ImGuiExtras::NodeSliderFloat("Brush Strength", "##SubjectBrushStrength", &importance.brushStrength, 0.0f, 1.0f, "%.2f", controlWidth)) {
        result.changed = true;
    }
    if (ImGui::IsItemActive()) {
        subjectImportanceActive = true;
    }
    if (ImGuiExtras::NodeSliderFloat("Brush Soft Edge", "##SubjectBrushFeather", &importance.brushFeather, 0.0f, 1.0f, "%.2f", controlWidth)) {
        result.changed = true;
    }
    if (ImGui::IsItemActive()) {
        subjectImportanceActive = true;
    }

    const float smallButtonWidth = std::max(92.0f, (controlWidth - buttonGap) * 0.5f);
    if (ImGuiExtras::RichFullWidthButton("Add Region", smallButtonWidth, 0.0f)) {
        EditorNodeGraph::DevelopSubjectImportanceRegion region;
        region.id = importance.nextRegionId++;
        region.mode = EditorNodeGraph::DevelopSubjectImportanceMode::Important;
        importance.enabled = true;
        importance.regions.push_back(region);
        importance.activeRegionId = region.id;
        EditorModule::NormalizeDevelopSubjectImportance(importance);
        subjectImportanceChanged = true;
        result.forceAutoReanalysis = true;
        result.changed = true;
    }
    ImGui::SameLine(0.0f, buttonGap);
    if (ImGuiExtras::RichFullWidthButton("Clear Regions", smallButtonWidth, 0.0f)) {
        if (!importance.regions.empty()) {
            importance.regions.clear();
            importance.nextRegionId = 1;
            importance.activeRegionId = 0;
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
    }
    if (ImGuiExtras::RichFullWidthButton("Clear Brush", smallButtonWidth, 0.0f)) {
        if (!importance.strokes.empty()) {
            importance.strokes.clear();
            importance.nextStrokeId = 1;
            importance.activeStrokeId = 0;
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
    }

    auto syncBrushToolFromStroke = [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
        importance.brushMode = stroke.mode;
        importance.brushSubtract = stroke.subtract;
        importance.brushRadius = stroke.radius;
        importance.brushFeather = stroke.feather;
        importance.brushStrength = stroke.strength;
    };

    for (std::size_t strokeIndex = 0; strokeIndex < importance.strokes.size(); ++strokeIndex) {
        EditorNodeGraph::DevelopSubjectImportanceStroke& stroke =
            importance.strokes[strokeIndex];
        ImGui::PushID("SubjectStroke");
        ImGui::PushID(static_cast<int>(stroke.id));
        ImGui::Separator();
        bool strokeEnabled = stroke.enabled;
        if (ImGui::Checkbox("##SubjectStrokeEnabled", &strokeEnabled)) {
            stroke.enabled = strokeEnabled;
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
        ImGui::SameLine();
        const bool activeStroke = importance.activeStrokeId == stroke.id;
        if (ImGui::RadioButton("##SubjectStrokeActive", activeStroke)) {
            importance.activeStrokeId = stroke.id;
            syncBrushToolFromStroke(stroke);
            result.changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select this brush stroke and copy its settings to the brush tool.");
        }
        ImGui::SameLine();
        ImGui::Text("Stroke %d  (%d pt%s)",
            stroke.id,
            static_cast<int>(stroke.points.size()),
            stroke.points.size() == 1 ? "" : "s");
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            const bool deletingActiveStroke = importance.activeStrokeId == stroke.id;
            importance.strokes.erase(
                importance.strokes.begin() + static_cast<std::ptrdiff_t>(strokeIndex));
            if (deletingActiveStroke) {
                importance.activeStrokeId =
                    importance.strokes.empty() ? 0 : importance.strokes.front().id;
                if (!importance.strokes.empty()) {
                    syncBrushToolFromStroke(importance.strokes.front());
                }
            }
            --strokeIndex;
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
            ImGui::PopID();
            ImGui::PopID();
            continue;
        }

        bool strokeSubtract = stroke.subtract;
        if (ImGui::Checkbox("Reduce Stroke", &strokeSubtract)) {
            stroke.subtract = strokeSubtract;
            if (importance.activeStrokeId == stroke.id) {
                importance.brushSubtract = stroke.subtract;
            }
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reduce strokes de-prioritize the painted path instead of increasing subject importance.");
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }

        int strokeMode = static_cast<int>(stroke.mode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Stroke Mode", &strokeMode, subjectImportanceModeLabels, IM_ARRAYSIZE(subjectImportanceModeLabels))) {
            stroke.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(
                std::clamp(strokeMode, 0, IM_ARRAYSIZE(subjectImportanceModeLabels) - 1));
            if (importance.activeStrokeId == stroke.id) {
                importance.brushMode = stroke.mode;
            }
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", EditorNodeGraph::DevelopSubjectImportanceModeDescription(stroke.mode));
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }

        const bool strokeStrengthChanged = ImGuiExtras::NodeSliderFloat(
            "Stroke Strength",
            "##SubjectStrokeStrength",
            &stroke.strength,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        if (strokeStrengthChanged && importance.activeStrokeId == stroke.id) {
            importance.brushStrength = stroke.strength;
        }
        markSubjectImportanceEdit(strokeStrengthChanged);

        const bool strokeRadiusChanged = ImGuiExtras::NodeSliderFloat(
            "Stroke Size",
            "##SubjectStrokeRadius",
            &stroke.radius,
            0.005f,
            0.25f,
            "%.3f",
            controlWidth);
        if (strokeRadiusChanged && importance.activeStrokeId == stroke.id) {
            importance.brushRadius = stroke.radius;
        }
        markSubjectImportanceEdit(strokeRadiusChanged);

        const bool strokeFeatherChanged = ImGuiExtras::NodeSliderFloat(
            "Stroke Soft Edge",
            "##SubjectStrokeFeather",
            &stroke.feather,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        if (strokeFeatherChanged && importance.activeStrokeId == stroke.id) {
            importance.brushFeather = stroke.feather;
        }
        markSubjectImportanceEdit(strokeFeatherChanged);
        ImGui::PopID();
        ImGui::PopID();
    }

    for (std::size_t regionIndex = 0; regionIndex < importance.regions.size(); ++regionIndex) {
        EditorNodeGraph::DevelopSubjectImportanceRegion& region =
            importance.regions[regionIndex];
        ImGui::PushID(static_cast<int>(region.id));
        ImGui::Separator();
        bool regionEnabled = region.enabled;
        if (ImGui::Checkbox("##SubjectRegionEnabled", &regionEnabled)) {
            region.enabled = regionEnabled;
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }
        ImGui::SameLine();
        const bool activeRegion = importance.activeRegionId == region.id;
        if (ImGui::RadioButton("##SubjectRegionActive", activeRegion)) {
            importance.activeRegionId = region.id;
            result.changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select this region for viewport editing.");
        }
        ImGui::SameLine();
        ImGui::Text("Region %d", region.id);
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            const bool deletingActiveRegion = importance.activeRegionId == region.id;
            importance.regions.erase(importance.regions.begin() + static_cast<std::ptrdiff_t>(regionIndex));
            if (deletingActiveRegion) {
                importance.activeRegionId =
                    importance.regions.empty() ? 0 : importance.regions.front().id;
            }
            --regionIndex;
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
            ImGui::PopID();
            continue;
        }

        int regionMode = static_cast<int>(region.mode);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Mode", &regionMode, subjectImportanceModeLabels, IM_ARRAYSIZE(subjectImportanceModeLabels))) {
            region.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(
                std::clamp(regionMode, 0, IM_ARRAYSIZE(subjectImportanceModeLabels) - 1));
            subjectImportanceChanged = true;
            result.forceAutoReanalysis = true;
            result.changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", EditorNodeGraph::DevelopSubjectImportanceModeDescription(region.mode));
        }
        if (ImGui::IsItemActive()) {
            subjectImportanceActive = true;
        }

        markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Strength", "##SubjectRegionStrength", &region.strength, 0.0f, 1.0f, "%.2f", controlWidth));
        markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Center X", "##SubjectRegionCenterX", &region.centerX, 0.0f, 1.0f, "%.2f", controlWidth));
        markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Center Y", "##SubjectRegionCenterY", &region.centerY, 0.0f, 1.0f, "%.2f", controlWidth));
        markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Width", "##SubjectRegionRadiusX", &region.radiusX, 0.01f, 1.0f, "%.2f", controlWidth));
        markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Height", "##SubjectRegionRadiusY", &region.radiusY, 0.01f, 1.0f, "%.2f", controlWidth));
        markSubjectImportanceEdit(ImGuiExtras::NodeSliderFloat("Soft Edge", "##SubjectRegionFeather", &region.feather, 0.0f, 1.0f, "%.2f", controlWidth));
        ImGui::PopID();
    }
    if (subjectImportanceChanged) {
        EditorModule::NormalizeDevelopSubjectImportance(importance);
        result.recordInteraction = true;
    } else if (subjectImportanceActive) {
        result.recordInteraction = true;
    }

    return result;
}

} // namespace Stack::Editor::DevelopSubjectControls
