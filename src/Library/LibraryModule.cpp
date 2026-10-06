#include "LibraryModule.h"
#include "App/AppPaths.h"
#include "App/Resources/EmbeddedTabIcons.h"
#include "App/settings/AppearanceTheme.h"
#include "LibraryManager.h"
#include "Library/Internal/LibraryModuleUIHelpers.h"
#include "Persistence/StackBinaryFormat.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"

#include "Utils/ImGuiExtras.h"
#include "Renderer/GLLoader.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <utility>

using namespace Stack::Library::ModuleUI;

namespace {

constexpr float kLibraryViewScaleMin = 0.55f;
constexpr float kLibraryViewScaleMax = 1.80f;

ImVec4 BlendColor(const ImVec4& from, const ImVec4& to, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        from.x + (to.x - from.x) * clamped,
        from.y + (to.y - from.y) * clamped,
        from.z + (to.z - from.z) * clamped,
        from.w + (to.w - from.w) * clamped);
}

std::filesystem::path GetLibraryViewStatePath() {
    return AppPaths::GetSettingsDirectory() / "LibraryViewState.json";
}

} // namespace

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
    const unsigned int fileMenuTextures[] = {
        m_FileNewIconTex, m_FileOpenIconTex, m_FileSaveIconTex, m_FileFolderIconTex,
        m_SearchIconTex, m_RawWorkspaceIconTex, m_ZoomMinusIconTex, m_ZoomPlusIconTex,
        m_ReloadIconTex, m_ChevronIconTex
    };
    for (const unsigned int texture : fileMenuTextures) {
        if (texture) {
            glDeleteTextures(1, &texture);
        }
    }
}

void LibraryModule::Initialize() {
    LoadViewState();
    LibraryManager::Get().RequestRefreshLibraryAsync();
}

void LibraryModule::AdjustViewScale(float steps) {
    if (std::abs(steps) < 0.0001f) {
        return;
    }
    const float previousScale = m_LibraryViewScale;
    m_LibraryViewScale = std::clamp(
        previousScale * std::pow(1.10f, steps),
        kLibraryViewScaleMin,
        kLibraryViewScaleMax);
    if (std::abs(m_LibraryViewScale - previousScale) > 0.0001f) {
        m_CachedLayoutKey.clear();
        SaveViewState();
    }
}

bool LibraryModule::CanZoomOut() const {
    return m_LibraryViewScale > kLibraryViewScaleMin + 0.0001f;
}

bool LibraryModule::CanZoomIn() const {
    return m_LibraryViewScale < kLibraryViewScaleMax - 0.0001f;
}

void LibraryModule::LoadViewState() {
    std::ifstream file(GetLibraryViewStatePath());
    if (!file) {
        return;
    }

    const StackBinaryFormat::json root = StackBinaryFormat::json::parse(file, nullptr, false);
    if (!root.is_object()) {
        return;
    }

    const auto scaleIt = root.find("gridScale");
    if (scaleIt == root.end() || !scaleIt->is_number()) {
        return;
    }

    const float savedScale = scaleIt->get<float>();
    if (std::isfinite(savedScale)) {
        m_LibraryViewScale = std::clamp(savedScale, kLibraryViewScaleMin, kLibraryViewScaleMax);
    }
}

