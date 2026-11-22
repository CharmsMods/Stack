#pragma once

namespace Stack::Editor {
struct NodeEditorDefinition;
inline constexpr int LightPanelTab=1;
// Keep the persisted IDs for Settings (2) and Assets (3).
inline constexpr int ColorPanelTab=4;
inline bool IsPhotographicTab(int tab) {return tab==LightPanelTab || tab==ColorPanelTab;}
int PreferredPhotographicTab(const NodeEditorDefinition& definition,int currentTab);
bool ShowPhotographicSection(const NodeEditorDefinition& definition,int tab);
}
