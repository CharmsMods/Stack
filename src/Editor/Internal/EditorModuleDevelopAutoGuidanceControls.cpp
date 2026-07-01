#include "Editor/Internal/EditorModuleDevelopAutoGuidanceControls.h"

#include "Editor/Internal/EditorModuleDevelopSubjectControls.h"
#include "Editor/Internal/EditorModuleRawControlShared.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Stack::Editor::DevelopAutoGuidanceControls {

using Stack::Editor::RawControls::GraphSliderRightClickWasConsumed;

bool SameDevelopAutoGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b) {
    return a.intent == b.intent &&
        a.autoStrength == b.autoStrength &&
        a.exposureBias == b.exposureBias &&
        a.dynamicRange == b.dynamicRange &&
        a.shadowLift == b.shadowLift &&
        a.highlightGuard == b.highlightGuard &&
        a.highlightCharacter == b.highlightCharacter &&
        a.contrastBias == b.contrastBias &&
        a.subjectSceneBias == b.subjectSceneBias &&
        a.moodReadabilityBias == b.moodReadabilityBias;
}

AutoGuidanceControlResult RenderDevelopAutoGuidanceControls(
    EditorNodeGraph::DevelopAutoGuidance& guidance,
    EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    Stack::EditorModuleTypes::DevelopAutoGuidanceDraftState& draftState,
    bool forceAutoReanalysis,
    float controlWidth) {
    AutoGuidanceControlResult result;
    result.forceAutoReanalysis = forceAutoReanalysis;
    result.forceFullAutoReanalysis = forceAutoReanalysis;

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("AUTO DEVELOP", 4.0f);
    const float buttonGap = 8.0f;
    const float buttonWidth = std::max(110.0f, (controlWidth - buttonGap) * 0.5f);
    if (!draftState.editing) {
        draftState.guidance = guidance;
    }
    bool autoSliderChanged = false;
    bool autoSliderActive = false;

    auto resettableDevelopSliderFloat = [&](const char* label,
                                            const char* id,
                                            float* value,
                                            float resetValue,
                                            float minValue,
                                            float maxValue,
                                            const char* format) {
        bool localChanged = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
        const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
        if (!GraphSliderRightClickWasConsumed() &&
            state.lastHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            std::abs(*value - resetValue) > 0.0001f) {
            *value = resetValue;
            localChanged = true;
        }
        return localChanged;
    };

    auto renderAutoGuidanceSlider = [&](const char* label,
                                       const char* id,
                                       float* value,
                                       float resetValue,
                                       float minValue,
                                       float maxValue,
                                       const char* format) {
        const bool localChanged =
            resettableDevelopSliderFloat(label, id, value, resetValue, minValue, maxValue, format);
        const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
        autoSliderChanged |= localChanged;
        autoSliderActive |= state.lastActive;
    };

    const char* intentLabels[] = {
        "Natural Finished",
        "Clean Base",
        "Flat Editing Base",
        "Bright Natural",
        "Dark Natural",
        "Punchy / High Contrast",
        "Maximum Range / Detail"
    };
    int intentIndex = static_cast<int>(guidance.intent);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Auto Mode / Intent", &intentIndex, intentLabels, IM_ARRAYSIZE(intentLabels))) {
        ++result.recordInteractionCount;
        guidance.intent = static_cast<EditorNodeGraph::DevelopAutoIntent>(
            std::clamp(intentIndex, 0, IM_ARRAYSIZE(intentLabels) - 1));
        draftState.guidance.intent = guidance.intent;
        draftState.editing = false;
        result.forceAutoReanalysis = true;
        result.forceFullAutoReanalysis = true;
        result.changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", EditorNodeGraph::DevelopAutoIntentDescription(guidance.intent));
    }
    ImGui::TextWrapped("%s", EditorNodeGraph::DevelopAutoIntentDescription(guidance.intent));
    if (ImGuiExtras::RichFullWidthButton("Auto Calibrate", buttonWidth, 0.0f)) {
        ++result.recordInteractionCount;
        result.forceAutoReanalysis = true;
        result.forceFullAutoReanalysis = true;
        result.changed = true;
    }
    ImGui::SameLine(0.0f, buttonGap);
    if (ImGuiExtras::RichFullWidthButton("Reset Auto", buttonWidth, 0.0f)) {
        ++result.recordInteractionCount;
        const EditorNodeGraph::DevelopAutoIntent selectedIntent = guidance.intent;
        guidance = EditorNodeGraph::DevelopAutoGuidance{};
        guidance.intent = selectedIntent;
        draftState.guidance = guidance;
        draftState.editing = false;
        result.forceAutoReanalysis = true;
        result.forceFullAutoReanalysis = true;
        result.changed = true;
    }

    const EditorNodeGraph::DevelopAutoGuidance defaultGuidance;
    const Stack::Editor::DevelopSubjectControls::SubjectImportanceControlResult subjectImportanceControls =
        Stack::Editor::DevelopSubjectControls::RenderDevelopSubjectImportanceControls(
            subjectImportance,
            controlWidth,
            buttonGap);
    result.changed |= subjectImportanceControls.changed;
    result.forceAutoReanalysis =
        result.forceAutoReanalysis || subjectImportanceControls.forceAutoReanalysis;
    if (subjectImportanceControls.recordInteraction) {
        ++result.recordInteractionCount;
    }
    const bool subjectSceneIntentChanged = resettableDevelopSliderFloat(
        "Subject / Scene Intent",
        "##DevelopSubjectSceneBias",
        &draftState.guidance.subjectSceneBias,
        defaultGuidance.subjectSceneBias,
        -1.0f,
        1.0f,
        "%+.2f");
    const ImGuiExtras::NodeControlState& subjectSceneIntentState = ImGuiExtras::GetNodeControlState();
    autoSliderChanged |= subjectSceneIntentChanged;
    autoSliderActive |= subjectSceneIntentState.lastActive;
    if (subjectSceneIntentState.lastHovered) {
        ImGui::SetTooltip("Negative favors global scene integrity; positive gives likely or user-marked subject priority. This biases Auto solving, not a hard mask.");
    }
    const bool moodReadabilityChanged = resettableDevelopSliderFloat(
        "Mood / Readability",
        "##DevelopMoodReadabilityBias",
        &draftState.guidance.moodReadabilityBias,
        defaultGuidance.moodReadabilityBias,
        -1.0f,
        1.0f,
        "%+.2f");
    const ImGuiExtras::NodeControlState& moodReadabilityState = ImGuiExtras::GetNodeControlState();
    autoSliderChanged |= moodReadabilityChanged;
    autoSliderActive |= moodReadabilityState.lastActive;
    if (moodReadabilityState.lastHovered) {
        ImGui::SetTooltip("Negative preserves low-key mood; positive asks Auto to improve subject or midtone readability when quality allows.");
    }
    renderAutoGuidanceSlider(
        "Auto Strength",
        "##DevelopAutoStrength",
        &draftState.guidance.autoStrength,
        defaultGuidance.autoStrength,
        0.0f,
        2.4f,
        "%.2f");
    const bool brightnessIntentChanged = resettableDevelopSliderFloat(
        "Brightness Intent",
        "##DevelopExposureBias",
        &draftState.guidance.exposureBias,
        defaultGuidance.exposureBias,
        -2.0f,
        2.0f,
        "%+.2f");
    const ImGuiExtras::NodeControlState& brightnessIntentState = ImGuiExtras::GetNodeControlState();
    autoSliderChanged |= brightnessIntentChanged;
    autoSliderActive |= brightnessIntentState.lastActive;
    if (brightnessIntentState.lastHovered) {
        ImGui::SetTooltip("Solver-facing rendered brightness intent. Auto may adjust RAW EV, local exposure, and tone together; Manual RAW Exposure is the literal EV control.");
    }
    renderAutoGuidanceSlider(
        "Dynamic Range",
        "##DevelopDynamicRange",
        &draftState.guidance.dynamicRange,
        defaultGuidance.dynamicRange,
        0.25f,
        3.0f,
        "%.2f");
    renderAutoGuidanceSlider(
        "Shadow Lift",
        "##DevelopShadowLift",
        &draftState.guidance.shadowLift,
        defaultGuidance.shadowLift,
        -1.25f,
        1.25f,
        "%.2f");
    renderAutoGuidanceSlider(
        "Highlight Guard",
        "##DevelopHighlightGuard",
        &draftState.guidance.highlightGuard,
        defaultGuidance.highlightGuard,
        -1.25f,
        1.25f,
        "%.2f");
    renderAutoGuidanceSlider(
        "Highlight Character",
        "##DevelopHighlightCharacter",
        &draftState.guidance.highlightCharacter,
        defaultGuidance.highlightCharacter,
        -1.25f,
        1.25f,
        "%.2f");
    renderAutoGuidanceSlider(
        "Contrast Bias",
        "##DevelopContrastBias",
        &draftState.guidance.contrastBias,
        defaultGuidance.contrastBias,
        -1.25f,
        1.25f,
        "%.2f");
    ImGui::TextDisabled("Stage map: Subject / Scene Intent steers solver priority. Brightness Intent can move RAW EV, Scene Prep, and Prepared Tone together. Contrast Bias is tone.");

    if (autoSliderActive) {
        ++result.recordInteractionCount;
    }

    const bool draftCommitRequested = !autoSliderActive &&
        (draftState.editing || autoSliderChanged) &&
        !SameDevelopAutoGuidance(draftState.guidance, guidance);
    if (draftCommitRequested) {
        ++result.recordInteractionCount;
        result.forceAutoReanalysis = true;
        result.forceFullAutoReanalysis =
            result.forceFullAutoReanalysis ||
            std::abs(draftState.guidance.exposureBias - guidance.exposureBias) > 0.0001f;
        guidance = draftState.guidance;
        result.changed = true;
    }
    draftState.editing = autoSliderActive;
    if (!draftState.editing) {
        draftState.guidance = guidance;
    }

    return result;
}

} // namespace Stack::Editor::DevelopAutoGuidanceControls
