#include "Utils/UiBusyState.h"
#include "LibraryModule.h"

#include "Async/TaskSystem.h"
#include "Composite/CompositeModule.h"
#include "Editor/EditorModule.h"
#include "Library/Internal/LibraryModuleUIHelpers.h"
#include "LibraryManager.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

using namespace Stack::Library::ModuleUI;


namespace {
namespace Notices = Stack::Notifications;

Notices::ActionResult ToActionResult(ConflictResolutionResult result) {
    switch (result.state) {
    case ConflictResolutionState::Resolved: return Notices::ActionResult::Success();
    case ConflictResolutionState::Pending: return Notices::ActionResult::Pending();
    case ConflictResolutionState::Failed: return Notices::ActionResult::Failure(std::move(result.message));
    }
    return Notices::ActionResult::Failure("The import could not be completed.");
}
}

void LibraryModule::SetNotificationScope(Notices::Notifier notifier) {
    m_Notifier = std::move(notifier);
}

void LibraryModule::RenderGlobalPopups() {
    RenderFolderImportPopup();
    UpdateNotificationDecisions();
}

void LibraryModule::UpdateNotificationDecisions() {
    if (!m_Notifier) return;
    auto& manager = LibraryManager::Get();
    const std::weak_ptr<int> lifetime = m_NotificationLifetime;
    for (auto it = m_ConflictNotices.begin(); it != m_ConflictNotices.end();) {
        if (manager.FindConflictIndex(it->first) < 0) {
            m_Notifier.Resolve(it->second);
            it = m_ConflictNotices.erase(it);
        } else ++it;
    }
    for (auto it = m_AssetConflictNotices.begin(); it != m_AssetConflictNotices.end();) {
        if (manager.FindAssetConflictIndex(it->first) < 0) {
            m_Notifier.Resolve(it->second);
            it = m_AssetConflictNotices.erase(it);
        } else ++it;
    }
    for (const auto& conflict : manager.GetPendingConflicts()) {
        const auto id = conflict.id;
        if (m_ConflictNotices.count(id)) continue;
        Notices::NoticeSpec notice;
        notice.title = "Project already exists";
        notice.message = "Choose which version to keep.";
        notice.context = conflict.localName.empty() ? conflict.localProjectFileName : conflict.localName;
        notice.severity = Notices::Severity::Warning;
        notice.dedupeKey = "import-project-conflict-" + std::to_string(id);
        notice.operationId = manager.GetImportOperationId();
        notice.dialogSize = Notices::DialogSize::Large;
        notice.customBody = [this, lifetime, id] {
            if (!lifetime.expired()) DrawImportConflictComparison(id);
        };
        const auto guard = [lifetime, id] {
            return !lifetime.expired() && LibraryManager::Get().FindConflictIndex(id) >= 0;
        };
        for (const auto choice : {ConflictAction::Ignore, ConflictAction::KeepBoth, ConflictAction::Replace}) {
            Notices::ActionSpec action;
            action.label = choice == ConflictAction::Ignore ? "Use existing"
                : choice == ConflictAction::KeepBoth ? "Keep both" : "Replace";
            action.canInvoke = guard;
            action.destructive = choice == ConflictAction::Replace;
            action.defaultAction = choice == ConflictAction::KeepBoth;
            action.invoke = [id, choice] { return ToActionResult(LibraryManager::Get().ResolveConflict(id, choice)); };
            notice.actions.push_back(std::move(action));
        }
        Notices::ActionSpec abort;
        abort.label = "Abort import";
        abort.safeCancel = true;
        abort.canInvoke = guard;
        abort.invoke = [] {
            LibraryManager::Get().ClearConflicts();
            return Notices::ActionResult::Success();
        };
        notice.actions.push_back(std::move(abort));
        m_ConflictNotices.emplace(id, m_Notifier.RequestDecision(std::move(notice)));
    }
    for (const auto& conflict : manager.GetPendingAssetConflicts()) {
        const auto id = conflict.id;
        if (m_AssetConflictNotices.count(id)) continue;
        Notices::NoticeSpec notice;
        notice.title = "Similar asset found";
        notice.message = "Choose which image to keep.";
        notice.context = conflict.importedDisplayName;
        notice.severity = Notices::Severity::Warning;
        notice.dedupeKey = "import-asset-conflict-" + std::to_string(id);
        notice.operationId = conflict.activity.operationId;
        notice.dialogSize = Notices::DialogSize::Large;
        notice.customBody = [this, lifetime, id] {
            if (!lifetime.expired()) DrawAssetConflictComparison(id);
        };
        const auto guard = [lifetime, id] {
            return !lifetime.expired() && LibraryManager::Get().FindAssetConflictIndex(id) >= 0;
        };
        for (const auto choice : {AssetConflictAction::UseExisting, AssetConflictAction::KeepBoth, AssetConflictAction::Replace}) {
            Notices::ActionSpec action;
            action.label = choice == AssetConflictAction::UseExisting ? "Use existing"
                : choice == AssetConflictAction::KeepBoth ? "Keep both" : "Replace";
            action.canInvoke = guard;
            action.destructive = choice == AssetConflictAction::Replace;
            action.defaultAction = choice == AssetConflictAction::KeepBoth;
            action.invoke = [id, choice] { return ToActionResult(LibraryManager::Get().ResolveAssetConflict(id, choice)); };
            notice.actions.push_back(std::move(action));
        }
        m_AssetConflictNotices.emplace(id, m_Notifier.RequestDecision(std::move(notice)));
    }
    RenderDeleteConfirmPopup();
}

