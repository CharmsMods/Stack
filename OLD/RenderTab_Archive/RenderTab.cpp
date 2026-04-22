#include "RenderTab.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Layout/RenderDockLayout.h"
#include "Renderer/GLLoader.h"
#include "RenderTab/Runtime/Import/RenderGltfImporter.h"
#include "Utils/FileDialogs.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>

namespace {

constexpr std::size_t kMaxConsoleLines = 48;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kViewportLookSensitivity = 0.18f;
constexpr float kViewportFastMultiplier = 3.0f;
constexpr float kViewportSlowMultiplier = 0.35f;
constexpr float kSelectionRayMinT = 0.001f;
constexpr float kGizmoHoverDistancePixels = 15.0f;
constexpr float kGizmoMinimumHandleWorldLength = 0.5f;
constexpr float kGizmoHandleDistanceFactor = 0.2f;
constexpr float kGizmoScaleSensitivity = 0.35f;
constexpr float kGizmoRotationSensitivity = 0.45f;
constexpr char kViewportNavigationReason[] = "Viewport fly camera changed.";

struct PickHit {
    RenderSelectionType type = RenderSelectionType::Scene;
    int objectId = -1;
    float distance = std::numeric_limits<float>::max();
};

struct GizmoProjection {
    bool valid = false;
    RenderFloat3 pivotWorld {};
    ImVec2 originScreen {};
    float handleWorldLength = 1.0f;
    std::array<RenderFloat3, 3> worldAxes {};
    std::array<ImVec2, 3> axisEndScreen {};
    std::array<float, 3> pixelsPerUnit {};
    std::array<bool, 3> axisVisible {};
};

bool IsViewportNavigationReason(const std::string& reason) {
    return reason == kViewportNavigationReason;
}

float DegreesToRadians(float degrees) {
    return degrees * (kPi / 180.0f);
}

RenderFloat3 RotateAroundX(const RenderFloat3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return MakeRenderFloat3(
        point.x,
        point.y * cosine - point.z * sine,
        point.y * sine + point.z * cosine);
}

RenderFloat3 RotateAroundY(const RenderFloat3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return MakeRenderFloat3(
        point.x * cosine + point.z * sine,
        point.y,
        -point.x * sine + point.z * cosine);
}

RenderFloat3 RotateAroundZ(const RenderFloat3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return MakeRenderFloat3(
        point.x * cosine - point.y * sine,
        point.x * sine + point.y * cosine,
        point.z);
}

RenderFloat3 RotateDirection(const RenderTransform& transform, const RenderFloat3& direction) {
    RenderFloat3 result = direction;
    result = RotateAroundX(result, DegreesToRadians(transform.rotationDegrees.x));
    result = RotateAroundY(result, DegreesToRadians(transform.rotationDegrees.y));
    result = RotateAroundZ(result, DegreesToRadians(transform.rotationDegrees.z));
    return Normalize(result);
}

RenderFloat3 GetAxisVector(int axis) {
    switch (axis) {
    case 0:
        return MakeRenderFloat3(1.0f, 0.0f, 0.0f);
    case 1:
        return MakeRenderFloat3(0.0f, 1.0f, 0.0f);
    case 2:
        return MakeRenderFloat3(0.0f, 0.0f, 1.0f);
    default:
        return MakeRenderFloat3(0.0f, 0.0f, 0.0f);
    }
}

RenderFloat3 GetTransformAxisVector(const RenderTransform& transform, int axis, RenderTransformSpace space) {
    const RenderFloat3 worldAxis = GetAxisVector(axis);
    if (space == RenderTransformSpace::World) {
        return worldAxis;
    }
    return RotateDirection(transform, worldAxis);
}

bool ProjectWorldToScreen(
    const RenderCamera& camera,
    const RenderViewportPanelResult& viewport,
    const RenderFloat3& worldPoint,
    ImVec2& screenPoint,
    float* cameraDepth = nullptr) {
    if (viewport.imageSize.x <= 0.0f || viewport.imageSize.y <= 0.0f) {
        return false;
    }

    const RenderFloat3 relative = Subtract(worldPoint, camera.GetPosition());
    const RenderFloat3 right = camera.GetRightVector();
    const RenderFloat3 up = camera.GetUpVector();
    const RenderFloat3 forward = camera.GetForwardVector();
    const float cameraX = Dot(relative, right);
    const float cameraY = Dot(relative, up);
    const float cameraZ = Dot(relative, forward);
    if (cameraDepth != nullptr) {
        *cameraDepth = cameraZ;
    }
    if (cameraZ <= 0.001f) {
        return false;
    }

    const float aspect = viewport.imageSize.x / viewport.imageSize.y;
    const float tanHalfFov = std::tan(DegreesToRadians(camera.GetFieldOfViewDegrees()) * 0.5f);
    const float ndcX = cameraX / (cameraZ * tanHalfFov * aspect);
    const float ndcY = cameraY / (cameraZ * tanHalfFov);
    if (std::fabs(ndcX) > 1.5f || std::fabs(ndcY) > 1.5f) {
        return false;
    }

    screenPoint.x = viewport.imageMin.x + (ndcX * 0.5f + 0.5f) * viewport.imageSize.x;
    screenPoint.y = viewport.imageMin.y + (-ndcY * 0.5f + 0.5f) * viewport.imageSize.y;
    return true;
}

RenderFloat3 BuildCameraRayDirection(const RenderCamera& camera, const RenderViewportPanelResult& viewport) {
    if (viewport.imageSize.x <= 0.0f || viewport.imageSize.y <= 0.0f) {
        return camera.GetForwardVector();
    }

    const float normalizedX = (viewport.mousePosition.x - viewport.imageMin.x) / viewport.imageSize.x;
    const float normalizedY = (viewport.mousePosition.y - viewport.imageMin.y) / viewport.imageSize.y;
    const float ndcX = normalizedX * 2.0f - 1.0f;
    const float ndcY = 1.0f - normalizedY * 2.0f;
    const float aspect = viewport.imageSize.x / viewport.imageSize.y;
    const float tanHalfFov = std::tan(DegreesToRadians(camera.GetFieldOfViewDegrees()) * 0.5f);

    RenderFloat3 rayDirection = camera.GetForwardVector();
    rayDirection = Add(rayDirection, Scale(camera.GetRightVector(), ndcX * tanHalfFov * aspect));
    rayDirection = Add(rayDirection, Scale(camera.GetUpVector(), ndcY * tanHalfFov));
    return Normalize(rayDirection);
}

bool IntersectSphere(
    const RenderFloat3& rayOrigin,
    const RenderFloat3& rayDirection,
    const RenderResolvedSphere& sphere,
    float& hitDistance) {
    const RenderFloat3 oc = Subtract(rayOrigin, sphere.center);
    const float a = Dot(rayDirection, rayDirection);
    const float b = 2.0f * Dot(oc, rayDirection);
    const float c = Dot(oc, oc) - sphere.radius * sphere.radius;
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) {
        return false;
    }

    const float sqrtDiscriminant = std::sqrt(discriminant);
    const float inverseDenominator = 0.5f / a;
    const float nearHit = (-b - sqrtDiscriminant) * inverseDenominator;
    const float farHit = (-b + sqrtDiscriminant) * inverseDenominator;

    if (nearHit > kSelectionRayMinT) {
        hitDistance = nearHit;
        return true;
    }
    if (farHit > kSelectionRayMinT) {
        hitDistance = farHit;
        return true;
    }
    return false;
}

bool IntersectSelectionBounds(
    const RenderFloat3& rayOrigin,
    const RenderFloat3& rayDirection,
    const RenderBounds& bounds,
    float& hitDistance) {
    const RenderFloat3 center = MakeRenderFloat3(
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f);
    const RenderFloat3 extents = Scale(Subtract(bounds.max, bounds.min), 0.5f);
    const float radius = std::max({ extents.x, extents.y, extents.z, 0.2f });
    RenderResolvedSphere proxySphere {};
    proxySphere.center = center;
    proxySphere.radius = radius;
    return IntersectSphere(rayOrigin, rayDirection, proxySphere, hitDistance);
}

