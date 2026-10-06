#include "Editor/EditorModule.h"

void EditorModule::SetGraphCatalogHost(const ImVec2& position, const ImVec2& size,
    bool expanded, float visibleWidth) {
    m_Sidebar.GetNodeGraphUI().SetCatalogHost(position, size, expanded, visibleWidth);
}
