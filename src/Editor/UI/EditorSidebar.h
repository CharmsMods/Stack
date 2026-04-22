#pragma once

#include "Tabs/LayersTab.h"
#include "Tabs/CanvasTab.h"
#include "Tabs/SelectedTab.h"
#include "Tabs/PipelineTab.h"

class EditorModule;

class EditorSidebar {
public:
    EditorSidebar();
    ~EditorSidebar();

    void Initialize();
    void Render(EditorModule* editor);

private:
    LayersTab m_LayersTab;
    CanvasTab m_CanvasTab;
    SelectedTab m_SelectedTab;
    PipelineTab m_PipelineTab;
};
