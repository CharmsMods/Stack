#include "RenderInspectorPanel.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

int ResolveMeshInstanceIndex(RenderScene& scene, int objectId) {
    return scene.FindMeshInstanceIndexById(objectId);
}

int ResolveSphereIndex(RenderScene& scene, int objectId) {
    return scene.FindSphereIndexById(objectId);
}

int ResolveTriangleIndex(RenderScene& scene, int objectId) {
    return scene.FindTriangleIndexById(objectId);
}

int ResolveLightIndex(RenderScene& scene, int objectId) {
    return scene.FindLightIndexById(objectId);
}

void ClampScale(RenderFloat3& scale) {
    scale.x = scale.x < 0.05f ? 0.05f : scale.x;
    scale.y = scale.y < 0.05f ? 0.05f : scale.y;
    scale.z = scale.z < 0.05f ? 0.05f : scale.z;
}

bool RenderTransformEditor(RenderTransform& transform, const char* worldNote = nullptr) {
    bool changed = false;

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::DragFloat3("Translation", transform.translation.Data(), 0.02f);
        changed |= ImGui::DragFloat3("Rotation", transform.rotationDegrees.Data(), 0.5f, -180.0f, 180.0f, "%.1f deg");
        changed |= ImGui::DragFloat3("Scale", transform.scale.Data(), 0.01f, 0.05f, 8.0f, "%.2f");
        ClampScale(transform.scale);

        if (worldNote != nullptr && worldNote[0] != '\0') {
            ImGui::TextDisabled("%s", worldNote);
        }
    }

    return changed;
}

RenderResolvedMaterial ResolvePreviewMaterial(const RenderScene& scene, int materialIndex, const RenderFloat3& tint) {
    if (scene.IsMaterialIndexValid(materialIndex)) {
        return ResolveMaterial(scene.GetMaterial(materialIndex), tint);
    }

    return RenderResolvedMaterial { tint, MakeRenderFloat3(0.0f, 0.0f, 0.0f) };
}

