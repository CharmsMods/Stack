#include "LibraryModule.h"

#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryModuleUIHelpers.h"
#include "LibraryManager.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

using namespace Stack::Library::ModuleUI;

namespace {

constexpr float kCardInputReadyAlpha = 0.18f;

} // namespace

bool LibraryModule::RenderProjectCard(const ProjectEntry& project, EditorModule* editor) {
    (void)editor;

    const bool isSelected = m_SelectedProjects.count(project.fileName) > 0;
    const bool matchesFilter = ProjectMatchesFilter(project, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
    const float dt = ImGui::GetIO().DeltaTime;
    const int frameCount = ImGui::GetFrameCount();

    LibraryCardMotionState& motion = GetCardMotionState(m_ProjectCardMotion, project.fileName);
    motion.lastSeenFrame = frameCount;
    const float entranceProgress = ResolveCardEntranceProgress(motion, ImGui::GetTime());
    motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, matchesFilter ? 1.0f : 0.0f, dt, kCardMotionSpeed);
    motion.selected = ImGuiExtras::AnimateTowards(motion.selected, isSelected ? 1.0f : 0.0f, dt, kCardMotionSpeed);

    if (!matchesFilter && motion.reveal <= 0.01f) {
        return false;
    }

    ImVec2 thumbSize = ComputeLibraryCardSize(static_cast<float>(project.sourceWidth), static_cast<float>(project.sourceHeight));

    const float cardWidth = thumbSize.x;
    const float totalHeight = thumbSize.y;
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImRect cardRect(screenMin, ImVec2(screenMin.x + cardWidth, screenMin.y + totalHeight));

    ImGui::BeginGroup();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cardAlpha = std::clamp(motion.reveal * entranceProgress, 0.0f, 1.0f);
    const float rounding = 14.0f;
    const ImVec4 baseColor(0.10f, 0.115f, 0.135f, 0.94f * cardAlpha);
    drawList->AddRectFilled(cardRect.Min, cardRect.Max, ImGui::ColorConvertFloat4ToU32(baseColor), rounding);

    const float xOffset = (cardWidth - thumbSize.x) * 0.5f;
    const ImRect imageRect(
        ImVec2(cardRect.Min.x + xOffset, cardRect.Min.y),
        ImVec2(cardRect.Min.x + xOffset + thumbSize.x, cardRect.Min.y + thumbSize.y));

    if (project.thumbnailTex) {
        ImVec2 uv0;
        ImVec2 uv1;
        ComputeCoverUv(static_cast<float>(project.sourceWidth), static_cast<float>(project.sourceHeight), imageRect, uv0, uv1);
        drawList->AddImageRounded(
            (ImTextureID)(intptr_t)project.thumbnailTex,
            imageRect.Min,
            imageRect.Max,
            uv0,
            uv1,
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * cardAlpha)),
            rounding);
    } else {
        drawList->AddRectFilled(imageRect.Min, imageRect.Max, IM_COL32(40, 46, 52, static_cast<int>(240.0f * cardAlpha)), rounding);
        const char* noPreview = "No Preview";
        const ImVec2 noPreviewSize = ImGui::CalcTextSize(noPreview);
        drawList->AddText(
            ImVec2(imageRect.Min.x + (imageRect.GetWidth() - noPreviewSize.x) * 0.5f, imageRect.Min.y + (imageRect.GetHeight() - noPreviewSize.y) * 0.5f),
            IM_COL32(220, 228, 236, static_cast<int>(210.0f * cardAlpha)),
            noPreview);
    }

    ImGui::InvisibleButton("##hitbox", ImVec2(cardWidth, totalHeight));
    const bool isHovered = ImGui::IsItemHovered();
    const bool inputReady = matchesFilter && cardAlpha > kCardInputReadyAlpha;
    const bool isLeftClicked = inputReady && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool isRightClicked = inputReady && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isDoubleClicked = inputReady && isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (inputReady && isHovered) ? 1.0f : 0.0f, dt, 24.0f);

    const ImRect motionRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const float overlayAlpha = ImGuiExtras::EaseOutCubic(motion.hover);
    drawList->AddRectFilled(
        motionRect.Min,
        motionRect.Max,
        IM_COL32(8, 11, 14, static_cast<int>(124.0f * overlayAlpha)),
        rounding);
    DrawCardOverlayText(
        drawList,
        motionRect,
        overlayAlpha,
        project.projectName,
        std::to_string(project.sourceWidth) + "x" + std::to_string(project.sourceHeight),
        project.timestamp);
    DrawCardMotionFrame(drawList, motionRect, motion.hover, motion.selected, isHovered);

    if (isLeftClicked) {
        if (ImGui::GetIO().KeyCtrl) {
            if (isSelected) m_SelectedProjects.erase(project.fileName);
            else m_SelectedProjects.insert(project.fileName);
        } else {
            m_SelectedProjects.clear();
            m_SelectedProjects.insert(project.fileName);
        }
        m_LastClickedProject = project.fileName;
    }

    if (isDoubleClicked) {
        const auto& projects = LibraryManager::Get().GetProjects();
        for (const auto& candidate : projects) {
            if (candidate && candidate->fileName == project.fileName) {
                m_PreviewProject = candidate;
                m_PreviewAsset = nullptr;
                m_ProjectPreviewTransition = 0.0f;
                m_ProjectPreviewClosing = false;
                m_AssetPreviewTransition = 0.0f;
                m_AssetPreviewClosing = false;
                m_CompareSplit = 0.5f;
                m_ProjectPreviewMenuHover = 0.0f;
                m_ProjectPreviewLaunchRect.minX = motionRect.Min.x;
                m_ProjectPreviewLaunchRect.minY = motionRect.Min.y;
                m_ProjectPreviewLaunchRect.maxX = motionRect.Max.x;
                m_ProjectPreviewLaunchRect.maxY = motionRect.Max.y;
                m_ProjectPreviewLaunchRect.valid = true;
                LibraryManager::Get().CancelAssetPreviewRequests();
                LibraryManager::Get().CancelProjectPreviewRequests();
                LibraryManager::Get().RequestProjectPreview(m_PreviewProject);
                SyncRenameBuffer();
                break;
            }
        }
    }

    if (isRightClicked) {
        if (!isSelected) {
            m_SelectedProjects.clear();
            m_SelectedProjects.insert(project.fileName);
        }
        m_BlockLibraryGridContextMenuThisFrame = true;
        const std::string popupId = std::string("ProjectCardContextMenu##") + project.fileName;
        ImGui::OpenPopup(popupId.c_str());
    }

    const std::string popupId = std::string("ProjectCardContextMenu##") + project.fileName;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    if (ImGui::BeginPopup(popupId.c_str())) {
        const bool multiple = m_SelectedProjects.size() > 1;
        if (!multiple) {
            if (project.projectKind == "editor" || project.projectKind.empty()) {
                if (ImGui::MenuItem("Open in Editor")) {
                    RequestOpenEditorProject(project.fileName);
                }
            }
            if (project.projectKind == "composite") {
                ImGui::BeginDisabled();
                ImGui::MenuItem("Legacy Composite Project (Unsupported)", nullptr, false, false);
                ImGui::EndDisabled();
            }
        }

        ImGui::BeginDisabled();
        ImGui::MenuItem("Load Selected into Composite", nullptr, false, false);
        ImGui::EndDisabled();

        ImGui::Separator();

        char deleteLabel[64];
        snprintf(deleteLabel, sizeof(deleteLabel), "Delete Selected (%d)", (int)m_SelectedProjects.size());
        if (ImGui::MenuItem(deleteLabel)) {
            m_PendingDeleteFileNames.assign(m_SelectedProjects.begin(), m_SelectedProjects.end());
            m_DeletingAssets = false;
            m_DeleteConfirmOpen = true;
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    ImGui::EndGroup();
    return true;
}

bool LibraryModule::RenderAssetCard(const AssetEntry& asset, EditorModule* editor) {
    const bool isSelected = m_SelectedAssets.count(asset.fileName) > 0;
    const bool matchesFilter = AssetMatchesFilter(asset, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
    const float dt = ImGui::GetIO().DeltaTime;
    const int frameCount = ImGui::GetFrameCount();

    LibraryCardMotionState& motion = GetCardMotionState(m_AssetCardMotion, asset.fileName);
    motion.lastSeenFrame = frameCount;
    const float entranceProgress = ResolveCardEntranceProgress(motion, ImGui::GetTime());
    motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, matchesFilter ? 1.0f : 0.0f, dt, kCardMotionSpeed);
    motion.selected = ImGuiExtras::AnimateTowards(motion.selected, isSelected ? 1.0f : 0.0f, dt, kCardMotionSpeed);

    if (!matchesFilter && motion.reveal <= 0.01f) {
        return false;
    }

    ImVec2 thumbSize = ComputeLibraryCardSize(static_cast<float>(asset.width), static_cast<float>(asset.height));

    const float cardWidth = thumbSize.x;
    const float totalHeight = thumbSize.y;
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImRect cardRect(screenMin, ImVec2(screenMin.x + cardWidth, screenMin.y + totalHeight));

    ImGui::BeginGroup();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cardAlpha = std::clamp(motion.reveal * entranceProgress, 0.0f, 1.0f);
    const float rounding = 14.0f;
    const ImVec4 baseColor(0.10f, 0.115f, 0.135f, 0.94f * cardAlpha);
    drawList->AddRectFilled(cardRect.Min, cardRect.Max, ImGui::ColorConvertFloat4ToU32(baseColor), rounding);

    const float xOffset = (cardWidth - thumbSize.x) * 0.5f;
    const ImRect imageRect(
        ImVec2(cardRect.Min.x + xOffset, cardRect.Min.y),
        ImVec2(cardRect.Min.x + xOffset + thumbSize.x, cardRect.Min.y + thumbSize.y));

    if (asset.thumbnailTex) {
        ImVec2 uv0;
        ImVec2 uv1;
        ComputeCoverUv(static_cast<float>(asset.width), static_cast<float>(asset.height), imageRect, uv0, uv1);
        drawList->AddImageRounded(
            (ImTextureID)(intptr_t)asset.thumbnailTex,
            imageRect.Min,
            imageRect.Max,
            uv0,
            uv1,
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * cardAlpha)),
            rounding);
    } else {
        drawList->AddRectFilled(imageRect.Min, imageRect.Max, IM_COL32(40, 46, 52, static_cast<int>(240.0f * cardAlpha)), rounding);
        const char* noPreview = "No Asset Preview";
        const ImVec2 noPreviewSize = ImGui::CalcTextSize(noPreview);
        drawList->AddText(
            ImVec2(imageRect.Min.x + (imageRect.GetWidth() - noPreviewSize.x) * 0.5f, imageRect.Min.y + (imageRect.GetHeight() - noPreviewSize.y) * 0.5f),
            IM_COL32(220, 228, 236, static_cast<int>(210.0f * cardAlpha)),
            noPreview);
    }

    ImGui::InvisibleButton("##hitbox", ImVec2(cardWidth, totalHeight));
    const bool isHovered = ImGui::IsItemHovered();
    const bool inputReady = matchesFilter && cardAlpha > kCardInputReadyAlpha;
    const bool isLeftClicked = inputReady && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool isRightClicked = inputReady && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isDoubleClicked = inputReady && isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (inputReady && isHovered) ? 1.0f : 0.0f, dt, 24.0f);
    const ImRect motionRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const float overlayAlpha = ImGuiExtras::EaseOutCubic(motion.hover);
    drawList->AddRectFilled(
        motionRect.Min,
        motionRect.Max,
        IM_COL32(8, 11, 14, static_cast<int>(124.0f * overlayAlpha)),
        rounding);
    DrawCardOverlayText(
        drawList,
        motionRect,
        overlayAlpha,
        asset.displayName,
        std::to_string(asset.width) + "x" + std::to_string(asset.height),
        !asset.projectName.empty() ? ("From: " + asset.projectName) : asset.fileName);
    DrawCardMotionFrame(drawList, motionRect, motion.hover, motion.selected, isHovered);

    if (isLeftClicked) {
        if (ImGui::GetIO().KeyCtrl) {
            if (isSelected) m_SelectedAssets.erase(asset.fileName);
            else m_SelectedAssets.insert(asset.fileName);
        } else {
            m_SelectedAssets.clear();
            m_SelectedAssets.insert(asset.fileName);
        }
        m_LastClickedAsset = asset.fileName;
    }

    if (isDoubleClicked) {
        const auto& assets = LibraryManager::Get().GetAssets();
        for (const auto& candidate : assets) {
            if (candidate && candidate->fileName == asset.fileName) {
                m_ShowAssets = true;
                m_PreviewAsset = candidate;
                m_PreviewProject = nullptr;
                m_AssetPreviewTransition = 0.0f;
                m_AssetPreviewClosing = false;
                m_ProjectPreviewTransition = 0.0f;
                m_ProjectPreviewClosing = false;
                m_ProjectPreviewRefreshAfterClose = false;
                m_ProjectPreviewMenuHover = 0.0f;
                m_ProjectPreviewLaunchRect.valid = false;
                m_AssetPreviewMenuHover = 0.0f;
                m_AssetPreviewLaunchRect.minX = motionRect.Min.x;
                m_AssetPreviewLaunchRect.minY = motionRect.Min.y;
                m_AssetPreviewLaunchRect.maxX = motionRect.Max.x;
                m_AssetPreviewLaunchRect.maxY = motionRect.Max.y;
                m_AssetPreviewLaunchRect.valid = true;
                LibraryManager::Get().CancelProjectPreviewRequests();
                LibraryManager::Get().CancelAssetPreviewRequests();
                LibraryManager::Get().RequestAssetPreview(m_PreviewAsset);
                break;
            }
        }
    }

    if (isRightClicked) {
        if (!isSelected) {
            m_SelectedAssets.clear();
            m_SelectedAssets.insert(asset.fileName);
        }
        m_BlockLibraryGridContextMenuThisFrame = true;
        const std::string popupId = std::string("AssetCardContextMenu##") + asset.fileName;
        ImGui::OpenPopup(popupId.c_str());
    }

    const std::string popupId = std::string("AssetCardContextMenu##") + asset.fileName;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    if (ImGui::BeginPopup(popupId.c_str())) {
        if (ImGui::MenuItem("Add Selected To Editor Composite", nullptr, false, !m_SelectedAssets.empty() && editor != nullptr)) {
            for (const auto& fn : m_SelectedAssets) {
                editor->AddCompositeLibraryAssetChain(fn);
            }
            if (m_CachedActiveTab) *m_CachedActiveTab = 1;
        }

        ImGui::Separator();

        char deleteLabel[64];
        snprintf(deleteLabel, sizeof(deleteLabel), "Delete Selected (%d)", (int)m_SelectedAssets.size());
        if (ImGui::MenuItem(deleteLabel)) {
            m_PendingDeleteFileNames.assign(m_SelectedAssets.begin(), m_SelectedAssets.end());
            m_DeletingAssets = true;
            m_DeleteConfirmOpen = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    ImGui::EndGroup();
    return true;
}
