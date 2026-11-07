#pragma once
#include "ToolSwitcher.h"
#include "AppHeaderStyle.h"
#include <vector>

namespace Stack::Tools {
inline void PlaceToolList(Switcher& sw, float scale, int active) {
    const auto* vp=ImGui::GetMainViewport();
    const ImVec2 lo(vp->WorkPos.x+12, vp->WorkPos.y+12);
    const ImVec2 hi(vp->WorkPos.x+vp->WorkSize.x-12,vp->WorkPos.y+vp->WorkSize.y-12);
    sw.rowHeight=std::max(30.f*scale,ImGui::GetFontSize()+16.f);
    sw.visibleRows=std::max(1,std::min(sw.count,int((hi.y-lo.y-16)/sw.rowHeight)));
    const float width=std::min(260.f*scale,hi.x-lo.x);
    const float height=sw.visibleRows*sw.rowHeight+16;
    sw.listMin={std::clamp(sw.controlsMin.x,lo.x,std::max(lo.x,hi.x-width)),
        std::clamp(sw.controlsMin.y,lo.y,std::max(lo.y,hi.y-height))};
    sw.listMax={sw.listMin.x+width,sw.listMin.y+height};
    sw.hovered=std::clamp(active,0,sw.count-1);
    sw.listPosition=sw.hovered+.5f;
    sw.firstRow=std::clamp(sw.hovered-sw.visibleRows/2,0,std::max(0,sw.count-sw.visibleRows));
}

inline void DrawToolList(ImDrawList& draw,const Switcher& state,
    const std::vector<std::string>& labels,const Header::Palette& palette) {
    // Interpolate the surface extent without reflowing editors or resizing fonts.
    const float amount=state.Amount();
    const auto mix=[&](ImVec2 a,ImVec2 b){return ImVec2(a.x+(b.x-a.x)*amount,a.y+(b.y-a.y)*amount);};
    const ImVec2 low=mix(state.controlsMin,state.listMin);
    const ImVec2 high=mix(state.controlsMax,state.listMax);
    const float rowHeight=std::max(1.f,(high.y-low.y-16)/state.visibleRows);
    const auto tint=[&](ImVec4 color) { color.w*=amount; return ImGui::GetColorU32(color); };
    const auto& sw=state;
    draw.AddRectFilled(low,high,tint(palette.activeTab),12.f);
    draw.PushClipRect(low,high,true);
    for(int row=0;row<sw.visibleRows;++row) {
        const int i=sw.firstRow+row;
        if(i>=int(labels.size())) break;
        const float y=low.y+8+row*rowHeight;
        if(i==sw.hovered) draw.AddRectFilled({low.x+5,y},{high.x-5,y+rowHeight},
            tint(palette.selected),7.f);
        draw.PushClipRect({low.x+14,y},{high.x-14,y+rowHeight},true);
        draw.AddText({low.x+14,y+(rowHeight-ImGui::GetFontSize())*.5f},
            tint(i==sw.hovered?palette.text:palette.mutedText),labels[i].c_str());
        draw.PopClipRect();
    }
    if(sw.count>sw.visibleRows) {
        const float h=high.y-low.y-16;
        const float y=low.y+8+h*sw.firstRow/sw.count;
        draw.AddRectFilled({high.x-4,y},{high.x-2,y+h*sw.visibleRows/sw.count},
            tint(palette.mutedText),1.f);
    }
    draw.PopClipRect();
}
}
