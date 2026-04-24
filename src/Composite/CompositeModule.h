#pragma once

#include "Persistence/StackBinaryFormat.h"
#include <array>
#include <string>
#include <vector>

enum class LayerKind {
    Image,
    EditorProject,
    ShapeRect,
    ShapeCircle,
    Text
};

enum class CompositeBlendMode {
    Normal,
    Multiply,
    Screen,
    Add,
    Overlay,
    SoftLight,
    HardLight,
    Hue,
    Color
};

enum class CompositeExportBoundsMode {
    Auto,
    Custom
};

enum class CompositeExportBackgroundMode {
    Transparent,
    Solid
};

enum class CompositeExportAspectPreset {
    Ratio1x1,
    Ratio4x3,
    Ratio3x2,
    Ratio16x9,
    Ratio9x16,
    Ratio2x3,
    Ratio5x4,
    Ratio21x9,
    Custom
};

enum class CompositeSnapModePreset {
    Full,
    ObjectOnly,
    Off,
    Custom
};

struct CompositeExportSettings {
    CompositeExportBoundsMode boundsMode = CompositeExportBoundsMode::Auto;
    CompositeExportBackgroundMode backgroundMode = CompositeExportBackgroundMode::Transparent;
    std::array<float, 4> backgroundColor { 0.08f, 0.08f, 0.08f, 1.0f };
    float customX = -512.0f;
    float customY = -512.0f;
    float customWidth = 1024.0f;
    float customHeight = 1024.0f;
    CompositeExportAspectPreset aspectPreset = CompositeExportAspectPreset::Ratio1x1;
    float customAspectRatio = 1.0f;
    int outputWidth = 1024;
    int outputHeight = 1024;
};

struct CompositeLayer {
    std::string id;
    std::string name;
    LayerKind kind = LayerKind::Image;
    float x = 0.0f;
    float y = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool preserveAspectRatio = false;
    float rotation = 0.0f;
    float opacity = 1.0f;
    int z = 0;
    bool visible = true;
    bool locked = false;
    bool flipX = false;
    bool flipY = false;
    CompositeBlendMode blendMode = CompositeBlendMode::Normal;
    std::array<float, 4> fillColor { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string textContent = "Text";
    float textFontSize = 72.0f;
    float textRenderStretchX = 0.0f;
    float textRenderStretchY = 0.0f;
    int imgW = 0;
    int imgH = 0;
    int logicalW = 0;
    int logicalH = 0;
    std::vector<uint8_t> rgba;
    unsigned int tex = 0;

    // Editor-backed metadata
    std::string embeddedProjectJson;
    std::vector<uint8_t> originalSourcePng;
    std::string linkedProjectFileName;
    std::string linkedProjectName;
    bool generatedFromImage = false;
};

class CompositeModule {
public:
    CompositeModule();
    ~CompositeModule();

    void Initialize();
    void Shutdown();
    void RenderUI();

    void NewProject();

    bool HasLayers() const;
    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }

    const std::string& GetCurrentProjectName() const { return m_ProjectName; }
    const std::string& GetCurrentProjectFileName() const { return m_ProjectFileName; }
    void SetCurrentProjectName(const std::string& name) { m_ProjectName = name; }
    void SetCurrentProjectFileName(const std::string& fileName) { m_ProjectFileName = fileName; }

    bool ApplyLibraryProject(const StackBinaryFormat::ProjectDocument& document);
    bool BuildProjectDocumentForSave(
        const std::string& displayName,
        StackBinaryFormat::ProjectDocument& outDocument) const;

    CompositeLayer* GetSelectedLayer();
    void UpdateLayerData(
        const std::string& layerId,
        const std::string& projectJson,
        const std::vector<uint8_t>& previewPixels,
        int w,
        int h);
    bool ConsumePendingOpenInEditorRequest();

    void AddImageLayerFromFile(const std::string& path);
    void AddProjectLayerFromFile(const std::string& path);

private:
    enum class HandleType {
        None,
        Move,
        Rotate,
        ResizeLeft,
        ResizeRight,
        ResizeTop,
        ResizeBottom,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomRight,
        ResizeBottomLeft
    };

    enum class ExportHandleType {
        None,
        Move,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomRight,
        BottomLeft
    };

    enum class LibraryPickerMode {
        AddProjectLayer,
        ReplaceSelectedWithProject
    };

    void MarkDocumentDirty();
    void MarkStageDirty();

    void ClearLayersGpu();
    void ClearStagePreviewGpu();
    void SyncLayerTextures();
    bool EnsureStageCompositeProgram();
    bool EnsureStagePreviewTargets(int width, int height);
    void RenderStagePreviewTexture(
        const std::vector<const CompositeLayer*>& layers,
        float viewX,
        float viewY,
        float viewWidth,
        float viewHeight,
        int width,
        int height);

    void RenderToolbar();
    void RenderLayerPane();
    void RenderSelectedInspector(CompositeLayer* selectedLayer);
    void RenderViewInspector();
    void RenderExportInspector();
    void RenderStage();
    void RenderLayerContextMenu(CompositeLayer* targetLayer);
    void RenderCanvasContextMenu();
    void RenderLibraryPicker();
    void RenderSavePopup();
    void RenderRenamePopup();
    void ResetWorkspaceLayout(bool markDirty);
    void BuildDefaultWorkspaceLayout(unsigned int dockspaceId);
    void CaptureWorkspaceLayout();
    bool ShouldShowExportBoundsOverlay() const;
    float GetCurrentExportOutputAspectRatio() const;
    void UpdateCustomExportAspectFromBounds();
    void SyncExportResolutionFromWidth();
    void SyncExportResolutionFromHeight();
    bool EnsureDefaultTextFontLoaded();
    bool RegenerateGeneratedLayerTexture(CompositeLayer& layer);
    void RefreshVisibleTextRasterQuality();
    CompositeSnapModePreset GetSnapModePreset() const;
    void ApplySnapModePreset(CompositeSnapModePreset preset);
    void RememberSnapStepDefaults();

