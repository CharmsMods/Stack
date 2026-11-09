#include "Editor/Internal/RawLab/CurveCanvasNavigation.h"
#include "Library/LibraryControls.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
void NavigateCurveCanvas(CurveCanvasState& state,const ImRect& box,bool hovered,bool active,
    bool enabled,bool editing,bool diagonal) {
    auto& view=state.framing;
    auto& io=ImGui::GetIO();
    if (!enabled || editing || state.viewPanning || io.AppFocusLost) state.zoomActive=false;
    if (state.zoomActive) {
        const float t=std::clamp(static_cast<float>((ImGui::GetTime()-state.zoomStarted)/.16),0.f,1.f);
        const float blend=1.f-std::pow(1.f-t,3.f);
        const auto mix=[&](RawLabToneGraphViewRange from,RawLabToneGraphViewRange to) {
            return RawLabToneGraphViewRange{from.minimum+(to.minimum-from.minimum)*blend,
                from.maximum+(to.maximum-from.maximum)*blend};
        };
        SetCurveCanvasView(view,mix(state.zoomFromX,state.zoomTargetX),mix(state.zoomFromY,state.zoomTargetY));
        if(t>=1.f) {SetCurveCanvasView(view,state.zoomTargetX,state.zoomTargetY);state.zoomActive=false;}
    }
    const bool inside=hovered && box.Contains(io.MousePos);
    // Bend keeps its wheel-width gesture; Ctrl+wheel always navigates the view.
    const bool zoomWheel=state.mode!=CurveGestureMode::Bend || io.KeyCtrl;
    if(enabled && inside && zoomWheel && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) &&
        !editing && !state.viewPanning && !io.MouseDown[1] && !io.MouseDown[2] && io.MouseWheel!=0) {
        const float factor=std::pow(1.2f,-std::clamp(io.MouseWheel,-10.f,10.f));
        const auto zoom=[&](RawLabToneGraphViewRange range,float anchor) {
            const float span=range.maximum-range.minimum;
            const float nextSpan=std::clamp(span*factor,1.f/256,1.f);
            const float minimum=std::clamp(range.minimum+std::clamp(anchor,0.f,1.f)*(span-nextSpan),0.f,1.f-nextSpan);
            return RawLabToneGraphViewRange{minimum,minimum+nextSpan};
        };
        const auto horizontal=zoom(state.zoomActive?state.zoomTargetX:view.horizontal,(io.MousePos.x-box.Min.x)/box.GetWidth());
        const auto vertical=diagonal?zoom(state.zoomActive?state.zoomTargetY:view.vertical,(box.Max.y-io.MousePos.y)/box.GetHeight()):view.vertical;
        state.zoomFromX=view.horizontal;state.zoomFromY=view.vertical;
        state.zoomTargetX=horizontal;state.zoomTargetY=vertical;
        state.zoomStarted=ImGui::GetTime();state.zoomActive=!LibraryUi::ReducedMotion();
        if(!state.zoomActive)SetCurveCanvasView(view,horizontal,vertical);
        view.fitPending=false;
    }
    if(enabled && inside && active && !editing && !io.MouseDown[0] && !io.MouseDown[1] && ImGui::IsMouseClicked(2)) {
        state.zoomActive=false;
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
