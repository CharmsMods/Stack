#pragma once
#include "Raw/MaskAuthoring.h"
#include <imgui.h>
namespace Stack::Editor {
const char* MaskToolName(MaskAuthoring::Tool tool);
ImGuiKey MaskToolKey(MaskAuthoring::Tool tool);
bool MaskToolChordDown();
bool BindMaskTool(MaskAuthoring::Tool tool,ImGuiKey key);
void DrawMaskGlyph(ImDrawList*,MaskAuthoring::Tool,ImVec2 center,float radius,ImU32 color);
}

