#include "LibraryModule.h"
#include "App/settings/AppearanceTheme.h"
#include "LibraryManager.h"
#include "Library/Internal/LibraryModuleUIHelpers.h"
#include "Async/TaskSystem.h"

#include "Utils/ImGuiExtras.h"
#include "Renderer/GLLoader.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <cstdio>
#include <utility>

using namespace Stack::Library::ModuleUI;

LibraryModule::LibraryModule() {}
LibraryModule::~LibraryModule() {
    if (m_OptionsIconTex) {
        glDeleteTextures(1, &m_OptionsIconTex);
        m_OptionsIconTex = 0;
    }
    if (m_AllProjectsIconTex) {
        glDeleteTextures(1, &m_AllProjectsIconTex);
        m_AllProjectsIconTex = 0;
    }
    if (m_AssetsIconTex) {
        glDeleteTextures(1, &m_AssetsIconTex);
        m_AssetsIconTex = 0;
    }
}

void LibraryModule::Initialize() {
    LibraryManager::Get().RequestRefreshLibraryAsync();
}

void LibraryModule::SyncRenameBuffer() {
    if (!m_PreviewProject) {
        m_RenameBuffer[0] = '\0';
        m_RenameTargetFileName.clear();
        return;
    }

    if (m_RenameTargetFileName == m_PreviewProject->fileName) {
        return;
    }

    std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", m_PreviewProject->projectName.c_str());
    m_RenameTargetFileName = m_PreviewProject->fileName;
}

void LibraryModule::OpenProjectPreviewByFileName(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }

    const auto& projects = LibraryManager::Get().GetProjects();
    for (const auto& project : projects) {
        if (!project || project->fileName != fileName) {
            continue;
        }

        m_ShowRawWorkspace = false;
        m_ShowAssets = false;
        m_PreviewProject = project;
        m_PreviewAsset = nullptr;
        m_ProjectPreviewTransition = 0.0f;
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewRefreshAfterClose = false;
        m_AssetPreviewTransition = 0.0f;
        m_AssetPreviewClosing = false;
        m_CompareSplit = 0.5f;
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
        LibraryManager::Get().CancelAssetPreviewRequests();
        LibraryManager::Get().CancelProjectPreviewRequests();
        LibraryManager::Get().RequestProjectPreview(m_PreviewProject);
        SyncRenameBuffer();
        return;
    }
}

void LibraryModule::OpenAssetPreviewByFileName(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }

    const auto& assets = LibraryManager::Get().GetAssets();
    for (const auto& asset : assets) {
        if (!asset || asset->fileName != fileName) {
            continue;
        }

        m_ShowRawWorkspace = false;
        m_ShowAssets = true;
        m_PreviewAsset = asset;
        m_PreviewProject = nullptr;
        m_AssetPreviewTransition = 0.0f;
        m_AssetPreviewClosing = false;
        m_ProjectPreviewTransition = 0.0f;
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewRefreshAfterClose = false;
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
        LibraryManager::Get().CancelProjectPreviewRequests();
        LibraryManager::Get().CancelAssetPreviewRequests();
        LibraryManager::Get().RequestAssetPreview(m_PreviewAsset);
        return;
    }
}