bool IntersectTriangle(
    const RenderFloat3& rayOrigin,
    const RenderFloat3& rayDirection,
    const RenderResolvedTriangle& triangle,
    float& hitDistance) {
    const RenderFloat3 edge1 = Subtract(triangle.b, triangle.a);
    const RenderFloat3 edge2 = Subtract(triangle.c, triangle.a);
    const RenderFloat3 p = Cross(rayDirection, edge2);
    const float determinant = Dot(edge1, p);
    if (std::fabs(determinant) < 0.000001f) {
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;
    const RenderFloat3 t = Subtract(rayOrigin, triangle.a);
    const float u = Dot(t, p) * inverseDeterminant;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const RenderFloat3 q = Cross(t, edge1);
    const float v = Dot(rayDirection, q) * inverseDeterminant;
    if (v < 0.0f || (u + v) > 1.0f) {
        return false;
    }

    const float distance = Dot(edge2, q) * inverseDeterminant;
    if (distance <= kSelectionRayMinT) {
        return false;
    }

    hitDistance = distance;
    return true;
}

RenderSelection PickSceneObject(
    const RenderScene& scene,
    const RenderCamera& camera,
    const RenderViewportPanelResult& viewport) {
    const RenderFloat3 rayOrigin = camera.GetPosition();
    const RenderFloat3 rayDirection = BuildCameraRayDirection(camera, viewport);

    PickHit bestHit {};

    for (int sphereIndex = 0; sphereIndex < scene.GetSphereCount(); ++sphereIndex) {
        float hitDistance = 0.0f;
        const RenderResolvedSphere resolvedSphere = ResolveSphere(scene.GetSphere(sphereIndex));
        if (!IntersectSphere(rayOrigin, rayDirection, resolvedSphere, hitDistance) || hitDistance >= bestHit.distance) {
            continue;
        }

        bestHit.type = RenderSelectionType::Sphere;
        bestHit.objectId = resolvedSphere.objectId;
        bestHit.distance = hitDistance;
    }

    for (int triangleIndex = 0; triangleIndex < scene.GetResolvedTriangleCount(); ++triangleIndex) {
        float hitDistance = 0.0f;
        const RenderResolvedTriangle& triangle = scene.GetResolvedTriangle(triangleIndex);
        if (!IntersectTriangle(rayOrigin, rayDirection, triangle, hitDistance) || hitDistance >= bestHit.distance) {
            continue;
        }

        bestHit.type = triangle.meshInstanceId > 0 ? RenderSelectionType::MeshInstance : RenderSelectionType::Triangle;
        bestHit.objectId = triangle.meshInstanceId > 0 ? triangle.meshInstanceId : triangle.triangleId;
        bestHit.distance = hitDistance;
    }

    for (int lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex) {
        float hitDistance = 0.0f;
        const RenderLight& light = scene.GetLight(lightIndex);
        if (!IntersectSelectionBounds(rayOrigin, rayDirection, ComputeBounds(light), hitDistance) || hitDistance >= bestHit.distance) {
            continue;
        }

        bestHit.type = RenderSelectionType::Light;
        bestHit.objectId = light.id;
        bestHit.distance = hitDistance;
    }

    return RenderSelection { bestHit.type, bestHit.objectId };
}

float DistancePointToSegmentSquared(const ImVec2& point, const ImVec2& a, const ImVec2& b) {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float apX = point.x - a.x;
    const float apY = point.y - a.y;
    const float denominator = abX * abX + abY * abY;
    if (denominator <= 0.0001f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return dx * dx + dy * dy;
    }

    const float t = std::clamp((apX * abX + apY * abY) / denominator, 0.0f, 1.0f);
    const float closestX = a.x + abX * t;
    const float closestY = a.y + abY * t;
    const float dx = point.x - closestX;
    const float dy = point.y - closestY;
    return dx * dx + dy * dy;
}

bool BuildGizmoProjection(
    const RenderCamera& camera,
    const RenderViewportPanelResult& viewport,
    const RenderTransform& transform,
    const RenderFloat3& pivotWorld,
    RenderTransformSpace transformSpace,
    GizmoProjection& projection) {
    float cameraDepth = 0.0f;
    if (!ProjectWorldToScreen(camera, viewport, pivotWorld, projection.originScreen, &cameraDepth)) {
        return false;
    }

    projection.valid = true;
    projection.pivotWorld = pivotWorld;
    projection.handleWorldLength = std::max(kGizmoMinimumHandleWorldLength, cameraDepth * kGizmoHandleDistanceFactor);

    for (int axis = 0; axis < 3; ++axis) {
        projection.worldAxes[axis] = GetTransformAxisVector(transform, axis, transformSpace);
        const RenderFloat3 axisEndWorld = Add(pivotWorld, Scale(projection.worldAxes[axis], projection.handleWorldLength));
        ImVec2 axisEndScreen {};
        float axisDepth = 0.0f;
        projection.axisVisible[axis] = ProjectWorldToScreen(camera, viewport, axisEndWorld, axisEndScreen, &axisDepth);
        if (!projection.axisVisible[axis] || axisDepth <= 0.001f) {
            projection.axisVisible[axis] = false;
            projection.axisEndScreen[axis] = projection.originScreen;
            projection.pixelsPerUnit[axis] = 1.0f;
            continue;
        }

        projection.axisEndScreen[axis] = axisEndScreen;
        const float deltaX = axisEndScreen.x - projection.originScreen.x;
        const float deltaY = axisEndScreen.y - projection.originScreen.y;
        const float pixelLength = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (pixelLength < 8.0f) {
            projection.axisVisible[axis] = false;
            projection.pixelsPerUnit[axis] = 1.0f;
            continue;
        }

        projection.pixelsPerUnit[axis] = pixelLength / projection.handleWorldLength;
    }

    return true;
}

int FindHoveredAxis(const GizmoProjection& projection, const ImVec2& mousePosition) {
    int hoveredAxis = -1;
    float hoveredDistance = kGizmoHoverDistancePixels * kGizmoHoverDistancePixels;

    for (int axis = 0; axis < 3; ++axis) {
        if (!projection.axisVisible[axis]) {
            continue;
        }

        const float distanceSquared =
            DistancePointToSegmentSquared(mousePosition, projection.originScreen, projection.axisEndScreen[axis]);
        if (distanceSquared < hoveredDistance) {
            hoveredDistance = distanceSquared;
            hoveredAxis = axis;
        }
    }

    return hoveredAxis;
}

RenderFloat3 ComputeBoundsCenter(const RenderBounds& bounds) {
    return MakeRenderFloat3(
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f);
}

ImU32 GetAxisColor(int axis, bool emphasized) {
    switch (axis) {
    case 0:
        return emphasized ? IM_COL32(255, 96, 96, 255) : IM_COL32(215, 64, 64, 255);
    case 1:
        return emphasized ? IM_COL32(96, 255, 128, 255) : IM_COL32(64, 200, 96, 255);
    case 2:
        return emphasized ? IM_COL32(96, 160, 255, 255) : IM_COL32(64, 112, 220, 255);
    default:
        return IM_COL32(220, 220, 220, 255);
    }
}

RenderLight BuildDefaultLight(RenderLightType type) {
    RenderLight light;
    light.type = type;
    light.enabled = true;
    switch (type) {
    case RenderLightType::RectArea:
        light.name = "Rect Area Light";
        light.transform.translation = MakeRenderFloat3(0.8f, 2.8f, 0.0f);
        light.transform.rotationDegrees = MakeRenderFloat3(0.0f, 0.0f, 0.0f);
        light.areaSize = MakeRenderFloat2(1.5f, 1.0f);
        light.intensity = 10.0f;
        break;
    case RenderLightType::Point:
        light.name = "Point Light";
        light.transform.translation = MakeRenderFloat3(0.6f, 1.8f, 0.2f);
        light.intensity = 18.0f;
        light.range = 18.0f;
        break;
    case RenderLightType::Spot:
        light.name = "Spot Light";
        light.transform.translation = MakeRenderFloat3(0.6f, 2.4f, 0.0f);
        light.transform.rotationDegrees = MakeRenderFloat3(0.0f, 0.0f, -35.0f);
        light.intensity = 26.0f;
        light.range = 24.0f;
        light.innerConeDegrees = 18.0f;
        light.outerConeDegrees = 32.0f;
        break;
    case RenderLightType::Sun:
        light.name = "Sun Light";
        light.transform.rotationDegrees = MakeRenderFloat3(0.0f, 28.0f, 24.0f);
        light.intensity = 3.5f;
        light.range = 10000.0f;
        break;
    }
    return light;
}

std::string BuildSafeRenderProjectFileName(const std::string& label) {
    std::string safe = label.empty() ? "render_scene" : label;
    for (char& ch : safe) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    while (safe.find("__") != std::string::npos) {
        safe.replace(safe.find("__"), 2, "_");
    }
    if (safe.empty()) {
        safe = "render_scene";
    }
    return safe + ".stack";
}

std::string BuildRenderAssetFileName(const std::string& projectFileName) {
    return std::filesystem::path(projectFileName).stem().string() + ".png";
}

StackBinaryFormat::json BuildRenderSettingsJson(const RenderSettings& settings) {
    StackBinaryFormat::json root = StackBinaryFormat::json::object();
    root["resolutionX"] = settings.GetResolutionX();
    root["resolutionY"] = settings.GetResolutionY();
    root["previewSampleTarget"] = settings.GetPreviewSampleTarget();
    root["accumulationEnabled"] = settings.IsAccumulationEnabled();
    root["integratorMode"] = static_cast<int>(settings.GetIntegratorMode());
    root["maxBounceCount"] = settings.GetMaxBounceCount();
    root["displayMode"] = static_cast<int>(settings.GetDisplayMode());
    root["tonemapMode"] = static_cast<int>(settings.GetTonemapMode());
    root["gizmoMode"] = static_cast<int>(settings.GetGizmoMode());
    root["transformSpace"] = static_cast<int>(settings.GetTransformSpace());
    root["debugViewMode"] = static_cast<int>(settings.GetDebugViewMode());
    root["bvhTraversalEnabled"] = settings.IsBvhTraversalEnabled();

    StackBinaryFormat::json finalRender = StackBinaryFormat::json::object();
    finalRender["resolutionX"] = settings.GetFinalRenderResolutionX();
    finalRender["resolutionY"] = settings.GetFinalRenderResolutionY();
    finalRender["sampleTarget"] = settings.GetFinalRenderSampleTarget();
    finalRender["maxBounceCount"] = settings.GetFinalRenderMaxBounceCount();
    finalRender["outputName"] = settings.GetFinalRenderOutputName();
    root["finalRender"] = std::move(finalRender);
    return root;
}

void ApplyRenderSettingsJson(const StackBinaryFormat::json& root, RenderSettings& settings) {
    if (!root.is_object()) {
        return;
    }

    settings.SetResolution(
        root.value("resolutionX", settings.GetResolutionX()),
        root.value("resolutionY", settings.GetResolutionY()));
    settings.SetPreviewSampleTarget(root.value("previewSampleTarget", settings.GetPreviewSampleTarget()));
    settings.SetAccumulationEnabled(root.value("accumulationEnabled", settings.IsAccumulationEnabled()));
    settings.SetIntegratorMode(static_cast<RenderIntegratorMode>(root.value("integratorMode", static_cast<int>(settings.GetIntegratorMode()))));
    settings.SetMaxBounceCount(root.value("maxBounceCount", settings.GetMaxBounceCount()));
    settings.SetDisplayMode(static_cast<RenderDisplayMode>(root.value("displayMode", static_cast<int>(settings.GetDisplayMode()))));
    settings.SetTonemapMode(static_cast<RenderTonemapMode>(root.value("tonemapMode", static_cast<int>(settings.GetTonemapMode()))));
    settings.SetGizmoMode(static_cast<RenderGizmoMode>(root.value("gizmoMode", static_cast<int>(settings.GetGizmoMode()))));
    settings.SetTransformSpace(static_cast<RenderTransformSpace>(root.value("transformSpace", static_cast<int>(settings.GetTransformSpace()))));
    settings.SetDebugViewMode(static_cast<RenderDebugViewMode>(root.value("debugViewMode", static_cast<int>(settings.GetDebugViewMode()))));
    settings.SetBvhTraversalEnabled(root.value("bvhTraversalEnabled", settings.IsBvhTraversalEnabled()));

    const StackBinaryFormat::json finalRender = root.value("finalRender", StackBinaryFormat::json::object());
    if (finalRender.is_object()) {
        settings.SetFinalRenderResolution(
            finalRender.value("resolutionX", settings.GetFinalRenderResolutionX()),
            finalRender.value("resolutionY", settings.GetFinalRenderResolutionY()));
        settings.SetFinalRenderSampleTarget(finalRender.value("sampleTarget", settings.GetFinalRenderSampleTarget()));
        settings.SetFinalRenderMaxBounceCount(finalRender.value("maxBounceCount", settings.GetFinalRenderMaxBounceCount()));
        settings.SetFinalRenderOutputName(finalRender.value("outputName", settings.GetFinalRenderOutputName()));
    }
}

} // namespace

void RenderTab::Initialize() {
    m_State.Initialize();
    m_Scene = RenderScene();
    m_Camera = RenderCamera();
    m_Settings = RenderSettings();
    m_Buffers.Release();
    m_FinalBuffers.Release();
    m_Job = RenderJob();
    m_PreviewRenderer.Shutdown();
    m_RasterPreviewRenderer.Shutdown();
    m_Selection = RenderSelection { RenderSelectionType::Camera, -1 };
    m_ObservedSceneRevision = m_Scene.GetRevision();
    m_ObservedCameraRevision = m_Camera.GetRevision();
    m_ObservedSettingsRevision = m_Settings.GetRevision();
    m_AutoStartPending = true;
    m_LastSceneSnapshotStatus.clear();
    m_LastSceneSnapshotPath.clear();
    m_LastImportStatus.clear();
    m_CurrentProjectName = m_Scene.GetValidationSceneLabel();
    m_CurrentProjectFileName.clear();
    m_HasUnsavedChanges = false;
    m_PendingDiscardActionType = PendingDiscardActionType::None;
    m_PendingDiscardSceneId = RenderValidationSceneId::Custom;
    m_PendingDiscardPath.clear();
    m_ImportGeneration = 0;
    m_ImportTaskState = Async::TaskState::Idle;
    m_ViewportPanelSeenThisFrame = false;
    m_ViewportNavigationActive = false;
    m_ViewportNavigationChangedPose = false;
    m_ViewportGizmoDragging = false;
    m_ViewportHoveredAxis = -1;
    m_ViewportActiveAxis = -1;

    const GLubyte* versionString = glGetString(GL_VERSION);
    if (versionString != nullptr) {
        m_ContextVersion = std::string("Context: ") + reinterpret_cast<const char*>(versionString);
    } else {
        m_ContextVersion = "Context: unknown OpenGL version";
    }

    m_ConsoleLines.clear();
    AppendConsoleLine("[Render] Baseline path-trace material and emissive-light slice initialized.");
    AppendConsoleLine("[Render] " + m_ContextVersion);
    AppendConsoleLine(std::string("[Render] Active scene: ") + m_Scene.GetValidationSceneLabel() + ".");
    AppendConsoleLine("[Render] Viewport fly camera controls ready.");
    AppendConsoleLine("[Render] Thin-lens depth-of-field controls ready.");
    AppendConsoleLine("[Render] Scene-owned environment, sun lights, and fog controls ready.");
}

void RenderTab::Shutdown() {
    EndViewportNavigation(false);
    m_PreviewRenderer.Shutdown();
    m_RasterPreviewRenderer.Shutdown();
    m_Buffers.Release();
    m_FinalBuffers.Release();
    m_State.Shutdown();
}

