#pragma once

#include "PanelRegistry.h"
#include "RenderTabState.h"
#include "Async/TaskState.h"
#include "Panels/RenderAssetBrowserPanel.h"
#include "Panels/RenderAovDebugPanel.h"
#include "Panels/RenderConsolePanel.h"
#include "Panels/RenderInspectorPanel.h"
#include "Panels/RenderManagerPanel.h"
#include "Panels/RenderOutlinerPanel.h"
#include "Panels/RenderSettingsPanel.h"
#include "Panels/RenderStatisticsPanel.h"
#include "Panels/RenderViewportPanel.h"
#include "RenderTab/Runtime/ComputePreviewRenderer.h"
#include "RenderTab/Runtime/RenderBuffers.h"
#include "RenderTab/Runtime/RenderCamera.h"
#include "RenderTab/Runtime/RenderJob.h"
#include "RenderTab/Runtime/RenderRasterPreviewRenderer.h"
#include "RenderTab/Runtime/RenderScene.h"
#include "RenderTab/Runtime/RenderSceneSerialization.h"
#include "RenderTab/Runtime/RenderSettings.h"
#include "Persistence/StackBinaryFormat.h"

#include <imgui.h>
#include <cstdint>
#include <string>
#include <vector>

class RenderTab {
public:
    void Initialize();
    void Shutdown();
    void RenderUI();
    bool BuildLibraryProjectPayload(
        StackBinaryFormat::json& outPayload,
        std::vector<unsigned char>& outBeautyPixels,
        int& outWidth,
        int& outHeight,
        std::string& errorMessage) const;
    bool ApplyLibraryProjectPayload(
        const StackBinaryFormat::json& payload,
        const std::string& projectName,
        const std::string& projectFileName,
        std::string& errorMessage);
    bool HasUnsavedChanges() const { return m_HasUnsavedChanges; }
    const std::string& GetCurrentProjectName() const { return m_CurrentProjectName; }
    const std::string& GetCurrentProjectFileName() const { return m_CurrentProjectFileName; }

private:
    enum class PendingDiscardActionType {
        None = 0,
        CreateEmptyScene,
        LoadValidationTemplate,
        LoadSceneSnapshot
    };

    void RenderMenuBar(ImGuiID dockSpaceId, const ImVec2& dockSpaceSize);
    void RenderToolbar();
    void RenderPanels();
    void RenderPanel(RenderPanelId id);
    void RenderDiscardChangesPopup();
    void HandleViewportNavigation(const RenderViewportPanelResult& result);
    void HandleViewportSelectionAndGizmo(const RenderViewportPanelResult& result);
    void HandleViewportShortcuts(const RenderViewportPanelResult& result);
    void RenderViewportOverlay(const RenderViewportPanelResult& result) const;
    void EndViewportNavigation(bool appendSummary);
    void EndViewportGizmoDrag();
    void HandleRenderManagerAction(RenderManagerAction action);
    void HandleOutlinerAction(const RenderOutlinerAction& action);
    void HandleAssetBrowserAction(const RenderAssetBrowserAction& action);
    void HandleRuntimeChanges();
    void StartPreview(const std::string& reason);
    void CancelPreview();
    void ResetPreview(const std::string& reason, bool appendConsole = true);
    void TickPreviewJob();
    void BeginGltfImport(const std::string& path);
    void SaveSceneSnapshot(const std::string& path);
    void LoadSceneSnapshot(const std::string& path);
    void StartFinalRender();
    void CancelFinalRender();
    void SanitizeSelection();
    bool IsSelectionObjectTransformable() const;
    bool ResolveSelectionTransform(RenderTransform& transform) const;
    bool UpdateSelectionTransform(const RenderTransform& transform);
    bool ResolveSelectionBounds(RenderBounds& bounds) const;
    bool ResolveSelectionPivot(RenderFloat3& pivot) const;
    bool FocusSelection();
    bool DuplicateSelection();
    bool DeleteSelection();
    bool CaptureTexturePixels(unsigned int texture, int width, int height, std::vector<unsigned char>& pixels, std::string& errorMessage) const;
    bool CaptureBeautyPixels(std::vector<unsigned char>& pixels, int& width, int& height, std::string& errorMessage) const;
    void SaveProjectToLibrary();
    void SaveProjectAsToLibrary();
    bool QueueDiscardAction(PendingDiscardActionType type, RenderValidationSceneId sceneId = RenderValidationSceneId::Custom, const std::string& path = "");
    void ExecutePendingDiscardAction();
    void MarkProjectDirty();
    void ClearProjectDirty();
    const char* GetIntegratorModeLabel() const;
    const char* GetGizmoModeLabel() const;
    const char* GetTransformSpaceLabel() const;
    void AppendConsoleLine(const std::string& line);

    RenderTabState m_State;
    RenderScene m_Scene;
    RenderCamera m_Camera;
    RenderSettings m_Settings;
    RenderBuffers m_Buffers;
    RenderBuffers m_FinalBuffers;
    RenderJob m_Job;
    ComputePreviewRenderer m_PreviewRenderer;
    RenderRasterPreviewRenderer m_RasterPreviewRenderer;
    RenderSelection m_Selection {};
    std::uint64_t m_ObservedSceneRevision = 0;
    std::uint64_t m_ObservedCameraRevision = 0;
    std::uint64_t m_ObservedSettingsRevision = 0;
    bool m_AutoStartPending = true;
    std::string m_ContextVersion;
    std::string m_LastSceneSnapshotStatus;
    std::string m_LastSceneSnapshotPath;
    std::string m_LastImportStatus;
    std::string m_CurrentProjectName;
    std::string m_CurrentProjectFileName;
    bool m_HasUnsavedChanges = false;
    PendingDiscardActionType m_PendingDiscardActionType = PendingDiscardActionType::None;
    RenderValidationSceneId m_PendingDiscardSceneId = RenderValidationSceneId::Custom;
    std::string m_PendingDiscardPath;
    bool m_OpenDiscardPopupPending = false;
    std::uint64_t m_ImportGeneration = 0;
    Async::TaskState m_ImportTaskState = Async::TaskState::Idle;
    std::vector<std::string> m_ConsoleLines;
    bool m_ViewportPanelSeenThisFrame = false;
    bool m_ViewportNavigationActive = false;
    bool m_ViewportNavigationChangedPose = false;
    float m_ViewportNavigationBaseSpeed = 4.0f;
    bool m_ViewportGizmoDragging = false;
    int m_ViewportHoveredAxis = -1;
    int m_ViewportActiveAxis = -1;
    RenderSelection m_ViewportGizmoSelection {};
    RenderTransform m_ViewportGizmoStartTransform {};
    ImVec2 m_ViewportGizmoDragStartMouse {};
    ImVec2 m_ViewportGizmoAxisScreenDirection {};
    float m_ViewportGizmoPixelsPerUnit = 1.0f;
};
