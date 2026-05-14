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
    const std::string& saveStatus = LibraryManager::Get().GetSaveStatusText();
    if (!saveStatus.empty()) {
        ImGui::TextDisabled("%s", saveStatus.c_str());
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    } else {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }

    (void)editor->ConsumeSelectedTabFocusRequest();
    m_NodeGraphUI.Render(editor);
}