    void AddShapeLayer(LayerKind kind);
    void AddTextLayer();
    void TriggerAddImage();
    void TriggerAddProject();
    void TriggerAddFromLibrary();
    void TriggerSaveToLibrary();
    void TriggerExportPng();
    bool ReplaceSelectedLayerWithImageFile(const std::string& path);
    bool ReplaceSelectedLayerWithProjectFile(const std::string& path);
    bool ConvertSelectedLayerToEditorProject();
    bool ExportCurrentPng(const std::string& path) const;
    bool UpdateLinkedProjectFromSelectedLayer();
    bool OpenSelectedLayerInEditor();

    void DuplicateSelectedLayer();
    void RemoveSelectedLayers();
    void BeginRenameSelectedLayer();

    bool BuildExportRaster(
        std::vector<uint8_t>& outRgba,
        int& outW,
        int& outH,
        bool useExportSettings) const;
    static void ResizeNearest(
        const std::vector<uint8_t>& src,
        int sw,
        int sh,
        int dw,
        int dh,
        std::vector<uint8_t>& dst);
    void EncodePng(const std::vector<uint8_t>& rgba, int w, int h, std::vector<uint8_t>& outPng) const;

    std::vector<CompositeLayer> m_Layers;
    std::string m_SelectedId;
    LibraryPickerMode m_LibraryPickerMode = LibraryPickerMode::AddProjectLayer;
    std::string m_LibraryPickerTargetLayerId;
    bool m_ShowLayersWindow = true;
    bool m_ShowSelectedWindow = true;
    bool m_ShowViewWindow = true;
    bool m_ShowExportWindow = true;
    std::string m_WorkspaceLayoutIni;
    unsigned int m_WorkspaceDockId = 0;
    bool m_PendingWorkspaceLayoutLoad = false;
    bool m_PendingWorkspaceLayoutReset = true;
    bool m_SuspendWorkspaceLayoutDirtyTracking = true;
    float m_RightMousePressX = 0.0f;
    float m_RightMousePressY = 0.0f;
    bool m_RightMousePressedOnCanvas = false;
    bool m_MiddleMousePanActive = false;
    bool m_OpenSavePopup = false;

    float m_ViewZoom = 1.0f;
    float m_ViewPanX = 0.0f;
    float m_ViewPanY = 0.0f;
    bool m_ShowChecker = true;

    HandleType m_ActiveHandle = HandleType::None;
    float m_StartScaleX = 1.0f;
    float m_StartScaleY = 1.0f;
    float m_StartRotation = 0.0f;
    float m_StartX = 0.0f;
    float m_StartY = 0.0f;
    float m_StartWidth = 0.0f;
    float m_StartHeight = 0.0f;
    float m_ResizeAnchorX = 0.0f;
    float m_ResizeAnchorY = 0.0f;
    float m_StartMouseAngle = 0.0f;
    float m_StartMouseDist = 1.0f;
    ExportHandleType m_ActiveExportHandle = ExportHandleType::None;
    float m_ExportDragStartX = 0.0f;
    float m_ExportDragStartY = 0.0f;
    float m_ExportDragStartWidth = 0.0f;
    float m_ExportDragStartHeight = 0.0f;
    float m_ExportDragStartMouseWorldX = 0.0f;
    float m_ExportDragStartMouseWorldY = 0.0f;
    bool m_ExportPanelActive = false;

    bool m_SnapEnabled = false;
    bool m_SnapToObjects = true;
    bool m_SnapToCenters = true;
    bool m_SnapToCanvasCenter = true;
    bool m_SnapToSpacing = true;
    bool m_ShowLibraryPicker = false;
    bool m_LimitProjectResolution = true;
    float m_GridSize = 24.0f;
    float m_RotateSnapStep = 15.0f;
    float m_ScaleSnapStep = 0.1f;
    float m_LastNonZeroGridSize = 24.0f;
    float m_LastNonZeroRotateSnapStep = 15.0f;
    float m_LastNonZeroScaleSnapStep = 0.1f;

    float m_CanvasW = 1000.0f;
    float m_CanvasH = 1000.0f;
    CompositeExportSettings m_ExportSettings;

    unsigned int m_StagePreviewTex[2] = { 0, 0 };
    unsigned int m_StagePreviewFbo[2] = { 0, 0 };
    int m_StagePreviewDisplayIndex = 0;
    unsigned int m_StagePreviewProgram = 0;
    unsigned int m_StagePreviewVao = 0;
    int m_StagePreviewTexW = 0;
    int m_StagePreviewTexH = 0;
    bool m_StagePreviewDirty = true;
    float m_LastStagePreviewZoom = -1.0f;
    float m_LastStagePreviewPanX = 0.0f;
    float m_LastStagePreviewPanY = 0.0f;
    int m_LastStagePreviewCanvasW = 0;
    int m_LastStagePreviewCanvasH = 0;

    bool m_PendingOpenInEditorRequest = false;
    bool m_OpenRenamePopup = false;
    std::string m_RenameLayerId;
    char m_RenameBuffer[256] = {};
    std::vector<unsigned char> m_DefaultTextFontBytes;
    bool m_DefaultTextFontLoadAttempted = false;

    std::string m_ProjectName = "Untitled Composite";
    std::string m_ProjectFileName;
    bool m_Dirty = false;
    bool m_Initialized = false;
};
