#include "EditorSidebar.h"

#include "Editor/EditorModule.h"
#include "Library/LibraryManager.h"
#include <imgui.h>

EditorSidebar::EditorSidebar() {}
EditorSidebar::~EditorSidebar() {}

void EditorSidebar::Initialize() {
    m_NodeGraphUI.Initialize();
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

    (void)editor->ConsumeSelectedTabFocusRequest();
    m_NodeGraphUI.Render(editor);

    ImGui::End();
}