void RenderTab::RenderUI() {
    if (m_AutoStartPending) {
        StartPreview("Live viewport auto-started for Render scene authoring.");
    }

    m_ViewportPanelSeenThisFrame = false;
    HandleRuntimeChanges();
    TickPreviewJob();

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("RenderWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleVar();

    const ImGuiID dockSpaceId = RenderDockLayout::GetDockSpaceId();
    RenderMenuBar(dockSpaceId, ImGui::GetContentRegionAvail());

    if (m_State.IsToolbarVisible()) {
        RenderToolbar();
    }

    const ImVec2 dockSpaceSize = ImGui::GetContentRegionAvail();
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (!m_State.WasDefaultLayoutApplied()) {
        RenderDockLayout::ApplyDefaultLayout(dockSpaceId, dockSpaceSize);
        m_State.SetDefaultLayoutApplied(true);
    }

    RenderPanels();

    if (m_OpenDiscardPopupPending) {
        ImGui::OpenPopup("Discard Unsaved Render Changes");
        m_OpenDiscardPopupPending = false;
    }

    RenderDiscardChangesPopup();

    if (!m_ViewportPanelSeenThisFrame) {
        EndViewportNavigation(true);
        EndViewportGizmoDrag();
    }

    ImGui::EndChild();
    m_State.SaveIfDirty();
}

void RenderTab::RenderMenuBar(ImGuiID dockSpaceId, const ImVec2& dockSpaceSize) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("Scene")) {
        if (ImGui::MenuItem("New Empty Scene")) {
            QueueDiscardAction(PendingDiscardActionType::CreateEmptyScene);
        }
        if (ImGui::MenuItem("Import glTF...")) {
            const std::string path = FileDialogs::OpenRenderGltfFileDialog("Import glTF Scene");
            if (!path.empty()) {
                BeginGltfImport(path);
            }
        }
        if (ImGui::MenuItem("Load Scene Snapshot...")) {
            const std::string path = FileDialogs::OpenRenderSceneFileDialog("Load Render Scene Snapshot");
            if (!path.empty()) {
                QueueDiscardAction(PendingDiscardActionType::LoadSceneSnapshot, RenderValidationSceneId::Custom, path);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Project")) {
            SaveProjectToLibrary();
        }
        if (ImGui::MenuItem("Save Project As")) {
            SaveProjectAsToLibrary();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Start Final Render")) {
            StartFinalRender();
        }
        if (ImGui::MenuItem("Cancel Final Render", nullptr, false, m_Job.IsFinalRequested() || m_Job.GetFinalState() == RenderJobState::Running)) {
            CancelFinalRender();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        bool showToolbar = m_State.IsToolbarVisible();
        if (ImGui::MenuItem("Show Toolbar", nullptr, &showToolbar)) {
            m_State.SetToolbarVisible(showToolbar);
        }

        ImGui::Separator();

        for (const RenderPanelDefinition& definition : RenderPanelRegistry::GetDefinitions()) {
            bool isOpen = m_State.IsPanelOpen(definition.id);
            if (ImGui::MenuItem(definition.toolbarLabel, nullptr, &isOpen)) {
                m_State.SetPanelOpen(definition.id, isOpen);
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Viewport")) {
        if (ImGui::MenuItem("Resume Live Viewport")) {
            StartPreview("Live viewport resumed from the Render menu.");
        }
        if (ImGui::MenuItem("Pause Live Viewport")) {
            CancelPreview();
        }
        if (ImGui::MenuItem("Reset Viewport Accumulation")) {
            ResetPreview("Viewport accumulation reset from the Render menu.");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Restore Default Render Layout")) {
            RenderDockLayout::ApplyDefaultLayout(dockSpaceId, dockSpaceSize);
            m_State.SetDefaultLayoutApplied(true);
        }

        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void RenderTab::RenderToolbar() {
    ImGui::BeginChild("RenderToolbar", ImVec2(0.0f, 46.0f), true);
    ImGui::AlignTextToFramePadding();

    if (ImGui::Button("New")) {
        QueueDiscardAction(PendingDiscardActionType::CreateEmptyScene);
    }

    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        const std::string path = FileDialogs::OpenRenderGltfFileDialog("Import glTF Scene");
        if (!path.empty()) {
            BeginGltfImport(path);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        ImGui::OpenPopup("RenderToolbarAddMenu");
    }
    if (ImGui::BeginPopup("RenderToolbarAddMenu")) {
        int newObjectId = -1;
        if (ImGui::MenuItem("Sphere")) {
            RenderSphere sphere;
            sphere.name = "Sphere";
            sphere.transform.translation = MakeRenderFloat3(0.0f, 0.75f, 0.0f);
            sphere.radius = 0.75f;
            sphere.materialIndex = 0;
            sphere.albedoTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);
            if (m_Scene.AddSphere(sphere, newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::Sphere, newObjectId };
            }
        }
        if (ImGui::MenuItem("Cube")) {
            if (m_Scene.AddBuiltInCube(0, newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::MeshInstance, newObjectId };
            }
        }
        if (ImGui::MenuItem("Plane")) {
            if (m_Scene.AddBuiltInPlane(0, newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::MeshInstance, newObjectId };
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rect Area Light")) {
            if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::RectArea), newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
            }
        }
        if (ImGui::MenuItem("Point Light")) {
            if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::Point), newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
            }
        }
        if (ImGui::MenuItem("Spot Light")) {
            if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::Spot), newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
            }
        }
        if (ImGui::MenuItem("Sun Light")) {
            if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::Sun), newObjectId)) {
                m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Material")) {
            m_Selection = RenderSelection {
                RenderSelectionType::Material,
                m_Scene.AddMaterial(BuildRenderMaterial("Material", MakeRenderFloat3(0.8f, 0.8f, 0.8f)))
            };
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SaveProjectToLibrary();
    }

    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
        SaveProjectAsToLibrary();
    }

    ImGui::SameLine();
    if (m_Job.IsFinalRequested() || m_Job.GetFinalState() == RenderJobState::Running) {
        if (ImGui::Button("Cancel Render")) {
            CancelFinalRender();
        }
    } else if (ImGui::Button("Render")) {
        StartFinalRender();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");

    ImGui::SameLine();
    {
        const bool isTranslate = m_Settings.GetGizmoMode() == RenderGizmoMode::Translate;
        if (isTranslate) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 1.0f, 0.75f)); }
        if (ImGui::Button("T##GizmoTranslate")) { m_Settings.SetGizmoMode(RenderGizmoMode::Translate); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Translate (T)"); }
        if (isTranslate) { ImGui::PopStyleColor(); }
    }

    ImGui::SameLine();
    {
        const bool isRotate = m_Settings.GetGizmoMode() == RenderGizmoMode::Rotate;
        if (isRotate) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 1.0f, 0.75f)); }
        if (ImGui::Button("R##GizmoRotate")) { m_Settings.SetGizmoMode(RenderGizmoMode::Rotate); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Rotate (R)"); }
        if (isRotate) { ImGui::PopStyleColor(); }
    }

    ImGui::SameLine();
    {
        const bool isScale = m_Settings.GetGizmoMode() == RenderGizmoMode::Scale;
        if (isScale) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 1.0f, 0.75f)); }
        if (ImGui::Button("S##GizmoScale")) { m_Settings.SetGizmoMode(RenderGizmoMode::Scale); }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Scale (S)"); }
        if (isScale) { ImGui::PopStyleColor(); }
    }

    ImGui::SameLine();
    {
        const bool isWorld = m_Settings.GetTransformSpace() == RenderTransformSpace::World;
        const char* spaceLabel = isWorld ? "W##Space" : "L##Space";
        const char* spaceTooltip = isWorld ? "World Space (L to toggle)" : "Local Space (L to toggle)";
        if (ImGui::Button(spaceLabel)) {
            m_Settings.SetTransformSpace(isWorld ? RenderTransformSpace::Local : RenderTransformSpace::World);
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", spaceTooltip); }
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::Text("%s%s", m_CurrentProjectName.empty() ? m_Scene.GetValidationSceneLabel().c_str() : m_CurrentProjectName.c_str(), m_HasUnsavedChanges ? " *" : "");
    ImGui::SameLine();
    if (m_Settings.GetIntegratorMode() == RenderIntegratorMode::RasterPreview) {
        ImGui::TextDisabled("Viewport raster live");
    } else {
        ImGui::TextDisabled("Viewport %u / %d", m_Buffers.GetSampleCount(), m_Settings.GetPreviewSampleTarget());
    }

    ImGui::EndChild();
}

void RenderTab::RenderPanels() {
    for (const RenderPanelDefinition& definition : RenderPanelRegistry::GetDefinitions()) {
        RenderPanel(definition.id);
    }
}

void RenderTab::RenderPanel(RenderPanelId id) {
    const RenderPanelDefinition& definition = RenderPanelRegistry::GetDefinition(id);
    bool isOpen = m_State.IsPanelOpen(id);
    if (!isOpen) {
        return;
    }

    if (ImGui::Begin(definition.windowTitle, &isOpen)) {
        switch (id) {
        case RenderPanelId::Viewport:
        {
            const bool rasterAuthoring = m_Settings.GetIntegratorMode() == RenderIntegratorMode::RasterPreview;
            unsigned int viewportTexture = m_Buffers.GetDisplayTexture();
            if (m_RasterPreviewRenderer.GetCompositedTexture() != 0) {
                viewportTexture = m_RasterPreviewRenderer.GetCompositedTexture();
            } else if (rasterAuthoring && m_RasterPreviewRenderer.GetSceneColorTexture() != 0) {
                viewportTexture = m_RasterPreviewRenderer.GetSceneColorTexture();
            }

            const char* viewportStateLabel = rasterAuthoring ? "Raster Preview" : m_Job.GetStateLabel();
            const std::string viewportStatusText = rasterAuthoring
                ? std::string("Interactive raster authoring viewport.")
                : m_Job.GetStatusText();
            const bool viewportActive = rasterAuthoring || m_Job.IsPreviewRequested();
            const unsigned int viewportSampleCount = rasterAuthoring ? 0u : m_Buffers.GetSampleCount();
            const RenderViewportPanelResult result = RenderViewportPanel::Render({
                viewportTexture,
                m_Buffers.GetWidth(),
                m_Buffers.GetHeight(),
                viewportSampleCount,
                m_Settings.GetPreviewSampleTarget(),
                m_Buffers.GetResetCount(),
                viewportActive,
                m_ViewportNavigationActive,
                m_ViewportNavigationBaseSpeed *
                    (ImGui::GetIO().KeyShift ? kViewportFastMultiplier :
                        (ImGui::GetIO().KeyCtrl ? kViewportSlowMultiplier : 1.0f)),
                IsSelectionObjectTransformable(),
                GetIntegratorModeLabel(),
                GetGizmoModeLabel(),
                GetTransformSpaceLabel(),
                viewportStateLabel,
                viewportStatusText,
                m_Job.GetLastResetReason(),
                m_ContextVersion
            });
            HandleViewportSelectionAndGizmo(result);
            HandleViewportNavigation(result);
            HandleViewportShortcuts(result);
            RenderViewportOverlay(result);
            break;
        }
        case RenderPanelId::Outliner:
            HandleOutlinerAction(RenderOutlinerPanel::Render({ m_Scene, m_Camera, m_Selection }));
            break;
        case RenderPanelId::Inspector:
            RenderInspectorPanel::Render({ m_Selection, m_Scene, m_Camera });
            break;
        case RenderPanelId::Settings:
            RenderSettingsPanel::Render({ m_Scene, m_Settings });
            break;
        case RenderPanelId::RenderManager:
            HandleRenderManagerAction(RenderManagerPanel::Render({
                m_Scene,
                m_Job,
                m_Buffers,
                m_Settings,
                m_CurrentProjectName,
                m_HasUnsavedChanges
            }));
            break;
        case RenderPanelId::Statistics:
            RenderStatisticsPanel::Render({
                m_Scene,
                m_Buffers,
                m_Settings,
                m_PreviewRenderer.GetDispatchGroupsX(),
                m_PreviewRenderer.GetDispatchGroupsY(),
                m_PreviewRenderer.GetUploadedSphereCount(),
                m_PreviewRenderer.GetUploadedTriangleCount(),
                m_PreviewRenderer.GetUploadedPrimitiveCount(),
                m_PreviewRenderer.GetUploadedBvhNodeCount(),
                m_Job.GetLastResetReason()
            });
            break;
        case RenderPanelId::Console:
            RenderConsolePanel::Render({ m_ConsoleLines });
            break;
        case RenderPanelId::AovDebug:
            RenderAovDebugPanel::Render({ m_Scene, m_Settings, m_Buffers });
            break;
        case RenderPanelId::AssetBrowser:
            HandleAssetBrowserAction(RenderAssetBrowserPanel::Render({
                m_Scene,
                m_LastSceneSnapshotStatus,
                m_LastSceneSnapshotPath,
                m_LastImportStatus,
                Async::IsBusy(m_ImportTaskState)
            }));
            break;
        case RenderPanelId::Count:
            break;
        }
    }
    ImGui::End();

    if (isOpen != m_State.IsPanelOpen(id)) {
        m_State.SetPanelOpen(id, isOpen);
    }
}

void RenderTab::HandleViewportNavigation(const RenderViewportPanelResult& result) {
    m_ViewportPanelSeenThisFrame = true;

    if (!result.hasRenderableImage) {
        EndViewportNavigation(true);
        return;
    }

    const bool rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!m_ViewportNavigationActive) {
        if (result.imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (!m_Job.IsPreviewRequested()) {
                StartPreview("Live viewport resumed from camera navigation.");
            }
            m_ViewportNavigationActive = true;
            m_ViewportNavigationChangedPose = false;
        } else {
            return;
        }
    }

    if (!rightMouseDown || !result.windowFocused) {
        EndViewportNavigation(true);
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float deltaTime = io.DeltaTime > 0.0f ? (io.DeltaTime > 0.05f ? 0.05f : io.DeltaTime) : 0.0f;

    float yaw = m_Camera.GetYawDegrees();
    float pitch = m_Camera.GetPitchDegrees();
    if (std::fabs(io.MouseDelta.x) > 0.0f || std::fabs(io.MouseDelta.y) > 0.0f) {
        yaw += io.MouseDelta.x * kViewportLookSensitivity;
        pitch -= io.MouseDelta.y * kViewportLookSensitivity;
    }

    RenderFloat3 moveInput {};
    const RenderFloat3 forward = m_Camera.GetForwardVector();
    const RenderFloat3 right = m_Camera.GetRightVector();
    const RenderFloat3 worldUp = MakeRenderFloat3(0.0f, 1.0f, 0.0f);

    if (ImGui::IsKeyDown(ImGuiKey_W)) {
        moveInput = Add(moveInput, forward);
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
        moveInput = Subtract(moveInput, forward);
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
        moveInput = Add(moveInput, right);
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
        moveInput = Subtract(moveInput, right);
    }
    if (ImGui::IsKeyDown(ImGuiKey_E)) {
        moveInput = Add(moveInput, worldUp);
    }
    if (ImGui::IsKeyDown(ImGuiKey_Q)) {
        moveInput = Subtract(moveInput, worldUp);
    }

    RenderFloat3 position = m_Camera.GetPosition();
    if (Length(moveInput) > 0.0001f && deltaTime > 0.0f) {
        float moveSpeed = m_ViewportNavigationBaseSpeed;
        if (io.KeyShift) {
            moveSpeed *= kViewportFastMultiplier;
        }
        if (io.KeyCtrl) {
            moveSpeed *= kViewportSlowMultiplier;
        }
        position = Add(position, Scale(Normalize(moveInput), moveSpeed * deltaTime));
    }

    if (m_Camera.ApplySnapshot(
            position,
            yaw,
            pitch,
            m_Camera.GetFieldOfViewDegrees(),
            m_Camera.GetFocusDistance(),
            m_Camera.GetApertureRadius(),
            m_Camera.GetExposure(),
            kViewportNavigationReason)) {
        m_ViewportNavigationChangedPose = true;
    }
}

void RenderTab::HandleViewportSelectionAndGizmo(const RenderViewportPanelResult& result) {
    if (!result.hasRenderableImage) {
        EndViewportGizmoDrag();
        m_ViewportHoveredAxis = -1;
        return;
    }

    RenderTransform selectionTransform {};
    RenderFloat3 selectionPivot {};
    GizmoProjection projection {};
    const bool canManipulateSelection =
        IsSelectionObjectTransformable() &&
        ResolveSelectionTransform(selectionTransform) &&
        ResolveSelectionPivot(selectionPivot) &&
        BuildGizmoProjection(m_Camera, result, selectionTransform, selectionPivot, m_Settings.GetTransformSpace(), projection);

    m_ViewportHoveredAxis =
        (canManipulateSelection && !m_ViewportNavigationActive) ? FindHoveredAxis(projection, result.mousePosition) : -1;

    if (m_ViewportGizmoDragging) {
        const bool sameSelection =
            m_Selection.type == m_ViewportGizmoSelection.type &&
            m_Selection.objectId == m_ViewportGizmoSelection.objectId;
        if (!sameSelection || (!result.mouseDownLeft && !result.leftReleased) || m_ViewportNavigationActive) {
            EndViewportGizmoDrag();
            return;
        }

        RenderTransform updatedTransform = m_ViewportGizmoStartTransform;
        const ImVec2 mouseDelta(
            result.mousePosition.x - m_ViewportGizmoDragStartMouse.x,
            result.mousePosition.y - m_ViewportGizmoDragStartMouse.y);
        const float dragProjection =
            mouseDelta.x * m_ViewportGizmoAxisScreenDirection.x +
            mouseDelta.y * m_ViewportGizmoAxisScreenDirection.y;
        const float worldUnits = dragProjection / std::max(m_ViewportGizmoPixelsPerUnit, 1.0f);

        switch (m_Settings.GetGizmoMode()) {
        case RenderGizmoMode::Translate:
            updatedTransform.translation = Add(
                m_ViewportGizmoStartTransform.translation,
                Scale(
                    GetTransformAxisVector(
                        m_ViewportGizmoStartTransform,
                        m_ViewportActiveAxis,
                        m_Settings.GetTransformSpace()),
                    worldUnits));
            break;
        case RenderGizmoMode::Rotate:
        {
            const ImVec2 tangent(-m_ViewportGizmoAxisScreenDirection.y, m_ViewportGizmoAxisScreenDirection.x);
            const float angleDelta =
                (mouseDelta.x * tangent.x + mouseDelta.y * tangent.y) * kGizmoRotationSensitivity;
            float* rotationComponent = nullptr;
            if (m_ViewportActiveAxis == 0) {
                rotationComponent = &updatedTransform.rotationDegrees.x;
            } else if (m_ViewportActiveAxis == 1) {
                rotationComponent = &updatedTransform.rotationDegrees.y;
            } else {
                rotationComponent = &updatedTransform.rotationDegrees.z;
            }
            *rotationComponent += angleDelta;
            break;
        }
        case RenderGizmoMode::Scale:
        {
            const float scaleDelta = worldUnits * kGizmoScaleSensitivity;
            float* scaleComponent = nullptr;
            if (m_ViewportActiveAxis == 0) {
                scaleComponent = &updatedTransform.scale.x;
            } else if (m_ViewportActiveAxis == 1) {
                scaleComponent = &updatedTransform.scale.y;
            } else {
                scaleComponent = &updatedTransform.scale.z;
            }
            *scaleComponent = std::max(0.05f, *scaleComponent + scaleDelta);
            break;
        }
        }

        if (UpdateSelectionTransform(updatedTransform) && !m_Job.IsPreviewRequested()) {
            StartPreview("Live viewport resumed from gizmo interaction.");
        }

        if (!result.mouseDownLeft) {
            EndViewportGizmoDrag();
        }
        return;
    }

    if (!result.leftClicked || !result.imageHovered || m_ViewportNavigationActive) {
        return;
    }

    if (canManipulateSelection && m_ViewportHoveredAxis >= 0) {
        const ImVec2 axisDelta(
            projection.axisEndScreen[static_cast<std::size_t>(m_ViewportHoveredAxis)].x - projection.originScreen.x,
            projection.axisEndScreen[static_cast<std::size_t>(m_ViewportHoveredAxis)].y - projection.originScreen.y);
        const float axisLength = std::sqrt(axisDelta.x * axisDelta.x + axisDelta.y * axisDelta.y);
        if (axisLength > 0.001f) {
            m_ViewportGizmoDragging = true;
            m_ViewportActiveAxis = m_ViewportHoveredAxis;
            m_ViewportGizmoSelection = m_Selection;
            m_ViewportGizmoStartTransform = selectionTransform;
            m_ViewportGizmoDragStartMouse = result.mousePosition;
            m_ViewportGizmoAxisScreenDirection = ImVec2(axisDelta.x / axisLength, axisDelta.y / axisLength);
            m_ViewportGizmoPixelsPerUnit = projection.pixelsPerUnit[static_cast<std::size_t>(m_ViewportHoveredAxis)];
            if (!m_Job.IsPreviewRequested()) {
                StartPreview("Live viewport resumed from gizmo interaction.");
            }
            return;
        }
    }

    const RenderSelection pickedSelection = PickSceneObject(m_Scene, m_Camera, result);
    if (pickedSelection.objectId > 0) {
        m_Selection = pickedSelection;
    } else {
        m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
    }
}

void RenderTab::HandleViewportShortcuts(const RenderViewportPanelResult& result) {
    if (!result.windowFocused || m_ViewportNavigationActive) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_T)) {
        m_Settings.SetGizmoMode(RenderGizmoMode::Translate);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        m_Settings.SetGizmoMode(RenderGizmoMode::Rotate);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S)) {
        m_Settings.SetGizmoMode(RenderGizmoMode::Scale);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L)) {
        m_Settings.SetTransformSpace(
            m_Settings.GetTransformSpace() == RenderTransformSpace::World
                ? RenderTransformSpace::Local
                : RenderTransformSpace::World);
    }

    if (!IsSelectionObjectTransformable()) {
        return;
    }

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        DuplicateSelection();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        DeleteSelection();
    }
}