void LibraryModule::SaveViewState() const {
    AppPaths::EnsureRuntimeDirectories();
    std::ofstream file(GetLibraryViewStatePath(), std::ios::trunc);
    if (!file) {
        return;
    }

    StackBinaryFormat::json root = StackBinaryFormat::json::object();
    root["version"] = 1;
    root["gridScale"] = m_LibraryViewScale;
    file << root.dump(2);
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
        m_ProjectPreviewMenuHover = 1.0f;
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
        m_OptionsIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibraryOptions_png_data, EmbeddedTabIcons::LibraryOptions_png_size);
    }
    if (m_AllProjectsIconTex == 0) {
        m_AllProjectsIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibraryProjects_png_data, EmbeddedTabIcons::LibraryProjects_png_size);
    }
    if (m_AssetsIconTex == 0) {
        m_AssetsIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibraryAssets_png_data, EmbeddedTabIcons::LibraryAssets_png_size);
    }
    if (m_SearchIconTex == 0) {
        m_SearchIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibrarySearch_png_data, EmbeddedTabIcons::LibrarySearch_png_size);
    }
    if (m_RawWorkspaceIconTex == 0) {
        m_RawWorkspaceIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibraryRawWorkspace_png_data, EmbeddedTabIcons::LibraryRawWorkspace_png_size);
    }
    if (m_FileNewIconTex == 0) {
        m_FileNewIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::FileNew_png_data, EmbeddedTabIcons::FileNew_png_size);
    }
    if (m_FileOpenIconTex == 0) {
        m_FileOpenIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::FileOpenProject_png_data, EmbeddedTabIcons::FileOpenProject_png_size);
    }
    if (m_FileSaveIconTex == 0) {
        m_FileSaveIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::FileSave_png_data, EmbeddedTabIcons::FileSave_png_size);
    }
    if (m_FileFolderIconTex == 0) {
        m_FileFolderIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::FileFolder_png_data, EmbeddedTabIcons::FileFolder_png_size);
    }
    if (m_ZoomMinusIconTex == 0) {
        m_ZoomMinusIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibraryZoomMinus_png_data, EmbeddedTabIcons::LibraryZoomMinus_png_size);
    }
    if (m_ZoomPlusIconTex == 0) {
        m_ZoomPlusIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::LibraryZoomPlus_png_data, EmbeddedTabIcons::LibraryZoomPlus_png_size);
    }
    if (m_ReloadIconTex == 0) {
        m_ReloadIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::Reload_png_data, EmbeddedTabIcons::Reload_png_size);
    }
    if (m_ChevronIconTex == 0) {
        m_ChevronIconTex = LoadIconTextureFromMemory(
            EmbeddedTabIcons::Chevron_png_data, EmbeddedTabIcons::Chevron_png_size);
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

    // The Library is the saved-project index. Unedited RAW sources live only
    // in RAW Lab's Gallery and are never presented as a competing Library tab.
    m_ShowRawWorkspace = false;
    m_ShowAssets = false;

    if (!m_PreviewProject && !m_PreviewAsset) {
        const auto autoRefreshStarted = std::chrono::steady_clock::now();
        m_LastRenderStats.autoRefresh = LibraryManager::Get().TickAutoRefresh();
        m_LastRenderStats.autoRefreshMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - autoRefreshStarted).count();
    }

    const bool importBusy = Async::IsBusy(LibraryManager::Get().GetImportTaskState());
    const bool exportBusy = Async::IsBusy(LibraryManager::Get().GetExportTaskState());
    const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState(m_CachedEditor));
    const bool loadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState(m_CachedEditor));
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
        (saveBusy || !LibraryManager::Get().GetSaveStatusText(m_CachedEditor).empty()) ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_LoadStatusAlpha = ImGuiExtras::AnimateTowards(
        m_LoadStatusAlpha,
        (loadBusy || !LibraryManager::Get().GetProjectLoadStatusText(m_CachedEditor).empty()) ? 1.0f : 0.0f,
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

    const float libraryHeaderHeight = 44.0f;
    const ImVec4 headerSurface = appearance
        ? surfacePalette.chromeSurface
        : ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, headerSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 0.0f));
    ImGui::BeginChild(
        "LibraryPrimaryHeader",
        ImVec2(0.0f, libraryHeaderHeight),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    constexpr float controlTopY = 9.0f;
    const float projTabWidth = ImGui::CalcTextSize("Projects").x + 16.0f;
    const float tabsWidth = projTabWidth;

    const auto renderPrimaryTab = [&](const char* id, const char* label, bool selected) {
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 tabSize(textSize.x + 16.0f, 24.0f);
        ImGui::PushID(id);
        ImGui::InvisibleButton("##LibraryPrimaryTab", tabSize);
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        if (selected || hovered)
            ImGui::GetWindowDrawList()->AddRectFilled(itemMin, ImGui::GetItemRectMax(),
                ImGui::GetColorU32(selected ? ImGuiCol_TabSelected : ImGuiCol_TabHovered), 4.0f);
        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        if (selected) {
            textColor = ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline);
        } else if (!hovered) {
            textColor.w *= 0.68f;
        }
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(
                itemMin.x + (tabSize.x - textSize.x) * 0.5f,
                itemMin.y + (tabSize.y - textSize.y) * 0.5f - 0.5f),
            ImGui::GetColorU32(textColor),
            label);
        ImGui::PopID();
        return clicked;
    };

    if (!m_SectionPanelHosted) {
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (ImGui::GetWindowSize().x - tabsWidth) * 0.5f),
            controlTopY));
        if (renderPrimaryTab("Projects", "Projects", !m_ShowRawWorkspace)) {
            m_ShowRawWorkspace = false;
            m_ShowAssets = false;
            m_SelectedAssets.clear();
            m_PreviewAsset = nullptr;
            LibraryManager::Get().CancelAssetPreviewRequests();
        }
        if (ImGui::IsItemHovered()) {
            const std::string projectsHint =
                "Saved projects in " + AppPaths::GetProjectsDirectory().string();
            ImGui::SetTooltip("%s", projectsHint.c_str());
        }
    }
    {
        constexpr float zoomHitSize = 24.0f;
        constexpr float zoomIconSize = 14.0f;
        constexpr float zoomGap = 4.0f;

        const auto renderZoomButton = [&](const char* id, const char* tooltip, unsigned int texture, bool enabled) {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(id, ImVec2(zoomHitSize, zoomHitSize));
            const bool hovered = enabled && ImGui::IsItemHovered();
            const bool held = enabled && ImGui::IsItemActive();
            ImVec4 tint = enabled
                ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            if (held) {
                tint = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            } else if (hovered) {
                tint = BlendColor(tint, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), 0.72f);
            } else if (enabled) {
                tint.w *= 0.58f;
            } else {
                tint.w *= 0.25f;
            }
            if (texture != 0) {
                const ImVec2 iconMin(
                    cursor.x + (zoomHitSize - zoomIconSize) * 0.5f,
                    cursor.y + (zoomHitSize - zoomIconSize) * 0.5f);
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)texture,
                    iconMin,
                    ImVec2(iconMin.x + zoomIconSize, iconMin.y + zoomIconSize),
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    ImGui::GetColorU32(tint));
            }
            if (hovered) {
                ImGui::SetTooltip("%s", tooltip);
            }
            return enabled && ImGui::IsItemClicked();
        };

        if (m_SectionPanelHosted) ImGui::SetCursorPos(ImVec2(22.0f, controlTopY));
        else {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::SetCursorPosY(controlTopY);
        }
        if (renderZoomButton("##LibraryZoomOut", "Zoom Out", m_ZoomMinusIconTex, CanZoomOut())) {
            AdjustViewScale(-1.0f);
        }
        ImGui::SameLine(0.0f, zoomGap);
        ImGui::SetCursorPosY(controlTopY);
        if (renderZoomButton("##LibraryZoomIn", "Zoom In", m_ZoomPlusIconTex, CanZoomIn())) {
            AdjustViewScale(1.0f);
        }
        if (m_SectionPanelHosted) {
            ImGui::SameLine(0.0f, 12.0f);
            if (renderZoomButton("##LibraryOptions", "Library options", m_OptionsIconTex, true))
                ImGui::OpenPopup("LibraryHeaderOptions");
            if (ImGui::BeginPopup("LibraryHeaderOptions")) {
                RenderLibraryMenuOptions(importBusy, exportBusy);
                ImGui::EndPopup();
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // The two views add their own inset; this padding separates their content
    // from the shared Library chrome without leaving room for the old overlay.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 18.0f));
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
            renderInlineStatus(LibraryManager::Get().GetSaveStatusText(m_CachedEditor).c_str(), m_SaveStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetProjectLoadStatusText(m_CachedEditor).c_str(), m_LoadStatusAlpha);
            renderInlineStatus(refreshSnapshot.statusText.c_str(), refreshStatusAlpha);
        }
        ImGui::EndChild();
        if (wallpaperSurfaces) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(); // Pop LibraryHeader's WindowPadding
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    RenderLibraryGrid(editor, appearance, wallpaperSurfaces, surfacePalette,
        refreshSnapshot, refreshBusy, importBusy, exportBusy, dt);
    ImGui::EndChild(); // End LibraryTabContainer

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(3);

    if (m_PreviewProject || m_ProjectPreviewTransition > 0.0f) {
        RenderPreviewPopup(editor, composite, activeTab);
    } else if (m_PreviewAsset || m_AssetPreviewTransition > 0.0f) {
        RenderAssetPreviewPopup(editor, composite, activeTab);
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
