#pragma once

#include "Editor/NodeGraph/EditorNodeGraphUI.h"

class EditorModule;

class EditorSidebar {
public:
    EditorSidebar();
    ~EditorSidebar();

    void Initialize();
    void Render(EditorModule* editor);

private:
    EditorNodeGraphUI m_NodeGraphUI;
};