void RenderTab::RenderViewportOverlay(const RenderViewportPanelResult& result) const {
    if (!result.hasRenderableImage) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(result.imageMin, result.imageMax, IM_COL32(255, 255, 255, 28));

    for (int lightIndex = 0; lightIndex < m_Scene.GetLightCount(); ++lightIndex) {
        const RenderLight& light = m_Scene.GetLight(lightIndex);
        ImVec2 screenPoint {};
        if (!ProjectWorldToScreen(m_Camera, result, light.transform.translation, screenPoint, nullptr)) {
            continue;
        }

        const bool selected =
            m_Selection.type == RenderSelectionType::Light &&
            m_Selection.objectId == light.id;
        const ImU32 color = selected ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 210, 96, 220);
        drawList->AddCircleFilled(screenPoint, selected ? 6.0f : 4.5f, color);
        drawList->AddCircle(screenPoint, selected ? 9.0f : 7.0f, IM_COL32(16, 18, 22, 220), 0, 2.0f);
        drawList->AddText(
            ImVec2(screenPoint.x + 8.0f, screenPoint.y - 9.0f),
            color,
            GetRenderLightTypeLabel(light.type));
    }

    RenderTransform selectionTransform {};
    RenderFloat3 selectionPivot {};
    GizmoProjection projection {};
    if (!IsSelectionObjectTransformable() ||
        !ResolveSelectionTransform(selectionTransform) ||
        !ResolveSelectionPivot(selectionPivot) ||
        !BuildGizmoProjection(m_Camera, result, selectionTransform, selectionPivot, m_Settings.GetTransformSpace(), projection)) {
        return;
    }

    drawList->AddCircleFilled(projection.originScreen, 4.0f, IM_COL32(255, 255, 255, 220));
    for (int axis = 0; axis < 3; ++axis) {
        if (!projection.axisVisible[axis]) {
            continue;
        }

        const bool emphasized = axis == m_ViewportActiveAxis || axis == m_ViewportHoveredAxis;
        const ImU32 color = GetAxisColor(axis, emphasized);
        if (m_Settings.GetGizmoMode() == RenderGizmoMode::Rotate) {
            drawList->AddCircle(projection.axisEndScreen[axis], emphasized ? 18.0f : 14.0f, color, 0, emphasized ? 3.5f : 2.0f);
        } else if (m_Settings.GetGizmoMode() == RenderGizmoMode::Scale) {
            drawList->AddLine(projection.originScreen, projection.axisEndScreen[axis], color, emphasized ? 3.5f : 2.0f);
            const float size = emphasized ? 5.5f : 4.0f;
            drawList->AddRectFilled(
                ImVec2(projection.axisEndScreen[axis].x - size, projection.axisEndScreen[axis].y - size),
                ImVec2(projection.axisEndScreen[axis].x + size, projection.axisEndScreen[axis].y + size),
                color);
        } else {
            drawList->AddLine(projection.originScreen, projection.axisEndScreen[axis], color, emphasized ? 3.5f : 2.0f);
            drawList->AddCircleFilled(projection.axisEndScreen[axis], emphasized ? 5.5f : 4.0f, color);
        }
        drawList->AddText(
            ImVec2(projection.axisEndScreen[axis].x + 6.0f, projection.axisEndScreen[axis].y - 8.0f),
            color,
            axis == 0 ? "X" : (axis == 1 ? "Y" : "Z"));
    }

    const char* interactionHelp =
        "LMB pick | drag axis to edit | T/R/S gizmo | L world/local | Ctrl+D duplicate | Delete remove";
    const ImVec2 helpPosition(result.imageMin.x + 12.0f, result.imageMax.y - 24.0f);
    drawList->AddRectFilled(
        ImVec2(helpPosition.x - 8.0f, helpPosition.y - 4.0f),
        ImVec2(helpPosition.x + 560.0f, helpPosition.y + 18.0f),
        IM_COL32(12, 18, 28, 170),
        6.0f);
    drawList->AddText(helpPosition, IM_COL32(230, 236, 248, 235), interactionHelp);
}

