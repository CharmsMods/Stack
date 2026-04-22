#include "RenderTab/Contracts/ViewportController.h"

#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace RenderContracts {

namespace {

using namespace RenderFoundation;

constexpr float kSelectionRayMinT = 0.001f;
constexpr float kGizmoHoverDistancePixels = 15.0f;
constexpr float kGizmoMinimumHandleWorldLength = 0.5f;
constexpr float kGizmoHandleDistanceFactor = 0.2f;
constexpr float kGizmoScaleSensitivity = 0.35f;
constexpr float kGizmoRotationSensitivity = 0.45f;

struct PickHit {
    SelectionType type = SelectionType::None;
    Id objectId = 0;
    float distance = std::numeric_limits<float>::max();
};

struct GizmoProjection {
    bool valid = false;
    Vec3 pivotWorld {};
    ImVec2 originScreen {};
    float handleWorldLength = 1.0f;
    std::array<Vec3, 3> worldAxes {};
    std::array<ImVec2, 3> axisEndScreen {};
    std::array<float, 3> pixelsPerUnit {};
    std::array<bool, 3> axisVisible {};
};

Vec3 FromRuntime(const RenderFloat3& value) {
    return { value.x, value.y, value.z };
}

Vec3 RotateAroundX(const Vec3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return { point.x, point.y * cosine - point.z * sine, point.y * sine + point.z * cosine };
}

Vec3 RotateAroundY(const Vec3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return { point.x * cosine + point.z * sine, point.y, -point.x * sine + point.z * cosine };
}

Vec3 RotateAroundZ(const Vec3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return { point.x * cosine - point.y * sine, point.x * sine + point.y * cosine, point.z };
}

Vec3 RotateDirection(const Transform& transform, const Vec3& direction) {
    Vec3 result = direction;
    result = RotateAroundX(result, DegreesToRadians(transform.rotationDegrees.x));
    result = RotateAroundY(result, DegreesToRadians(transform.rotationDegrees.y));
    result = RotateAroundZ(result, DegreesToRadians(transform.rotationDegrees.z));
    return Normalize(result);
}

Vec3 GetAxisVector(int axis) {
    switch (axis) {
        case 1:
            return { 0.0f, 1.0f, 0.0f };
        case 2:
            return { 0.0f, 0.0f, 1.0f };
        case 0:
        default:
            return { 1.0f, 0.0f, 0.0f };
    }
}

Vec3 GetTransformAxisVector(const Transform& transform, int axis, TransformSpace space) {
    const Vec3 axisVector = GetAxisVector(axis);
    if (space == TransformSpace::World) {
        return axisVector;
    }
    return RotateDirection(transform, axisVector);
}

bool ProjectWorldToScreen(
    const Camera& camera,
    const ViewportInputFrame& input,
    const Vec3& worldPoint,
    ImVec2& screenPoint,
    float* cameraDepth = nullptr) {
    const float width = input.viewportRect.GetWidth();
    const float height = input.viewportRect.GetHeight();
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    const Vec3 forward = ForwardFromYawPitch(camera.yawDegrees, camera.pitchDegrees);
    const Vec3 right = RightFromForward(forward);
    const Vec3 up = UpFromBasis(right, forward);
    const Vec3 relative = worldPoint - camera.position;
    const float cameraX = Dot(relative, right);
    const float cameraY = Dot(relative, up);
    const float cameraZ = Dot(relative, forward);
    if (cameraDepth != nullptr) {
        *cameraDepth = cameraZ;
    }
    if (cameraZ <= 0.001f) {
        return false;
    }

    const float aspect = width / height;
    const float tanHalfFov = std::tan(DegreesToRadians(camera.fieldOfViewDegrees) * 0.5f);
    const float ndcX = cameraX / (cameraZ * tanHalfFov * aspect);
    const float ndcY = cameraY / (cameraZ * tanHalfFov);
    if (std::fabs(ndcX) > 1.5f || std::fabs(ndcY) > 1.5f) {
        return false;
    }

    screenPoint.x = input.viewportRect.Min.x + (ndcX * 0.5f + 0.5f) * width;
    screenPoint.y = input.viewportRect.Min.y + (-ndcY * 0.5f + 0.5f) * height;
    return true;
}

Vec3 BuildCameraRayDirection(const Camera& camera, const ViewportInputFrame& input) {
    const float width = input.viewportRect.GetWidth();
    const float height = input.viewportRect.GetHeight();
    if (width <= 0.0f || height <= 0.0f) {
        return ForwardFromYawPitch(camera.yawDegrees, camera.pitchDegrees);
    }

    const float normalizedX = (input.mousePosition.x - input.viewportRect.Min.x) / width;
    const float normalizedY = (input.mousePosition.y - input.viewportRect.Min.y) / height;
    const float ndcX = normalizedX * 2.0f - 1.0f;
    const float ndcY = 1.0f - normalizedY * 2.0f;
    const float aspect = width / height;
    const float tanHalfFov = std::tan(DegreesToRadians(camera.fieldOfViewDegrees) * 0.5f);
    const Vec3 forward = ForwardFromYawPitch(camera.yawDegrees, camera.pitchDegrees);
    const Vec3 right = RightFromForward(forward);
    const Vec3 up = UpFromBasis(right, forward);

    Vec3 rayDirection = forward;
    rayDirection = rayDirection + right * (ndcX * tanHalfFov * aspect);
    rayDirection = rayDirection + up * (ndcY * tanHalfFov);
    return Normalize(rayDirection);
}

bool IntersectSphere(const Vec3& rayOrigin, const Vec3& rayDirection, const RenderResolvedSphere& sphere, float& hitDistance) {
    const Vec3 center = FromRuntime(sphere.center);
    const Vec3 oc = rayOrigin - center;
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

bool IntersectTriangle(const Vec3& rayOrigin, const Vec3& rayDirection, const RenderResolvedTriangle& triangle, float& hitDistance) {
    const Vec3 a = FromRuntime(triangle.a);
    const Vec3 b = FromRuntime(triangle.b);
    const Vec3 c = FromRuntime(triangle.c);
    const Vec3 edge1 = b - a;
    const Vec3 edge2 = c - a;
    const Vec3 p = Cross(rayDirection, edge2);
    const float determinant = Dot(edge1, p);
    if (std::fabs(determinant) < 0.000001f) {
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;
    const Vec3 t = rayOrigin - a;
    const float u = Dot(t, p) * inverseDeterminant;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const Vec3 q = Cross(t, edge1);
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

bool IntersectBounds(const Vec3& rayOrigin, const Vec3& rayDirection, const RenderBounds& bounds, float& nearHit) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    const float origin[3] = { rayOrigin.x, rayOrigin.y, rayOrigin.z };
    const float direction[3] = { rayDirection.x, rayDirection.y, rayDirection.z };
    const float minValue[3] = { bounds.min.x, bounds.min.y, bounds.min.z };
    const float maxValue[3] = { bounds.max.x, bounds.max.y, bounds.max.z };

    for (int axis = 0; axis < 3; ++axis) {
        const float axisDirection = direction[axis];
        if (std::fabs(axisDirection) < 0.000001f) {
            if (origin[axis] < minValue[axis] || origin[axis] > maxValue[axis]) {
                return false;
            }
            continue;
        }

        const float inverseDirection = 1.0f / axisDirection;
        float t0 = (minValue[axis] - origin[axis]) * inverseDirection;
        float t1 = (maxValue[axis] - origin[axis]) * inverseDirection;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) {
            return false;
        }
    }

    nearHit = tMin;
    return tMax > kSelectionRayMinT;
}

bool IntersectSelectionBounds(const Vec3& rayOrigin, const Vec3& rayDirection, const RenderBounds& bounds, float& hitDistance) {
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
    const Camera& camera,
    const ViewportInputFrame& input,
    const Transform& transform,
    const Vec3& pivotWorld,
    TransformSpace transformSpace,
    GizmoProjection& projection) {
    float cameraDepth = 0.0f;
    if (!ProjectWorldToScreen(camera, input, pivotWorld, projection.originScreen, &cameraDepth)) {
        return false;
    }

    projection.valid = true;
    projection.pivotWorld = pivotWorld;
    projection.handleWorldLength = std::max(kGizmoMinimumHandleWorldLength, cameraDepth * kGizmoHandleDistanceFactor);

    for (int axis = 0; axis < 3; ++axis) {
        projection.worldAxes[axis] = GetTransformAxisVector(transform, axis, transformSpace);
        const Vec3 axisEndWorld = pivotWorld + projection.worldAxes[axis] * projection.handleWorldLength;
        ImVec2 axisEndScreen {};
        float axisDepth = 0.0f;
        projection.axisVisible[axis] = ProjectWorldToScreen(camera, input, axisEndWorld, axisEndScreen, &axisDepth);
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

} // namespace

void ViewportController::Reset() {
    m_RightMouseCandidate = false;
    m_NavigationActive = false;
    m_RightMouseStartX = 0.0f;
    m_RightMouseStartY = 0.0f;
    m_GizmoDragging = false;
    m_HoveredAxis = -1;
    m_ActiveAxis = -1;
    m_GizmoSelection = {};
    m_GizmoStartTransform = {};
    m_GizmoDragStartMouse = ImVec2(0.0f, 0.0f);
    m_GizmoAxisScreenDirection = ImVec2(0.0f, 0.0f);
    m_GizmoPixelsPerUnit = 1.0f;
}

SceneChangeSet ViewportController::HandleInput(
    const ViewportInputFrame& input,
    const SceneSnapshot& snapshot,
    const CompiledScene& compiledScene,
    RenderFoundation::State& state,
    bool& openContextMenu) {

    openContextMenu = false;
    SceneChangeSet changeSet;
    if (!compiledScene.valid) {
        return changeSet;
    }

    const bool wantsKeyboard = input.keyboardAvailable;
    if (wantsKeyboard && !m_NavigationActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            state.GetSettings().transformMode = TransformMode::Translate;
            changeSet = { DirtyFlags::Display | DirtyFlags::Settings, ResetClass::DisplayOnly, "Transform mode set to translate." };
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            state.GetSettings().transformMode = TransformMode::Rotate;
            changeSet = { DirtyFlags::Display | DirtyFlags::Settings, ResetClass::DisplayOnly, "Transform mode set to rotate." };
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            state.GetSettings().transformMode = TransformMode::Scale;
            changeSet = { DirtyFlags::Display | DirtyFlags::Settings, ResetClass::DisplayOnly, "Transform mode set to scale." };
        }
    }

    if (wantsKeyboard && ImGui::IsKeyPressed(ImGuiKey_T)) {
        state.GetSettings().transformSpace =
            state.GetSettings().transformSpace == TransformSpace::Local
                ? TransformSpace::World
                : TransformSpace::Local;
        changeSet = { DirtyFlags::Display | DirtyFlags::Settings, ResetClass::DisplayOnly, "Transform space toggled." };
    }

    if (input.viewportHovered && input.rightClicked) {
        m_RightMouseCandidate = true;
        m_NavigationActive = false;
        m_RightMouseStartX = input.mousePosition.x;
        m_RightMouseStartY = input.mousePosition.y;
    }

    if (m_RightMouseCandidate && input.rightDown) {
        const float dx = input.mousePosition.x - m_RightMouseStartX;
        const float dy = input.mousePosition.y - m_RightMouseStartY;
        if ((dx * dx + dy * dy) > 9.0f) {
            m_NavigationActive = true;
        }
    }

    if (m_NavigationActive && input.rightDown) {
        Camera& camera = state.GetCamera();
        camera.yawDegrees += input.mouseDelta.x * 0.18f;
        camera.pitchDegrees = ClampFloat(camera.pitchDegrees - input.mouseDelta.y * 0.18f, -89.0f, 89.0f);

        const Vec3 forward = ForwardFromYawPitch(camera.yawDegrees, camera.pitchDegrees);
        const Vec3 right = RightFromForward(forward);
        const Vec3 up = UpFromBasis(right, forward);
        float speed = camera.movementSpeed * std::max(input.deltaTime, 0.0001f);
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
            speed *= 2.5f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
            speed *= 0.35f;
        }

        bool moved = false;
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            camera.position = camera.position + forward * speed;
            moved = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            camera.position = camera.position - forward * speed;
            moved = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            camera.position = camera.position - right * speed;
            moved = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            camera.position = camera.position + right * speed;
            moved = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            camera.position = camera.position - up * speed;
            moved = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            camera.position = camera.position + up * speed;
            moved = true;
        }

        return {
            DirtyFlags::Camera | DirtyFlags::Viewport,
            ResetClass::FullAccumulation,
            moved ? "Camera navigated." : "Camera rotated."
        };
    }

