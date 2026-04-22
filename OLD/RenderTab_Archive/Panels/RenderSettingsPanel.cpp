#include "RenderSettingsPanel.h"

#include <imgui.h>

namespace RenderSettingsPanel {

void Render(RenderSettingsPanelModel model) {
    ImGui::TextUnformatted("Viewport");
    ImGui::Separator();

    int resolutionX = model.settings.GetResolutionX();
    int resolutionY = model.settings.GetResolutionY();
    if (ImGui::InputInt("Resolution X", &resolutionX)) {
        model.settings.SetResolution(resolutionX, model.settings.GetResolutionY());
    }
    if (ImGui::InputInt("Resolution Y", &resolutionY)) {
        model.settings.SetResolution(model.settings.GetResolutionX(), resolutionY);
    }

    int previewSampleTarget = model.settings.GetPreviewSampleTarget();
    if (ImGui::SliderInt("Viewport Sample Goal", &previewSampleTarget, 1, 256)) {
        model.settings.SetPreviewSampleTarget(previewSampleTarget);
    }

    bool accumulationEnabled = model.settings.IsAccumulationEnabled();
    if (ImGui::Checkbox("Enable Accumulation", &accumulationEnabled)) {
        model.settings.SetAccumulationEnabled(accumulationEnabled);
    }

    int integratorMode = static_cast<int>(model.settings.GetIntegratorMode());
    if (ImGui::Combo("Integrator Mode", &integratorMode, "Raster Preview\0Path Trace Preview\0Debug Preview\0")) {
        model.settings.SetIntegratorMode(static_cast<RenderIntegratorMode>(integratorMode));
    }

    int maxBounceCount = model.settings.GetMaxBounceCount();
    if (ImGui::SliderInt("Max Bounces", &maxBounceCount, 1, 8)) {
        model.settings.SetMaxBounceCount(maxBounceCount);
    }

    int displayMode = static_cast<int>(model.settings.GetDisplayMode());
    if (ImGui::Combo(
            "Display Mode",
            &displayMode,
            "Color\0Luminance\0Sample Tint\0Albedo AOV\0World Normal AOV\0Depth AOV\0Material ID AOV\0Primitive ID AOV\0Sample Count AOV\0Variance AOV\0")) {
        model.settings.SetDisplayMode(static_cast<RenderDisplayMode>(displayMode));
    }

    int tonemapMode = static_cast<int>(model.settings.GetTonemapMode());
    if (ImGui::Combo("Preview Tonemap", &tonemapMode, "Linear Clamp\0Reinhard\0ACES Film\0")) {
        model.settings.SetTonemapMode(static_cast<RenderTonemapMode>(tonemapMode));
    }

    int gizmoMode = static_cast<int>(model.settings.GetGizmoMode());
    if (ImGui::Combo("Gizmo Mode", &gizmoMode, "Translate\0Rotate\0Scale\0")) {
        model.settings.SetGizmoMode(static_cast<RenderGizmoMode>(gizmoMode));
    }

    int transformSpace = static_cast<int>(model.settings.GetTransformSpace());
    if (ImGui::Combo("Transform Space", &transformSpace, "World\0Local\0")) {
        model.settings.SetTransformSpace(static_cast<RenderTransformSpace>(transformSpace));
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Scene Background");

    int backgroundMode = static_cast<int>(model.scene.GetBackgroundMode());
    if (ImGui::Combo("Background Mode", &backgroundMode, "Gradient\0Checker\0Grid\0Black\0")) {
        model.scene.SetBackgroundMode(static_cast<RenderBackgroundMode>(backgroundMode));
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Scene lighting, sun lights, fog, and environment live in the Scene or Light inspector.");
    ImGui::TextDisabled("Raster Preview is the fast layout viewport. Path Trace Preview is the realism view.");
    ImGui::TextDisabled("Final still settings live in Render Manager.");
}

} // namespace RenderSettingsPanel