void RenderTab::EndViewportNavigation(bool appendSummary) {
    if (!m_ViewportNavigationActive) {
        return;
    }

    m_ViewportNavigationActive = false;
    if (appendSummary && m_ViewportNavigationChangedPose) {
        AppendConsoleLine("[Viewport] Fly camera navigation updated the active camera pose.");
    }
    m_ViewportNavigationChangedPose = false;
}

void RenderTab::EndViewportGizmoDrag() {
    m_ViewportGizmoDragging = false;
    m_ViewportActiveAxis = -1;
    m_ViewportHoveredAxis = -1;
    m_ViewportGizmoSelection = RenderSelection {};
    m_ViewportGizmoAxisScreenDirection = ImVec2(0.0f, 0.0f);
    m_ViewportGizmoPixelsPerUnit = 1.0f;
}

void RenderTab::HandleRenderManagerAction(RenderManagerAction action) {
    switch (action) {
    case RenderManagerAction::None:
        break;
    case RenderManagerAction::StartPreview:
        StartPreview("Live viewport resumed from Render Manager.");
        break;
    case RenderManagerAction::CancelPreview:
        CancelPreview();
        break;
    case RenderManagerAction::ResetAccumulation:
        ResetPreview("Viewport accumulation reset from Render Manager.");
        break;
    case RenderManagerAction::SaveProjectToLibrary:
        SaveProjectToLibrary();
        break;
    case RenderManagerAction::SaveProjectAsToLibrary:
        SaveProjectAsToLibrary();
        break;
    case RenderManagerAction::StartFinalRender:
        StartFinalRender();
        break;
    case RenderManagerAction::CancelFinalRender:
        CancelFinalRender();
        break;
    }
}

void RenderTab::HandleOutlinerAction(const RenderOutlinerAction& action) {
    int newObjectId = -1;

    switch (action.type) {
    case RenderOutlinerActionType::None:
        break;
    case RenderOutlinerActionType::CreateEmptyScene:
        QueueDiscardAction(PendingDiscardActionType::CreateEmptyScene);
        break;
    case RenderOutlinerActionType::LoadSceneSnapshot:
        if (!action.path.empty()) {
            QueueDiscardAction(PendingDiscardActionType::LoadSceneSnapshot, RenderValidationSceneId::Custom, action.path);
        }
        break;
    case RenderOutlinerActionType::SaveProjectToLibrary:
        SaveProjectToLibrary();
        break;
    case RenderOutlinerActionType::SaveProjectAsToLibrary:
        SaveProjectAsToLibrary();
        break;
    case RenderOutlinerActionType::AddSphere:
    {
        RenderSphere sphere;
        sphere.name = "Sphere";
        sphere.transform.translation = MakeRenderFloat3(0.0f, 0.75f, 0.0f);
        sphere.radius = 0.75f;
        sphere.materialIndex = 0;
        sphere.albedoTint = MakeRenderFloat3(1.0f, 1.0f, 1.0f);
        if (m_Scene.AddSphere(sphere, newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::Sphere, newObjectId };
        }
        break;
    }
    case RenderOutlinerActionType::AddCube:
        if (m_Scene.AddBuiltInCube(0, newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::MeshInstance, newObjectId };
        }
        break;
    case RenderOutlinerActionType::AddPlane:
        if (m_Scene.AddBuiltInPlane(0, newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::MeshInstance, newObjectId };
        }
        break;
    case RenderOutlinerActionType::AddMaterial:
        m_Selection = RenderSelection {
            RenderSelectionType::Material,
            m_Scene.AddMaterial(BuildRenderMaterial("Material", MakeRenderFloat3(0.8f, 0.8f, 0.8f)))
        };
        break;
    case RenderOutlinerActionType::AddRectAreaLight:
        if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::RectArea), newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
        }
        break;
    case RenderOutlinerActionType::AddPointLight:
        if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::Point), newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
        }
        break;
    case RenderOutlinerActionType::AddSpotLight:
        if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::Spot), newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
        }
        break;
    case RenderOutlinerActionType::AddSunLight:
        if (m_Scene.AddLight(BuildDefaultLight(RenderLightType::Sun), newObjectId)) {
            m_Selection = RenderSelection { RenderSelectionType::Light, newObjectId };
        }
        break;
    case RenderOutlinerActionType::DuplicateSelection:
        m_Selection = action.selection;
        DuplicateSelection();
        break;
    case RenderOutlinerActionType::DeleteSelection:
        m_Selection = action.selection;
        DeleteSelection();
        break;
    case RenderOutlinerActionType::FocusSelection:
        m_Selection = action.selection;
        FocusSelection();
        break;
    }
}

void RenderTab::HandleAssetBrowserAction(const RenderAssetBrowserAction& action) {
    switch (action.type) {
    case RenderAssetBrowserActionType::None:
        break;
    case RenderAssetBrowserActionType::CreateEmptyScene:
        QueueDiscardAction(PendingDiscardActionType::CreateEmptyScene);
        break;
    case RenderAssetBrowserActionType::SelectValidationScene:
        QueueDiscardAction(PendingDiscardActionType::LoadValidationTemplate, action.sceneId);
        break;
    case RenderAssetBrowserActionType::ImportGltfScene:
        BeginGltfImport(action.path);
        break;
    case RenderAssetBrowserActionType::SaveSceneSnapshot:
        SaveSceneSnapshot(action.path);
        break;
    case RenderAssetBrowserActionType::LoadSceneSnapshot:
        LoadSceneSnapshot(action.path);
        break;
    }
}

void RenderTab::HandleRuntimeChanges() {
    std::string resetReason;

    if (m_Scene.GetRevision() != m_ObservedSceneRevision) {
        m_ObservedSceneRevision = m_Scene.GetRevision();
        resetReason = m_Scene.GetLastChangeReason();
        AppendConsoleLine("[Scene] " + m_Scene.GetLastChangeReason());
        SanitizeSelection();
        MarkProjectDirty();
    }

    if (m_Camera.GetRevision() != m_ObservedCameraRevision) {
        m_ObservedCameraRevision = m_Camera.GetRevision();
        resetReason = m_Camera.GetLastChangeReason();
        if (!IsViewportNavigationReason(m_Camera.GetLastChangeReason())) {
            AppendConsoleLine("[Camera] " + m_Camera.GetLastChangeReason());
        }
        MarkProjectDirty();
    }

    if (m_Settings.GetRevision() != m_ObservedSettingsRevision) {
        m_ObservedSettingsRevision = m_Settings.GetRevision();
        resetReason = m_Settings.GetLastChangeReason();
        AppendConsoleLine("[Settings] " + m_Settings.GetLastChangeReason());
        MarkProjectDirty();
    }

    const bool storageChanged = m_Buffers.EnsureStorage(m_Settings.GetResolutionX(), m_Settings.GetResolutionY());
    if (storageChanged) {
        AppendConsoleLine("[Render] Preview buffers allocated at " +
            std::to_string(m_Settings.GetResolutionX()) + "x" +
            std::to_string(m_Settings.GetResolutionY()) + ".");
        if (resetReason.empty()) {
            resetReason = "Preview buffers resized to match render resolution.";
        }
    }

    if (!resetReason.empty()) {
        ResetPreview(resetReason, !IsViewportNavigationReason(resetReason));
    }
}

void RenderTab::StartPreview(const std::string& reason) {
    m_AutoStartPending = false;
    const RenderJobState previousState = m_Job.GetState();

    const bool storageChanged = m_Buffers.EnsureStorage(m_Settings.GetResolutionX(), m_Settings.GetResolutionY());
    if (storageChanged) {
        AppendConsoleLine("[Render] Preview buffers allocated at " +
            std::to_string(m_Settings.GetResolutionX()) + "x" +
            std::to_string(m_Settings.GetResolutionY()) + ".");
    }

    if (!m_RasterPreviewRenderer.Initialize()) {
        m_Job.FailPreview(m_RasterPreviewRenderer.GetLastError());
        AppendConsoleLine("[Render] " + m_RasterPreviewRenderer.GetLastError());
        return;
    }
    if (m_Settings.GetIntegratorMode() != RenderIntegratorMode::RasterPreview &&
        !m_PreviewRenderer.Initialize()) {
        m_Job.FailPreview(m_PreviewRenderer.GetLastError());
        AppendConsoleLine("[Render] " + m_PreviewRenderer.GetLastError());
        return;
    }

    m_Job.StartPreview();
    AppendConsoleLine("[Render] " + reason);

    const bool needsFreshStart =
        storageChanged ||
        m_Buffers.GetSampleCount() == 0 ||
        previousState == RenderJobState::Failed;

    if (needsFreshStart) {
        ResetPreview(reason.empty() ? std::string("Preview reset before start.") : reason);
    }
}

void RenderTab::CancelPreview() {
    m_AutoStartPending = false;
    if (!m_Job.IsPreviewRequested() && m_Job.GetState() == RenderJobState::Canceled) {
        return;
    }

    m_Job.CancelPreview();
    AppendConsoleLine("[Render] Live viewport paused.");
}

void RenderTab::ResetPreview(const std::string& reason, bool appendConsole) {
    m_Buffers.ResetAccumulation();
    m_Job.SetLastResetReason(reason);
    if (m_Job.IsPreviewRequested()) {
        m_Job.StartPreview();
    }
    if (appendConsole) {
        AppendConsoleLine("[Reset] " + reason);
    }
}

