#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

namespace Stack::Editor::NodeGraphUIVisuals {
void DrawGraphNodeSpotlightSurface(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    const ImVec4& fillColor,
    const ImVec4& borderColor,
    const ImVec4& accentColor,
    const GraphStyleTokens& tokens,
    bool selected,
    bool expanded,
    float uiScale,
    float rounding,
    float borderThickness,
    float selectionAlpha) {
    drawList->AddRectFilled(min,max,ImGui::GetColorU32(tokens.enabled ? tokens.nodeAppearance.surface : fillColor),rounding);
    drawList->AddRect(min,max,ImGui::GetColorU32(borderColor),rounding,0,borderThickness);
    ImVec4 outline=tokens.selected; outline.w*=selectionAlpha<0 ? (selected ? 1.0f : 0.0f) : selectionAlpha;
    if (outline.w>0.001f) drawList->AddRect(min,max,ImGui::GetColorU32(outline),rounding,0,2.0f*uiScale);

}

}
