#include "RenderStatisticsPanel.h"

#include <imgui.h>

namespace RenderStatisticsPanel {

void Render(const RenderStatisticsPanelModel& model) {
    const char* integratorLabel = "Unknown";
    switch (model.settings.GetIntegratorMode()) {
    case RenderIntegratorMode::RasterPreview:
        integratorLabel = "Raster Preview";
        break;
    case RenderIntegratorMode::PathTracePreview:
        integratorLabel = "Path Trace Preview";
        break;
    case RenderIntegratorMode::DebugPreview:
        integratorLabel = "Debug Preview";
        break;
    }
    const char* displayModeLabel = "Unknown";
    switch (model.settings.GetDisplayMode()) {
    case RenderDisplayMode::Color:
        displayModeLabel = "Color";
        break;
    case RenderDisplayMode::Luminance:
        displayModeLabel = "Luminance";
        break;
    case RenderDisplayMode::SampleTint:
        displayModeLabel = "Sample Tint";
        break;
    case RenderDisplayMode::AlbedoAov:
        displayModeLabel = "Albedo AOV";
        break;
    case RenderDisplayMode::WorldNormalAov:
        displayModeLabel = "World Normal AOV";
        break;
    case RenderDisplayMode::DepthAov:
        displayModeLabel = "Depth AOV";
        break;
    case RenderDisplayMode::MaterialIdAov:
        displayModeLabel = "Material ID AOV";
        break;
    case RenderDisplayMode::PrimitiveIdAov:
        displayModeLabel = "Primitive ID AOV";
        break;
    case RenderDisplayMode::SampleCountAov:
        displayModeLabel = "Sample Count AOV";
        break;
    case RenderDisplayMode::VarianceAov:
        displayModeLabel = "Variance AOV";
        break;
    }
    const char* tonemapLabel = "Unknown";
    switch (model.settings.GetTonemapMode()) {
    case RenderTonemapMode::LinearClamp:
        tonemapLabel = "Linear Clamp";
        break;
    case RenderTonemapMode::Reinhard:
        tonemapLabel = "Reinhard";
        break;
    case RenderTonemapMode::AcesFilm:
        tonemapLabel = "ACES Film";
        break;
    }
    const char* gizmoLabel = "Unknown";
    switch (model.settings.GetGizmoMode()) {
    case RenderGizmoMode::Translate:
        gizmoLabel = "Translate";
        break;
    case RenderGizmoMode::Rotate:
        gizmoLabel = "Rotate";
        break;
    case RenderGizmoMode::Scale:
        gizmoLabel = "Scale";
        break;
    }
    const char* transformSpaceLabel = model.settings.GetTransformSpace() == RenderTransformSpace::World
        ? "World"
        : "Local";
    ImGui::BulletText("Active Scene: %s", model.scene.GetValidationSceneLabel().c_str());
    ImGui::BulletText("Resolution: %d x %d", model.settings.GetResolutionX(), model.settings.GetResolutionY());
    ImGui::BulletText("Integrator: %s", integratorLabel);
    ImGui::BulletText("Display Mode: %s", displayModeLabel);
    ImGui::BulletText("Preview Tonemap: %s", tonemapLabel);
    ImGui::BulletText("Gizmo Mode: %s", gizmoLabel);
    ImGui::BulletText("Transform Space: %s", transformSpaceLabel);
    ImGui::BulletText("Max Bounces: %d", model.settings.GetMaxBounceCount());
    ImGui::BulletText("Environment: %s", model.scene.IsEnvironmentEnabled() ? "Enabled" : "Disabled");
    ImGui::BulletText("Environment Intensity: %.2f", model.scene.GetEnvironmentIntensity());
    ImGui::BulletText("Fog: %s", model.scene.IsFogEnabled() ? "Enabled" : "Disabled");
    ImGui::BulletText("Fog Density: %.3f", model.scene.GetFogDensity());
    ImGui::BulletText("Dispatch Groups: %d x %d", model.dispatchGroupsX, model.dispatchGroupsY);
    ImGui::BulletText("Sample Count: %u", model.buffers.GetSampleCount());
    ImGui::BulletText("Reset Count: %u", model.buffers.GetResetCount());
    ImGui::BulletText("Approx Buffer Footprint: %.2f MB", static_cast<double>(model.buffers.GetTotalByteSize()) / (1024.0 * 1024.0));
    ImGui::Separator();
    ImGui::BulletText("Scene Materials: %d", model.scene.GetMaterialCount());
    ImGui::BulletText("Emissive Materials: %d", model.scene.GetEmissiveMaterialCount());
    int dielectricMaterialCount = 0;
    for (int i = 0; i < model.scene.GetMaterialCount(); ++i) {
        if (IsSmoothDielectric(model.scene.GetMaterial(i))) {
            ++dielectricMaterialCount;
        }
    }
    ImGui::BulletText("Dielectric Materials: %d", dielectricMaterialCount);
    ImGui::BulletText("Mesh Definitions: %d", model.scene.GetMeshDefinitionCount());
    ImGui::BulletText("Mesh Instances: %d", model.scene.GetMeshInstanceCount());
    ImGui::BulletText("Scene Spheres: %d", model.scene.GetSphereCount());
    ImGui::BulletText("Standalone Triangles: %d", model.scene.GetTriangleCount());
    ImGui::BulletText("Scene Lights: %d", model.scene.GetLightCount());
    ImGui::BulletText("Imported Assets: %d", model.scene.GetImportedAssetCount());
    ImGui::BulletText("Resolved Triangles: %d", model.scene.GetResolvedTriangleCount());
    ImGui::BulletText("Primitive Refs: %llu", static_cast<unsigned long long>(model.scene.GetPrimitiveCount()));
    ImGui::BulletText("BVH Nodes: %llu", static_cast<unsigned long long>(model.scene.GetBvhNodes().size()));
    ImGui::Separator();
    ImGui::BulletText("Uploaded Spheres: %d", model.uploadedSphereCount);
    ImGui::BulletText("Uploaded Resolved Triangles: %d", model.uploadedTriangleCount);
    ImGui::BulletText("Uploaded Primitive Refs: %d", model.uploadedPrimitiveCount);
    ImGui::BulletText("Uploaded BVH Nodes: %d", model.uploadedBvhNodeCount);

    ImGui::Spacing();
    ImGui::TextWrapped("Last Reset: %s", model.lastResetReason.c_str());
}

} // namespace RenderStatisticsPanel
