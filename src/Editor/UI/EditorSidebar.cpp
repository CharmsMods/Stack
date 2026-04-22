#include "EditorSidebar.h"

#include "Editor/EditorModule.h"
#include "Library/LibraryManager.h"
#include <imgui.h>

EditorSidebar::EditorSidebar() {}
EditorSidebar::~EditorSidebar() {}

void EditorSidebar::Initialize() {
    m_LayersTab.Initialize();
    m_CanvasTab.Initialize();
    m_SelectedTab.Initialize();
    m_PipelineTab.Initialize();
}

void EditorSidebar::Render(EditorModule* editor) {
    ImGui::Begin("Inspector Panel Sidebar");

    const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState());
    ImGui::BeginDisabled(saveBusy);
    if (ImGui::Button(saveBusy ? "SAVING TO LIBRARY..." : "SAVE TO LIBRARY", ImVec2(-1, 0))) {
        const std::string projectName = editor->GetCurrentProjectName().empty()
            ? "New Project"
            : editor->GetCurrentProjectName();
        LibraryManager::Get().RequestSaveProject(projectName, editor, editor->GetCurrentProjectFileName());
    }
    ImGui::EndDisabled();

    if (!LibraryManager::Get().GetSaveStatusText().empty()) {
        ImGui::TextDisabled("%s", LibraryManager::Get().GetSaveStatusText().c_str());
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("SidebarTabs")) {
        if (ImGui::BeginTabItem("Layers")) {
            m_LayersTab.Render(editor);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Canvas")) {
            m_CanvasTab.Render(editor);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Selected")) {
            m_SelectedTab.Render(editor);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Pipeline")) {
            m_PipelineTab.Render(editor);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
