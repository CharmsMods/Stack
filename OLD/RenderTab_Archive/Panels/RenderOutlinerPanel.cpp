#include "RenderOutlinerPanel.h"

#include "Utils/FileDialogs.h"

#include <imgui.h>
#include <string>
#include <vector>

namespace {

bool IsSelected(const RenderSelection& selection, RenderSelectionType type, int index) {
    return selection.type == type && selection.objectId == index;
}

const char* GetBackgroundLabel(RenderBackgroundMode mode) {
    switch (mode) {
    case RenderBackgroundMode::Gradient:
        return "Gradient";
    case RenderBackgroundMode::Checker:
        return "Checker";
    case RenderBackgroundMode::Grid:
        return "Grid";
    case RenderBackgroundMode::Black:
        return "Black";
    }

    return "Unknown";
}

void OpenNodeContextMenu(RenderOutlinerAction& action, const RenderSelection& selection, bool allowFocus) {
    if (!ImGui::BeginPopupContextItem()) {
        return;
    }

    if (ImGui::MenuItem("Duplicate")) {
        action.type = RenderOutlinerActionType::DuplicateSelection;
        action.selection = selection;
    }
    if (ImGui::MenuItem("Delete")) {
        action.type = RenderOutlinerActionType::DeleteSelection;
        action.selection = selection;
    }
    if (allowFocus && ImGui::MenuItem("Focus")) {
        action.type = RenderOutlinerActionType::FocusSelection;
        action.selection = selection;
    }

    ImGui::EndPopup();
}

int ResolveMeshSourceAssetIndex(const RenderScene& scene, const RenderMeshInstance& meshInstance) {
    if (!scene.IsMeshDefinitionIndexValid(meshInstance.meshIndex)) {
        return -1;
    }
    return scene.GetMeshDefinition(meshInstance.meshIndex).sourceAssetIndex;
}

} // namespace