void LibraryModule::RenderUI(
    EditorModule* editor,
    CompositeModule* composite,
    StackAppearance::AppearanceManager* appearance,
    int* activeTab,
    int rawWorkspaceTabId,
    std::function<void(const std::string&)> onLoadEditorProject) {
    if (m_OptionsIconTex == 0) {
        m_OptionsIconTex = LoadIconTexture("options.png");
    }
    if (m_AllProjectsIconTex == 0) {
        m_AllProjectsIconTex = LoadIconTexture("all projects.png");
    }
    if (m_AssetsIconTex == 0) {
        m_AssetsIconTex = LoadIconTexture("assets.png");
    }

    m_CachedEditor = editor;
    m_CachedComposite = composite;
    m_CachedActiveTab = activeTab;
    m_CachedRawWorkspaceTabId = rawWorkspaceTabId;
    m_OnLoadEditorProject = std::move(onLoadEditorProject);
    m_BlockLibraryGridContextMenuThisFrame = false;
    const float dt = ImGui::GetIO().DeltaTime;
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        appearance ? appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
    m_LastRenderStats = {};

    if (!m_PreviewProject && !m_PreviewAsset) {
        const auto autoRefreshStarted = std::chrono::steady_clock::now();
        m_LastRenderStats.autoRefresh = LibraryManager::Get().TickAutoRefresh();
        m_LastRenderStats.autoRefreshMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - autoRefreshStarted).count();
    }

    const bool importBusy = Async::IsBusy(LibraryManager::Get().GetImportTaskState());
    const bool exportBusy = Async::IsBusy(LibraryManager::Get().GetExportTaskState());
    const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState());
    const bool loadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    const LibraryRefreshSnapshot refreshSnapshot = LibraryManager::Get().GetRefreshSnapshot();
    const bool refreshBusy = Async::IsBusy(refreshSnapshot.state);
    const bool refreshFailed = refreshSnapshot.state == Async::TaskState::Failed;
    float refreshStatusAlpha = (refreshBusy || refreshFailed) ? 1.0f : 0.0f;

    m_ImportStatusAlpha = ImGuiExtras::AnimateTowards(
        m_ImportStatusAlpha,
        !LibraryManager::Get().GetImportStatusText().empty() ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_ExportStatusAlpha = ImGuiExtras::AnimateTowards(
        m_ExportStatusAlpha,
        !LibraryManager::Get().GetExportStatusText().empty() ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_SaveStatusAlpha = ImGuiExtras::AnimateTowards(
        m_SaveStatusAlpha,
        (saveBusy || !LibraryManager::Get().GetSaveStatusText().empty()) ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_LoadStatusAlpha = ImGuiExtras::AnimateTowards(
        m_LoadStatusAlpha,
        (loadBusy || !LibraryManager::Get().GetProjectLoadStatusText().empty()) ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);

    auto renderStatusLine = [&](const char* text, float& alphaState) {
        if (text == nullptr || text[0] == '\0' || alphaState <= 0.01f) {
            return false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alphaState);
        ImGui::TextDisabled("%s", text);
        ImGui::PopStyleVar();
        return true;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

    if (wallpaperSurfaces) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, surfacePalette.controlSurface);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, surfacePalette.controlSurfaceHovered);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, surfacePalette.controlSurfaceActive);
        ImGui::PushStyleColor(ImGuiCol_Button, surfacePalette.controlSurface);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, surfacePalette.controlSurfaceHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, surfacePalette.controlSurfaceActive);
        ImGui::PushStyleColor(ImGuiCol_Border, surfacePalette.border);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    } else {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 18));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 24));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 32));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 14));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 26));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 34));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(220, 236, 244, 34));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(80.0f, 32.0f));
    ImGui::BeginChild("LibraryTabContainer", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    if (!m_ShowRawWorkspace &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_ShowAssets) {
                if (!m_SelectedAssets.empty()) {
                    m_PendingDeleteFileNames.assign(m_SelectedAssets.begin(), m_SelectedAssets.end());
                    m_DeletingAssets = true;
                    m_DeleteConfirmOpen = true;
                }
            } else {
                if (!m_SelectedProjects.empty()) {
                    m_PendingDeleteFileNames.assign(m_SelectedProjects.begin(), m_SelectedProjects.end());
                    m_DeletingAssets = false;
                    m_DeleteConfirmOpen = true;
                }
            }
        }
    }

    float headerHeight = 0.0f;
    if (m_ImportStatusAlpha > 0.01f ||
        m_ExportStatusAlpha > 0.01f ||
        m_SaveStatusAlpha > 0.01f ||
        m_LoadStatusAlpha > 0.01f ||
        refreshStatusAlpha > 0.01f) {
        headerHeight = 24.0f;
    }

    if (headerHeight > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (wallpaperSurfaces) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        }
        if (ImGui::BeginChild("LibraryHeader", ImVec2(0, headerHeight), false, ImGuiWindowFlags_NoScrollbar)) {
            bool hasPreviousStatus = false;
            auto renderInlineStatus = [&](const char* text, float& alphaState) {
                if (text == nullptr || text[0] == '\0' || alphaState <= 0.01f) {
                    return;
                }
                if (hasPreviousStatus) {
                    ImGui::SameLine(0.0f, 18.0f);
                }
                renderStatusLine(text, alphaState);
                hasPreviousStatus = true;
            };

            renderInlineStatus(LibraryManager::Get().GetImportStatusText().c_str(), m_ImportStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetExportStatusText().c_str(), m_ExportStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetSaveStatusText().c_str(), m_SaveStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetProjectLoadStatusText().c_str(), m_LoadStatusAlpha);
            renderInlineStatus(refreshSnapshot.statusText.c_str(), refreshStatusAlpha);
        }
        ImGui::EndChild();
        if (wallpaperSurfaces) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(); // Pop LibraryHeader's WindowPadding
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    if (m_ShowRawWorkspace) {
        RenderRawWorkspaceView(editor, activeTab, rawWorkspaceTabId);
    } else {
        RenderLibraryGrid(editor, appearance, wallpaperSurfaces, surfacePalette, refreshSnapshot, refreshBusy, importBusy, exportBusy, dt);
    }
    ImGui::EndChild(); // End LibraryTabContainer

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(3);

    if (m_PreviewProject || m_ProjectPreviewTransition > 0.0f) {
        RenderPreviewPopup(editor, composite, activeTab);
    } else if (m_PreviewAsset || m_AssetPreviewTransition > 0.0f) {
        RenderAssetPreviewPopup(editor, composite, activeTab);
    }

    if (importBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetImportStatusText().c_str());
    } else if (exportBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetExportStatusText().c_str());
    } else if (saveBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    } else if (loadBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }
}

void LibraryModule::RequestOpenEditorProject(const std::string& projectFileName) {
    if (projectFileName.empty()) {
        return;
    }

    if (m_OnLoadEditorProject) {
        m_OnLoadEditorProject(projectFileName);
        return;
    }

    if (m_CachedEditor == nullptr) {
        return;
    }

    LibraryManager::Get().RequestLoadProject(projectFileName, m_CachedEditor, [this](bool success) {
        if (success && m_CachedActiveTab != nullptr) {
            *m_CachedActiveTab = 1;
        }
    });
}

void LibraryModule::DismissPreviewsForProjectLoad() {
    LibraryManager::Get().CancelProjectPreviewRequests();
    LibraryManager::Get().CancelAssetPreviewRequests();
    m_PreviewProject = nullptr;
    m_PreviewAsset = nullptr;
    m_ProjectPreviewTransition = 0.0f;
    m_ProjectPreviewClosing = false;
    m_ProjectPreviewRefreshAfterClose = false;
    m_AssetPreviewTransition = 0.0f;
    m_AssetPreviewClosing = false;
    m_ProjectPreviewMenuHover = 0.0f;
    m_AssetPreviewMenuHover = 0.0f;
    m_ProjectPreviewLaunchRect.valid = false;
    m_AssetPreviewLaunchRect.valid = false;
    m_RenameTargetFileName.clear();
}
