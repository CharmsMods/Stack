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

void LibraryModule::RenderGlobalPopups() {
    RenderConfirmLoadPopup();
    RenderFolderImportPopup();
    RenderImportConflictPopup();
    RenderAssetConflictPopup();
    RenderDeleteConfirmPopup();
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
            bool importPng = m_ImportExtPng;
            bool importJpg = m_ImportExtJpg;
            bool importBmp = m_ImportExtBmp;
            bool importTga = m_ImportExtTga;
            std::string path = m_PendingFolderImportPath;

            Async::TaskSystem::Get().Submit([path, importPng, importJpg, importBmp, importTga]() {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (!entry.is_regular_file()) continue;

                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    bool shouldImport = false;
                    if (ext == ".png" && importPng) shouldImport = true;
                    if ((ext == ".jpg" || ext == ".jpeg") && importJpg) shouldImport = true;
                    if (ext == ".bmp" && importBmp) shouldImport = true;
                    if (ext == ".tga" && importTga) shouldImport = true;

                    if (shouldImport) {
                        FILE* f = nullptr;
#ifdef _WIN32
                        _wfopen_s(&f, entry.path().wstring().c_str(), L"rb");
#else
                        f = fopen(entry.path().string().c_str(), "rb");
#endif
                        if (f) {
                            fseek(f, 0, SEEK_END);
                            long size = ftell(f);
                            fseek(f, 0, SEEK_SET);

                            if (size > 0) {
                                std::vector<unsigned char> fileBytes(size);
                                fread(fileBytes.data(), 1, size, f);
                                fclose(f);

                                LibraryManager::Get().QueueLooseAssetSave(
                                    entry.path().stem().string(),
                                    fileBytes,
                                    entry.path().filename().string());
                            } else {
                                fclose(f);
                            }
                        }
                    }
                }
            });

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void LibraryModule::RenderConfirmLoadPopup() {
    if (m_ConfirmLoadOpen) {
        ImGui::OpenPopup("Project Already Open##Library");
        m_ConfirmLoadOpen = false;
    }

    if (m_SaveNamePromptOpen) {
        ImGui::OpenPopup("Save New Project##Library");
        m_SaveNamePromptOpen = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    static double s_projectAlreadyOpenOpenedAt = 0.0;
    static double s_saveNameOpenedAt = 0.0;

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Already Open##Library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            s_projectAlreadyOpenOpenedAt = ImGui::GetTime();
        }
        const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_projectAlreadyOpenOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
        ImGui::TextWrapped("There is already a project open in the %s tab. Would you like to save it before loading the new project?", m_PendingLoadTarget == PendingLoadTarget::Editor ? "Editor" : "Composite");
        ImGui::Spacing();

        if (ImGui::Button("Save & Load", ImVec2(140.0f, 0.0f))) {
            bool needsName = false;
            if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                if (m_CachedEditor->GetCurrentProjectFileName().empty() &&
                    !m_CachedEditor->IsRawWorkspaceProjectActive()) {
                    needsName = true;
                } else {
                    m_CachedEditor->RequestSaveCurrentProject(
                        m_CachedEditor->GetCurrentProjectName(),
                        [this](bool success) {
                            if (!success || !m_CachedEditor) {
                                return;
                            }
                            if (m_PreviewProject) {
                                m_ProjectPreviewClosing = true;
                                m_ProjectPreviewRefreshAfterClose = false;
                            }
                            if (m_PreviewAsset) {
                                m_AssetPreviewClosing = true;
                            }
                            RequestOpenEditorProject(m_PendingLoadProjectFileName);
                        });
                }
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                if (m_CachedComposite->GetCurrentProjectFileName().empty()) {
                    needsName = true;
                } else {
                    LibraryManager::Get().RequestSaveCompositeProject(
                        m_CachedComposite->GetCurrentProjectName(),
                        m_CachedComposite,
                        m_CachedComposite->GetCurrentProjectFileName(),
                        [this](bool success) {
                            if (!success || !m_CachedComposite) {
                                return;
                            }
                            if (m_PreviewProject) {
                                m_ProjectPreviewClosing = true;
                                m_ProjectPreviewRefreshAfterClose = false;
                            }
                            if (m_PreviewAsset) {
                                m_AssetPreviewClosing = true;
                            }
                            LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool loadSuccess) {
                                if (loadSuccess && m_CachedActiveTab) *m_CachedActiveTab = 2;
                            });
                        });
                }
            }

            if (needsName) {
                m_SaveNamePromptOpen = true;
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Discard & Load", ImVec2(140.0f, 0.0f))) {
            if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                if (m_PreviewProject) {
                    m_ProjectPreviewClosing = true;
                    m_ProjectPreviewRefreshAfterClose = false;
                }
                if (m_PreviewAsset) {
                    m_AssetPreviewClosing = true;
                }
                RequestOpenEditorProject(m_PendingLoadProjectFileName);
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                if (m_PreviewProject) {
                    m_ProjectPreviewClosing = true;
                    m_ProjectPreviewRefreshAfterClose = false;
                }
                if (m_PreviewAsset) {
                    m_AssetPreviewClosing = true;
                }
                LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool success) {
                    if (success && m_CachedActiveTab) *m_CachedActiveTab = 2;
                });
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save New Project##Library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            s_saveNameOpenedAt = ImGui::GetTime();
        }
        const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_saveNameOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
        ImGui::Text("Enter a name for the current project:");
        ImGui::Spacing();
        ImGui::InputText("##ProjectName", m_SaveNameBuffer, sizeof(m_SaveNameBuffer));
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            std::string newName = m_SaveNameBuffer;
            if (newName.empty()) {
                newName = "Untitled Project";
            }

            if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                m_CachedEditor->RequestSaveCurrentProject(newName, [this](bool success) {
                    if (!success || !m_CachedEditor) {
                        return;
                    }
                    if (m_PreviewProject) {
                        m_ProjectPreviewClosing = true;
                        m_ProjectPreviewRefreshAfterClose = false;
                    }
                    if (m_PreviewAsset) {
                        m_AssetPreviewClosing = true;
                    }
                    RequestOpenEditorProject(m_PendingLoadProjectFileName);
                });
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                LibraryManager::Get().RequestSaveCompositeProject(newName, m_CachedComposite, "", [this](bool success) {
                    if (!success || !m_CachedComposite) {
                        return;
                    }
                    if (m_PreviewProject) {
                        m_ProjectPreviewClosing = true;
                        m_ProjectPreviewRefreshAfterClose = false;
                    }
                    if (m_PreviewAsset) {
                        m_AssetPreviewClosing = true;
                    }
                    LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool loadSuccess) {
                        if (loadSuccess && m_CachedActiveTab) *m_CachedActiveTab = 2;
                    });
                });
            }

            m_SaveNameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}