namespace RenderOutlinerPanel {

RenderOutlinerAction Render(RenderOutlinerPanelModel model) {
    RenderOutlinerAction action;
    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (IsSelected(model.selection, RenderSelectionType::Scene, -1)) {
        rootFlags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string rootLabel = model.scene.GetValidationSceneLabel().empty()
        ? model.scene.GetName()
        : model.scene.GetValidationSceneLabel();
    const bool rootOpen = ImGui::TreeNodeEx(rootLabel.c_str(), rootFlags);
    if (ImGui::IsItemClicked()) {
        model.selection = RenderSelection { RenderSelectionType::Scene, -1 };
    }
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("New Scene")) {
            action.type = RenderOutlinerActionType::CreateEmptyScene;
        }
        if (ImGui::MenuItem("Load Scene Snapshot")) {
            std::string path = FileDialogs::OpenRenderSceneFileDialog("Load Render Scene Snapshot");
            if (!path.empty()) {
                action.type = RenderOutlinerActionType::LoadSceneSnapshot;
                action.path = std::move(path);
            }
        }
        if (ImGui::MenuItem("Save Project")) {
            action.type = RenderOutlinerActionType::SaveProjectToLibrary;
        }
        if (ImGui::MenuItem("Save Project As")) {
            action.type = RenderOutlinerActionType::SaveProjectAsToLibrary;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Add Sphere")) {
            action.type = RenderOutlinerActionType::AddSphere;
        }
        if (ImGui::MenuItem("Add Cube")) {
            action.type = RenderOutlinerActionType::AddCube;
        }
        if (ImGui::MenuItem("Add Plane")) {
            action.type = RenderOutlinerActionType::AddPlane;
        }
        if (ImGui::BeginMenu("Add Light")) {
            if (ImGui::MenuItem("Rect Area")) {
                action.type = RenderOutlinerActionType::AddRectAreaLight;
            }
            if (ImGui::MenuItem("Point")) {
                action.type = RenderOutlinerActionType::AddPointLight;
            }
            if (ImGui::MenuItem("Spot")) {
                action.type = RenderOutlinerActionType::AddSpotLight;
            }
            if (ImGui::MenuItem("Sun")) {
                action.type = RenderOutlinerActionType::AddSunLight;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Add Material")) {
            action.type = RenderOutlinerActionType::AddMaterial;
        }
        ImGui::EndPopup();
    }

    if (!rootOpen) {
        return action;
    }

    ImGui::BulletText("Scene: %s", model.scene.GetValidationSceneLabel().c_str());
    ImGui::BulletText("Background: %s", GetBackgroundLabel(model.scene.GetBackgroundMode()));
    ImGui::BulletText("Environment: %s | Intensity %.2f", model.scene.IsEnvironmentEnabled() ? "On" : "Off", model.scene.GetEnvironmentIntensity());
    ImGui::BulletText("Fog: %s | Density %.3f", model.scene.IsFogEnabled() ? "On" : "Off", model.scene.GetFogDensity());
    ImGui::BulletText("Scene revision: %llu", static_cast<unsigned long long>(model.scene.GetRevision()));
    ImGui::BulletText("Materials: %d", model.scene.GetMaterialCount());
    ImGui::BulletText("Mesh instances: %d", model.scene.GetMeshInstanceCount());
    ImGui::BulletText("Lights: %d", model.scene.GetLightCount());

    ImGuiTreeNodeFlags cameraFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
    if (IsSelected(model.selection, RenderSelectionType::Camera, -1)) {
        cameraFlags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::TreeNodeEx(model.camera.GetName().c_str(), cameraFlags);
    if (ImGui::IsItemClicked()) {
        model.selection = RenderSelection { RenderSelectionType::Camera, -1 };
    }

    if (model.scene.GetImportedAssetCount() > 0 &&
        ImGui::TreeNodeEx("Imported Assets", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth)) {
        for (int assetIndex = 0; assetIndex < model.scene.GetImportedAssetCount(); ++assetIndex) {
            const RenderImportedAsset& asset = model.scene.GetImportedAsset(assetIndex);
            std::string assetLabel = asset.name.empty() ? ("Imported Asset " + std::to_string(assetIndex + 1)) : asset.name;
            if (ImGui::TreeNodeEx(assetLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth)) {
                bool anyInstances = false;
                for (int i = 0; i < model.scene.GetMeshInstanceCount(); ++i) {
                    const RenderMeshInstance& meshInstance = model.scene.GetMeshInstance(i);
                    if (ResolveMeshSourceAssetIndex(model.scene, meshInstance) != assetIndex) {
                        continue;
                    }
                    anyInstances = true;
                    const char* meshName = model.scene.IsMeshDefinitionIndexValid(meshInstance.meshIndex)
                        ? model.scene.GetMeshDefinition(meshInstance.meshIndex).name.c_str()
                        : "Missing Mesh";
                    const std::string label = meshInstance.name + " [" + meshName + "]";
                    const RenderSelection itemSelection { RenderSelectionType::MeshInstance, meshInstance.id };
                    ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
                    if (IsSelected(model.selection, RenderSelectionType::MeshInstance, meshInstance.id)) {
                        meshFlags |= ImGuiTreeNodeFlags_Selected;
                    }
                    ImGui::TreeNodeEx(label.c_str(), meshFlags);
                    if (ImGui::IsItemClicked()) {
                        model.selection = itemSelection;
                    }
                    OpenNodeContextMenu(action, itemSelection, true);
                }
                if (!anyInstances) {
                    ImGui::TextDisabled("No instantiated objects from this asset are active.");
                }
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Objects", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth)) {
        for (int i = 0; i < model.scene.GetMeshInstanceCount(); ++i) {
            const RenderMeshInstance& meshInstance = model.scene.GetMeshInstance(i);
            if (ResolveMeshSourceAssetIndex(model.scene, meshInstance) >= 0) {
                continue;
            }
            const char* meshName = model.scene.IsMeshDefinitionIndexValid(meshInstance.meshIndex)
                ? model.scene.GetMeshDefinition(meshInstance.meshIndex).name.c_str()
                : "Missing Mesh";
            const std::string label = meshInstance.name + " [" + meshName + "]";
            const RenderSelection itemSelection { RenderSelectionType::MeshInstance, meshInstance.id };

            ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
            if (IsSelected(model.selection, RenderSelectionType::MeshInstance, meshInstance.id)) {
                meshFlags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(label.c_str(), meshFlags);
            if (ImGui::IsItemClicked()) {
                model.selection = itemSelection;
            }
            OpenNodeContextMenu(action, itemSelection, true);
        }

        for (int i = 0; i < model.scene.GetSphereCount(); ++i) {
            const RenderSphere& sphere = model.scene.GetSphere(i);
            const RenderSelection itemSelection { RenderSelectionType::Sphere, sphere.id };
            ImGuiTreeNodeFlags sphereFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
            if (IsSelected(model.selection, RenderSelectionType::Sphere, sphere.id)) {
                sphereFlags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(sphere.name.c_str(), sphereFlags);
            if (ImGui::IsItemClicked()) {
                model.selection = itemSelection;
            }
            OpenNodeContextMenu(action, itemSelection, true);
        }
        ImGui::TreePop();
    }

    if (model.scene.GetTriangleCount() > 0 &&
        ImGui::TreeNodeEx("Debug Triangles", ImGuiTreeNodeFlags_SpanFullWidth)) {
        for (int i = 0; i < model.scene.GetTriangleCount(); ++i) {
            const RenderTriangle& triangle = model.scene.GetTriangle(i);
            const RenderSelection itemSelection { RenderSelectionType::Triangle, triangle.id };
            ImGuiTreeNodeFlags triangleFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
            if (IsSelected(model.selection, RenderSelectionType::Triangle, triangle.id)) {
                triangleFlags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(triangle.name.c_str(), triangleFlags);
            if (ImGui::IsItemClicked()) {
                model.selection = itemSelection;
            }
            OpenNodeContextMenu(action, itemSelection, true);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth)) {
        for (int i = 0; i < model.scene.GetLightCount(); ++i) {
            const RenderLight& light = model.scene.GetLight(i);
            const std::string label = light.name + " [" + GetRenderLightTypeLabel(light.type) + "]";
            const RenderSelection itemSelection { RenderSelectionType::Light, light.id };
            ImGuiTreeNodeFlags lightFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
            if (IsSelected(model.selection, RenderSelectionType::Light, light.id)) {
                lightFlags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(label.c_str(), lightFlags);
            if (ImGui::IsItemClicked()) {
                model.selection = itemSelection;
            }
            OpenNodeContextMenu(action, itemSelection, true);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth)) {
        for (int i = 0; i < model.scene.GetMaterialCount(); ++i) {
            const RenderMaterial& material = model.scene.GetMaterial(i);
            const std::string label = material.name + (IsEmissive(material) ? " [Emissive]" : "");
            const RenderSelection itemSelection { RenderSelectionType::Material, i };
            ImGuiTreeNodeFlags materialFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
            if (IsSelected(model.selection, RenderSelectionType::Material, i)) {
                materialFlags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::TreeNodeEx(label.c_str(), materialFlags);
            if (ImGui::IsItemClicked()) {
                model.selection = itemSelection;
            }
            OpenNodeContextMenu(action, itemSelection, false);
        }
        ImGui::TreePop();
    }

    ImGui::TreePop();
    return action;
}

} // namespace RenderOutlinerPanel
