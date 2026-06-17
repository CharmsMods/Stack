#pragma once

#include <imgui.h>
#include <vector>

class EditorModule;

class EditorViewport {
public:
    enum class HostMode {
        DockedPane,
        DetachedFullscreen
    };

    EditorViewport();
    ~EditorViewport();

    void Initialize();
    void Render(EditorModule* editor, float revealAlpha = 1.0f, HostMode hostMode = HostMode::DockedPane);
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

    enum class DevelopSubjectRegionHandle {
        None,
        Move,
        Resize
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
    bool  m_ShowStaticSingleCompare = false;
    float m_StaticSingleCompareBlend = 0.0f;
    bool  m_StaticCompareRectsInitialized = false;
    ImVec2 m_StaticCompareOutputMin = ImVec2(0.0f, 0.0f);
    ImVec2 m_StaticCompareOutputMax = ImVec2(0.0f, 0.0f);
    ImVec2 m_StaticCompareSourceMin = ImVec2(0.0f, 0.0f);
    ImVec2 m_StaticCompareSourceMax = ImVec2(0.0f, 0.0f);
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
    DevelopSubjectRegionHandle m_ActiveDevelopSubjectHandle = DevelopSubjectRegionHandle::None;
    int m_ActiveDevelopSubjectNodeId = -1;
    int m_ActiveDevelopSubjectRegionId = -1;
    int m_ActiveDevelopSubjectStrokeId = -1;
    bool m_DevelopSubjectBrushStrokeActive = false;
    float m_DevelopSubjectDragStartU = 0.0f;
    float m_DevelopSubjectDragStartV = 0.0f;
    float m_DevelopSubjectStartCenterX = 0.5f;
    float m_DevelopSubjectStartCenterY = 0.5f;
    float m_DevelopSubjectStartRadiusX = 0.18f;
    float m_DevelopSubjectStartRadiusY = 0.18f;
};
