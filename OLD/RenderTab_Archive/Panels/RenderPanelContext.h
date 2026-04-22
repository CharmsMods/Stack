#pragma once

#include "RenderTab/Runtime/RenderBuffers.h"
#include "RenderTab/Runtime/RenderCamera.h"
#include "RenderTab/Runtime/RenderJob.h"
#include "RenderTab/Runtime/RenderScene.h"
#include "RenderTab/Runtime/RenderSettings.h"

#include <imgui.h>
#include <string>
#include <vector>

enum class RenderSelectionType {
    Scene = 0,
    Camera,
    MeshInstance,
    Sphere,
    Triangle,
    Light,
    Material
};

struct RenderSelection {
    RenderSelectionType type = RenderSelectionType::Camera;
    int objectId = -1;
};

enum class RenderManagerAction {
    None = 0,
    StartPreview,
    CancelPreview,
    ResetAccumulation,
    SaveProjectToLibrary,
    SaveProjectAsToLibrary,
    StartFinalRender,
    CancelFinalRender
};

enum class RenderAssetBrowserActionType {
    None = 0,
    CreateEmptyScene,
    SelectValidationScene,
    ImportGltfScene,
    SaveSceneSnapshot,
    LoadSceneSnapshot
};

struct RenderAssetBrowserAction {
    RenderAssetBrowserActionType type = RenderAssetBrowserActionType::None;
    RenderValidationSceneId sceneId = RenderValidationSceneId::Custom;
    bool defaultEnvironmentEnabled = true;
    std::string path;
};

enum class RenderOutlinerActionType {
    None = 0,
    CreateEmptyScene,
    LoadSceneSnapshot,
    SaveProjectToLibrary,
    SaveProjectAsToLibrary,
    AddSphere,
    AddCube,
    AddPlane,
    AddMaterial,
    AddRectAreaLight,
    AddPointLight,
    AddSpotLight,
    AddSunLight,
    DuplicateSelection,
    DeleteSelection,
    FocusSelection
};

struct RenderOutlinerAction {
    RenderOutlinerActionType type = RenderOutlinerActionType::None;
    RenderSelection selection {};
    std::string path;
};

struct RenderViewportPanelModel {
    unsigned int textureId = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    unsigned int sampleCount = 0;
    int sampleTarget = 0;
    unsigned int resetCount = 0;
    bool previewRequested = false;
    bool navigationActive = false;
    float navigationMoveSpeed = 0.0f;
    bool hasSelection = false;
    const char* integratorLabel = "";
    const char* gizmoLabel = "";
    const char* transformSpaceLabel = "";
    const char* jobStateLabel = "";
    const std::string& jobStatusText;
    const std::string& lastResetReason;
    const std::string& contextVersion;
};

struct RenderViewportPanelResult {
    bool hasRenderableImage = false;
    bool imageHovered = false;
    bool windowFocused = false;
    bool leftClicked = false;
    bool rightClicked = false;
    bool leftReleased = false;
    bool rightReleased = false;
    bool mouseDownLeft = false;
    bool mouseDownRight = false;
    ImVec2 imageMin {};
    ImVec2 imageMax {};
    ImVec2 imageSize {};
    ImVec2 mousePosition {};
};

struct RenderOutlinerPanelModel {
    const RenderScene& scene;
    const RenderCamera& camera;
    RenderSelection& selection;
};

struct RenderInspectorPanelModel {
    RenderSelection& selection;
    RenderScene& scene;
    RenderCamera& camera;
};

struct RenderSettingsPanelModel {
    RenderScene& scene;
    RenderSettings& settings;
};

struct RenderManagerPanelModel {
    const RenderScene& scene;
    const RenderJob& job;
    const RenderBuffers& buffers;
    RenderSettings& settings;
    const std::string& projectName;
    bool hasUnsavedChanges = false;
};

struct RenderStatisticsPanelModel {
    const RenderScene& scene;
    const RenderBuffers& buffers;
    const RenderSettings& settings;
    int dispatchGroupsX = 0;
    int dispatchGroupsY = 0;
    int uploadedSphereCount = 0;
    int uploadedTriangleCount = 0;
    int uploadedPrimitiveCount = 0;
    int uploadedBvhNodeCount = 0;
    const std::string& lastResetReason;
};

struct RenderConsolePanelModel {
    const std::vector<std::string>& lines;
};

struct RenderAovDebugPanelModel {
    const RenderScene& scene;
    RenderSettings& settings;
    const RenderBuffers& buffers;
};

struct RenderAssetBrowserPanelModel {
    RenderScene& scene;
    const std::string& snapshotStatus;
    const std::string& snapshotPath;
    const std::string& importStatus;
    bool importBusy = false;
};