void RenderTab::TickPreviewJob() {
    if (m_Buffers.GetWidth() <= 0 || m_Buffers.GetHeight() <= 0) {
        return;
    }

    bool rasterReady = false;
    if (m_RasterPreviewRenderer.Initialize()) {
        rasterReady = m_RasterPreviewRenderer.RenderScenePreview(
            m_Scene,
            m_Camera,
            m_Settings,
            m_Buffers.GetWidth(),
            m_Buffers.GetHeight());
        if (!rasterReady &&
            m_Settings.GetIntegratorMode() == RenderIntegratorMode::RasterPreview &&
            m_Job.GetStatusText() != m_RasterPreviewRenderer.GetLastError()) {
            m_Job.FailPreview(m_RasterPreviewRenderer.GetLastError());
            AppendConsoleLine("[Render] " + m_RasterPreviewRenderer.GetLastError());
        }
    } else if (m_Settings.GetIntegratorMode() == RenderIntegratorMode::RasterPreview &&
        m_Job.GetStatusText() != m_RasterPreviewRenderer.GetLastError()) {
        m_Job.FailPreview(m_RasterPreviewRenderer.GetLastError());
        AppendConsoleLine("[Render] " + m_RasterPreviewRenderer.GetLastError());
    }

    const int selectedObjectId = IsSelectionObjectTransformable() ? m_Selection.objectId : -1;
    if (m_Settings.GetIntegratorMode() == RenderIntegratorMode::RasterPreview) {
        unsigned int baseTexture = m_RasterPreviewRenderer.GetSceneColorTexture();
        if (rasterReady && baseTexture != 0) {
            if (!m_RasterPreviewRenderer.ComposeViewport(
                    baseTexture,
                    m_Buffers.GetWidth(),
                    m_Buffers.GetHeight(),
                    selectedObjectId) &&
                m_Job.GetStatusText() != m_RasterPreviewRenderer.GetLastError()) {
                m_Job.FailPreview(m_RasterPreviewRenderer.GetLastError());
                AppendConsoleLine("[Render] " + m_RasterPreviewRenderer.GetLastError());
            }
        }
        return;
    }

    if (!m_Job.IsPreviewRequested()) {
        if (rasterReady && m_Buffers.GetDisplayTexture() != 0) {
            m_RasterPreviewRenderer.ComposeViewport(
                m_Buffers.GetDisplayTexture(),
                m_Buffers.GetWidth(),
                m_Buffers.GetHeight(),
                selectedObjectId);
        }
    } else {
        m_Job.MarkRunning();
        if (!m_PreviewRenderer.RenderPreview(m_Scene, m_Camera, m_Settings, m_Buffers)) {
            m_Job.FailPreview(m_PreviewRenderer.GetLastError());
            AppendConsoleLine("[Render] " + m_PreviewRenderer.GetLastError());
            return;
        }

        if (rasterReady && m_Buffers.GetDisplayTexture() != 0) {
            if (!m_RasterPreviewRenderer.ComposeViewport(
                    m_Buffers.GetDisplayTexture(),
                    m_Buffers.GetWidth(),
                    m_Buffers.GetHeight(),
                    selectedObjectId) &&
                m_Job.GetStatusText() != m_RasterPreviewRenderer.GetLastError()) {
                AppendConsoleLine("[Render] " + m_RasterPreviewRenderer.GetLastError());
            }
        }
    }

    if (m_Job.IsFinalRequested()) {
        const RenderFinalRenderSettings& finalSettings = m_Settings.GetFinalRenderSettings();
        const bool finalStorageChanged = m_FinalBuffers.EnsureStorage(finalSettings.resolutionX, finalSettings.resolutionY);
        if (finalStorageChanged) {
            m_FinalBuffers.ResetAccumulation();
        }

        if (!m_PreviewRenderer.Initialize()) {
            m_Job.FailFinal(m_PreviewRenderer.GetLastError());
            AppendConsoleLine("[Render] " + m_PreviewRenderer.GetLastError());
            return;
        }

        RenderSettings renderSettings = m_Settings;
        renderSettings.SetResolution(finalSettings.resolutionX, finalSettings.resolutionY);
        renderSettings.SetIntegratorMode(RenderIntegratorMode::PathTracePreview);
        renderSettings.SetDisplayMode(RenderDisplayMode::Color);
        renderSettings.SetAccumulationEnabled(true);
        renderSettings.SetMaxBounceCount(finalSettings.maxBounceCount);

        if (!m_PreviewRenderer.RenderPreview(m_Scene, m_Camera, renderSettings, m_FinalBuffers)) {
            m_Job.FailFinal(m_PreviewRenderer.GetLastError());
            AppendConsoleLine("[Render] " + m_PreviewRenderer.GetLastError());
            return;
        }

        m_Job.MarkFinalRunning(m_FinalBuffers.GetSampleCount(), static_cast<unsigned int>(std::max(finalSettings.sampleTarget, 1)));
        if (m_FinalBuffers.GetSampleCount() >= static_cast<unsigned int>(std::max(finalSettings.sampleTarget, 1))) {
            std::vector<unsigned char> pixels;
            std::string errorMessage;
            if (!CaptureTexturePixels(m_FinalBuffers.GetDisplayTexture(), m_FinalBuffers.GetWidth(), m_FinalBuffers.GetHeight(), pixels, errorMessage)) {
                m_Job.FailFinal(errorMessage);
                AppendConsoleLine("[Render] " + errorMessage);
                return;
            }

            StackBinaryFormat::json payload;
            std::vector<unsigned char> ignoredBeautyPixels;
            int ignoredWidth = 0;
            int ignoredHeight = 0;
            const std::string predictedProjectFileName = m_CurrentProjectFileName.empty()
                ? BuildSafeRenderProjectFileName(m_CurrentProjectName.empty() ? finalSettings.outputName : m_CurrentProjectName)
                : m_CurrentProjectFileName;
            const std::string predictedAssetFileName = BuildRenderAssetFileName(predictedProjectFileName);
            if (!BuildLibraryProjectPayload(payload, ignoredBeautyPixels, ignoredWidth, ignoredHeight, errorMessage)) {
                m_Job.FailFinal(errorMessage);
                AppendConsoleLine("[Render] " + errorMessage);
                return;
            }
            payload["latestFinalAssetFileName"] = predictedAssetFileName;

            const std::string projectName = m_CurrentProjectName.empty()
                ? m_Scene.GetValidationSceneLabel()
                : m_CurrentProjectName;
            const std::string existingFileName = m_CurrentProjectFileName.empty() ? std::string() : m_CurrentProjectFileName;
            const std::uint64_t savedSceneRevision = m_Scene.GetRevision();
            const std::uint64_t savedCameraRevision = m_Camera.GetRevision();
            const std::uint64_t savedSettingsRevision = m_Settings.GetRevision();
            const std::string statusText = "[Render] Final still reached sample target. Saving to Library.";
            AppendConsoleLine(statusText);
            m_Job.CancelFinal();
            LibraryManager::Get().RequestSaveRenderProject(
                projectName,
                payload,
                pixels,
                m_FinalBuffers.GetWidth(),
                m_FinalBuffers.GetHeight(),
                existingFileName,
                [this, savedSceneRevision, savedCameraRevision, savedSettingsRevision](bool success, const std::string& savedFileName, const std::string& assetFileName) {
                    if (!success) {
                        m_Job.FailFinal("Failed to save the final still render to the Library.");
                        AppendConsoleLine("[Library] Failed to save the final still render to the Library.");
                        return;
                    }

                    m_CurrentProjectFileName = savedFileName;
                    if (m_CurrentProjectName.empty()) {
                        m_CurrentProjectName = m_Scene.GetValidationSceneLabel();
                    }
                    m_Job.CompleteFinal(assetFileName);
                    if (m_Scene.GetRevision() == savedSceneRevision &&
                        m_Camera.GetRevision() == savedCameraRevision &&
                        m_Settings.GetRevision() == savedSettingsRevision) {
                        ClearProjectDirty();
                    }
                    AppendConsoleLine("[Library] Final still render saved to " + assetFileName + ".");
                });
        }
    }
}

void RenderTab::SaveSceneSnapshot(const std::string& path) {
    std::string errorMessage;
    const std::filesystem::path snapshotPath(path);
    if (RenderSceneSerialization::WriteSnapshotFile(snapshotPath, m_Scene, m_Camera, errorMessage)) {
        m_LastSceneSnapshotPath = snapshotPath.string();
        m_LastSceneSnapshotStatus = std::string("Saved render scene snapshot to ") + snapshotPath.filename().string() + ".";
        AppendConsoleLine("[Snapshot] " + m_LastSceneSnapshotStatus);
        return;
    }

    m_LastSceneSnapshotPath = snapshotPath.string();
    m_LastSceneSnapshotStatus = errorMessage.empty()
        ? "Failed to save the render scene snapshot."
        : std::string("Failed to save the render scene snapshot: ") + errorMessage;
    AppendConsoleLine("[Snapshot] " + m_LastSceneSnapshotStatus);
}

void RenderTab::BeginGltfImport(const std::string& path) {
    if (path.empty()) {
        return;
    }

    ++m_ImportGeneration;
    const std::uint64_t generation = m_ImportGeneration;
    const std::filesystem::path importPath(path);
    m_ImportTaskState = Async::TaskState::Queued;
    m_LastImportStatus = std::string("Queued glTF import from ") + importPath.filename().string() + ".";
    AppendConsoleLine("[Import] " + m_LastImportStatus);

    Async::TaskSystem::Get().Submit([this, generation, path]() {
        RenderImportResult importResult;
        std::string errorMessage;
        const bool success = RenderGltfImporter::ImportScene(path, importResult, errorMessage);

        Async::TaskSystem::Get().PostToMain([this,
                                             generation,
                                             path,
                                             success,
                                             importResult = std::move(importResult),
                                             errorMessage = std::move(errorMessage)]() mutable {
            if (generation != m_ImportGeneration) {
                return;
            }

            const std::filesystem::path importPath(path);
            if (!success) {
                m_ImportTaskState = Async::TaskState::Failed;
                m_LastImportStatus = errorMessage.empty()
                    ? std::string("Failed to import glTF scene.")
                    : std::string("Failed to import glTF scene: ") + errorMessage;
                AppendConsoleLine("[Import] " + m_LastImportStatus);
                return;
            }

            m_ImportTaskState = Async::TaskState::Applying;
            m_Scene.MergeImportedScene(
                importResult.label,
                importResult.description,
                std::move(importResult.importedAssets),
                std::move(importResult.importedTextures),
                std::move(importResult.materials),
                std::move(importResult.meshes),
                std::move(importResult.meshInstances),
                std::move(importResult.spheres),
                std::move(importResult.triangles),
                {},
                std::string("Imported glTF scene from ") + importPath.filename().string() + ".");
            m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
            FocusSelection();
            EndViewportGizmoDrag();
            if (m_CurrentProjectName.empty()) {
                m_CurrentProjectName = m_Scene.GetValidationSceneLabel();
            }
            m_LastImportStatus = std::string("Imported glTF scene from ") + importPath.filename().string() + " and merged into the active scene.";
            m_ImportTaskState = Async::TaskState::Idle;
            AppendConsoleLine("[Import] " + m_LastImportStatus);
        });
    });
}

void RenderTab::LoadSceneSnapshot(const std::string& path) {
    std::string errorMessage;
    RenderSceneSnapshotDocument document;
    const std::filesystem::path snapshotPath(path);
    if (!RenderSceneSerialization::ReadSnapshotFile(snapshotPath, document, errorMessage)) {
        m_LastSceneSnapshotPath = snapshotPath.string();
        m_LastSceneSnapshotStatus = errorMessage.empty()
            ? "Failed to load the render scene snapshot."
            : std::string("Failed to load the render scene snapshot: ") + errorMessage;
        AppendConsoleLine("[Snapshot] " + m_LastSceneSnapshotStatus);
        return;
    }

    m_Scene.ApplySceneSnapshot(
        document.label,
        document.description,
        document.backgroundMode,
        document.environmentEnabled,
        document.environmentIntensity,
        document.fogEnabled,
        document.fogColor,
        document.fogDensity,
        document.fogAnisotropy,
        std::move(document.importedAssets),
        std::move(document.importedTextures),
        std::move(document.materials),
        std::move(document.meshes),
        std::move(document.meshInstances),
        std::move(document.spheres),
        std::move(document.triangles),
        std::move(document.lights),
        std::string("Scene snapshot loaded from ") + snapshotPath.filename().string() + ".");
    m_Camera.ApplySnapshot(
        document.cameraPosition,
        document.cameraYawDegrees,
        document.cameraPitchDegrees,
        document.cameraFieldOfViewDegrees,
        document.cameraFocusDistance,
        document.cameraApertureRadius,
        document.cameraExposure,
        std::string("Camera snapshot loaded from ") + snapshotPath.filename().string() + ".");
    m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
    EndViewportGizmoDrag();
    m_CurrentProjectName = m_Scene.GetValidationSceneLabel();
    m_CurrentProjectFileName.clear();
    m_Job.ClearLatestFinalAssetFileName();
    m_FinalBuffers.Release();
    m_LastSceneSnapshotPath = snapshotPath.string();
    m_LastSceneSnapshotStatus = std::string("Loaded render scene snapshot from ") + snapshotPath.filename().string() + ".";
    AppendConsoleLine("[Snapshot] " + m_LastSceneSnapshotStatus);
    HandleRuntimeChanges();
    ClearProjectDirty();
}