bool RenderMaterialSelector(RenderScene& scene, int& materialIndex) {
    if (scene.GetMaterialCount() <= 0) {
        ImGui::TextDisabled("No scene materials are available.");
        return false;
    }

    materialIndex = scene.IsMaterialIndexValid(materialIndex) ? materialIndex : 0;
    bool changed = false;
    const char* previewLabel = scene.GetMaterial(materialIndex).name.c_str();
    if (ImGui::BeginCombo("Material", previewLabel)) {
        for (int i = 0; i < scene.GetMaterialCount(); ++i) {
            const bool selected = i == materialIndex;
            const RenderMaterial& material = scene.GetMaterial(i);
            if (ImGui::Selectable(material.name.c_str(), selected)) {
                materialIndex = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    return changed;
}

bool MakeUniqueMaterial(RenderScene& scene, int& materialIndex) {
    int duplicateIndex = -1;
    if (!scene.DuplicateMaterial(materialIndex, duplicateIndex) || duplicateIndex < 0) {
        return false;
    }
    materialIndex = duplicateIndex;
    return true;
}

bool RenderNameEditor(const char* label, std::string& value) {
    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer, sizeof(buffer))) {
        return false;
    }

    value = buffer;
    return true;
}

void RenderSharedMaterialEditor(RenderScene& scene, int materialIndex) {
    if (!scene.IsMaterialIndexValid(materialIndex)) {
        return;
    }

    RenderMaterial material = scene.GetMaterial(materialIndex);
    bool changed = false;
    int preset = static_cast<int>(material.surfacePreset);
    if (ImGui::Combo("Surface Preset", &preset, "Diffuse\0Metal\0Glass\0Emissive\0")) {
        ApplyRenderSurfacePreset(material, static_cast<RenderSurfacePreset>(preset));
        changed = true;
    }

    if (ImGui::CollapsingHeader("Basic Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::ColorEdit3("Base Color", material.baseColor.Data());
        changed |= ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Transmission", &material.transmission, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("IOR", &material.ior, 1.0f, 3.0f, "%.2f");
        changed |= ImGui::ColorEdit3("Emission Color", material.emissionColor.Data());
        changed |= ImGui::DragFloat("Emission Strength", &material.emissionStrength, 0.05f, 0.0f, 64.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Advanced Physical Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Thin Walled", &material.thinWalled);
        changed |= ImGui::ColorEdit3("Absorption Color", material.absorptionColor.Data());
        changed |= ImGui::DragFloat("Absorption Distance", &material.absorptionDistance, 0.01f, 0.01f, 100.0f, "%.2f");
        changed |= ImGui::SliderFloat("Transmission Roughness", &material.transmissionRoughness, 0.0f, 1.0f, "%.2f");
    }

    if (changed) {
        scene.UpdateMaterial(materialIndex, material);
    }

    ImGui::TextDisabled("Shared material: %s", material.name.c_str());
    ImGui::TextDisabled("Edits apply to every primitive using this material.");
    ImGui::TextDisabled("Path Trace Preview shows the full glass, absorption, and fog response.");
    ImGui::TextDisabled("Raster Preview keeps the authored intent readable while staying fast.");
}

void RenderMaterialActions(RenderScene& scene, int& materialIndex, bool allowDelete, bool* objectChanged = nullptr) {
    bool changedObject = false;

    if (ImGui::Button("Duplicate Material")) {
        int duplicateIndex = -1;
        if (scene.DuplicateMaterial(materialIndex, duplicateIndex)) {
            materialIndex = duplicateIndex;
            changedObject = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Make Unique")) {
        if (MakeUniqueMaterial(scene, materialIndex)) {
            changedObject = true;
        }
    }
    ImGui::SameLine();
    if (allowDelete && ImGui::Button("Delete Material")) {
        if (!scene.DeleteMaterial(materialIndex)) {
            materialIndex = scene.IsMaterialIndexValid(materialIndex) ? materialIndex : 0;
        } else {
            materialIndex = scene.IsMaterialIndexValid(materialIndex) ? materialIndex : 0;
            changedObject = true;
        }
    }

    if (scene.IsMaterialIndexValid(materialIndex)) {
        RenderMaterial material = scene.GetMaterial(materialIndex);
        bool emissive = material.emissionStrength > 0.0001f;
        if (ImGui::Checkbox("Emissive", &emissive)) {
            if (emissive) {
                material.surfacePreset = RenderSurfacePreset::Emissive;
                ApplyRenderSurfacePreset(material, RenderSurfacePreset::Emissive);
            } else {
                material.surfacePreset = material.transmission > 0.0001f
                    ? RenderSurfacePreset::Glass
                    : RenderSurfacePreset::Diffuse;
                ApplyRenderSurfacePreset(material, material.surfacePreset);
                material.emissionStrength = 0.0f;
            }
            scene.UpdateMaterial(materialIndex, material);
        }
    }

    if (objectChanged != nullptr) {
        *objectChanged |= changedObject;
    }
}

void RenderSceneInspector(RenderScene& scene) {
    std::string sceneLabel = scene.GetValidationSceneLabel();
    if (RenderNameEditor("Scene Label", sceneLabel)) {
        scene.SetSceneLabel(sceneLabel);
    }

    ImGui::TextUnformatted(scene.GetValidationSceneLabel().c_str());
    ImGui::TextDisabled("Scene revision %llu", static_cast<unsigned long long>(scene.GetRevision()));
    ImGui::Separator();
    ImGui::BulletText("Materials: %d", scene.GetMaterialCount());
    ImGui::BulletText("Emissive materials: %d", scene.GetEmissiveMaterialCount());
    ImGui::BulletText("Mesh definitions: %d", scene.GetMeshDefinitionCount());
    ImGui::BulletText("Mesh instances: %d", scene.GetMeshInstanceCount());
    ImGui::BulletText("Spheres: %d", scene.GetSphereCount());
    ImGui::BulletText("Standalone triangles: %d", scene.GetTriangleCount());
    ImGui::BulletText("Resolved triangles: %d", scene.GetResolvedTriangleCount());
    ImGui::BulletText("Primitive refs: %zu", scene.GetPrimitiveCount());
    ImGui::BulletText("BVH nodes: %zu", scene.GetBvhNodes().size());
    ImGui::BulletText("Lights: %d", scene.GetLightCount());
    ImGui::Spacing();
    bool environmentEnabled = scene.IsEnvironmentEnabled();
    if (ImGui::Checkbox("Enable Environment", &environmentEnabled)) {
        scene.SetEnvironmentEnabled(environmentEnabled);
    }
    float environmentIntensity = scene.GetEnvironmentIntensity();
    if (ImGui::SliderFloat("Environment Intensity", &environmentIntensity, 0.0f, 8.0f, "%.2f")) {
        scene.SetEnvironmentIntensity(environmentIntensity);
    }
    bool fogEnabled = scene.IsFogEnabled();
    if (ImGui::Checkbox("Enable Fog", &fogEnabled)) {
        scene.SetFogEnabled(fogEnabled);
    }
    RenderFloat3 fogColor = scene.GetFogColor();
    if (ImGui::ColorEdit3("Fog Color", fogColor.Data())) {
        scene.SetFogColor(fogColor);
    }
    float fogDensity = scene.GetFogDensity();
    if (ImGui::DragFloat("Fog Density", &fogDensity, 0.001f, 0.0f, 1.5f, "%.3f")) {
        scene.SetFogDensity(fogDensity);
    }
    float fogAnisotropy = scene.GetFogAnisotropy();
    if (ImGui::SliderFloat("Fog Anisotropy", &fogAnisotropy, -0.95f, 0.95f, "%.2f")) {
        scene.SetFogAnisotropy(fogAnisotropy);
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", scene.GetValidationSceneDescription().c_str());
}

void RenderCameraInspector(RenderCamera& camera) {
    ImGui::TextUnformatted(camera.GetName().c_str());
    ImGui::TextDisabled("Camera revision %llu", static_cast<unsigned long long>(camera.GetRevision()));
    ImGui::Separator();

    RenderFloat3 position = camera.GetPosition();
    if (ImGui::DragFloat3("Position", position.Data(), 0.05f)) {
        camera.SetPosition(position);
    }

    float yaw = camera.GetYawDegrees();
    if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f, "%.1f deg")) {
        camera.SetYawDegrees(yaw);
    }

    float pitch = camera.GetPitchDegrees();
    if (ImGui::SliderFloat("Pitch", &pitch, -89.0f, 89.0f, "%.1f deg")) {
        camera.SetPitchDegrees(pitch);
    }

    float fieldOfView = camera.GetFieldOfViewDegrees();
    if (ImGui::SliderFloat("Field Of View", &fieldOfView, 20.0f, 110.0f, "%.1f deg")) {
        camera.SetFieldOfViewDegrees(fieldOfView);
    }

    float focusDistance = camera.GetFocusDistance();
    if (ImGui::SliderFloat("Focus Distance", &focusDistance, 0.1f, 25.0f, "%.2f m")) {
        camera.SetFocusDistance(focusDistance);
    }

    float apertureRadius = camera.GetApertureRadius();
    if (ImGui::SliderFloat("Aperture Radius", &apertureRadius, 0.0f, 1.5f, "%.3f m")) {
        camera.SetApertureRadius(apertureRadius);
    }

    float exposure = camera.GetExposure();
    if (ImGui::SliderFloat("Exposure", &exposure, 0.25f, 4.0f, "%.2f")) {
        camera.SetExposure(exposure);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset To Default View")) {
        camera.ResetToDefaultView();
    }
    ImGui::TextDisabled("Viewport: hold RMB over the preview, then use WASD + Q/E to fly.");
    ImGui::TextDisabled(
        "Depth of field: %s",
        camera.GetApertureRadius() > 0.0001f ? "enabled (thin-lens path trace)" : "disabled (pinhole)");

    const RenderFloat3 forward = camera.GetForwardVector();
    ImGui::TextDisabled(
        "Forward %.2f %.2f %.2f",
        forward.x,
        forward.y,
        forward.z);
    ImGui::TextDisabled("%s", camera.GetLastChangeReason().c_str());
}

void RenderSphereInspector(RenderScene& scene, int index) {
    if (!scene.IsSphereIndexValid(index)) {
        ImGui::TextWrapped("Selected sphere no longer exists in the active validation scene.");
        return;
    }

    RenderSphere sphere = scene.GetSphere(index);
    ImGui::TextUnformatted(sphere.name.c_str());
    ImGui::Separator();

    bool changed = false;
    changed |= RenderNameEditor("Name", sphere.name);
    changed |= RenderTransformEditor(sphere.transform, "Sphere rotation only affects the local-center offset in this debug renderer.");

    if (ImGui::CollapsingHeader("Local Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::DragFloat3("Local Center", sphere.localCenter.Data(), 0.02f);
        changed |= ImGui::DragFloat("Radius", &sphere.radius, 0.01f, 0.05f, 4.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= RenderMaterialSelector(scene, sphere.materialIndex);
        changed |= ImGui::ColorEdit3("Albedo Tint", sphere.albedoTint.Data());
        RenderMaterialActions(scene, sphere.materialIndex, false, &changed);
        RenderSharedMaterialEditor(scene, sphere.materialIndex);
    }

    const RenderResolvedSphere resolved = ResolveSphere(sphere);
    const RenderResolvedMaterial resolvedMaterial = ResolvePreviewMaterial(scene, sphere.materialIndex, sphere.albedoTint);

    ImGui::Spacing();
    ImGui::TextDisabled(
        "World Center %.2f %.2f %.2f | World Radius %.2f",
        resolved.center.x,
        resolved.center.y,
        resolved.center.z,
        resolved.radius);
    ImGui::TextDisabled(
        "Resolved Albedo %.2f %.2f %.2f | Emission %.2f %.2f %.2f",
        resolvedMaterial.albedo.x,
        resolvedMaterial.albedo.y,
        resolvedMaterial.albedo.z,
        resolvedMaterial.emission.x,
        resolvedMaterial.emission.y,
        resolvedMaterial.emission.z);
    ImGui::TextDisabled(
        "Resolved Roughness %.2f | Metallic %.2f | Transmission %.2f | IOR %.2f",
        resolvedMaterial.roughness,
        resolvedMaterial.metallic,
        resolvedMaterial.transmission,
        resolvedMaterial.ior);

    if (changed) {
        if (sphere.radius < 0.05f) {
            sphere.radius = 0.05f;
        }
        ClampScale(sphere.transform.scale);
        scene.UpdateSphere(index, sphere);
    }
}

void RenderMeshInstanceInspector(RenderScene& scene, int index) {
    if (!scene.IsMeshInstanceIndexValid(index)) {
        ImGui::TextWrapped("Selected mesh instance no longer exists in the active validation scene.");
        return;
    }

    RenderMeshInstance meshInstance = scene.GetMeshInstance(index);
    ImGui::TextUnformatted(meshInstance.name.c_str());
    ImGui::Separator();

    const bool meshValid = scene.IsMeshDefinitionIndexValid(meshInstance.meshIndex);
    const RenderMeshDefinition* meshDefinition = meshValid ? &scene.GetMeshDefinition(meshInstance.meshIndex) : nullptr;
    static int s_AssignMaterialObjectId = -1;
    static int s_AssignMaterialIndex = 0;
    if (s_AssignMaterialObjectId != meshInstance.id) {
        s_AssignMaterialObjectId = meshInstance.id;
        s_AssignMaterialIndex = 0;
    }

    bool changed = false;
    changed |= RenderNameEditor("Name", meshInstance.name);
    changed |= RenderTransformEditor(meshInstance.transform);

    if (ImGui::CollapsingHeader("Mesh Instance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Mesh: %s", meshValid ? meshDefinition->name.c_str() : "Missing mesh definition");
        changed |= ImGui::ColorEdit3("Color Tint", meshInstance.colorTint.Data());

        if (meshValid) {
            ImGui::BulletText("Local Triangles: %zu", meshDefinition->triangles.size());
            ImGui::BulletText(
                "Local Bounds Min %.2f %.2f %.2f",
                meshDefinition->localBounds.min.x,
                meshDefinition->localBounds.min.y,
                meshDefinition->localBounds.min.z);
            ImGui::BulletText(
                "Local Bounds Max %.2f %.2f %.2f",
                meshDefinition->localBounds.max.x,
                meshDefinition->localBounds.max.y,
                meshDefinition->localBounds.max.z);

            std::vector<int> materialIndices;
            materialIndices.reserve(meshDefinition->triangles.size());
            for (const RenderMeshTriangle& triangle : meshDefinition->triangles) {
                if (std::find(materialIndices.begin(), materialIndices.end(), triangle.materialIndex) == materialIndices.end()) {
                    materialIndices.push_back(triangle.materialIndex);
                }
            }

            if (!materialIndices.empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted("Mesh Materials");
                for (int materialIndex : materialIndices) {
                    const char* label = scene.IsMaterialIndexValid(materialIndex)
                        ? scene.GetMaterial(materialIndex).name.c_str()
                        : "Missing Material";
                    ImGui::BulletText("%s", label);
                }
                if (!scene.IsMaterialIndexValid(s_AssignMaterialIndex)) {
                    s_AssignMaterialIndex = materialIndices.front();
                }
                ImGui::Separator();
                ImGui::TextUnformatted("Assign Shared Material");
                RenderMaterialSelector(scene, s_AssignMaterialIndex);
                if (ImGui::Button("Assign To Whole Object")) {
                    scene.AssignMaterialToMeshInstance(meshInstance.id, s_AssignMaterialIndex);
                }
                ImGui::TextDisabled("This creates a mesh-definition variant for the selected object so imported siblings keep their own materials.");
            }
        }
    }

    if (meshValid) {
        const RenderBounds worldBounds = ComputeBounds(meshInstance, *meshDefinition);
        ImGui::Spacing();
        ImGui::TextDisabled(
            "World Bounds Min %.2f %.2f %.2f",
            worldBounds.min.x,
            worldBounds.min.y,
            worldBounds.min.z);
        ImGui::TextDisabled(
            "World Bounds Max %.2f %.2f %.2f",
            worldBounds.max.x,
            worldBounds.max.y,
            worldBounds.max.z);
    }

    if (changed) {
        ClampScale(meshInstance.transform.scale);
        scene.UpdateMeshInstance(index, meshInstance);
    }
}

void RenderTriangleInspector(RenderScene& scene, int index) {
    if (!scene.IsTriangleIndexValid(index)) {
        ImGui::TextWrapped("Selected triangle no longer exists in the active validation scene.");
        return;
    }

    RenderTriangle triangle = scene.GetTriangle(index);
    ImGui::TextUnformatted(triangle.name.c_str());
    ImGui::Separator();

    bool changed = false;
    changed |= RenderNameEditor("Name", triangle.name);
    changed |= RenderTransformEditor(triangle.transform);

    if (ImGui::CollapsingHeader("Local Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::DragFloat3("Local Vertex A", triangle.localA.Data(), 0.02f);
        changed |= ImGui::DragFloat3("Local Vertex B", triangle.localB.Data(), 0.02f);
        changed |= ImGui::DragFloat3("Local Vertex C", triangle.localC.Data(), 0.02f);
    }

    if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= RenderMaterialSelector(scene, triangle.materialIndex);
        changed |= ImGui::ColorEdit3("Albedo Tint", triangle.albedoTint.Data());
        RenderMaterialActions(scene, triangle.materialIndex, false, &changed);
        RenderSharedMaterialEditor(scene, triangle.materialIndex);
    }

    const RenderResolvedTriangle resolved = ResolveTriangle(triangle);
    const RenderResolvedMaterial resolvedMaterial = ResolvePreviewMaterial(scene, triangle.materialIndex, triangle.albedoTint);
    const RenderBounds worldBounds = ComputeBounds(triangle);

    ImGui::Spacing();
    ImGui::TextDisabled(
        "World Bounds Min %.2f %.2f %.2f",
        worldBounds.min.x,
        worldBounds.min.y,
        worldBounds.min.z);
    ImGui::TextDisabled(
        "World Bounds Max %.2f %.2f %.2f",
        worldBounds.max.x,
        worldBounds.max.y,
        worldBounds.max.z);
    ImGui::TextDisabled(
        "Resolved A %.2f %.2f %.2f",
        resolved.a.x,
        resolved.a.y,
        resolved.a.z);
    ImGui::TextDisabled(
        "Resolved Albedo %.2f %.2f %.2f | Emission %.2f %.2f %.2f",
        resolvedMaterial.albedo.x,
        resolvedMaterial.albedo.y,
        resolvedMaterial.albedo.z,
        resolvedMaterial.emission.x,
        resolvedMaterial.emission.y,
        resolvedMaterial.emission.z);
    ImGui::TextDisabled(
        "Resolved Roughness %.2f | Metallic %.2f | Transmission %.2f | IOR %.2f",
        resolvedMaterial.roughness,
        resolvedMaterial.metallic,
        resolvedMaterial.transmission,
        resolvedMaterial.ior);

    if (changed) {
        ClampScale(triangle.transform.scale);
        scene.UpdateTriangle(index, triangle);
    }
}

void RenderLightInspector(RenderScene& scene, int index) {
    if (!scene.IsLightIndexValid(index)) {
        ImGui::TextWrapped("Selected light no longer exists in the active scene.");
        return;
    }

    RenderLight light = scene.GetLight(index);
    ImGui::TextUnformatted(light.name.c_str());
    ImGui::Separator();

    bool changed = false;
    char nameBuffer[128] = {};
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", light.name.c_str());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        light.name = nameBuffer;
        changed = true;
    }

    int lightType = static_cast<int>(light.type);
    if (ImGui::Combo("Light Type", &lightType, "Rect Area\0Point\0Spot\0Sun\0")) {
        light.type = static_cast<RenderLightType>(lightType);
        changed = true;
    }

    changed |= RenderTransformEditor(light.transform);
    changed |= ImGui::Checkbox("Enabled", &light.enabled);
    changed |= ImGui::ColorEdit3("Color", light.color.Data());
    changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 128.0f, "%.2f");

    if (light.type == RenderLightType::RectArea) {
        changed |= ImGui::DragFloat2("Area Size", light.areaSize.Data(), 0.02f, 0.05f, 20.0f, "%.2f");
        light.areaSize.x = std::max(light.areaSize.x, 0.05f);
        light.areaSize.y = std::max(light.areaSize.y, 0.05f);
    } else if (light.type == RenderLightType::Point || light.type == RenderLightType::Spot) {
        changed |= ImGui::DragFloat("Range", &light.range, 0.1f, 0.5f, 500.0f, "%.2f");
        light.range = std::max(light.range, 0.5f);
    }

    if (light.type == RenderLightType::Spot) {
        changed |= ImGui::SliderFloat("Inner Cone", &light.innerConeDegrees, 1.0f, 85.0f, "%.1f deg");
        changed |= ImGui::SliderFloat("Outer Cone", &light.outerConeDegrees, 1.0f, 89.0f, "%.1f deg");
        if (light.outerConeDegrees < light.innerConeDegrees) {
            light.outerConeDegrees = light.innerConeDegrees;
        }
    }

    if (changed) {
        ClampScale(light.transform.scale);
        scene.UpdateLight(index, light);
    }

    const RenderFloat3 direction = GetRenderLightDirection(light);
    ImGui::Spacing();
    ImGui::TextDisabled("Direction %.2f %.2f %.2f", direction.x, direction.y, direction.z);
}

void RenderMaterialInspector(RenderScene& scene, int& materialIndex) {
    if (!scene.IsMaterialIndexValid(materialIndex)) {
        ImGui::TextWrapped("Selected material no longer exists in the active scene.");
        return;
    }

    RenderMaterial material = scene.GetMaterial(materialIndex);
    ImGui::TextUnformatted(material.name.c_str());
    ImGui::Separator();

    if (RenderNameEditor("Name", material.name)) {
        scene.UpdateMaterial(materialIndex, material);
    }

    RenderMaterialActions(scene, materialIndex, true);
    RenderSharedMaterialEditor(scene, materialIndex);
}

} // namespace

namespace RenderInspectorPanel {

void Render(RenderInspectorPanelModel model) {
    switch (model.selection.type) {
    case RenderSelectionType::Scene:
        RenderSceneInspector(model.scene);
        break;
    case RenderSelectionType::Camera:
        RenderCameraInspector(model.camera);
        break;
    case RenderSelectionType::MeshInstance:
        RenderMeshInstanceInspector(model.scene, ResolveMeshInstanceIndex(model.scene, model.selection.objectId));
        break;
    case RenderSelectionType::Sphere:
        RenderSphereInspector(model.scene, ResolveSphereIndex(model.scene, model.selection.objectId));
        break;
    case RenderSelectionType::Triangle:
        RenderTriangleInspector(model.scene, ResolveTriangleIndex(model.scene, model.selection.objectId));
        break;
    case RenderSelectionType::Light:
        RenderLightInspector(model.scene, ResolveLightIndex(model.scene, model.selection.objectId));
        break;
    case RenderSelectionType::Material:
        RenderMaterialInspector(model.scene, model.selection.objectId);
        break;
    }
}

} // namespace RenderInspectorPanel
