#pragma once

#include <imgui.h>

class EditorModule;

class EditorAdvancedNodeEditor {
public:
    EditorAdvancedNodeEditor();
    ~EditorAdvancedNodeEditor();

    void Initialize();
    void Render(EditorModule* editor, const ImVec2& workspacePos, const ImVec2& workspaceSize);

private:
    float m_VisibleAlpha = 0.0f;
    bool m_CloseRequested = false;
};
