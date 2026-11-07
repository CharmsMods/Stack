#include "Editor/Internal/RawLab/CurveCanvasNavigation.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
void NavigateCurveCanvas(CurveCanvasState& state,const ImRect& box,bool hovered,bool active,
    bool enabled,bool editing,bool diagonal) {
    auto& view=state.framing;
    auto& io=ImGui::GetIO();
    const bool inside=hovered && box.Contains(io.MousePos);
    // Bend keeps its wheel-width gesture; Ctrl+wheel always navigates the view.
    const bool zoomWheel=state.mode!=CurveGestureMode::Bend || io.KeyCtrl;
    if(enabled && inside && zoomWheel && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) &&
        !editing && !state.viewPanning && !io.MouseDown[1] && !io.MouseDown[2] && io.MouseWheel!=0) {
        const float factor=std::pow(1.2f,-std::clamp(io.MouseWheel,-10.f,10.f));
        const auto zoom=[&](RawLabToneGraphViewRange range,float anchor) {
            const float span=range.maximum-range.minimum;
            const float nextSpan=std::clamp(span*factor,1.f/256,1.f);
            const float minimum=range.minimum+std::clamp(anchor,0.f,1.f)*(span-nextSpan);
            return RawLabToneGraphViewRange{minimum,minimum+nextSpan};
        };
        const auto horizontal=zoom(view.horizontal,(io.MousePos.x-box.Min.x)/box.GetWidth());
        const auto vertical=diagonal?zoom(view.vertical,(box.Max.y-io.MousePos.y)/box.GetHeight()):view.vertical;
        SetCurveCanvasView(view,horizontal,vertical);view.fitPending=false;
    }
    if(enabled && inside && active && !editing && !io.MouseDown[0] && !io.MouseDown[1] && ImGui::IsMouseClicked(2)) {
        state.viewPanning=true;state.viewPress=io.MousePos;
        state.beforeViewX=view.horizontal;state.beforeViewY=view.vertical;
        view.fitPending=false;
    }
    if(!state.viewPanning)return;
    const bool cancel=!enabled || io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
        (io.MouseDown[2] && (!active || io.MouseDown[0] || io.MouseDown[1]));
    if(cancel) {
        SetCurveCanvasView(view,state.beforeViewX,state.beforeViewY);
        state.viewPanning=false;if(active)ImGui::ClearActiveID();return;
    }
    if(io.MouseDown[2] || ImGui::IsMouseReleased(2)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        const float dx=-(io.MousePos.x-state.viewPress.x)/box.GetWidth()*(state.beforeViewX.maximum-state.beforeViewX.minimum);
        const float dy=diagonal?(io.MousePos.y-state.viewPress.y)/box.GetHeight()*(state.beforeViewY.maximum-state.beforeViewY.minimum):0.f;
        const RawLabToneGraphViewRange horizontal{state.beforeViewX.minimum+dx,state.beforeViewX.maximum+dx};
        const RawLabToneGraphViewRange vertical{state.beforeViewY.minimum+dy,state.beforeViewY.maximum+dy};
        if(horizontal.minimum!=view.horizontal.minimum || vertical.minimum!=view.vertical.minimum)
            SetCurveCanvasView(view,horizontal,vertical);
    }
    if(!io.MouseDown[2])state.viewPanning=false;
}
void DrawCurveCanvasViewControl(CurveCanvasFraming& state,const GraphUI::GraphPlotStyle& style) {
    if(DrawCurveControlGlyph(5,false,
        "Graph view. Wheel to zoom at the pointer; middle-drag to pan. In Bend mode, use Ctrl+wheel to zoom.",style))
        ImGui::OpenPopup("curve-view");
    if(ImGui::BeginPopup("curve-view")) {
        if(ImGui::MenuItem("Fit image range"))state.request=CurveCanvasFraming::Request::Fit;
        if(ImGui::MenuItem("Full range"))state.request=CurveCanvasFraming::Request::Full;
        ImGui::EndPopup();
    }
}
}
