#include "RenderAovDebugPanel.h"

#include <imgui.h>

namespace {

const char* GetIntegratorModeLabel(RenderIntegratorMode mode) {
    switch (mode) {
    case RenderIntegratorMode::RasterPreview:
        return "Raster Preview";
    case RenderIntegratorMode::PathTracePreview:
        return "Path Trace Preview";
    case RenderIntegratorMode::DebugPreview:
        return "Debug Preview";
    }

    return "Unknown";
}

const char* GetDisplayModeLabel(RenderDisplayMode mode) {
    switch (mode) {
    case RenderDisplayMode::Color:
        return "Color";
    case RenderDisplayMode::Luminance:
        return "Luminance";
    case RenderDisplayMode::SampleTint:
        return "Sample Tint";
    case RenderDisplayMode::AlbedoAov:
        return "Albedo AOV";
    case RenderDisplayMode::WorldNormalAov:
        return "World Normal AOV";
    case RenderDisplayMode::DepthAov:
        return "Depth AOV";
    case RenderDisplayMode::MaterialIdAov:
        return "Material ID AOV";
    case RenderDisplayMode::PrimitiveIdAov:
        return "Primitive ID AOV";
    case RenderDisplayMode::SampleCountAov:
        return "Sample Count AOV";
    case RenderDisplayMode::VarianceAov:
        return "Variance AOV";
    }

    return "Unknown";
}

const char* GetTonemapModeLabel(RenderTonemapMode mode) {
    switch (mode) {
    case RenderTonemapMode::LinearClamp:
        return "Linear Clamp";
    case RenderTonemapMode::Reinhard:
        return "Reinhard";
    case RenderTonemapMode::AcesFilm:
        return "ACES Film";
    }

    return "Unknown";
}

} // namespace

namespace RenderAovDebugPanel {

void Render(const RenderAovDebugPanelModel& model) {
    ImGui::TextWrapped("This panel now drives the shared compute renderer's debug and early path-trace preview controls.");
    ImGui::Separator();

    if (model.settings.GetIntegratorMode() == RenderIntegratorMode::DebugPreview) {
        int debugView = static_cast<int>(model.settings.GetDebugViewMode());
        if (ImGui::Combo("Debug View", &debugView, "Disabled\0World Normal\0Hit Distance\0Primitive ID\0BVH Depth\0")) {
            model.settings.SetDebugViewMode(static_cast<RenderDebugViewMode>(debugView));
        }
    } else {
        ImGui::TextDisabled("Debug views are available in Debug Preview mode. Switch integrators in Settings to inspect them.");
    }

    bool useBvhTraversal = model.settings.IsBvhTraversalEnabled();
    if (ImGui::Checkbox("Use BVH Traversal", &useBvhTraversal)) {
        model.settings.SetBvhTraversalEnabled(useBvhTraversal);
    }

    ImGui::Spacing();
    ImGui::BulletText("Integrator: %s", GetIntegratorModeLabel(model.settings.GetIntegratorMode()));
    ImGui::BulletText("Display mode: %s", GetDisplayModeLabel(model.settings.GetDisplayMode()));
    ImGui::BulletText("Preview tonemap: %s", GetTonemapModeLabel(model.settings.GetTonemapMode()));
    ImGui::BulletText(
        "Gizmo / Space: %s / %s",
        model.settings.GetGizmoMode() == RenderGizmoMode::Translate
            ? "Translate"
            : (model.settings.GetGizmoMode() == RenderGizmoMode::Rotate ? "Rotate" : "Scale"),
        model.settings.GetTransformSpace() == RenderTransformSpace::World ? "World" : "Local");
    ImGui::BulletText("Max bounces: %d", model.settings.GetMaxBounceCount());
    ImGui::BulletText("Environment: %s", model.scene.IsEnvironmentEnabled() ? "Enabled" : "Disabled");
    ImGui::BulletText("Environment intensity: %.2f", model.scene.GetEnvironmentIntensity());
    ImGui::BulletText("Fog: %s", model.scene.IsFogEnabled() ? "Enabled" : "Disabled");
    ImGui::BulletText("Fog density: %.3f", model.scene.GetFogDensity());
    ImGui::BulletText("Accumulation: %s", model.settings.IsAccumulationEnabled() ? "Enabled" : "Single sample");
    ImGui::BulletText("Preview sample count: %u", model.buffers.GetSampleCount());
    ImGui::BulletText("Active scene: %s", model.scene.GetValidationSceneLabel().c_str());

    ImGui::Spacing();
    ImGui::TextDisabled("Debug controls stay in this panel so the main authoring UI can stay focused on scene editing.");
    ImGui::TextDisabled("Path Trace Preview now covers emissive lights, explicit lights, rough transmission, absorption, and scene fog. Raster Preview stays approximate but stable.");
}

} // namespace RenderAovDebugPanel
