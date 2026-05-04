#pragma once

#include "RenderContracts.h"
#include "SceneCompiler.h"

#include "RenderTab/Foundation/RenderFoundationState.h"

#include <imgui.h>

namespace RenderContracts {

class ViewportController {
public:
    ViewportController() = default;

    void Reset();

    SceneChangeSet HandleInput(
        const ViewportInputFrame& input,
        const SceneSnapshot& snapshot,
        const CompiledScene& compiledScene,
        RenderFoundation::Camera& viewportCamera,
        RenderFoundation::State& state,
        bool& openContextMenu);

    void DrawGizmo(
        ImDrawList* drawList,
        const ViewportInputFrame& input,
        const SceneSnapshot& snapshot,
        const CompiledScene& compiledScene,
        const RenderFoundation::Camera& viewportCamera,
        const RenderFoundation::State& state) const;

    bool TraceFirstPrimitiveHit(
        const CompiledScene& compiledScene,
        const RenderFoundation::Vec3& rayOrigin,
        const RenderFoundation::Vec3& rayDirection,
        RenderFoundation::Vec3& outHitPoint,
        RenderFoundation::Vec3& outHitNormal,
        float& outHitDistance) const;

    bool IsNavigationActive() const { return m_NavigationActive; }

private:
    bool ResolveSelectionTransform(const RenderFoundation::State& state, RenderFoundation::Transform& outTransform) const;
    bool ResolveSelectionPivot(
        const SceneSnapshot& snapshot,
        const CompiledScene& compiledScene,
        const RenderFoundation::State& state,
        RenderFoundation::Vec3& outPivot) const;
    bool UpdateSelectionTransform(RenderFoundation::State& state, const RenderFoundation::Transform& transform) const;
    int PickObjectId(const ViewportInputFrame& input, const CompiledScene& compiledScene) const;
    RenderFoundation::SelectionType ResolvePickedSelectionType(
        int objectId,
        const SceneSnapshot& snapshot,
        const CompiledScene& compiledScene) const;

    bool m_RightMouseCandidate = false;
    bool m_NavigationActive = false;
    float m_RightMouseStartX = 0.0f;
    float m_RightMouseStartY = 0.0f;
    bool m_GizmoDragging = false;
    int m_HoveredAxisMask = 0;
    int m_ActiveAxisMask = 0;
    RenderFoundation::Selection m_GizmoSelection {};
    RenderFoundation::Transform m_GizmoStartTransform {};
    ImVec2 m_GizmoDragStartMouse {};
    ImVec2 m_GizmoAxisScreenDirection {};
    float m_GizmoPixelsPerUnit = 1.0f;
};

} // namespace RenderContracts