    if (m_RightMouseCandidate && input.rightReleased) {
        if (!m_NavigationActive && input.viewportHovered) {
            openContextMenu = true;
        }
        m_RightMouseCandidate = false;
        m_NavigationActive = false;
    }

    Transform selectionTransform {};
    Vec3 selectionPivot {};
    GizmoProjection projection {};
    const bool hasSelectionTransform =
        ResolveSelectionTransform(state, selectionTransform) &&
        ResolveSelectionPivot(snapshot, compiledScene, state, selectionPivot) &&
        BuildGizmoProjection(state.GetCamera(), input, selectionTransform, selectionPivot, state.GetSettings().transformSpace, projection);

    m_HoveredAxis = hasSelectionTransform ? FindHoveredAxis(projection, input.mousePosition) : -1;
    if (!hasSelectionTransform) {
        m_GizmoDragging = false;
        m_ActiveAxis = -1;
    }

    if (m_GizmoDragging) {
        const bool selectionStable =
            state.GetSelection().type == m_GizmoSelection.type &&
            state.GetSelection().id == m_GizmoSelection.id &&
            m_ActiveAxis >= 0;
        if (!selectionStable || !input.leftDown) {
            m_GizmoDragging = false;
            m_ActiveAxis = -1;
            return { DirtyFlags::Display, ResetClass::DisplayOnly, "Viewport gizmo drag completed." };
        }

        Transform updatedTransform = m_GizmoStartTransform;
        const ImVec2 mouseDelta(
            input.mousePosition.x - m_GizmoDragStartMouse.x,
            input.mousePosition.y - m_GizmoDragStartMouse.y);
        const float dragProjection =
            mouseDelta.x * m_GizmoAxisScreenDirection.x +
            mouseDelta.y * m_GizmoAxisScreenDirection.y;
        const float worldUnits = dragProjection / std::max(m_GizmoPixelsPerUnit, 1.0f);

        switch (state.GetSettings().transformMode) {
            case TransformMode::Translate:
                updatedTransform.translation =
                    m_GizmoStartTransform.translation +
                    GetTransformAxisVector(m_GizmoStartTransform, m_ActiveAxis, state.GetSettings().transformSpace) * worldUnits;
                break;
            case TransformMode::Rotate: {
                const ImVec2 tangent(-m_GizmoAxisScreenDirection.y, m_GizmoAxisScreenDirection.x);
                const float angleDelta =
                    (mouseDelta.x * tangent.x + mouseDelta.y * tangent.y) * kGizmoRotationSensitivity;
                if (m_ActiveAxis == 0) {
                    updatedTransform.rotationDegrees.x += angleDelta;
                } else if (m_ActiveAxis == 1) {
                    updatedTransform.rotationDegrees.y += angleDelta;
                } else {
                    updatedTransform.rotationDegrees.z += angleDelta;
                }
                break;
            }
            case TransformMode::Scale: {
                const float scaleDelta = worldUnits * kGizmoScaleSensitivity;
                if (state.GetSettings().transformSpace == TransformSpace::World) {
                    updatedTransform.scale = updatedTransform.scale + Vec3 { scaleDelta, scaleDelta, scaleDelta };
                } else if (m_ActiveAxis == 0) {
                    updatedTransform.scale.x += scaleDelta;
                } else if (m_ActiveAxis == 1) {
                    updatedTransform.scale.y += scaleDelta;
                } else {
                    updatedTransform.scale.z += scaleDelta;
                }
                updatedTransform.scale.x = std::max(0.05f, updatedTransform.scale.x);
                updatedTransform.scale.y = std::max(0.05f, updatedTransform.scale.y);
                updatedTransform.scale.z = std::max(0.05f, updatedTransform.scale.z);
                break;
            }
        }

        if (UpdateSelectionTransform(state, updatedTransform)) {
            return {
                DirtyFlags::SceneContent | DirtyFlags::Viewport,
                ResetClass::FullAccumulation,
                "Viewport gizmo updated the selected transform."
            };
        }
        return {};
    }

