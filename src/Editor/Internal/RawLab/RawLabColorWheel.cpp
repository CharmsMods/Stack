#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Raw/ColorWarpMath.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
bool DrawColorWarpWheel(RawLabColorSurfaceArgs& args,RawRecipe::RawColorWarpPin& pin,float size) {
    auto& state=*args.state;
    auto* draw=ImGui::GetWindowDrawList();
    const auto origin=ImGui::GetCursorScreenPos();
    const ImVec2 min(origin.x+std::max(0.f,(ImGui::GetContentRegionAvail().x-size)*.5f),origin.y);
    const ImVec2 center(min.x+size*.5f,min.y+size*.5f);
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##ColorWarpWheel",{size,size});
    if(!ImGui::IsItemVisible())return false;
    state.wheel.QueueDraw(*draw,min,{min.x+size,min.y+size},args.workingSpace,.7f,ColorDiscExtent);
    DrawColorWarpCloud(args,min,size);
    constexpr float radians=3.14159265359f/180.f;
    const float sourceHue=RawRecipe::ColorWarpHue(pin.sourceA,pin.sourceB);
    const float sourceChroma=std::hypot(pin.sourceA,pin.sourceB);
    const auto screen=[&](float hue,float chroma) {
        const auto projected=ProjectColorDisc(chroma*.7f*std::cos(hue*radians),chroma*.7f*std::sin(hue*radians));
        return ImVec2(center.x+projected.x/ColorDiscExtent*size*.5f,center.y-projected.y/ColorDiscExtent*size*.5f);
    };
    for(const auto& other:args.colorWarp.pins) {
        if(other.id==pin.id)continue;
        const auto point=screen(RawRecipe::ColorWarpHue(other.sourceA,other.sourceB),std::hypot(other.sourceA,other.sourceB));
        draw->AddCircle(point,3.f,ImGui::GetColorU32(ImGuiCol_Text,other.enabled?.35f:.15f),16,1.f);
    }
    const auto source=screen(sourceHue,sourceChroma);
    const auto destination=RawRecipe::ColorWarpDestination(pin);
    const auto target=screen(RawRecipe::ColorWarpHue(destination[0],destination[1]),std::hypot(destination[0],destination[1]));
    const auto white=ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddLine(source,target,white,1.5f);
    draw->AddCircle(source,4,white,20,1.2f);
    draw->AddCircleFilled(target,6.5f,IM_COL32(0,0,0,150));
    draw->AddCircleFilled(target,5,white);
    const auto rangePoint=[&](float angle,float radius) {return ImVec2(center.x+std::cos(angle*radians)*radius,center.y-std::sin(angle*radians)*radius);};
    std::array<ImVec2,8> handles{};
    ImVec2 reachHandle{};
    if(state.selectRange && pin.sampleRadius>0) {
        std::array<ImVec2,65> outline{};
        for(int i=0;i<=64;++i) {
            const float angle=i*2.f*3.14159265359f/64.f;
            const auto q=ProjectColorDisc((pin.sourceA+pin.sampleRadius*std::cos(angle))*.7f,
                (pin.sourceB+pin.sampleRadius*std::sin(angle))*.7f);
            const ImVec2 point(center.x+q.x/ColorDiscExtent*size*.5f,center.y-q.y/ColorDiscExtent*size*.5f);
            outline[i]=point;if(i==0)reachHandle=point;
        }
        draw->AddPolyline(outline.data(),int(outline.size()),IM_COL32(0,0,0,150),ImDrawFlags_Closed,3.f);
        draw->AddPolyline(outline.data(),int(outline.size()),white,ImDrawFlags_Closed,1.f);
        draw->AddCircleFilled(reachHandle,5.5f,IM_COL32(0,0,0,150));
        draw->AddCircleFilled(reachHandle,4.f,white);
    } else if(state.selectRange) {
        const float lo=sourceHue+pin.hueRange.low, hi=sourceHue+pin.hueRange.high;
        const float angles[]={lo,hi,lo-pin.hueRange.featherLow,hi+pin.hueRange.featherHigh};
        for(int i=0;i<4;++i) {
            const float radius=size*(i==0?.32f:i==1?.38f:.46f);
            handles[i]=rangePoint(angles[i],radius);
            draw->AddLine(center,rangePoint(angles[i],size*.48f),i<2?white:IM_COL32(230,230,230,90),1);
            draw->AddCircle(handles[i],i<2?5:3.5f,white,16,1.2f);
        }
        if(pin.saturationRange.enabled) {
            const auto& r=pin.saturationRange;
            const float radii[]={r.low,r.high,std::max(0.f,r.low-r.featherLow),r.high+r.featherHigh};
            for(int i=0;i<4;++i) {
                handles[i+4]=screen(sourceHue+(i-1.5f)*8.f,radii[i]);
                const auto projected=ProjectColorDisc(radii[i]*.7f,0);
                draw->AddCircle(center,projected.x/ColorDiscExtent*size*.5f,i<2?white:IM_COL32(230,230,230,70),64,1);
                draw->AddCircle(handles[i+4],i<2?4:3,white,16,1.2f);
            }
        }
    }
    auto& io=ImGui::GetIO();
    const float dx=io.MousePos.x-center.x,dy=center.y-io.MousePos.y;
    const float pointerHue=RawRecipe::ColorWarpHue(dx,dy);
    const float radius=std::min(std::hypot(dx,dy)/(size*.5f)*ColorDiscExtent,ColorDiscExtent-.001f);
    const auto raw=UnprojectColorDisc(radius,0);
    const float pointerChroma=raw.x/.7f;
    if(ImGui::IsItemActivated()) {
        if(state.selectRange && pin.sampleRadius>0) {
            state.dragging=std::hypot(reachHandle.x-io.MousePos.x,reachHandle.y-io.MousePos.y)<14.f?9:-1;
        } else if(state.selectRange) {
            float nearest=14.f;state.dragging=-1;
            for(int i=0;i<(pin.saturationRange.enabled?8:4);++i) {
                const float d=std::hypot(handles[i].x-io.MousePos.x,handles[i].y-io.MousePos.y);
                if(d<nearest){nearest=d;state.dragging=i+1;}
            }
        } else if(!pin.protectColor && std::hypot(dx,dy)<size*.5f) {
            state.dragging=0;state.lastHue=pointerHue;state.startChroma=pointerChroma;state.startSaturation=pin.saturation;
            state.movePress={pointerChroma*std::cos(pointerHue*radians),pointerChroma*std::sin(pointerHue*radians)};
            const auto destination=RawRecipe::ColorWarpDestination(pin);state.moveStart={destination[0],destination[1]};
        }
    }
    bool changed=false;
    if(ImGui::IsItemActive() && state.dragging>=0 && (io.MouseDelta.x!=0 || io.MouseDelta.y!=0)) {
        auto& hue=pin.hueRange;auto& sat=pin.saturationRange;
        const float relative=RawRecipe::ColorWarpHueDelta(pointerHue-sourceHue);
        const auto unwrapped=[&](float reference){return reference+RawRecipe::ColorWarpHueDelta(relative-reference);};
        switch(state.dragging) {
        case 0:
            if(pin.moveColors || sourceChroma<.02f) {
                const float a=state.moveStart.x+.15f*(pointerChroma*std::cos(pointerHue*radians)-state.movePress.x);
                const float b=state.moveStart.y+.15f*(pointerChroma*std::sin(pointerHue*radians)-state.movePress.y);
                if(sourceChroma<.02f) {
                    const auto current=RawRecipe::ColorWarpDestination(pin);
                    pin.tintA=std::clamp(a-(current[0]-pin.tintA),-1.f,1.f);
                    pin.tintB=std::clamp(b-(current[1]-pin.tintB),-1.f,1.f);
                } else {
                    pin.hueShift=RawRecipe::ColorWarpHueDelta(RawRecipe::ColorWarpHue(a-pin.tintA,b-pin.tintB)-sourceHue);
                    pin.saturation=std::clamp(std::hypot(a-pin.tintA,b-pin.tintB)/sourceChroma-1.f,-1.f,5.f);
                }
                break;
            }
            if(std::hypot(dx,dy)>3)pin.hueShift=std::clamp(pin.hueShift+.15f*RawRecipe::ColorWarpHueDelta(pointerHue-state.lastHue),-180.f,180.f);
            state.lastHue=pointerHue;
            pin.saturation=std::clamp(state.startSaturation+.15f*(pointerChroma-state.startChroma)/std::max(.01f,sourceChroma),-1.f,5.f);break;
        case 9:pin.sampleRadius=std::clamp(std::hypot(pointerChroma*std::cos(pointerHue*radians)-pin.sourceA,
            pointerChroma*std::sin(pointerHue*radians)-pin.sourceB),.01f,1.f);break;
        case 1:hue.low=std::min(relative,hue.high);break;
        case 2:hue.high=std::max(relative,hue.low);break;
        case 3:hue.featherLow=std::clamp(hue.low-unwrapped(hue.low-hue.featherLow),0.f,360.f);break;
        case 4:hue.featherHigh=std::clamp(unwrapped(hue.high+hue.featherHigh)-hue.high,0.f,360.f);break;
        case 5:sat.low=std::min(pointerChroma,sat.high);break;
        case 6:sat.high=std::max(pointerChroma,sat.low);break;
        case 7:sat.featherLow=std::max(0.f,sat.low-pointerChroma);break;
        case 8:sat.featherHigh=std::max(0.f,pointerChroma-sat.high);break;
        }
        RawRecipe::SanitizeColorWarpPin(pin);changed=true;
    }
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive() && state.dragging>=0;
    args.ui.colorWarpGestureOwner=args.ui.colorWarpInteractionActive?1:0;
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))state.EndInteraction();
    return changed;
}
}