void RenderTab::StartFinalRender() {
    if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        AppendConsoleLine("[Render] Final still render waits for the current Library save to finish.");
        return;
    }

    const RenderFinalRenderSettings& finalSettings = m_Settings.GetFinalRenderSettings();
    m_FinalBuffers.EnsureStorage(finalSettings.resolutionX, finalSettings.resolutionY);
    m_FinalBuffers.ResetAccumulation();
    m_Job.QueueFinal(finalSettings);
    AppendConsoleLine(
        "[Render] Final still queued at " +
        std::to_string(finalSettings.resolutionX) + "x" +
        std::to_string(finalSettings.resolutionY) + ", " +
        std::to_string(finalSettings.sampleTarget) + " samples.");
}

void RenderTab::CancelFinalRender() {
    if (!m_Job.IsFinalRequested() && !m_Job.IsFinalRunning()) {
        return;
    }

    m_Job.CancelFinal();
    AppendConsoleLine("[Render] Final still render canceled.");
}

bool RenderTab::QueueDiscardAction(
    PendingDiscardActionType type,
    RenderValidationSceneId sceneId,
    const std::string& path) {
    if (type == PendingDiscardActionType::None) {
        return false;
    }

    if (!m_HasUnsavedChanges) {
        m_PendingDiscardActionType = type;
        m_PendingDiscardSceneId = sceneId;
        m_PendingDiscardPath = path;
        ExecutePendingDiscardAction();
        return true;
    }

    m_PendingDiscardActionType = type;
    m_PendingDiscardSceneId = sceneId;
    m_PendingDiscardPath = path;
    m_OpenDiscardPopupPending = true;
    return true;
}

void RenderTab::ExecutePendingDiscardAction() {
    switch (m_PendingDiscardActionType) {
    case PendingDiscardActionType::None:
        break;
    case PendingDiscardActionType::CreateEmptyScene:
        m_Scene.CreateEmptyScene();
        m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
        m_CurrentProjectName = m_Scene.GetValidationSceneLabel();
        m_CurrentProjectFileName.clear();
        m_Job.ClearLatestFinalAssetFileName();
        m_FinalBuffers.Release();
        EndViewportGizmoDrag();
        HandleRuntimeChanges();
        ClearProjectDirty();
        AppendConsoleLine("[Scene] Started a new empty render scene.");
        break;
    case PendingDiscardActionType::LoadValidationTemplate:
        m_Scene.SetValidationScene(m_PendingDiscardSceneId);
        m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
        m_CurrentProjectName = m_Scene.GetValidationSceneLabel();
        m_CurrentProjectFileName.clear();
        m_Job.ClearLatestFinalAssetFileName();
        m_FinalBuffers.Release();
        EndViewportGizmoDrag();
        HandleRuntimeChanges();
        ClearProjectDirty();
        AppendConsoleLine("[Scene] Loaded validation template " + m_Scene.GetValidationSceneLabel() + ".");
        break;
    case PendingDiscardActionType::LoadSceneSnapshot:
        if (!m_PendingDiscardPath.empty()) {
            LoadSceneSnapshot(m_PendingDiscardPath);
        }
        break;
    }

    m_PendingDiscardActionType = PendingDiscardActionType::None;
    m_PendingDiscardSceneId = RenderValidationSceneId::Custom;
    m_PendingDiscardPath.clear();
}