void LibraryModule::RequestDeleteItems(std::vector<std::string> fileNames, bool assets) {
    if (fileNames.empty() || !m_Notifier) return;
    struct DeleteRequest { std::vector<std::string> remaining; bool assets = false; };
    const auto request = std::make_shared<DeleteRequest>();
    request->remaining = std::move(fileNames);
    request->assets = assets;
    const std::weak_ptr<int> lifetime = m_NotificationLifetime;
    Notices::NoticeSpec notice;
    notice.title = assets ? "Delete assets?" : "Delete projects?";
    notice.message = "This cannot be undone.";
    notice.details = assets ? "Removes the selected Library images. Original camera files stay in place."
        : "Removes the saved projects and their linked previews. Original camera files stay in place.";
    notice.severity = Notices::Severity::Warning;
    notice.foreground = true;
    notice.operationId = m_Notifier.NewOperation();
    notice.customBody = [request] {
        if (request->remaining.size() == 1) {
            ImGui::TextWrapped("%s", request->remaining.front().c_str());
        } else {
            ImGui::BeginChild("##DeleteItems", ImVec2(0.0f, std::min(180.0f, request->remaining.size() * ImGui::GetTextLineHeightWithSpacing())), true);
            for (const auto& name : request->remaining) ImGui::BulletText("%s", name.c_str());
            ImGui::EndChild();
        }
    };
    Notices::ActionSpec cancel;
    cancel.label = "Cancel";
    cancel.safeCancel = true;
    cancel.defaultAction = true;
    cancel.canInvoke = [lifetime] { return !lifetime.expired(); };
    cancel.invoke = [] { return Notices::ActionResult::Success(); };
    notice.actions.push_back(std::move(cancel));
    Notices::ActionSpec remove;
    remove.label = "Delete";
    remove.destructive = true;
    remove.canInvoke = [lifetime] { return !lifetime.expired(); };
    remove.invoke = [this, lifetime, request] {
        if (lifetime.expired()) return Notices::ActionResult::Failure("The Library view is no longer available.");
        std::vector<std::string> failed;
        for (const auto& name : request->remaining) {
            const bool deleted = request->assets ? LibraryManager::Get().DeleteAsset(name) : LibraryManager::Get().DeleteProject(name);
            if (!deleted) { failed.push_back(name); continue; }
            m_SelectedProjects.erase(name);
            m_SelectedAssets.erase(name);
            if (m_PreviewProject && m_PreviewProject->fileName == name) {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
            }
            if (m_PreviewAsset && m_PreviewAsset->fileName == name) m_AssetPreviewClosing = true;
        }
        request->remaining = std::move(failed);
        LibraryManager::Get().RequestRefreshLibraryAsync();
        if (!request->remaining.empty()) {
            return Notices::ActionResult::Failure("Some items could not be deleted. The remaining items are shown above. Check folder access, then try again.");
        }
        return Notices::ActionResult::Success();
    };
    notice.actions.push_back(std::move(remove));
    m_Notifier.RequestDecision(std::move(notice));
}

void LibraryModule::RenderDeleteConfirmPopup() {
    if (!m_DeleteConfirmOpen) return;
    m_DeleteConfirmOpen = false;
    RequestDeleteItems(std::move(m_PendingDeleteFileNames), m_DeletingAssets);
    m_PendingDeleteFileNames.clear();
}

