#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Raw/ColorWarpMath.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
bool DrawColorWarpBrightness(RawLabColorSurfaceArgs& args,RawRecipe::RawColorWarpPin& pin) {
    auto& state=*args.state;
    auto& range=pin.exposureRange;
    bool changed=false;
    ImGui::PushID("brightness-range");
    ImGui::TextUnformatted("Brightness range");
    ContinueRawLabControlRow("All brightness");
    if(BareTextButton("All brightness",!range.enabled)) {range.enabled=!range.enabled;changed=true;}
    LabTooltip("Choose which input brightness levels receive this color edit.");
    const float scale=ImGui::GetFontSize()/16.f;
    const auto origin=ImGui::GetCursorScreenPos();
    const float width=std::max(80.f,ImGui::GetContentRegionAvail().x);
    const float left=origin.x+8*scale,right=origin.x+width-8*scale;
    const float top=origin.y+22*scale,bottom=top+22*scale;
    ImGui::InvisibleButton("strip",{width,72*scale});
    const bool active=ImGui::IsItemActive(),hovered=ImGui::IsItemHovered();
    const auto inputKey=args.scope.graphInputFingerprint?args.scope.graphInputFingerprint:args.cloudInputFingerprint;
    if(args.scope.valid && !args.scope.pixels.empty() &&
        (!state.brightnessInputReady || state.brightnessInputKey!=inputKey || state.brightnessSpace!=args.workingSpace)) {
        // Reduce the accepted input once, never rescan it for slider/drag frames.
        const bool rec=args.workingSpace==Raw::RawWorkingSpace::LinearRec2020D65;
        float low=INFINITY,high=-INFINITY;
        for(std::size_t i=0;i+2<args.scope.pixels.size();i+=3) {
            const auto& rgb=args.scope.pixels;
            const float y=(rec?.2627002f:.2126729f)*rgb[i]+(rec?.6779981f:.7151522f)*rgb[i+1]+(rec?.0593017f:.0721750f)*rgb[i+2];
            if(std::isfinite(y)){low=std::min(low,y);high=std::max(high,y);}
        }
        if(std::isfinite(low) && std::isfinite(high)) {
            state.brightnessInputLow=std::log2(std::max(low,.000001f)/.18f);
            state.brightnessInputHigh=std::log2(std::max(high,.000001f)/.18f);
        }
        state.brightnessInputKey=inputKey;state.brightnessSpace=args.workingSpace;state.brightnessInputReady=true;
    }
    if(!active) {
        state.brightnessViewLow=std::min({-8.f,state.brightnessInputLow,range.low-range.featherLow});
        state.brightnessViewHigh=std::max({8.f,state.brightnessInputHigh,range.high+range.featherHigh});
    }
    const float span=state.brightnessViewHigh-state.brightnessViewLow;
    const auto x=[&](float ev){return left+std::clamp((ev-state.brightnessViewLow)/span,0.f,1.f)*(right-left);};
    const auto ev=[&](float px){return state.brightnessViewLow+std::clamp((px-left)/(right-left),0.f,1.f)*span;};
    const float values[]{range.low-range.featherLow,range.low,range.high,range.high+range.featherHigh};
    const float ys[]{bottom+7*scale,top-7*scale,top-7*scale,bottom+7*scale};
    int nearest=-1;float distance=12*scale;
    if(range.enabled) for(int i=0;i<4;++i) {
        const float d=std::hypot(ImGui::GetIO().MousePos.x-x(values[i]),ImGui::GetIO().MousePos.y-ys[i]);
        if(d<distance){distance=d;nearest=i;}
    }
    if(ImGui::IsItemActivated()) {
        state.brightnessDrag=nearest;
        if(nearest<0 && range.enabled && ImGui::GetIO().MousePos.x>=x(range.low) &&
            ImGui::GetIO().MousePos.x<=x(range.high))state.brightnessDrag=4;
        state.brightnessStart=range;state.brightnessPress=ev(ImGui::GetIO().MousePos.x);
    }
    if(active && state.brightnessDrag>=0 && ImGui::GetIO().MouseDelta.x!=0) {
        const auto& start=state.brightnessStart;
        const float delta=.15f*(ev(ImGui::GetIO().MousePos.x)-state.brightnessPress);
        switch(state.brightnessDrag) {
        case 0:range.featherLow=std::clamp(start.featherLow-delta,0.f,std::min(64.f,range.low-state.brightnessViewLow));break;
        case 1:range.low=std::clamp(start.low+delta,std::max(-32.f,state.brightnessViewLow+range.featherLow),range.high);break;
        case 2:range.high=std::clamp(start.high+delta,range.low,std::min(32.f,state.brightnessViewHigh-range.featherHigh));break;
        case 3:range.featherHigh=std::clamp(start.featherHigh+delta,0.f,std::min(64.f,state.brightnessViewHigh-range.high));break;
        case 4:{const float move=std::clamp(delta,std::max(-32.f-start.low,state.brightnessViewLow-start.low+start.featherLow),std::min(32.f-start.high,state.brightnessViewHigh-start.high-start.featherHigh));
            range.low=start.low+move;range.high=start.high+move;break;}
        }
        changed=true;
    }
    if(active && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
        range=state.brightnessStart;changed=true;ImGui::ClearActiveID();state.brightnessDrag=-1;
    }
    args.ui.colorWarpInteractionActive|=active && state.brightnessDrag>=0;
    if(!active)state.brightnessDrag=-1;
    if(hovered && nearest>=0)ImGui::SetTooltip("%s: %+.2f stops",nearest==0||nearest==3?"Feather edge":"Full selection",values[nearest]);
    if(ImGui::BeginPopupContextItem("Exact brightness")) {
        ImGui::TextUnformatted("Brightness range (stops)");
        bool all=!range.enabled;
        if(ImGui::Checkbox("All brightness",&all)){range.enabled=!all;changed=true;}
        ImGui::BeginDisabled(!range.enabled);
        ImGui::SetNextItemWidth(160*scale);changed|=ImGui::InputFloat("Minimum",&range.low,0,0,"%.2f");
        ImGui::SetNextItemWidth(160*scale);changed|=ImGui::InputFloat("Maximum",&range.high,0,0,"%.2f");
        ImGui::SetNextItemWidth(160*scale);changed|=ImGui::InputFloat("Shadow feather",&range.featherLow,0,0,"%.2f");
        ImGui::SetNextItemWidth(160*scale);changed|=ImGui::InputFloat("Highlight feather",&range.featherHigh,0,0,"%.2f");
        ImGui::EndDisabled();ImGui::EndPopup();
    }
    if(changed)RawRecipe::SanitizeColorWarpPin(pin);
    if(ImGui::IsRectVisible(origin,{origin.x+width,origin.y+72*scale})) {
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(origin,{origin.x+width,origin.y+72*scale},true);
        const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
        const auto muted=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddText({left,origin.y},muted,"Shadows");
        const auto middle=ImGui::CalcTextSize("Midtones");
        draw->AddText({x(0)-middle.x*.5f,origin.y},muted,"Midtones");
        draw->AddText({right-ImGui::CalcTextSize("Highlights").x,origin.y},muted,"Highlights");
        for(int i=0;i<64;++i) {
            const float a=left+(right-left)*i/64.f,b=left+(right-left)*(i+1)/64.f;
            const float weight=RawRecipe::ColorWarpRangeWeight(range,ev((a+b)*.5f));
            const float gray=.12f+.72f*(i+.5f)/64.f;
            draw->AddRectFilled({a,top},{b,bottom},ImGui::GetColorU32(ImVec4(gray,gray,gray,.25f+.55f*weight)));
            const float y=bottom-weight*(bottom-top);
            draw->AddLine({a,y},{b,y},ink,1.2f*scale);
        }
        if(range.enabled) {
            const float points[]{range.low-range.featherLow,range.low,range.high,range.high+range.featherHigh};
            for(int i=0;i<4;++i) {
                const float px=x(points[i]);
                draw->AddLine({px,top},{px,bottom},i==0||i==3?muted:ink,scale);
                draw->AddCircleFilled({px,ys[i]},(i==nearest?4.5f:3.5f)*scale,ink,16);
            }
        }
        draw->PopClipRect();
    }
    ImGui::PopID();return changed;
}
}
