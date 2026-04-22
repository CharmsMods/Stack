#include "RenderAssetBrowserPanel.h"

#include "RenderTab/Runtime/Debug/ValidationScenes.h"
#include "Utils/FileDialogs.h"

#include <imgui.h>
#include <cctype>
#include <string>

namespace {

std::string BuildDefaultSnapshotFileName(const RenderScene& scene) {
    std::string fileName = scene.GetValidationSceneLabel();
    if (fileName.empty()) {
        fileName = "render_scene";
    }

    for (char& ch : fileName) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!std::isalnum(value)) {
            ch = '_';
        }
    }

    return fileName + ".renderscene";
}

} // namespace

namespace RenderAssetBrowserPanel {

RenderAssetBrowserAction Render(const RenderAssetBrowserPanelModel& model) {
    RenderAssetBrowserAction action;

    ImGui::TextWrapped("Use this panel for templates, additive glTF import, and manual snapshot compatibility. Day-to-day scene authoring should happen from the Outliner, Inspector, and Render Manager.");
    ImGui::Separator();
    ImGui::Text("Current Scene: %s", model.scene.GetValidationSceneLabel().c_str());
    ImGui::TextWrapped("%s", model.scene.GetValidationSceneDescription().c_str());
    ImGui::Spacing();

    if (ImGui::Button("New Empty Scene")) {
        action.type = RenderAssetBrowserActionType::CreateEmptyScene;
        return action;
    }

    ImGui::SameLine();

    if (ImGui::Button("Import glTF Into Scene")) {
        std::string path = FileDialogs::OpenRenderGltfFileDialog("Import glTF Scene");
        if (!path.empty()) {
            action.type = RenderAssetBrowserActionType::ImportGltfScene;
            action.path = std::move(path);
            return action;
        }
    }

    if (model.importBusy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Import running...");
    }

    if (ImGui::Button("Save Scene Snapshot")) {
        std::string path = FileDialogs::SaveRenderSceneFileDialog("Save Render Scene Snapshot", BuildDefaultSnapshotFileName(model.scene).c_str());
        if (!path.empty()) {
            action.type = RenderAssetBrowserActionType::SaveSceneSnapshot;
            action.path = std::move(path);
            return action;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Load Scene Snapshot")) {
        std::string path = FileDialogs::OpenRenderSceneFileDialog("Load Render Scene Snapshot");
        if (!path.empty()) {
            action.type = RenderAssetBrowserActionType::LoadSceneSnapshot;
            action.path = std::move(path);
            return action;
        }
    }

    if (!model.snapshotStatus.empty()) {
        ImGui::TextWrapped("%s", model.snapshotStatus.c_str());
    }
    if (!model.snapshotPath.empty()) {
        ImGui::TextDisabled("Last Snapshot Path: %s", model.snapshotPath.c_str());
    }
    if (!model.importStatus.empty()) {
        ImGui::TextWrapped("%s", model.importStatus.c_str());
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Templates / Validation Scenes");
    ImGui::Separator();

    for (const RenderValidationSceneOption& option : GetRenderValidationSceneOptions()) {
        const bool selected = model.scene.GetValidationSceneId() == option.id;
        if (ImGui::Selectable(option.label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            action.type = RenderAssetBrowserActionType::SelectValidationScene;
            action.sceneId = option.id;
            action.defaultEnvironmentEnabled = option.defaultEnvironmentEnabled;
            return action;
        }
        ImGui::TextDisabled("%s", option.description);
        ImGui::TextDisabled("Default environment: %s", option.defaultEnvironmentEnabled ? "On" : "Off");
        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::Text("Shared Materials In Active Scene");
    if (model.scene.GetMaterialCount() == 0) {
        ImGui::TextDisabled("No shared materials are present in the active scene.");
    } else {
        for (int i = 0; i < model.scene.GetMaterialCount(); ++i) {
            const RenderMaterial& material = model.scene.GetMaterial(i);
            ImGui::BulletText(
                "%s | base %.2f %.2f %.2f | rough %.2f | metal %.2f | transmission %.2f | ior %.2f | emission %.2f",
                material.name.c_str(),
                material.baseColor.x,
                material.baseColor.y,
                material.baseColor.z,
                material.roughness,
                material.metallic,
                material.transmission,
                material.ior,
                material.emissionStrength);
        }
        ImGui::TextDisabled("Material editing happens from Inspector. Imported scenes now merge into the current scene instead of replacing it.");
    }

    ImGui::Separator();
    ImGui::Text("Imported Assets");
    if (model.scene.GetImportedAssetCount() == 0) {
        ImGui::TextDisabled("No external glTF assets are active in this scene.");
    } else {
        for (int i = 0; i < model.scene.GetImportedAssetCount(); ++i) {
            const RenderImportedAsset& asset = model.scene.GetImportedAsset(i);
            ImGui::BulletText("%s%s", asset.name.c_str(), asset.binaryContainer ? " (.glb)" : " (.gltf)");
            if (!asset.sourcePath.empty()) {
                ImGui::TextDisabled("Path: %s", asset.sourcePath.c_str());
            }
        }
        ImGui::TextDisabled("Imported textures: %d", model.scene.GetImportedTextureCount());
    }

    ImGui::Separator();
    ImGui::Text("Built-In Meshes In Active Scene");
    if (model.scene.GetMeshDefinitionCount() == 0) {
        ImGui::TextDisabled("This validation scene does not currently use mesh definitions.");
    } else {
        for (int i = 0; i < model.scene.GetMeshDefinitionCount(); ++i) {
            const RenderMeshDefinition& mesh = model.scene.GetMeshDefinition(i);
            ImGui::BulletText("%s (%zu triangles)", mesh.name.c_str(), mesh.triangles.size());
        }
        ImGui::TextDisabled("Active mesh instances: %d", model.scene.GetMeshInstanceCount());
    }

    ImGui::Separator();
    ImGui::BulletText("Shared scene materials: active");
    ImGui::BulletText("Additive glTF import: active");
    ImGui::BulletText("Final still PNG output: active");
    ImGui::BulletText("EXR outputs: deferred");
    return action;
}

} // namespace RenderAssetBrowserPanel