    if (hasSelectionTransform && input.viewportHovered && input.leftClicked && m_HoveredAxis >= 0) {
        const ImVec2 axisDelta(
            projection.axisEndScreen[static_cast<std::size_t>(m_HoveredAxis)].x - projection.originScreen.x,
            projection.axisEndScreen[static_cast<std::size_t>(m_HoveredAxis)].y - projection.originScreen.y);
        const float axisLength = std::sqrt(axisDelta.x * axisDelta.x + axisDelta.y * axisDelta.y);
        if (axisLength > 1.0f) {
            m_GizmoDragging = true;
            m_ActiveAxis = m_HoveredAxis;
            m_GizmoSelection = state.GetSelection();
            m_GizmoStartTransform = selectionTransform;
            m_GizmoDragStartMouse = input.mousePosition;
            m_GizmoAxisScreenDirection = ImVec2(axisDelta.x / axisLength, axisDelta.y / axisLength);
            m_GizmoPixelsPerUnit = projection.pixelsPerUnit[static_cast<std::size_t>(m_HoveredAxis)];
            return {};
        }
    }

    if (input.viewportHovered && input.leftClicked) {
        const int pickedObjectId = PickObjectId(input, compiledScene);
        if (pickedObjectId <= 0) {
            state.SelectNone();
            return { DirtyFlags::Display, ResetClass::DisplayOnly, "Viewport selection cleared." };
        }

        const SelectionType pickedType = ResolvePickedSelectionType(pickedObjectId, snapshot, compiledScene);
        if (pickedType == SelectionType::Primitive) {
            state.SelectPrimitive(static_cast<Id>(pickedObjectId));
            return { DirtyFlags::Display, ResetClass::DisplayOnly, "Viewport primitive selected." };
        }
        if (pickedType == SelectionType::Light) {
            state.SelectLight(static_cast<Id>(pickedObjectId));
            return { DirtyFlags::Display, ResetClass::DisplayOnly, "Viewport light selected." };
        }
    }

