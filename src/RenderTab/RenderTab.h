#pragma once

#include "RenderTab/Contracts/RenderDelegator.h"
#include "RenderTab/Contracts/SceneCompiler.h"
#include "RenderTab/Contracts/ViewportController.h"
#include "RenderTab/Foundation/RenderFoundationState.h"
#include "Persistence/StackBinaryFormat.h"

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
    void RenderToolbar();
    void RenderBody();
    void RenderInspectorPanel(float width);
    void RenderViewportPanel(float width);
    void RenderScenePanel(float width);
    void RenderCameraWindow();
    void RenderResetScenePopup();
    bool SyncCompiledScene(std::string& errorMessage);
    void ApplySceneChange(const RenderContracts::SceneChangeSet& changeSet);
    int GetSelectedRuntimeObjectId() const;

    bool RequestSaveProject();

private:
    RenderFoundation::State m_State;
    RenderContracts::SceneCompiler m_SceneCompiler;
    mutable RenderContracts::RenderDelegator m_RenderDelegator;
    RenderContracts::ViewportController m_ViewportController;
    RenderContracts::SceneSnapshot m_ActiveSnapshot {};
    RenderContracts::CompiledScene m_CompiledScene {};
    float m_InspectorWidth = 320.0f;
    float m_SceneWidth = 300.0f;
    bool m_OpenResetSceneConfirm = false;
    bool m_ShowCameraWindow = true;
    std::string m_StatusText;
    std::string m_LatestFinalAssetFileName;
    std::string m_LastViewportError;
};
