#pragma once

#include "RenderTab/Contracts/RenderDelegator.h"
#include "RenderTab/Contracts/SceneCompiler.h"
#include "RenderTab/Contracts/ViewportController.h"
#include "RenderTab/Foundation/RenderFoundationState.h"
#include "RenderTab/Runtime/Debug/ValidationScenes.h"
#include "RenderTab/Runtime/Geometry/RenderMath.h"
#include "RenderTab/Runtime/RenderSceneTypes.h"
#include "Persistence/StackBinaryFormat.h"
#include "Async/TaskState.h"

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

    bool HasUnsavedChanges() const;
    const std::string& GetCurrentProjectName() const;
    const std::string& GetCurrentProjectFileName() const;

private:
    enum class VirtualSelection {
        None,
        Fog,
        Environment
    };

    void RenderMenuBar();
    void RenderBody();
    void RenderInspectorPanel();
    void RenderViewportPanel();
    void RenderOutlinerPanel();
    void RenderCameraWindow();
    void RenderFinalRenderSettingsWindow();
    void RenderFinalRenderWindow();
    void RenderTestScenesWindow();
    void RenderDiagnosticsWindow();
    void RenderResetScenePopup();
    void RenderImportModelPopup();
    void RequestResetRenderLayout();
    void BeginImportModelFlow(const std::string& filePath);
    void SubmitPendingModelImport();
    bool EnsureViewportSceneCompilation();
    bool EnsureCameraPreviewSceneCompilation();
    bool SyncCompiledFinalRenderScene(std::string& errorMessage);
    void ApplySceneChange(const RenderContracts::SceneChangeSet& changeSet);
    void EnableTestScenePreview(RenderValidationSceneId id);
    void DisableTestScenePreview();
    void ResetTestScenePreviewTemplate();
    void ResetTestScenePreviewAccumulation();
    void ResetAuxiliaryAccumulation();
    void SyncAuxiliaryAccumulationEpochs();
    void ResetEditorCameraToDefault();
    void ResetEditorCameraFromRenderCamera();
    RenderFoundation::Settings BuildCameraPreviewSettings() const;
    RenderFoundation::Settings BuildFinalRenderSettings() const;
    bool StartFinalRenderSession();
    void CancelFinalRenderSession();
    bool CompleteFinalRenderSession(std::string& errorMessage);
    int GetSelectedRuntimeObjectId() const;
    bool TryBuildImportedPrimitiveBounds(
        const RenderFoundation::Primitive& primitive,
        RenderBounds& outLocalBounds,
        RenderBounds& outWorldBounds) const;
    void FrameEditorCameraToBounds(const RenderBounds& bounds);
    bool IsViewportProxyActive() const;
    void DrawViewportProxyPreview(ImDrawList* drawList, const ImRect& imageRect) const;
    void InvalidateCompiledSceneTargets();

    bool RequestSaveProject();
    void SelectNone();
    void SelectPrimitive(RenderFoundation::Id id);
    void SelectLight(RenderFoundation::Id id);
    void SelectCamera();
    void SelectVirtual(VirtualSelection selection);
    bool HasVirtualSelection() const;

private:
    RenderFoundation::State m_State;
    RenderContracts::SceneCompiler m_SceneCompiler;
    mutable RenderContracts::RenderDelegator m_RenderDelegator;
    RenderContracts::ViewportController m_ViewportController;
    RenderContracts::SceneSnapshot m_ActiveSnapshot {};
    RenderContracts::CompiledScene m_CompiledScene {};
    RenderContracts::CompiledScene m_CameraPreviewScene {};
    RenderContracts::CompiledScene m_FinalRenderScene {};
    RenderContracts::AccumulationManager m_CameraAccumulationManager {};
    RenderContracts::AccumulationManager m_FinalRenderAccumulationManager {};
    RenderFoundation::Camera m_EditorCamera {};
    struct ImportDialogState {
        bool open = false;
        std::string filePath;
        RenderFoundation::ImportedModelOptions options {};
    } m_ImportDialog {};
    struct ImportTaskState {
        Async::TaskState state = Async::TaskState::Idle;
        std::uint64_t generation = 0;
        std::string filePath;
        RenderFoundation::ImportedModelOptions options {};
    } m_ImportTask {};
    struct CompileTaskState {
        Async::TaskState state = Async::TaskState::Idle;
        std::uint64_t generation = 0;
        std::uint64_t requestedSceneRevision = 0;
        std::uint64_t appliedSceneRevision = 0;
        double lastCompileMs = 0.0;
        std::string lastError;
    } m_ViewportCompile {}, m_CameraPreviewCompile {};
    float m_InspectorWidth = 320.0f;
    float m_SceneWidth = 300.0f;
    unsigned int m_RenderDockId = 0;
    bool m_RenderDockLayoutBuilt = false;
    bool m_ResetRenderDockLayout = false;
    bool m_OpenResetSceneConfirm = false;
    bool m_ShowCameraWindow = false;
    bool m_ShowFinalRenderSettingsWindow = false;
    bool m_ShowTestScenesWindow = false;
    bool m_ShowDiagnosticsWindow = false;
    bool m_TestScenePreviewEnabled = false;
    VirtualSelection m_VirtualSelection = VirtualSelection::None;
    RenderValidationSceneId m_TestScenePreviewId = RenderValidationSceneId::CornellBox;
    RenderValidationSceneTemplate m_TestScenePreviewTemplate {};
    std::uint64_t m_TestScenePreviewRevision = 1;
    std::uint64_t m_LastAuxiliaryTransportEpoch = 0;
    struct FinalRenderSessionState {
        bool active = false;
        bool exportStarted = false;
        bool completed = false;
        std::string outputPath;
        std::string statusText;
    } m_FinalRenderSession {};
    std::string m_StatusText;
    std::string m_LatestFinalAssetFileName;
    std::string m_LastViewportError;
    std::string m_LastCameraPreviewError;
    std::string m_LastFinalRenderError;
    double m_LastViewportUploadMs = 0.0;
    float m_LastViewportImageAspect = 16.0f / 9.0f;
    int m_CameraPreviewRenderWidth = 960;
    int m_CameraPreviewRenderHeight = 540;
};