    return changeSet;
}

void ViewportController::DrawGizmo(
    ImDrawList* drawList,
    const ViewportInputFrame& input,
    const SceneSnapshot& snapshot,
    const CompiledScene& compiledScene,
    const RenderFoundation::State& state) const {

    if (!compiledScene.valid) {
        return;
    }

    Transform selectionTransform {};
    Vec3 selectionPivot {};
    GizmoProjection projection {};
    if (!ResolveSelectionTransform(state, selectionTransform) ||
        !ResolveSelectionPivot(snapshot, compiledScene, state, selectionPivot) ||
        !BuildGizmoProjection(state.GetCamera(), input, selectionTransform, selectionPivot, state.GetSettings().transformSpace, projection)) {
        return;
    }

    for (int axis = 0; axis < 3; ++axis) {
        if (!projection.axisVisible[axis]) {
            continue;
        }

        const bool emphasized = axis == m_HoveredAxis || axis == m_ActiveAxis;
        const float thickness =
            state.GetSettings().transformMode == TransformMode::Rotate ? 4.5f :
            state.GetSettings().transformMode == TransformMode::Scale ? 5.0f :
            3.0f;
        drawList->AddLine(
            projection.originScreen,
            projection.axisEndScreen[axis],
            GetAxisColor(axis, emphasized),
            emphasized ? thickness + 1.0f : thickness);
        drawList->AddCircleFilled(projection.axisEndScreen[axis], emphasized ? 5.5f : 4.5f, GetAxisColor(axis, true));
    }

    drawList->AddCircleFilled(projection.originScreen, 5.0f, IM_COL32(235, 235, 235, 235));
}

