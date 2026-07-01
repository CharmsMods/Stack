#include "LibraryModule.h"

#include "Async/TaskSystem.h"
#include "Composite/CompositeModule.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryModuleUIHelpers.h"
#include "LibraryManager.h"
#include "Persistence/StackBinaryFormat.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

using namespace Stack::Library::ModuleUI;

void LibraryModule::RenderPreviewPopup(
    EditorModule* editor,
    CompositeModule* composite,
    int* activeTab) {
    if (!m_PreviewProject && m_ProjectPreviewTransition <= 0.0f) {
        return;
    }

    const float previewProgressTarget = (m_PreviewProject && !m_ProjectPreviewClosing) ? 1.0f : 0.0f;
    m_ProjectPreviewTransition = ImGuiExtras::AnimateTowards(m_ProjectPreviewTransition, previewProgressTarget, ImGui::GetIO().DeltaTime, kPreviewMotionSpeed);
    const float previewAlpha = ImGuiExtras::EaseOutCubic(m_ProjectPreviewTransition);
    const float closeProgress = std::clamp(1.0f - m_ProjectPreviewTransition, 0.0f, 1.0f);
    const float delayedContentClose = std::clamp((closeProgress - 0.34f) / 0.66f, 0.0f, 1.0f);
    const float closeContentAmount = ImGuiExtras::EaseOutCubic(delayedContentClose);

    if ((m_ProjectPreviewClosing || !m_PreviewProject) && m_ProjectPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelProjectPreviewRequests();
        m_PreviewProject = nullptr;
        m_PreviewAsset = nullptr;
        m_RenameTargetFileName.clear();
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        if (m_ProjectPreviewRefreshAfterClose) {
            LibraryManager::Get().RequestRefreshLibraryAsync();
            m_ProjectPreviewRefreshAfterClose = false;
        }
        return;
    }

    if (!m_PreviewProject) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowBgAlpha(0.04f + (0.93f * previewAlpha));
    ImGui::Begin("Library Project Preview", nullptr, flags);

    const bool previewReady = (m_PreviewProject->sourcePreviewTex != 0 && m_PreviewProject->fullPreviewTex != 0);
    const bool previewBusy = Async::IsBusy(m_PreviewProject->previewTaskState);
    const bool renderProject = IsRenderProject(*m_PreviewProject);
    const bool compositeProject = IsCompositeProject(*m_PreviewProject);

    const float contentProgress = ImGuiExtras::EaseOutCubic(previewAlpha);
    const float contentVisibility = m_ProjectPreviewClosing
        ? std::clamp(1.0f - closeContentAmount, 0.0f, 1.0f)
        : std::clamp((contentProgress - 0.08f) / 0.92f, 0.0f, 1.0f);
    const float contentScale = m_ProjectPreviewClosing
        ? (1.0f - (0.045f * closeContentAmount))
        : (0.972f + (0.028f * contentProgress));
    const ImVec2 baseContentSize(
        std::min(viewport->Size.x * 0.76f, 1260.0f),
        std::min(viewport->Size.y * 0.70f, 820.0f));
    const ImVec2 contentSize(baseContentSize.x * contentScale, baseContentSize.y * contentScale);
    const float detailsCollapsedWidth = 52.0f;
    const float detailsExpandedWidth = 348.0f;
    const float detailsWidth =
        detailsCollapsedWidth + ((detailsExpandedWidth - detailsCollapsedWidth) * m_ProjectPreviewMenuHover);
    const float gap = 18.0f;
    const ImVec2 contentMin(
        viewport->Pos.x + (viewport->Size.x - contentSize.x) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - contentSize.y) * 0.5f + ((1.0f - contentProgress) * 14.0f) + (closeContentAmount * 8.0f));
    const ImVec2 previewArea(std::max(360.0f, contentSize.x - detailsWidth - gap), contentSize.y);
    const ImVec2 sideArea(detailsWidth, contentSize.y);

    const ImVec2 fittedImageSize = FitImageToBounds(
        static_cast<float>(m_PreviewProject->sourceWidth),
        static_cast<float>(m_PreviewProject->sourceHeight),
        previewArea);
    const ImRect finalImageRect(
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f)),
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f) + fittedImageSize.x,
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f) + fittedImageSize.y));
    const ImRect detailsRect(
        ImVec2(contentMin.x + previewArea.x + gap, contentMin.y),
        ImVec2(contentMin.x + previewArea.x + gap + sideArea.x, contentMin.y + sideArea.y));

    const bool canCloseFromBackdrop = contentProgress >= 0.92f;
    const bool clickedBackdrop =
        canCloseFromBackdrop &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !m_ProjectPreviewClosing &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !finalImageRect.Contains(ImGui::GetIO().MousePos) &&
        !detailsRect.Contains(ImGui::GetIO().MousePos);
    if (clickedBackdrop) {
        m_ProjectPreviewClosing = true;
    }

    if (!m_ProjectPreviewClosing && m_ProjectPreviewLaunchRect.valid) {
        const float launchBlend = std::clamp(1.0f - contentProgress, 0.0f, 1.0f);
        if (launchBlend > 0.01f) {
            const ImRect launchRect(
                ImVec2(m_ProjectPreviewLaunchRect.minX, m_ProjectPreviewLaunchRect.minY),
                ImVec2(m_ProjectPreviewLaunchRect.maxX, m_ProjectPreviewLaunchRect.maxY));
            const auto lerp = [](float a, float b, float t) { return a + ((b - a) * t); };
            const ImRect animatedRect(
                ImVec2(lerp(launchRect.Min.x, finalImageRect.Min.x, contentProgress), lerp(launchRect.Min.y, finalImageRect.Min.y, contentProgress)),
                ImVec2(lerp(launchRect.Max.x, finalImageRect.Max.x, contentProgress), lerp(launchRect.Max.y, finalImageRect.Max.y, contentProgress)));
            const ImTextureID animTex = previewReady
                ? (ImTextureID)(intptr_t)m_PreviewProject->fullPreviewTex
                : (ImTextureID)(intptr_t)m_PreviewProject->thumbnailTex;
            if (animTex) {
                ImGui::GetWindowDrawList()->AddImageRounded(
                    animTex,
                    animatedRect.Min,
                    animatedRect.Max,
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    IM_COL32(255, 255, 255, static_cast<int>(255.0f * launchBlend)),
                    18.0f);
            }
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentVisibility);
    if (previewReady) {
        ImGui::SetCursorPos(ImVec2(
            finalImageRect.Min.x - viewport->Pos.x,
            finalImageRect.Min.y - viewport->Pos.y));
        if (renderProject || compositeProject) {
            ImGui::InvisibleButton("##PreviewImageHit", fittedImageSize);
            const ImRect imageRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::GetWindowDrawList()->AddImageRounded(
                (ImTextureID)(intptr_t)m_PreviewProject->fullPreviewTex,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0, 1),
                ImVec2(1, 0),
                IM_COL32(255, 255, 255, static_cast<int>(255.0f * contentVisibility)),
                18.0f);
        } else {
            DrawComparePreview(*m_PreviewProject, fittedImageSize, m_CompareSplit, 18.0f, false, contentVisibility);
        }
    } else {
        const ImVec2 spinnerPos(
            contentMin.x + std::max(0.0f, (previewArea.x - 140.0f) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - 70.0f) * 0.44f));
        ImGui::SetCursorPos(ImVec2(spinnerPos.x - viewport->Pos.x, spinnerPos.y - viewport->Pos.y));
        const char* spinnerLabel = m_PreviewProject->previewStatusText.empty()
            ? "Rendering preview..."
            : m_PreviewProject->previewStatusText.c_str();
        ImGuiExtras::DrawSpinner(spinnerLabel, 18.0f, 4, IM_COL32(220, 220, 220, 240));
    }
    ImGui::SetCursorPos(ImVec2(
        contentMin.x + previewArea.x + gap - viewport->Pos.x,
        contentMin.y - viewport->Pos.y));
    ImGui::BeginChild("LibraryPreviewDetails", sideArea, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
        IM_COL32(255, 255, 255, 10),
        18.0f);
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool detailsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || anyPopupOpen;
    m_ProjectPreviewMenuHover = ImGuiExtras::AnimateTowards(
        m_ProjectPreviewMenuHover,
        detailsHovered ? 1.0f : 0.0f,
        ImGui::GetIO().DeltaTime,
        11.0f);
    const bool detailsExpanded = m_ProjectPreviewMenuHover > 0.30f;

    if (!detailsExpanded) {
        const ImVec2 panelPos = ImGui::GetWindowPos();
        const ImVec2 panelSize = ImGui::GetWindowSize();
        const ImVec2 center(panelPos.x + panelSize.x * 0.5f, panelPos.y + panelSize.y * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - 5.0f, center.y - 12.0f),
            IM_COL32(235, 239, 245, 210),
            "<");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();

        if (m_ProjectPreviewClosing && m_ProjectPreviewTransition <= 0.01f) {
            LibraryManager::Get().CancelProjectPreviewRequests();
            m_PreviewProject = nullptr;
            m_PreviewAsset = nullptr;
            m_RenameTargetFileName.clear();
            m_ProjectPreviewClosing = false;
            m_ProjectPreviewMenuHover = 0.0f;
            m_ProjectPreviewLaunchRect.valid = false;
            if (m_ProjectPreviewRefreshAfterClose) {
                LibraryManager::Get().RequestRefreshLibraryAsync();
                m_ProjectPreviewRefreshAfterClose = false;
            }
        }
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Indent(18.0f);
    const float panelWrapX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 34.0f;
    const float contentWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - 28.0f);

    ImGui::PushTextWrapPos(panelWrapX);
    ImGui::TextWrapped("%s", m_PreviewProject->projectName.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("CREATED");
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextDisabled("%s", m_PreviewProject->timestamp.c_str());
    ImGui::TextDisabled("SIZE");
    ImGui::SameLine(0.0f, 38.0f);
    ImGui::TextDisabled("%dx%d", m_PreviewProject->sourceWidth, m_PreviewProject->sourceHeight);
    ImGui::TextDisabled("FILE");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", m_PreviewProject->fileName.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushItemWidth(contentWidth);
    ImGui::InputText("##renameproject", m_RenameBuffer, sizeof(m_RenameBuffer));
    ImGui::PopItemWidth();
    ImGui::Spacing();

    const bool projectLoadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    const float actionWidth = std::max(108.0f, (contentWidth - 12.0f) * 0.5f);
    const ImVec2 actionSize(actionWidth, 34.0f);

    if (ImGui::Button("Save Name", actionSize)) {
        if (LibraryManager::Get().RenameProject(m_PreviewProject->fileName, m_RenameBuffer)) {
            m_RenameTargetFileName.clear();
            SyncRenameBuffer();
        }
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("Close Preview", actionSize)) {
        m_ProjectPreviewClosing = true;
    }

    ImGui::BeginDisabled(
        m_ProjectPreviewClosing || projectLoadBusy || previewBusy
        || renderProject
        || compositeProject);
    if (ImGui::Button(
            projectLoadBusy
                ? "Loading Project..."
                : ((renderProject || compositeProject) ? "Load Unsupported" : "Load Into Editor"),
            actionSize)) {
        const std::string projectFileName = m_PreviewProject->fileName;
        if (!renderProject && !compositeProject) {
            if (editor != nullptr && editor->IsDirty()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Editor;
                m_ConfirmLoadOpen = true;
            } else {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
                (void)editor;
                (void)activeTab;
                RequestOpenEditorProject(projectFileName);
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("Delete Project...", actionSize)) {
        ImGui::OpenPopup("Confirm Delete Project");
    }

    ImGui::Spacing();
    if (ImGui::Button("Export Project...", ImVec2(contentWidth, 34.0f))) {
        std::string exportPath = FileDialogs::SaveProjectFileDialog("Export Project File", m_PreviewProject->fileName.c_str());
        if (!exportPath.empty()) {
            LibraryManager::Get().ExportProject(m_PreviewProject->fileName, exportPath);
        }
    }

    if (compositeProject) {
        ImGui::Spacing();
        ImGui::TextDisabled("Legacy standalone composite projects are no longer supported.");
    }

    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }

    if (ImGui::BeginPopupModal("Confirm Delete Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static double s_deletePopupOpenedAt = 0.0;
        if (ImGui::IsWindowAppearing()) {
            s_deletePopupOpenedAt = ImGui::GetTime();
        }
        const float dialogProgress = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_deletePopupOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, dialogProgress);
        ImGui::TextWrapped("Delete \"%s\" from the library?", m_PreviewProject ? m_PreviewProject->projectName.c_str() : "this project");
        ImGui::TextDisabled("This removes the saved project file and any linked rendered preview asset from the Library.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            const std::string fileName = m_PreviewProject ? m_PreviewProject->fileName : "";
            if (!fileName.empty()) {
                LibraryManager::Get().DeleteProject(fileName);
                m_ProjectPreviewRefreshAfterClose = true;
                m_ProjectPreviewClosing = true;
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    ImGui::Unindent(18.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();

    if (m_ProjectPreviewClosing && m_ProjectPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelProjectPreviewRequests();
        m_PreviewProject = nullptr;
        m_PreviewAsset = nullptr;
        m_RenameTargetFileName.clear();
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        if (m_ProjectPreviewRefreshAfterClose) {
            LibraryManager::Get().RequestRefreshLibraryAsync();
            m_ProjectPreviewRefreshAfterClose = false;
        }
    }
}

void LibraryModule::RenderAssetPreviewPopup(
    EditorModule* editor,
    CompositeModule* composite,
    int* activeTab) {
    if (!m_PreviewAsset && m_AssetPreviewTransition <= 0.0f) {
        return;
    }

    const float previewProgressTarget = (m_PreviewAsset && !m_AssetPreviewClosing) ? 1.0f : 0.0f;
    m_AssetPreviewTransition = ImGuiExtras::AnimateTowards(m_AssetPreviewTransition, previewProgressTarget, ImGui::GetIO().DeltaTime, kPreviewMotionSpeed);
    const float previewAlpha = ImGuiExtras::EaseOutCubic(m_AssetPreviewTransition);
    const float closeProgress = std::clamp(1.0f - m_AssetPreviewTransition, 0.0f, 1.0f);
    const float delayedContentClose = std::clamp((closeProgress - 0.34f) / 0.66f, 0.0f, 1.0f);
    const float closeContentAmount = ImGuiExtras::EaseOutCubic(delayedContentClose);

    if ((m_AssetPreviewClosing || !m_PreviewAsset) && m_AssetPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelAssetPreviewRequests();
        m_PreviewAsset = nullptr;
        m_PreviewProject = nullptr;
        m_AssetPreviewClosing = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
        return;
    }

    if (!m_PreviewAsset) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowBgAlpha(0.04f + (0.93f * previewAlpha));
    ImGui::Begin("Library Asset Preview", nullptr, flags);

    const bool previewReady = (m_PreviewAsset->fullPreviewTex != 0);
    const bool previewBusy = Async::IsBusy(m_PreviewAsset->previewTaskState);
    const bool projectLoadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    const bool renderLinkedProject = m_PreviewAsset->projectKind == StackBinaryFormat::kRenderProjectKind;
    const bool compositeLinkedProject = m_PreviewAsset->projectKind == StackBinaryFormat::kCompositeProjectKind;
    const float contentProgress = ImGuiExtras::EaseOutCubic(previewAlpha);
    const float contentVisibility = m_AssetPreviewClosing
        ? std::clamp(1.0f - closeContentAmount, 0.0f, 1.0f)
        : std::clamp((contentProgress - 0.08f) / 0.92f, 0.0f, 1.0f);
    const float contentScale = m_AssetPreviewClosing
        ? (1.0f - (0.045f * closeContentAmount))
        : (0.972f + (0.028f * contentProgress));
    const ImVec2 baseContentSize(
        std::min(viewport->Size.x * 0.76f, 1260.0f),
        std::min(viewport->Size.y * 0.70f, 820.0f));
    const ImVec2 contentSize(baseContentSize.x * contentScale, baseContentSize.y * contentScale);
    const float detailsCollapsedWidth = 52.0f;
    const float detailsExpandedWidth = 348.0f;
    const float detailsWidth =
        detailsCollapsedWidth + ((detailsExpandedWidth - detailsCollapsedWidth) * m_AssetPreviewMenuHover);
    const float gap = 18.0f;
    const ImVec2 contentMin(
        viewport->Pos.x + (viewport->Size.x - contentSize.x) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - contentSize.y) * 0.5f + ((1.0f - contentProgress) * 14.0f) + (closeContentAmount * 8.0f));
    const ImVec2 previewArea(std::max(360.0f, contentSize.x - detailsWidth - gap), contentSize.y);
    const ImVec2 sideArea(detailsWidth, contentSize.y);

    const ImVec2 fittedImageSize = FitImageToBounds(
        static_cast<float>(m_PreviewAsset->width),
        static_cast<float>(m_PreviewAsset->height),
        previewArea);
    const ImRect finalImageRect(
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f)),
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f) + fittedImageSize.x,
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f) + fittedImageSize.y));
    const ImRect detailsRect(
        ImVec2(contentMin.x + previewArea.x + gap, contentMin.y),
        ImVec2(contentMin.x + previewArea.x + gap + sideArea.x, contentMin.y + sideArea.y));

    const bool canCloseFromBackdrop = contentProgress >= 0.92f;
    const bool clickedBackdrop =
        canCloseFromBackdrop &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !m_AssetPreviewClosing &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !finalImageRect.Contains(ImGui::GetIO().MousePos) &&
        !detailsRect.Contains(ImGui::GetIO().MousePos);
    if (clickedBackdrop) {
        m_AssetPreviewClosing = true;
    }

    if (!m_AssetPreviewClosing && m_AssetPreviewLaunchRect.valid) {
        const float launchBlend = std::clamp(1.0f - contentProgress, 0.0f, 1.0f);
        if (launchBlend > 0.01f) {
            const ImRect launchRect(
                ImVec2(m_AssetPreviewLaunchRect.minX, m_AssetPreviewLaunchRect.minY),
                ImVec2(m_AssetPreviewLaunchRect.maxX, m_AssetPreviewLaunchRect.maxY));
            const auto lerp = [](float a, float b, float t) { return a + ((b - a) * t); };
            const ImRect animatedRect(
                ImVec2(lerp(launchRect.Min.x, finalImageRect.Min.x, contentProgress), lerp(launchRect.Min.y, finalImageRect.Min.y, contentProgress)),
                ImVec2(lerp(launchRect.Max.x, finalImageRect.Max.x, contentProgress), lerp(launchRect.Max.y, finalImageRect.Max.y, contentProgress)));
            const ImTextureID animTex = previewReady
                ? (ImTextureID)(intptr_t)m_PreviewAsset->fullPreviewTex
                : (ImTextureID)(intptr_t)m_PreviewAsset->thumbnailTex;
            if (animTex) {
                ImGui::GetWindowDrawList()->AddImageRounded(
                    animTex,
                    animatedRect.Min,
                    animatedRect.Max,
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    IM_COL32(255, 255, 255, static_cast<int>(255.0f * launchBlend)),
                    18.0f);
            }
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentVisibility);
    if (previewReady) {
        ImGui::SetCursorPos(ImVec2(
            finalImageRect.Min.x - viewport->Pos.x,
            finalImageRect.Min.y - viewport->Pos.y));
        ImGui::InvisibleButton("##AssetPreviewImageHit", fittedImageSize);
        const ImRect imageRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)(intptr_t)m_PreviewAsset->fullPreviewTex,
            imageRect.Min,
            imageRect.Max,
            ImVec2(0, 1),
            ImVec2(1, 0),
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * contentVisibility)),
            18.0f);
    } else {
        const ImVec2 spinnerPos(
            contentMin.x + std::max(0.0f, (previewArea.x - 140.0f) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - 70.0f) * 0.44f));
        ImGui::SetCursorPos(ImVec2(spinnerPos.x - viewport->Pos.x, spinnerPos.y - viewport->Pos.y));
        const char* spinnerLabel = m_PreviewAsset->previewStatusText.empty()
            ? "Loading full-quality asset..."
            : m_PreviewAsset->previewStatusText.c_str();
        ImGuiExtras::DrawSpinner(spinnerLabel, 18.0f, 4, IM_COL32(220, 220, 220, 240));
    }

    ImGui::SetCursorPos(ImVec2(
        contentMin.x + previewArea.x + gap - viewport->Pos.x,
        contentMin.y - viewport->Pos.y));
    ImGui::BeginChild("LibraryAssetDetails", sideArea, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
        IM_COL32(255, 255, 255, 10),
        18.0f);
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool detailsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || anyPopupOpen;
    m_AssetPreviewMenuHover = ImGuiExtras::AnimateTowards(
        m_AssetPreviewMenuHover,
        detailsHovered ? 1.0f : 0.0f,
        ImGui::GetIO().DeltaTime,
        11.0f);
    const bool detailsExpanded = m_AssetPreviewMenuHover > 0.30f;

    if (!detailsExpanded) {
        const ImVec2 panelPos = ImGui::GetWindowPos();
        const ImVec2 panelSize = ImGui::GetWindowSize();
        const ImVec2 center(panelPos.x + panelSize.x * 0.5f, panelPos.y + panelSize.y * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - 5.0f, center.y - 12.0f),
            IM_COL32(235, 239, 245, 210),
            "<");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();

        if (m_AssetPreviewClosing && m_AssetPreviewTransition <= 0.01f) {
            LibraryManager::Get().CancelAssetPreviewRequests();
            m_PreviewAsset = nullptr;
            m_PreviewProject = nullptr;
            m_AssetPreviewClosing = false;
            m_AssetPreviewMenuHover = 0.0f;
            m_AssetPreviewLaunchRect.valid = false;
        }
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Indent(18.0f);
    const float panelWrapX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 34.0f;
    const float contentWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - 28.0f);

    ImGui::PushTextWrapPos(panelWrapX);
    ImGui::TextWrapped("%s", m_PreviewAsset->displayName.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("CREATED");
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextDisabled("%s", m_PreviewAsset->timestamp.c_str());
    ImGui::TextDisabled("SIZE");
    ImGui::SameLine(0.0f, 38.0f);
    ImGui::TextDisabled("%dx%d", m_PreviewAsset->width, m_PreviewAsset->height);
    ImGui::TextDisabled("FILE");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", m_PreviewAsset->fileName.c_str());
    if (!m_PreviewAsset->projectName.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("LINKED");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_PreviewAsset->projectName.c_str());
    } else if (!m_PreviewAsset->projectFileName.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("LINKED FILE");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_PreviewAsset->projectFileName.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float actionWidth = std::max(108.0f, (contentWidth - 12.0f) * 0.5f);
    const ImVec2 actionSize(actionWidth, 34.0f);
    if (ImGui::Button("Download Asset", actionSize)) {
        std::string exportPath = FileDialogs::SavePngFileDialog("Download Library Asset", m_PreviewAsset->fileName.c_str());
        if (!exportPath.empty()) {
            LibraryManager::Get().ExportAsset(m_PreviewAsset->fileName, exportPath);
        }
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("Close Preview", actionSize)) {
        m_AssetPreviewClosing = true;
    }

    ImGui::BeginDisabled(
        m_AssetPreviewClosing || projectLoadBusy || previewBusy || m_PreviewAsset->projectFileName.empty()
        || renderLinkedProject
        || compositeLinkedProject);
    if (ImGui::Button(
            projectLoadBusy
                ? "Loading Project..."
                : ((renderLinkedProject || compositeLinkedProject) ? "Load Unsupported" : "Load Into Editor"),
            ImVec2(contentWidth, 34.0f))) {
        const std::string projectFileName = m_PreviewAsset->projectFileName;
        if (!renderLinkedProject && !compositeLinkedProject) {
            if (editor != nullptr && editor->IsDirty()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Editor;
                m_ConfirmLoadOpen = true;
            } else {
                m_AssetPreviewClosing = true;
                (void)editor;
                (void)activeTab;
                RequestOpenEditorProject(projectFileName);
            }
        }
    }
    ImGui::EndDisabled();
    if (compositeLinkedProject) {
        ImGui::Spacing();
        ImGui::TextDisabled("Linked legacy composite projects are no longer supported.");
    } else if (m_PreviewAsset->projectFileName.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No linked project metadata was found for this asset.");
    }

    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }

    ImGui::Unindent(18.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();

    if (m_AssetPreviewClosing && m_AssetPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelAssetPreviewRequests();
        m_PreviewAsset = nullptr;
        m_PreviewProject = nullptr;
        m_AssetPreviewClosing = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
    }
}
