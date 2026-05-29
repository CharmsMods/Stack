#pragma once

#include "Editor/NodeGraph/EditorNodeGraphUI.h"

class EditorModule;

class EditorSidebar {
public:
    EditorSidebar();
    ~EditorSidebar();

    void Initialize();
    void Render(EditorModule* editor);

    EditorNodeGraphUI& GetNodeGraphUI() { return m_NodeGraphUI; }
    const EditorNodeGraphUI& GetNodeGraphUI() const { return m_NodeGraphUI; }

private:
    void RenderExportSettings(EditorModule* editor);
    void RenderSettings(EditorModule* editor);
    void RenderComplexNodeSettings(EditorModule* editor);

    EditorNodeGraphUI m_NodeGraphUI;
};