bool ViewportController::ResolveSelectionTransform(const RenderFoundation::State& state, RenderFoundation::Transform& outTransform) const {
    const Selection selection = state.GetSelection();
    if (selection.type == SelectionType::Primitive) {
        const Primitive* primitive = state.FindPrimitive(selection.id);
        if (primitive == nullptr) {
            return false;
        }
        outTransform = primitive->transform;
        return true;
    }
    if (selection.type == SelectionType::Light) {
        const Light* light = state.FindLight(selection.id);
        if (light == nullptr) {
            return false;
        }
        outTransform = light->transform;
        return true;
    }
    return false;
}

bool ViewportController::ResolveSelectionPivot(
    const SceneSnapshot& snapshot,
    const CompiledScene& compiledScene,
    const RenderFoundation::State& state,
    RenderFoundation::Vec3& outPivot) const {

    const Selection selection = state.GetSelection();
    if (selection.type == SelectionType::Primitive) {
        const Primitive* primitive = state.FindPrimitive(selection.id);
        if (primitive != nullptr) {
            outPivot = primitive->transform.translation;
            return true;
        }
        return false;
    }
    if (selection.type == SelectionType::Light) {
        const Light* light = state.FindLight(selection.id);
        if (light != nullptr) {
            outPivot = light->transform.translation;
            return true;
        }
        return false;
    }
    (void)snapshot;
    (void)compiledScene;
    return false;
}