void LibraryModule::RenderImportConflictPopup() {
    auto& manager = LibraryManager::Get();
    if (!manager.HasPendingConflicts()) return;

    const char* popupName = "Import Conflict Resolution";
    if (!ImGui::IsPopupOpen(popupName)) {
        ImGui::OpenPopup(popupName);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.9f, viewport->Size.y * 0.9f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const auto& conflicts = manager.GetPendingConflicts();
        int currentIndex = 0;
        auto& conflict = const_cast<ImportConflict&>(conflicts[currentIndex]);

        ImGui::BeginChild("ConflictHeader", ImVec2(0, 80), true);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // TODO: Use a larger font if available
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "IMPORT CONFLICT DETECTED");
        ImGui::PopFont();
        ImGui::Text("Project \"%s\" already exists in your library. Choose how to proceed.", conflict.localName.c_str());
        ImGui::TextDisabled("Conflict 1 of %d remaining", (int)conflicts.size());
        ImGui::EndChild();

        const float detailsHeight = 100.0f;
        const float footerHeight = 60.0f;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 previewAreaSize(available.x, available.y - detailsHeight - footerHeight - 20.0f);

        // Preview Area with Wipe Slider
        ImGui::BeginChild("ConflictPreview", previewAreaSize, true);
        if (!conflict.previewsReady && !conflict.previewFailed) {
            manager.PrepareConflictPreview(currentIndex);
            ImGuiExtras::DrawSpinner("Generating comparison previews...", 20.0f, 4, IM_COL32(200, 200, 200, 255));
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
            drawList->AddText(ImVec2(rect.Min.x + 15, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "NEW / IMPORTING");
            drawList->AddText(ImVec2(rect.Max.x - 120, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "CURRENT / LOCAL");
        } else if (conflict.previewFailed) {
            ImGui::TextWrapped("%s", conflict.previewStatusText.empty()
                ? "Failed to generate comparison previews for this conflict."
                : conflict.previewStatusText.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Retry Preview Generation")) {
                manager.ResetConflictPreview(currentIndex);
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Preview generation is still pending.");
        }
        ImGui::EndChild();

        // Comparison Details
        ImGui::BeginChild("ConflictDetails", ImVec2(0, detailsHeight), true);
        ImGui::Columns(2, "DetailsSplit", false);
        ImGui::Text("LOCAL (Existing)");
        ImGui::TextDisabled("Modified: %s", conflict.localTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.localWidth, conflict.localHeight);

        ImGui::NextColumn();
        ImGui::Text("IMPORTED (Incoming)");
        ImGui::TextDisabled("Modified: %s", conflict.importedTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.importedWidth, conflict.importedHeight);
        if (conflict.areIdentical) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ IDENTICAL ]");
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        // Action Buttons
        ImGui::Spacing();
        if (ImGui::Button("Ignore (Skip This)", ImVec2(180, 40))) {
            manager.ResolveConflict(currentIndex, ConflictAction::Ignore);
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace Local Version", ImVec2(220, 40))) {
            manager.ResolveConflict(currentIndex, ConflictAction::Replace);
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Both (Import as Copy)", ImVec2(220, 40))) {
            manager.ResolveConflict(currentIndex, ConflictAction::KeepBoth);
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 170);
        if (ImGui::Button("Abort Import", ImVec2(150, 40))) {
            manager.ClearConflicts();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

void LibraryModule::RenderAssetConflictPopup() {
    auto& manager = LibraryManager::Get();
    if (!manager.HasPendingAssetConflicts()) return;

    const char* popupName = "Library Asset Conflict Resolution";
    if (!ImGui::IsPopupOpen(popupName)) {
        ImGui::OpenPopup(popupName);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.88f, viewport->Size.y * 0.84f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const auto& conflicts = manager.GetPendingAssetConflicts();
        int currentIndex = 0;
        auto& conflict = const_cast<AssetImportConflict&>(conflicts[currentIndex]);

        ImGui::BeginChild("AssetConflictHeader", ImVec2(0, 82), true);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "ASSET CONFLICT DETECTED");
        ImGui::Text("A similar Library asset already exists. Choose how to proceed.");
        ImGui::TextDisabled("Conflict 1 of %d remaining", static_cast<int>(conflicts.size()));
        ImGui::EndChild();

        const float detailsHeight = 112.0f;
        const float footerHeight = 68.0f;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 previewAreaSize(available.x, available.y - detailsHeight - footerHeight - 20.0f);

        ImGui::BeginChild("AssetConflictPreview", previewAreaSize, true);
        if (!conflict.previewsReady) {
            manager.PrepareAssetConflictPreview(currentIndex);
            ImGuiExtras::DrawSpinner("Generating asset comparison previews...", 20.0f, 4, IM_COL32(200, 200, 200, 255));
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

            drawList->AddText(ImVec2(rect.Min.x + 15, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "NEW / IMPORTING");
            drawList->AddText(ImVec2(rect.Max.x - 130, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "CURRENT / LOCAL");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to generate previews for this asset conflict.");
        }
        ImGui::EndChild();

        ImGui::BeginChild("AssetConflictDetails", ImVec2(0, detailsHeight), true);
        ImGui::Columns(2, "AssetConflictSplit", false);
        ImGui::Text("LOCAL (Existing)");
        ImGui::TextDisabled("%s", conflict.localDisplayName.c_str());
        ImGui::TextDisabled("Saved: %s", conflict.localTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.localWidth, conflict.localHeight);

        ImGui::NextColumn();
        ImGui::Text("IMPORTED (Incoming)");
        ImGui::TextDisabled("%s", conflict.importedDisplayName.c_str());
        ImGui::TextDisabled("Saved: %s", conflict.importedTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.importedWidth, conflict.importedHeight);
        if (conflict.areIdentical) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ IDENTICAL ]");
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGui::Button("Use Existing Asset", ImVec2(190, 40))) {
            manager.ResolveAssetConflict(currentIndex, AssetConflictAction::UseExisting);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace Existing Asset", ImVec2(210, 40))) {
            manager.ResolveAssetConflict(currentIndex, AssetConflictAction::Replace);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Both (Import Copy)", ImVec2(220, 40))) {
            manager.ResolveAssetConflict(currentIndex, AssetConflictAction::KeepBoth);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

void LibraryModule::RenderLibraryMenuOptions(bool importBusy, bool exportBusy) {
    ImGui::BeginDisabled(importBusy);
    if (ImGui::MenuItem("Import Library Bundle...")) {
        std::string path = FileDialogs::OpenLibraryBundleFileDialog("Import Library Bundle");
        if (!path.empty()) {
            LibraryManager::Get().RequestImportLibraryBundle(path);
        }
    }
    if (ImGui::MenuItem("Import Folder Assets...")) {
        std::string path = FileDialogs::OpenFolderDialog("Select Folder for Assets");
        if (!path.empty()) {
            m_PendingFolderImportPath = path;
            m_FolderImportPopupOpen = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::BeginDisabled(exportBusy);
    if (ImGui::MenuItem("Export Library Bundle...")) {
        std::string path = FileDialogs::SaveLibraryBundleFileDialog("Export Library Bundle", "modular_studio_library.stacklib");
        if (!path.empty()) {
            LibraryManager::Get().RequestExportLibraryBundle(path);
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::MenuItem("Refresh Now")) {
        LibraryManager::Get().RequestRefreshLibraryAsync();
    }
}

void LibraryModule::RenderDeleteConfirmPopup() {
    if (m_DeleteConfirmOpen) {
        ImGui::OpenPopup("Confirm Delete");
        m_DeleteConfirmOpen = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    static double s_deleteConfirmOpenedAt = 0.0;
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            s_deleteConfirmOpenedAt = ImGui::GetTime();
        }
        const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_deleteConfirmOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
        const char* itemType = m_DeletingAssets ? "asset(s)" : "project(s)";
        ImGui::Text("Are you sure you want to permanently delete %d %s?", (int)m_PendingDeleteFileNames.size(), itemType);
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        float listHeight = std::min((float)m_PendingDeleteFileNames.size() * 20.0f, 200.0f);
        ImGui::BeginChild("DeleteList", ImVec2(400, listHeight), true);
        for (const auto& fn : m_PendingDeleteFileNames) {
            ImGui::BulletText("%s", fn.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "This action cannot be undone.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            const bool deletingCurrentProjectPreview =
                m_PreviewProject && std::find(m_PendingDeleteFileNames.begin(), m_PendingDeleteFileNames.end(), m_PreviewProject->fileName) != m_PendingDeleteFileNames.end();
            const bool deletingCurrentAssetPreview =
                m_PreviewAsset && std::find(m_PendingDeleteFileNames.begin(), m_PendingDeleteFileNames.end(), m_PreviewAsset->fileName) != m_PendingDeleteFileNames.end();
            for (const auto& fn : m_PendingDeleteFileNames) {
                if (m_DeletingAssets) {
                    LibraryManager::Get().DeleteAsset(fn);
                } else {
                    LibraryManager::Get().DeleteProject(fn);
                }
            }
            LibraryManager::Get().RequestRefreshLibraryAsync();
            m_SelectedProjects.clear();
            m_SelectedAssets.clear();
            m_PendingDeleteFileNames.clear();
            if (deletingCurrentProjectPreview) {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
            }
            if (deletingCurrentAssetPreview) {
                m_AssetPreviewClosing = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_PendingDeleteFileNames.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}
