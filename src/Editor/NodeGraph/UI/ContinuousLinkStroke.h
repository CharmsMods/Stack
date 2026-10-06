#pragma once
#include <imgui.h>
#include <vector>
namespace Stack::Editor::NodeGraphUIVisuals {
struct CurveSample { ImVec2 point; float t; };
std::vector<CurveSample> SampleLinkCurve(ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d);
void DrawContinuousLinkStroke(ImDrawList* draw,ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,
    ImU32 color,float thickness,bool straight,ImVec2 fadeMin,ImVec2 fadeMax,float fadeDistance,
    float gapStart=-1,float gapEnd=-1);
}
