#pragma once

#include <imgui.h>
#include <vector>

class EditorModule;

class EditorViewport {
public:
    EditorViewport();
    ~EditorViewport();

    void Initialize();
    void Render(EditorModule* editor);
    void ResetSinglePreviewState();

private:
    enum class ExportHandleType {
        None,
        Move,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left
    };

    enum class SceneHandleType {
        None,
        Move,
        Rotate,
        ResizeTopLeft,
        ResizeTop,
        ResizeTopRight,
        ResizeRight,
        ResizeBottomRight,
        ResizeBottom,
        ResizeBottomLeft,
        ResizeLeft
    };

    struct SnapGuideLine {
        ImVec2 a;
        ImVec2 b;
        ImU32 color = 0;
    };

    float m_ZoomLevel = 1.0f;
    float m_PanX = 0.0f;
    float m_PanY = 0.0f;
    bool  m_IsLocked = false;
    unsigned int m_CheckerTex = 0;
    bool m_ShowCompositeAssetPicker = false;
    ExportHandleType m_ActiveExportHandle = ExportHandleType::None;
    float m_ExportDragStartX = 0.0f;
    float m_ExportDragStartY = 0.0f;
    float m_ExportDragStartWidth = 0.0f;
    float m_ExportDragStartHeight = 0.0f;
    float m_ExportDragStartMouseWorldX = 0.0f;
    float m_ExportDragStartMouseWorldY = 0.0f;
    SceneHandleType m_ActiveSceneHandle = SceneHandleType::None;
    int m_ActiveSceneOutputNodeId = -1;
    float m_SceneDragStartMouseWorldX = 0.0f;
    float m_SceneDragStartMouseWorldY = 0.0f;
    float m_SceneStartX = 0.0f;
    float m_SceneStartY = 0.0f;
    float m_SceneStartScaleX = 1.0f;
    float m_SceneStartScaleY = 1.0f;
    float m_SceneStartRotation = 0.0f;
    float m_SceneStartWidth = 1.0f;
    float m_SceneStartHeight = 1.0f;
    float m_SceneResizeAnchorX = 0.0f;
    float m_SceneResizeAnchorY = 0.0f;
    float m_SceneStartMouseAngle = 0.0f;
    std::vector<SnapGuideLine> m_CompositeSnapGuides;
};