void LibraryModule::RenderFolderImportPopup() {
    if (m_FolderImportPopupOpen) {
        ImGui::OpenPopup("Import Folder Assets");
        m_FolderImportPopupOpen = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Import Folder Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Selected Folder:");
        ImGui::TextDisabled("%s", m_PendingFolderImportPath.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Select image formats to import:");
        ImGui::Checkbox(".png", &m_ImportExtPng); ImGui::SameLine();
        ImGui::Checkbox(".jpg / .jpeg", &m_ImportExtJpg); ImGui::SameLine();
        ImGui::Checkbox(".bmp", &m_ImportExtBmp); ImGui::SameLine();
        ImGui::Checkbox(".tga", &m_ImportExtTga);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Import", ImVec2(100.0f, 0.0f))) {
            LibraryManager::Get().RequestImportFolderAssets(
                std::filesystem::u8path(m_PendingFolderImportPath),
                m_ImportExtPng, m_ImportExtJpg, m_ImportExtBmp, m_ImportExtTga);

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}



void LibraryModule::DrawImportConflictComparison(std::uint64_t id) {
    auto& manager = LibraryManager::Get();
    const int currentIndex = manager.FindConflictIndex(id);
    if (currentIndex < 0) return;
    const auto& conflict = manager.GetPendingConflicts()[static_cast<std::size_t>(currentIndex)];
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 4.5f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 previewAreaSize(available.x, std::max(100.0f, std::min(320.0f, available.y - detailsHeight - 12.0f)));
        ImGui::BeginChild("ConflictPreview", previewAreaSize, true);
        if (!conflict.previewsReady && !conflict.previewFailed) {
            manager.PrepareConflictPreview(currentIndex);
        } else if (conflict.localPreviewTex && conflict.importedPreviewTex) {
            ImVec2 imageSize = FitImageToBounds(
                static_cast<float>(conflict.localWidth),
                static_cast<float>(conflict.localHeight),
                previewAreaSize);

            ImGui::SetCursorPosX((previewAreaSize.x - imageSize.x) * 0.5f);
            ImGui::SetCursorPosY((previewAreaSize.y - imageSize.y) * 0.5f);

            ImGui::InvisibleButton("##ConflictWipe", imageSize);
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const ImGuiID handleId = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered()) {
                m_ConflictCompareSplit = (ImGui::GetIO().MousePos.x - rect.Min.x) / std::max(1.0f, rect.GetWidth());
                m_ConflictCompareSplit = std::clamp(m_ConflictCompareSplit, 0.0f, 1.0f);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(10, 10, 10, 255));
            drawList->AddImage((ImTextureID)(intptr_t)conflict.localPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));

            float splitX = rect.Min.x + rect.GetWidth() * m_ConflictCompareSplit;
            drawList->PushClipRect(rect.Min, ImVec2(splitX, rect.Max.y), true);
            drawList->AddImage((ImTextureID)(intptr_t)conflict.importedPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));
            drawList->PopClipRect();
            DrawSplitHandle(drawList, rect, m_ConflictCompareSplit, handleId, hovered, active);

            // Labels
            drawList->AddText(ImVec2(rect.Min.x + 15, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "Imported");
            drawList->AddText(ImVec2(rect.Max.x - 120, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "Existing");
        } else if (conflict.previewFailed) {
            ImGui::TextWrapped("%s", conflict.previewStatusText.empty()
                ? "Failed to generate comparison previews for this conflict."
                : conflict.previewStatusText.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Retry preview")) {
                manager.ResetConflictPreview(currentIndex);
            }
        } else {
            ImGui::TextDisabled("Preparing comparison...");
        }
        ImGui::EndChild();

        if (ImGui::CollapsingHeader("Compare details")) {
        ImGui::BeginChild("ConflictDetails", ImVec2(0, detailsHeight), true);
        ImGui::Columns(2, "DetailsSplit", false);
        ImGui::Text("Existing");
        ImGui::TextDisabled("Modified: %s", conflict.localTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.localWidth, conflict.localHeight);

        ImGui::NextColumn();
        ImGui::Text("Imported");
        ImGui::TextDisabled("Modified: %s", conflict.importedTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.importedWidth, conflict.importedHeight);
        if (conflict.areIdentical) {
            ImGui::SameLine();
            ImGui::TextDisabled("Identical");
        }
        ImGui::Columns(1);
        ImGui::EndChild();
        }

}


void LibraryModule::DrawAssetConflictComparison(std::uint64_t id) {
    auto& manager = LibraryManager::Get();
    const int currentIndex = manager.FindAssetConflictIndex(id);
    if (currentIndex < 0) return;
    const auto& conflict = manager.GetPendingAssetConflicts()[static_cast<std::size_t>(currentIndex)];
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 5.5f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 previewAreaSize(available.x, std::max(100.0f, std::min(320.0f, available.y - detailsHeight - 12.0f)));
        ImGui::BeginChild("AssetConflictPreview", previewAreaSize, true);
        if (!conflict.previewsReady) {
            manager.PrepareAssetConflictPreview(currentIndex);
        } else if (conflict.localPreviewTex && conflict.importedPreviewTex) {
            ImVec2 imageSize = FitImageToBounds(
                static_cast<float>(std::max(conflict.localWidth, conflict.importedWidth)),
                static_cast<float>(std::max(conflict.localHeight, conflict.importedHeight)),
                previewAreaSize);

            ImGui::SetCursorPosX((previewAreaSize.x - imageSize.x) * 0.5f);
            ImGui::SetCursorPosY((previewAreaSize.y - imageSize.y) * 0.5f);
            ImGui::InvisibleButton("##AssetConflictWipe", imageSize);
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const ImGuiID handleId = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered()) {
                m_AssetConflictCompareSplit = (ImGui::GetIO().MousePos.x - rect.Min.x) / std::max(1.0f, rect.GetWidth());
                m_AssetConflictCompareSplit = std::clamp(m_AssetConflictCompareSplit, 0.0f, 1.0f);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(10, 10, 10, 255));
            drawList->AddImage((ImTextureID)(intptr_t)conflict.localPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));

            const float splitX = rect.Min.x + rect.GetWidth() * m_AssetConflictCompareSplit;
            drawList->PushClipRect(rect.Min, ImVec2(splitX, rect.Max.y), true);
            drawList->AddImage((ImTextureID)(intptr_t)conflict.importedPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));
            drawList->PopClipRect();
            DrawSplitHandle(drawList, rect, m_AssetConflictCompareSplit, handleId, hovered, active);

            drawList->AddText(ImVec2(rect.Min.x + 15, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "Imported");
            drawList->AddText(ImVec2(rect.Max.x - 130, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "Existing");
        } else {
            ImGui::TextWrapped("Comparison previews are unavailable. You can still choose which asset to keep.");
        }
        ImGui::EndChild();

        if (ImGui::CollapsingHeader("Compare details")) {
        ImGui::BeginChild("AssetConflictDetails", ImVec2(0, detailsHeight), true);
        ImGui::Columns(2, "AssetConflictSplit", false);
        ImGui::Text("Existing");
        ImGui::TextDisabled("%s", conflict.localDisplayName.c_str());
        ImGui::TextDisabled("Saved: %s", conflict.localTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.localWidth, conflict.localHeight);

        ImGui::NextColumn();
        ImGui::Text("Imported");
        ImGui::TextDisabled("%s", conflict.importedDisplayName.c_str());
        ImGui::TextDisabled("Saved: %s", conflict.importedTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.importedWidth, conflict.importedHeight);
        if (conflict.areIdentical) {
            ImGui::SameLine();
            ImGui::TextDisabled("Identical");
        }
        ImGui::Columns(1);
        ImGui::EndChild();
        }

}