void RenderTab::RenderDiscardChangesPopup() {
    if (!ImGui::BeginPopupModal("Discard Unsaved Render Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Discard unsaved render-scene changes and continue?");
    ImGui::TextDisabled("This affects the active scene, camera, and render project settings.");
    ImGui::Spacing();

    if (ImGui::Button("Discard Changes", ImVec2(140.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        ExecutePendingDiscardAction();
    }

    ImGui::SameLine();

    if (ImGui::Button("Keep Editing", ImVec2(140.0f, 0.0f))) {
        m_PendingDiscardActionType = PendingDiscardActionType::None;
        m_PendingDiscardSceneId = RenderValidationSceneId::Custom;
        m_PendingDiscardPath.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void RenderTab::MarkProjectDirty() {
    m_HasUnsavedChanges = true;
}

void RenderTab::ClearProjectDirty() {
    m_HasUnsavedChanges = false;
    m_ObservedSceneRevision = m_Scene.GetRevision();
    m_ObservedCameraRevision = m_Camera.GetRevision();
    m_ObservedSettingsRevision = m_Settings.GetRevision();
}

bool RenderTab::BuildLibraryProjectPayload(
    StackBinaryFormat::json& outPayload,
    std::vector<unsigned char>& outBeautyPixels,
    int& outWidth,
    int& outHeight,
    std::string& errorMessage) const {
    errorMessage.clear();
    outBeautyPixels.clear();
    outWidth = 0;
    outHeight = 0;
    outPayload = StackBinaryFormat::json::object();
    outPayload["snapshot"] = RenderSceneSerialization::BuildSnapshotJson(m_Scene, m_Camera);
    outPayload["settings"] = BuildRenderSettingsJson(m_Settings);
    outPayload["latestFinalAssetFileName"] = m_Job.GetLatestFinalAssetFileName();
    return true;
}

bool RenderTab::ApplyLibraryProjectPayload(
    const StackBinaryFormat::json& payload,
    const std::string& projectName,
    const std::string& projectFileName,
    std::string& errorMessage) {
    errorMessage.clear();

    const StackBinaryFormat::json snapshotJson = payload.contains("snapshot")
        ? payload["snapshot"]
        : payload;
    RenderSceneSnapshotDocument document;
    if (!RenderSceneSerialization::ParseSnapshotJson(snapshotJson, document)) {
        errorMessage = "The saved render project payload is invalid.";
        return false;
    }

    m_Scene.ApplySceneSnapshot(
        document.label,
        document.description,
        document.backgroundMode,
        document.environmentEnabled,
        document.environmentIntensity,
        document.fogEnabled,
        document.fogColor,
        document.fogDensity,
        document.fogAnisotropy,
        std::move(document.importedAssets),
        std::move(document.importedTextures),
        std::move(document.materials),
        std::move(document.meshes),
        std::move(document.meshInstances),
        std::move(document.spheres),
        std::move(document.triangles),
        std::move(document.lights),
        "Render project loaded from the Library.");
    m_Camera.ApplySnapshot(
        document.cameraPosition,
        document.cameraYawDegrees,
        document.cameraPitchDegrees,
        document.cameraFieldOfViewDegrees,
        document.cameraFocusDistance,
        document.cameraApertureRadius,
        document.cameraExposure,
        "Render camera loaded from the Library.");
    ApplyRenderSettingsJson(payload.value("settings", StackBinaryFormat::json::object()), m_Settings);
    m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
    EndViewportGizmoDrag();
    m_CurrentProjectName = projectName;
    m_CurrentProjectFileName = projectFileName;
    m_Job.ClearLatestFinalAssetFileName();
    m_FinalBuffers.Release();
    const StackBinaryFormat::json latestAssetJson = payload.value("latestFinalAssetFileName", StackBinaryFormat::json());
    if (latestAssetJson.is_string() && !latestAssetJson.get<std::string>().empty()) {
        m_Job.CompleteFinal(latestAssetJson.get<std::string>());
    }
    AppendConsoleLine("[Library] Loaded render project " + projectName + ".");
    HandleRuntimeChanges();
    ClearProjectDirty();
    return true;
}

bool RenderTab::IsSelectionObjectTransformable() const {
    switch (m_Selection.type) {
    case RenderSelectionType::MeshInstance:
        return m_Scene.FindMeshInstanceIndexById(m_Selection.objectId) >= 0;
    case RenderSelectionType::Sphere:
        return m_Scene.FindSphereIndexById(m_Selection.objectId) >= 0;
    case RenderSelectionType::Triangle:
        return m_Scene.FindTriangleIndexById(m_Selection.objectId) >= 0;
    case RenderSelectionType::Light:
        return m_Scene.FindLightIndexById(m_Selection.objectId) >= 0;
    case RenderSelectionType::Scene:
    case RenderSelectionType::Camera:
    case RenderSelectionType::Material:
        return false;
    }

    return false;
}

bool RenderTab::ResolveSelectionTransform(RenderTransform& transform) const {
    switch (m_Selection.type) {
    case RenderSelectionType::MeshInstance:
    {
        const int index = m_Scene.FindMeshInstanceIndexById(m_Selection.objectId);
        if (!m_Scene.IsMeshInstanceIndexValid(index)) {
            return false;
        }
        transform = m_Scene.GetMeshInstance(index).transform;
        return true;
    }
    case RenderSelectionType::Sphere:
    {
        const int index = m_Scene.FindSphereIndexById(m_Selection.objectId);
        if (!m_Scene.IsSphereIndexValid(index)) {
            return false;
        }
        transform = m_Scene.GetSphere(index).transform;
        return true;
    }
    case RenderSelectionType::Triangle:
    {
        const int index = m_Scene.FindTriangleIndexById(m_Selection.objectId);
        if (!m_Scene.IsTriangleIndexValid(index)) {
            return false;
        }
        transform = m_Scene.GetTriangle(index).transform;
        return true;
    }
    case RenderSelectionType::Light:
    {
        const int index = m_Scene.FindLightIndexById(m_Selection.objectId);
        if (!m_Scene.IsLightIndexValid(index)) {
            return false;
        }
        transform = m_Scene.GetLight(index).transform;
        return true;
    }
    case RenderSelectionType::Scene:
    case RenderSelectionType::Camera:
    case RenderSelectionType::Material:
        break;
    }

    return false;
}

bool RenderTab::UpdateSelectionTransform(const RenderTransform& transform) {
    switch (m_Selection.type) {
    case RenderSelectionType::MeshInstance:
    {
        const int index = m_Scene.FindMeshInstanceIndexById(m_Selection.objectId);
        if (!m_Scene.IsMeshInstanceIndexValid(index)) {
            return false;
        }
        RenderMeshInstance meshInstance = m_Scene.GetMeshInstance(index);
        if (NearlyEqual(meshInstance.transform, transform)) {
            return false;
        }
        meshInstance.transform = transform;
        return m_Scene.UpdateMeshInstance(index, meshInstance);
    }
    case RenderSelectionType::Sphere:
    {
        const int index = m_Scene.FindSphereIndexById(m_Selection.objectId);
        if (!m_Scene.IsSphereIndexValid(index)) {
            return false;
        }
        RenderSphere sphere = m_Scene.GetSphere(index);
        if (NearlyEqual(sphere.transform, transform)) {
            return false;
        }
        sphere.transform = transform;
        return m_Scene.UpdateSphere(index, sphere);
    }
    case RenderSelectionType::Triangle:
    {
        const int index = m_Scene.FindTriangleIndexById(m_Selection.objectId);
        if (!m_Scene.IsTriangleIndexValid(index)) {
            return false;
        }
        RenderTriangle triangle = m_Scene.GetTriangle(index);
        if (NearlyEqual(triangle.transform, transform)) {
            return false;
        }
        triangle.transform = transform;
        return m_Scene.UpdateTriangle(index, triangle);
    }
    case RenderSelectionType::Light:
    {
        const int index = m_Scene.FindLightIndexById(m_Selection.objectId);
        if (!m_Scene.IsLightIndexValid(index)) {
            return false;
        }
        RenderLight light = m_Scene.GetLight(index);
        if (NearlyEqual(light.transform, transform)) {
            return false;
        }
        light.transform = transform;
        return m_Scene.UpdateLight(index, light);
    }
    case RenderSelectionType::Scene:
    case RenderSelectionType::Camera:
    case RenderSelectionType::Material:
        break;
    }

    return false;
}

bool RenderTab::ResolveSelectionBounds(RenderBounds& bounds) const {
    switch (m_Selection.type) {
    case RenderSelectionType::MeshInstance:
    {
        const int index = m_Scene.FindMeshInstanceIndexById(m_Selection.objectId);
        if (!m_Scene.IsMeshInstanceIndexValid(index)) {
            return false;
        }
        const RenderMeshInstance& meshInstance = m_Scene.GetMeshInstance(index);
        if (!m_Scene.IsMeshDefinitionIndexValid(meshInstance.meshIndex)) {
            return false;
        }
        bounds = ComputeBounds(meshInstance, m_Scene.GetMeshDefinition(meshInstance.meshIndex));
        return true;
    }
    case RenderSelectionType::Sphere:
    {
        const int index = m_Scene.FindSphereIndexById(m_Selection.objectId);
        if (!m_Scene.IsSphereIndexValid(index)) {
            return false;
        }
        bounds = ComputeBounds(m_Scene.GetSphere(index));
        return true;
    }
    case RenderSelectionType::Triangle:
    {
        const int index = m_Scene.FindTriangleIndexById(m_Selection.objectId);
        if (!m_Scene.IsTriangleIndexValid(index)) {
            return false;
        }
        bounds = ComputeBounds(m_Scene.GetTriangle(index));
        return true;
    }
    case RenderSelectionType::Light:
    {
        const int index = m_Scene.FindLightIndexById(m_Selection.objectId);
        if (!m_Scene.IsLightIndexValid(index)) {
            return false;
        }
        bounds = ComputeBounds(m_Scene.GetLight(index));
        return true;
    }
    case RenderSelectionType::Scene:
    {
        if (m_Scene.GetBvhNodes().empty()) {
            return false;
        }
        bounds = m_Scene.GetBvhNodes()[0].bounds;
        return true;
    }
    case RenderSelectionType::Camera:
    case RenderSelectionType::Material:
        break;
    }

    return false;
}

bool RenderTab::ResolveSelectionPivot(RenderFloat3& pivot) const {
    RenderTransform transform {};
    if (!ResolveSelectionTransform(transform)) {
        return false;
    }

    pivot = transform.translation;
    return true;
}

bool RenderTab::FocusSelection() {
    RenderBounds bounds {};
    if (!ResolveSelectionBounds(bounds)) {
        return false;
    }

    const RenderFloat3 center = ComputeBoundsCenter(bounds);
    const RenderFloat3 extents = Subtract(bounds.max, bounds.min);
    const float radius = std::max({ extents.x, extents.y, extents.z, 0.5f }) * 0.5f;
    const float focusDistance = std::max(radius * 3.0f, 1.5f);
    const RenderFloat3 newPosition = Subtract(center, Scale(m_Camera.GetForwardVector(), focusDistance));
    bool changed = false;
    changed |= m_Camera.SetPosition(newPosition);
    changed |= m_Camera.SetFocusDistance(focusDistance);
    if (changed) {
        AppendConsoleLine("[Viewport] Focused selection.");
    }
    return changed;
}

bool RenderTab::DuplicateSelection() {
    int newObjectId = -1;
    bool duplicated = false;

    switch (m_Selection.type) {
    case RenderSelectionType::MeshInstance:
        duplicated = m_Scene.DuplicateMeshInstance(m_Selection.objectId, newObjectId);
        break;
    case RenderSelectionType::Sphere:
        duplicated = m_Scene.DuplicateSphere(m_Selection.objectId, newObjectId);
        break;
    case RenderSelectionType::Triangle:
        duplicated = m_Scene.DuplicateTriangle(m_Selection.objectId, newObjectId);
        break;
    case RenderSelectionType::Light:
        duplicated = m_Scene.DuplicateLight(m_Selection.objectId, newObjectId);
        break;
    case RenderSelectionType::Material:
        duplicated = m_Scene.DuplicateMaterial(m_Selection.objectId, newObjectId);
        break;
    case RenderSelectionType::Scene:
    case RenderSelectionType::Camera:
        break;
    }

    if (!duplicated) {
        return false;
    }

    m_Selection.objectId = newObjectId;
    if (!m_Job.IsPreviewRequested()) {
        StartPreview("Live viewport resumed from object duplication.");
    }
    AppendConsoleLine("[Viewport] Selected object duplicated.");
    return true;
}

bool RenderTab::DeleteSelection() {
    bool deleted = false;

    switch (m_Selection.type) {
    case RenderSelectionType::MeshInstance:
        deleted = m_Scene.DeleteMeshInstance(m_Selection.objectId);
        break;
    case RenderSelectionType::Sphere:
        deleted = m_Scene.DeleteSphere(m_Selection.objectId);
        break;
    case RenderSelectionType::Triangle:
        deleted = m_Scene.DeleteTriangle(m_Selection.objectId);
        break;
    case RenderSelectionType::Light:
        deleted = m_Scene.DeleteLight(m_Selection.objectId);
        break;
    case RenderSelectionType::Material:
        deleted = m_Scene.DeleteMaterial(m_Selection.objectId);
        break;
    case RenderSelectionType::Scene:
    case RenderSelectionType::Camera:
        break;
    }

    if (!deleted) {
        return false;
    }

    m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
    EndViewportGizmoDrag();
    if (!m_Job.IsPreviewRequested()) {
        StartPreview("Live viewport resumed from object deletion.");
    }
    AppendConsoleLine("[Viewport] Selected object deleted.");
    return true;
}

void RenderTab::SanitizeSelection() {
    switch (m_Selection.type) {
    case RenderSelectionType::Scene:
    case RenderSelectionType::Camera:
        return;
    case RenderSelectionType::MeshInstance:
        if (m_Scene.FindMeshInstanceIndexById(m_Selection.objectId) < 0) {
            m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
            EndViewportGizmoDrag();
        }
        return;
    case RenderSelectionType::Sphere:
        if (m_Scene.FindSphereIndexById(m_Selection.objectId) < 0) {
            m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
            EndViewportGizmoDrag();
        }
        return;
    case RenderSelectionType::Triangle:
        if (m_Scene.FindTriangleIndexById(m_Selection.objectId) < 0) {
            m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
            EndViewportGizmoDrag();
        }
        return;
    case RenderSelectionType::Light:
        if (m_Scene.FindLightIndexById(m_Selection.objectId) < 0) {
            m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
            EndViewportGizmoDrag();
        }
        return;
    case RenderSelectionType::Material:
        if (!m_Scene.IsMaterialIndexValid(m_Selection.objectId)) {
            m_Selection = RenderSelection { RenderSelectionType::Scene, -1 };
            EndViewportGizmoDrag();
        }
        return;
    }
}

bool RenderTab::CaptureTexturePixels(
    unsigned int texture,
    int width,
    int height,
    std::vector<unsigned char>& pixels,
    std::string& errorMessage) const {
    errorMessage.clear();
    if (texture == 0 || width <= 0 || height <= 0) {
        errorMessage = "No beauty preview is available to export.";
        return false;
    }

    pixels.assign(static_cast<std::size_t>(width * height * 4), 0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool RenderTab::CaptureBeautyPixels(std::vector<unsigned char>& pixels, int& width, int& height, std::string& errorMessage) const {
    width = m_Buffers.GetWidth();
    height = m_Buffers.GetHeight();
    unsigned int texture = m_RasterPreviewRenderer.GetCompositedTexture();
    if (texture == 0) {
        texture = m_Buffers.GetDisplayTexture();
    }
    if (texture == 0 && m_Settings.GetIntegratorMode() == RenderIntegratorMode::RasterPreview) {
        texture = m_RasterPreviewRenderer.GetSceneColorTexture();
    }
    return CaptureTexturePixels(texture, width, height, pixels, errorMessage);
}

void RenderTab::SaveProjectToLibrary() {
    std::vector<unsigned char> beautyPixels;
    int width = 0;
    int height = 0;
    std::string errorMessage;
    if (!CaptureBeautyPixels(beautyPixels, width, height, errorMessage)) {
        AppendConsoleLine("[Library] " + errorMessage);
        return;
    }

    StackBinaryFormat::json payload;
    std::vector<unsigned char> ignoredBeautyPixels;
    int ignoredWidth = 0;
    int ignoredHeight = 0;
    if (!BuildLibraryProjectPayload(payload, ignoredBeautyPixels, ignoredWidth, ignoredHeight, errorMessage)) {
        AppendConsoleLine("[Library] " + errorMessage);
        return;
    }

    const std::string projectName = m_CurrentProjectName.empty()
        ? m_Scene.GetValidationSceneLabel()
        : m_CurrentProjectName;
    const std::uint64_t savedSceneRevision = m_Scene.GetRevision();
    const std::uint64_t savedCameraRevision = m_Camera.GetRevision();
    const std::uint64_t savedSettingsRevision = m_Settings.GetRevision();
    if (m_CurrentProjectFileName.empty()) {
        m_CurrentProjectFileName = BuildSafeRenderProjectFileName(projectName);
    }
    m_CurrentProjectName = projectName;
    payload["latestFinalAssetFileName"] = m_Job.GetLatestFinalAssetFileName();
    LibraryManager::Get().RequestSaveRenderProject(
        projectName,
        payload,
        beautyPixels,
        width,
        height,
        m_CurrentProjectFileName,
        [this, projectName, savedSceneRevision, savedCameraRevision, savedSettingsRevision](bool success, const std::string& savedFileName, const std::string&) {
            if (!success) {
                AppendConsoleLine("[Library] Failed to save render project " + projectName + ".");
                return;
            }

            m_CurrentProjectName = projectName;
            m_CurrentProjectFileName = savedFileName;
            if (m_Scene.GetRevision() == savedSceneRevision &&
                m_Camera.GetRevision() == savedCameraRevision &&
                m_Settings.GetRevision() == savedSettingsRevision) {
                ClearProjectDirty();
            }
            AppendConsoleLine("[Library] Render project saved as " + savedFileName + ".");
        });
    AppendConsoleLine("[Library] Saving render project " + projectName + ".");
}

void RenderTab::SaveProjectAsToLibrary() {
    const std::string projectName = m_CurrentProjectName.empty()
        ? m_Scene.GetValidationSceneLabel()
        : m_CurrentProjectName;
    m_CurrentProjectName = projectName;
    m_CurrentProjectFileName.clear();
    SaveProjectToLibrary();
}

const char* RenderTab::GetIntegratorModeLabel() const {
    switch (m_Settings.GetIntegratorMode()) {
    case RenderIntegratorMode::RasterPreview:
        return "Raster Preview";
    case RenderIntegratorMode::PathTracePreview:
        return "Path Trace Preview";
    case RenderIntegratorMode::DebugPreview:
        return "Debug Preview";
    }

    return "Unknown";
}

const char* RenderTab::GetGizmoModeLabel() const {
    switch (m_Settings.GetGizmoMode()) {
    case RenderGizmoMode::Translate:
        return "Translate";
    case RenderGizmoMode::Rotate:
        return "Rotate";
    case RenderGizmoMode::Scale:
        return "Scale";
    }

    return "Unknown";
}

const char* RenderTab::GetTransformSpaceLabel() const {
    switch (m_Settings.GetTransformSpace()) {
    case RenderTransformSpace::World:
        return "World";
    case RenderTransformSpace::Local:
        return "Local";
    }

    return "Unknown";
}

void RenderTab::AppendConsoleLine(const std::string& line) {
    if (line.empty()) {
        return;
    }

    m_ConsoleLines.push_back(line);
    if (m_ConsoleLines.size() > kMaxConsoleLines) {
        m_ConsoleLines.erase(m_ConsoleLines.begin());
    }
}
