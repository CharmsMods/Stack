#include "Editor/Internal/EditorModuleDevelopScenePrepControls.h"

#include "Editor/Internal/EditorModulePreLocalExposureControls.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>

namespace Stack::Editor::DevelopScenePrepControls {

using Stack::Editor::PreLocalExposureControls::NormalizeIntegratedScenePrepSettings;
using Stack::Editor::PreLocalExposureControls::RenderAutoGainDiagnosticsControls;
using Stack::Editor::PreLocalExposureControls::RenderAutoGainPresetControls;
using Stack::Editor::PreLocalExposureControls::RenderDevelopScenePrepAdvancedBiasControls;
using Stack::Editor::PreLocalExposureControls::RenderDevelopScenePrepNormalControls;
using Stack::Editor::PreLocalExposureControls::RenderPreLocalExposureExpertOverrides;
using Stack::Editor::PreLocalExposureControls::RenderPreLocalExposureSmoothing;
using Stack::Editor::PreLocalExposureControls::RenderPreLocalExposureSpatialModel;
using Stack::Editor::PreLocalExposureControls::RenderPreLocalExposureSummarySection;
using Stack::Editor::PreLocalExposureControls::SameRawDetailFusionSettings;

bool SameDevelopScenePrepSettings(
    const Raw::RawDetailFusionSettings& a,
    const Raw::RawDetailFusionSettings& b) {
    return SameRawDetailFusionSettings(a, b);
}

bool RenderDevelopScenePrepControls(
    Raw::RawDetailFusionSettings& settings,
    const RenderPipeline::PreLocalExposureSummary* liveSummary,
    bool hasRawSourceInput,
    float controlWidth,
    bool showAdvancedControls) {
    bool changed = false;

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGuiExtras::RichSectionLabel("SCENE PREP: LOCAL EXPOSURE", 4.0f);
    changed |= RenderDevelopScenePrepNormalControls(settings, controlWidth, "RawDevelopScenePrep");
    RenderPreLocalExposureSummarySection(liveSummary, hasRawSourceInput, controlWidth);

    if (showAdvancedControls) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("SCENE PREP ADVANCED", 4.0f);
        changed |= RenderDevelopScenePrepAdvancedBiasControls(settings, controlWidth, "RawDevelopScenePrep");
        changed |= RenderAutoGainPresetControls(settings, controlWidth, "RawDevelopScenePrep", true);
        changed |= RenderPreLocalExposureExpertOverrides(settings, controlWidth, "RawDevelopScenePrep");
        changed |= RenderPreLocalExposureSpatialModel(settings, controlWidth, "RawDevelopScenePrep");
        changed |= RenderPreLocalExposureSmoothing(settings, controlWidth, "RawDevelopScenePrep");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGuiExtras::RichSectionLabel("SCENE PREP DIAGNOSTICS", 4.0f);
        changed |= RenderAutoGainDiagnosticsControls(settings, controlWidth, "RawDevelopScenePrep", true, "Preview");
    }

    return changed;
}

void NormalizeDevelopScenePrepSettings(Raw::RawDetailFusionSettings& settings) {
    NormalizeIntegratedScenePrepSettings(settings);
}

} // namespace Stack::Editor::DevelopScenePrepControls
