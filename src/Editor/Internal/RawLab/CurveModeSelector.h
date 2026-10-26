#pragma once
#include <imgui.h>

namespace Stack::Editor::GraphUI { struct GraphPlotStyle; }
namespace Stack::Editor::RawLabInternal {
struct CurveModeSelectorState {
    ImGuiID capture=0;
    int initial=0,lastFrame=-1;
    float position=0,grabOffset=0;
    bool dragging=false;
};
bool DrawCurveControlGlyph(int index,bool selected,const char* tooltip,const GraphUI::GraphPlotStyle&);
bool DrawCurveModeSelector(int& selected,CurveModeSelectorState&,const GraphUI::GraphPlotStyle&);
}