bool ViewportController::UpdateSelectionTransform(RenderFoundation::State& state, const RenderFoundation::Transform& transform) const {
    const Selection selection = state.GetSelection();
    if (selection.type == SelectionType::Primitive) {
        Primitive* primitive = state.FindPrimitive(selection.id);
        if (primitive == nullptr) {
            return false;
        }
        primitive->transform = transform;
        return true;
    }
    if (selection.type == SelectionType::Light) {
        Light* light = state.FindLight(selection.id);
        if (light == nullptr) {
            return false;
        }
        light->transform = transform;
        return true;
    }
    return false;
}

int ViewportController::PickObjectId(const ViewportInputFrame& input, const CompiledScene& compiledScene) const {
    if (!compiledScene.valid || !input.mouseAvailable) {
        return 0;
    }

    const Vec3 rayOrigin {
        compiledScene.camera.GetPosition().x,
        compiledScene.camera.GetPosition().y,
        compiledScene.camera.GetPosition().z
    };
    Camera selectionCamera {};
    selectionCamera.position = rayOrigin;
    selectionCamera.yawDegrees = compiledScene.camera.GetYawDegrees();
    selectionCamera.pitchDegrees = compiledScene.camera.GetPitchDegrees();
    selectionCamera.fieldOfViewDegrees = compiledScene.camera.GetFieldOfViewDegrees();
    selectionCamera.focusDistance = compiledScene.camera.GetFocusDistance();
    selectionCamera.apertureRadius = compiledScene.camera.GetApertureRadius();
    selectionCamera.exposure = compiledScene.camera.GetExposure();
    const Vec3 rayDirection = BuildCameraRayDirection(selectionCamera, input);

    PickHit bestHit {};
    const std::vector<RenderBvhNode>& nodes = compiledScene.scene.GetBvhNodes();
    const std::vector<RenderPrimitiveRef>& refs = compiledScene.scene.GetPrimitiveRefs();
    if (!nodes.empty()) {
        std::vector<int> stack;
        stack.reserve(nodes.size());
        stack.push_back(0);
        while (!stack.empty()) {
            const RenderBvhNode& node = nodes[static_cast<std::size_t>(stack.back())];
            stack.pop_back();
            float boundsHit = 0.0f;
            if (!IntersectBounds(rayOrigin, rayDirection, node.bounds, boundsHit) || boundsHit > bestHit.distance) {
                continue;
            }

            if (node.IsLeaf()) {
                for (int i = 0; i < node.primitiveCount; ++i) {
                    const RenderPrimitiveRef& ref = refs[static_cast<std::size_t>(node.firstPrimitive + i)];
                    float hitDistance = 0.0f;
                    if (ref.type == RenderPrimitiveType::Sphere) {
                        const RenderResolvedSphere sphere = ResolveSphere(compiledScene.scene.GetSphere(ref.index));
                        if (!IntersectSphere(rayOrigin, rayDirection, sphere, hitDistance) || hitDistance >= bestHit.distance) {
                            continue;
                        }
                        bestHit.type = SelectionType::Primitive;
                        bestHit.objectId = static_cast<Id>(sphere.objectId);
                        bestHit.distance = hitDistance;
                    } else {
                        const RenderResolvedTriangle& triangle = compiledScene.scene.GetResolvedTriangle(ref.index);
                        if (!IntersectTriangle(rayOrigin, rayDirection, triangle, hitDistance) || hitDistance >= bestHit.distance) {
                            continue;
                        }
                        bestHit.type = SelectionType::Primitive;
                        bestHit.objectId = static_cast<Id>(triangle.meshInstanceId > 0 ? triangle.meshInstanceId : triangle.triangleId);
                        bestHit.distance = hitDistance;
                    }
                }
                continue;
            }

            if (node.leftChild >= 0) {
                stack.push_back(node.leftChild);
            }
            if (node.rightChild >= 0) {
                stack.push_back(node.rightChild);
            }
        }
    }

    for (int lightIndex = 0; lightIndex < compiledScene.scene.GetLightCount(); ++lightIndex) {
        float hitDistance = 0.0f;
        const RenderLight& light = compiledScene.scene.GetLight(lightIndex);
        if (!IntersectSelectionBounds(rayOrigin, rayDirection, ComputeBounds(light), hitDistance) || hitDistance >= bestHit.distance) {
            continue;
        }

        bestHit.type = SelectionType::Light;
        bestHit.objectId = static_cast<Id>(light.id);
        bestHit.distance = hitDistance;
    }

    return static_cast<int>(bestHit.objectId);
}

RenderFoundation::SelectionType ViewportController::ResolvePickedSelectionType(
    int objectId,
    const SceneSnapshot& snapshot,
    const CompiledScene& compiledScene) const {
    (void)compiledScene;
    for (const Primitive& primitive : snapshot.primitives) {
        if (static_cast<int>(primitive.id) == objectId) {
            return SelectionType::Primitive;
        }
    }
    for (const Light& light : snapshot.lights) {
        if (static_cast<int>(light.id) == objectId) {
            return SelectionType::Light;
        }
    }
    return SelectionType::None;
}

} // namespace RenderContracts