void LibraryModule::RenderLibraryMenuOptions(bool importBusy, bool exportBusy) {
    const auto drawMenuIcon = [](unsigned int texture, bool enabled = true) {
        if (texture == 0) return;
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float iconSize = std::min(15.0f, itemMax.y - itemMin.y - 5.0f);
        const ImVec2 iconMin(itemMin.x + 6.0f, itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);
        ImVec4 tint = ImGui::GetStyleColorVec4(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)texture,
            iconMin,
            ImVec2(iconMin.x + iconSize, iconMin.y + iconSize),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            ImGui::ColorConvertFloat4ToU32(tint));
    };
    const auto menuItem = [&drawMenuIcon](const char* label, unsigned int texture, bool enabled = true) {
        const std::string paddedLabel = std::string("        ") + label;
        const bool activated = ImGui::MenuItem(paddedLabel.c_str(), nullptr, false, enabled);
        drawMenuIcon(texture, enabled);
        return activated;
    };
    const auto menuSeparator = []() {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    };

    Stack::UiActivity::BeginDisabledForWork(importBusy);
    if (menuItem("Import Library Bundle...", m_FileOpenIconTex, !importBusy)) {
        std::string path = FileDialogs::OpenLibraryBundleFileDialog("Import Library Bundle");
        if (!path.empty()) {
            LibraryManager::Get().RequestImportLibraryBundle(path);
        }
    }
    if (menuItem("Import Folder Assets...", m_FileFolderIconTex, !importBusy)) {
        std::string path = FileDialogs::OpenFolderDialog("Select Folder for Assets");
        if (!path.empty()) {
            m_PendingFolderImportPath = path;
            m_FolderImportPopupOpen = true;
        }
    }
    ImGui::EndDisabled();

    menuSeparator();

    Stack::UiActivity::BeginDisabledForWork(exportBusy);
    if (menuItem("Export Library Bundle...", m_FileSaveIconTex, !exportBusy)) {
        std::string path = FileDialogs::SaveLibraryBundleFileDialog("Export Library Bundle", "modular_studio_library.stacklib");
        if (!path.empty()) {
            LibraryManager::Get().RequestExportLibraryBundle(path);
        }
    }
    ImGui::EndDisabled();

    menuSeparator();

    if (menuItem("Refresh Now", m_FileNewIconTex)) {
        LibraryManager::Get().RequestRefreshLibraryAsync();
    }
}
