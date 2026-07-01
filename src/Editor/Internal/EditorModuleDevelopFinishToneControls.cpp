#include "Editor/Internal/EditorModuleDevelopFinishToneControls.h"

#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleDevelopDefaults.h"
#include "Editor/Layers/ToneLayers.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>

namespace Stack::Editor::DevelopFinishToneControls {

using Stack::Editor::DevelopDefaults::BuildDefaultIntegratedToneLayerJson;

namespace {

ToneCurveLayer BuildIntegratedDevelopToneLayer(const nlohmann::json& layerJson) {
    ToneCurveLayer toneCurve;
    if (layerJson.is_object()) {
        toneCurve.Deserialize(layerJson);
    } else {
        toneCurve.Deserialize(BuildDefaultIntegratedToneLayerJson());
    }
    return toneCurve;
}

} // namespace

bool RenderDevelopFinishToneControls(
    EditorModule& editor,
    int nodeId,
    nlohmann::json& integratedToneLayerJson,
    bool upstreamDevelopSettingsChanged,
    float controlWidth,
    bool showAdvancedControls) {
    ToneCurveLayer integratedTone = BuildIntegratedDevelopToneLayer(integratedToneLayerJson);
    editor.RestoreIntegratedToneTransientState(nodeId, integratedTone);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("FINISH TONE: FINAL GRAPH", 4.0f);
    bool integratedToneChanged =
        integratedTone.RenderDevelopFinishGraphPanel(controlWidth, true, showAdvancedControls);
    if (showAdvancedControls) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("PREPARED TONE ADVANCED", 4.0f);
        integratedToneChanged |= integratedTone.RenderDevelopPreparedControlsPanel(controlWidth, true);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("FOUNDATION TONE ADVANCED", 4.0f);
        integratedToneChanged |= integratedTone.RenderDevelopFoundationControlsPanel(controlWidth, true);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("LOCAL SCOPE / MASKING", 4.0f);
        integratedToneChanged |= integratedTone.RenderDevelopScopedMaskPanel(&editor, nodeId, controlWidth, true);
    } else {
        ImGui::TextDisabled("Prepared Tone, Foundation Tone, and local masking controls are in the sidebar.");
    }

    if (upstreamDevelopSettingsChanged && integratedTone.HasAutoPreparedState()) {
        integratedTone.NotifyUpstreamDevelopChanged();
        integratedToneChanged = true;
    }

    const nlohmann::json integratedToneJson = integratedTone.Serialize();
    editor.StoreIntegratedToneTransientState(nodeId, integratedTone);
    if (integratedToneLayerJson != integratedToneJson) {
        integratedToneLayerJson = integratedToneJson;
        return true;
    }
    return integratedToneChanged;
}

} // namespace Stack::Editor::DevelopFinishToneControls
