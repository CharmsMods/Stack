#include "GraphCursor.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ImGuiExtras {
namespace {
constexpr float pi=3.14159265358979323846f;
struct CursorSurface { ImRect rect; ImVec4 color; };
struct FrameRequest {
    const void* graph=nullptr;
    ImVec2 min{},max{},anchor{},releaseTarget{};
    bool released=false;
    bool hovered=false,valueHovered=false,dragging=false;
    ImGuiID value=0;
    ImVec4 surface{},accent{};
    StackAppearance::ResolvedCreamPalette palette{};
    ImGuiViewport* viewport=nullptr;
    std::vector<CursorSurface> surfaces;
} request;
struct Motion {
    const void* graph=nullptr;
    ImGuiID value=0;
    ImVec2 position{},from{},anchor{};
    float progress=0,rotation=0,glow=0,morph=0,returnMorph=0;
    enum Phase { Idle,Outgoing,Adjusting,Returning } phase=Idle;
} motion;
ImGuiContext* context=nullptr;
GraphCursorSnapshot snapshot;
float Ease(float t) { t=std::clamp(t,0.0f,1.0f); return t*t*(3-2*t); }
ImVec4 Alpha(ImVec4 c,float a) { c.w*=a; return c; }
void ColorWheel(ImDrawList* draw,ImVec2 center,float radius,float rotation,const ImVec4* colors,float opacity) {
    // Internal edges share positions with no antialias fringe. Only the disc's
    // circumference fades, so adjacent sectors cannot show canvas-colored seams.
    constexpr int segments=72;
    const auto uv=ImGui::GetFontTexUvWhitePixel();
    draw->PrimReserve(segments*9,segments*5);
    for (int i=0;i<segments;++i) {
        const float a=rotation+i*(2*pi/segments),b=rotation+(i+1)*(2*pi/segments);
        const ImVec2 u(std::cos(a),std::sin(a)),v(std::cos(b),std::sin(b));
        const ImU32 color=ImGui::GetColorU32(Alpha(colors[i/12],opacity));
        const ImU32 clear=color&~IM_COL32_A_MASK;
        const auto base=static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
        draw->PrimWriteIdx(base); draw->PrimWriteIdx(base+1); draw->PrimWriteIdx(base+2);
        draw->PrimWriteIdx(base+1); draw->PrimWriteIdx(base+3); draw->PrimWriteIdx(base+4);
        draw->PrimWriteIdx(base+1); draw->PrimWriteIdx(base+4); draw->PrimWriteIdx(base+2);
        draw->PrimWriteVtx(center,uv,color);
        draw->PrimWriteVtx(ImVec2(center.x+u.x*radius,center.y+u.y*radius),uv,color);
        draw->PrimWriteVtx(ImVec2(center.x+v.x*radius,center.y+v.y*radius),uv,color);
        draw->PrimWriteVtx(ImVec2(center.x+u.x*(radius+1),center.y+u.y*(radius+1)),uv,clear);
        draw->PrimWriteVtx(ImVec2(center.x+v.x*(radius+1),center.y+v.y*(radius+1)),uv,clear);
    }
}
}
void BeginGraphCursorFrame() {
    if (context!=ImGui::GetCurrentContext()) { context=ImGui::GetCurrentContext(); motion={}; }
    auto surfaces=std::move(request.surfaces); surfaces.clear();
    request={}; request.surfaces=std::move(surfaces); snapshot.visible=false;
}
void ConfigureGraphCursor(const void* graph,ImVec2 min,ImVec2 max,bool hovered,
    const StackAppearance::ResolvedCreamPalette& palette,ImGuiViewport* viewport) {
    request.graph=graph; request.min=min; request.max=max; request.hovered=hovered;
    request.palette=palette; request.surface=palette.workspace.background; request.viewport=viewport;
}
void RegisterGraphCursorSurface(const void* graph,ImVec2 min,ImVec2 max,ImVec4 surface) { if (graph==request.graph) request.surfaces.push_back({ImRect(min,max),surface}); }
void RequestGraphValueCursor(const void* graph,ImGuiID value,bool hovered,bool dragging,ImVec2 anchor,ImVec4 accent) {
    if (graph!=request.graph || (!hovered && !dragging)) return;
    request.value=value; request.valueHovered=hovered; request.dragging=dragging;
    request.anchor=anchor; request.accent=accent;
}
void SetGraphCursorReleaseTarget(ImVec2 position) { request.releaseTarget=position; request.released=true; }
GraphCursorSnapshot GetGraphCursorSnapshot() { return snapshot; }
bool RenderGraphCursor(bool focused,bool pointerCaptured,bool softwarePreview) {
    auto& io=ImGui::GetIO();
    const bool blocked=!focused || io.AppFocusLost || !request.graph ||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel) || io.WantTextInput;
    if (blocked || (motion.graph && motion.graph!=request.graph)) { motion={}; snapshot={}; if (blocked) return false; }
    const ImVec2 pointer=request.released ? request.releaseTarget : io.MousePos;
    if (!ImGui::IsMousePosValid(&pointer)) { motion={}; snapshot={}; return false; }
    const float dt=std::clamp(io.DeltaTime,0.0f,0.05f);
    const float scale=request.viewport ? request.viewport->DpiScale : 1.0f;
    const bool dragging=request.dragging && io.MouseDown[0];
    if (dragging && (motion.phase==Motion::Idle || motion.phase==Motion::Returning || motion.value!=request.value)) {
        motion.graph=request.graph; motion.value=request.value; motion.from=pointer;
        motion.position=pointer; motion.anchor=request.anchor; motion.progress=0; motion.phase=Motion::Outgoing;
        // Keep the wheel in the visible canvas when a node meets the top edge.
        motion.anchor.x=std::clamp(motion.anchor.x,request.min.x+14*scale,std::max(request.min.x+14*scale,request.max.x-14*scale));
        motion.anchor.y=std::max(motion.anchor.y,request.min.y+14*scale);
    }
    if (!dragging && (motion.phase==Motion::Outgoing || motion.phase==Motion::Adjusting)) {
        motion.phase=Motion::Returning; motion.from=motion.position; motion.progress=0; motion.returnMorph=motion.morph;
    }
    if (motion.phase==Motion::Returning && (ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1)||ImGui::IsMouseClicked(2))) motion.phase=Motion::Idle;
    if (motion.phase==Motion::Outgoing) {
        motion.progress=std::min(1.0f,motion.progress+dt/0.24f);
        const float t=Ease(motion.progress); motion.position=ImLerp(motion.from,motion.anchor,t); motion.morph=t;
        if (motion.progress>=1) motion.phase=Motion::Adjusting;
    } else if (motion.phase==Motion::Adjusting) { motion.position=motion.anchor; motion.morph=1; }
    else if (motion.phase==Motion::Returning) {
        motion.progress=std::min(1.0f,motion.progress+dt/0.18f);
        const float t=Ease(motion.progress); motion.position=ImLerp(motion.from,pointer,t); motion.morph=motion.returnMorph*(1-t);
        if (motion.progress>=1) motion.phase=Motion::Idle;
    }
    if (motion.phase==Motion::Idle) { motion.position=pointer; motion.morph=0; }
    const bool active=motion.phase!=Motion::Idle;
    const bool nativeText=ImGui::GetMouseCursor()==ImGuiMouseCursor_TextInput;
    if (!active && (!request.hovered || nativeText)) { snapshot={}; return false; }
    motion.glow+=(float)((request.valueHovered || dragging ? 1.0f : 0.0f)-motion.glow)*(1-std::exp(-dt*18));
    if (active) motion.rotation=std::fmod(motion.rotation+dt*(2*pi/3.6f),2*pi);
    auto* draw=ImGui::GetForegroundDrawList(request.viewport ? request.viewport : ImGui::GetMainViewport());
    const auto& palette=request.palette;
    auto background=palette.workspace.background;
    for (auto it=request.surfaces.rbegin();it!=request.surfaces.rend();++it)
        if (it->rect.Contains(motion.position)) { background=it->color; break; }
    const ImVec4 dot=StackAppearance::CreamContrastRatio(palette.nodeAppearance.text,background)>
        StackAppearance::CreamContrastRatio(palette.text,background) ? palette.nodeAppearance.text : palette.text;
    const auto glow=request.valueHovered || dragging ? request.accent : dot;
    snapshot={true,dragging,motion.phase==Motion::Returning,motion.position,motion.morph,motion.glow,motion.rotation};
    snapshot.nativeDot=!active && !pointerCaptured;
    snapshot.scale=scale; snapshot.dotColor=dot; snapshot.glowColor=glow;
    // The OS positions the ordinary dot independently of application rendering.
    // Specimens can explicitly request a software copy without changing input.
    if (snapshot.nativeDot && !softwarePreview) return true;
    const float radius=4*scale;
    if (motion.glow>0.001f) {
        draw->AddCircleFilled(motion.position,radius+2*scale,
            ImGui::GetColorU32(Alpha(glow,motion.glow*(1-motion.morph)*0.12f)),40);
    }
    draw->AddCircleFilled(motion.position,radius*(1-0.55f*motion.morph),ImGui::GetColorU32(Alpha(dot,1-motion.morph)),32);
    if (motion.morph>0.001f) {
        const ImVec4 colors[]{palette.analysisSocket,palette.imageSocket,palette.maskSocket,palette.valueSocket,palette.selection,palette.text};
        ColorWheel(draw,motion.position,(4+6*motion.morph)*scale,motion.rotation,colors,motion.morph);
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    return true;
}
}
