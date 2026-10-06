#pragma once
#include "Editor/UI/GraphFrameTransition.h"

#include <imgui.h>
#include <cstdint>
#include <deque>
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
    GraphFrameTransition& FrameTransition() { return m_FrameTransition; }
    const GraphFrameTransition& FrameTransition() const { return m_FrameTransition; }
    void Render(EditorModule* editor, float revealAlpha = 1.0f, HostMode hostMode = HostMode::DockedPane);
    void ResetSinglePreviewState();

private:
    GraphFrameTransition m_FrameTransition;
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

    struct PixelInspectionDrawCommand {
        unsigned int program = 0;
        unsigned int vertexArray = 0;
        unsigned int texture = 0;
        int fullWidth = 0;
        int fullHeight = 0;
        int tileX = 0;
        int tileY = 0;
        int tileHaloX = 0;
        int tileHaloY = 0;
        bool tiled = false;
        ImVec2 imageMin = ImVec2(0.0f, 0.0f);
        ImVec2 imageMax = ImVec2(0.0f, 0.0f);
        ImVec2 displayPos = ImVec2(0.0f, 0.0f);
        ImVec2 displaySize = ImVec2(0.0f, 0.0f);
        ImVec2 framebufferScale = ImVec2(1.0f, 1.0f);
        float revealAlpha = 1.0f;
    };

    static void DrawPixelInspectionCallback(const ImDrawList* parentList, const ImDrawCmd* command);

    void RenderCompositeMode(
        EditorModule* editor,
        float viewportRevealAlpha,
        float deltaTime,
        bool wallpaperSurfaces,
        const ImVec2& hostAvail,
        const ImVec2& hostScreen,
        ImDrawList* hostDrawList,
        bool inputBlocked);
    void RenderSingleImageMode(
        EditorModule* editor,
        float viewportRevealAlpha,
        float deltaTime,
        bool wallpaperSurfaces,
        const ImVec2& hostAvail,
        const ImVec2& hostScreen,
        ImDrawList* hostDrawList,
        bool inputBlocked);

    float m_ZoomLevel = 1.0f;
    float m_PanX = 0.0f;
    float m_PanY = 0.0f;
    float m_ZoomTarget = 1.0f;
    bool m_ZoomAnimating = false;
    bool m_Panning = false;
    ImVec2 m_ZoomFocusScreen;
    ImVec2 m_ZoomFocusUv;
    bool  m_IsLocked = false;
    bool  m_ShowStaticSingleCompare = false;
    float m_StaticSingleCompareBlend = 0.0f;
    bool  m_StaticCompareRectsInitialized = false;
    ImVec2 m_StaticCompareOutputMin = ImVec2(0.0f, 0.0f);
    ImVec2 m_StaticCompareOutputMax = ImVec2(0.0f, 0.0f);
    ImVec2 m_StaticCompareSourceMin = ImVec2(0.0f, 0.0f);
    ImVec2 m_StaticCompareSourceMax = ImVec2(0.0f, 0.0f);
    unsigned int m_CheckerTex = 0;
    unsigned int m_DetachedToggleTexture = 0;
    bool m_ShowCompositeAssetPicker = false;
    bool m_PendingCompositeAddImageDialog = false;
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
    float m_DetachedToggleHoverAnim = 0.0f;
    float m_DetachedTogglePressAnim = 0.0f;
    float m_ViewportHudAnim = 0.0f;
    float m_CompositeSelectionOutlineAnim = 0.0f;
    float m_CompositeSelectionHandleAnim = 0.0f;
    float m_CompositeHoverOutlineAnim = 0.0f;
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
    std::vector<unsigned char> m_PixelInspectionPixels;
    std::uint64_t m_PixelInspectionTextureSignature = 0;
    int m_PixelInspectionWidth = 0;
    int m_PixelInspectionHeight = 0;
    int m_PixelInspectionOriginX = 0;
    int m_PixelInspectionOriginY = 0;
    unsigned int m_PixelInspectionProgram = 0;
    unsigned int m_PixelInspectionVertexArray = 0;
    std::deque<PixelInspectionDrawCommand> m_PixelInspectionDrawCommands;
};
