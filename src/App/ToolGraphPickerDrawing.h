#pragma once
#include "App/ToolSwitcher.h"
#include "App/AppHeaderStyle.h"
#include "Editor/RawLayerToolPickerCache.h"
#include <imgui_internal.h>

namespace Stack::Tools {
inline ImVec2 GraphPickerPoint(const Switcher& picker,const Layout& layout,ImVec2 point) {
    return {layout.center.x+(point.x-picker.graphCenter.x)*picker.graphZoom,
        layout.center.y+(point.y-picker.graphCenter.y)*picker.graphZoom};
}
inline int PickGraphTool(const Switcher& picker,const Layout& layout,
    const Editor::RawLayerToolPickerCache& graph,ImVec2 crosshair) {
    for (auto it=graph.cards.rbegin();it!=graph.cards.rend();++it) {
        if(it->choice<0) continue;
        const auto p=GraphPickerPoint(picker,layout,it->position);
        if(ImRect(p,{p.x+190*picker.graphZoom,p.y+60*picker.graphZoom}).Contains(crosshair)) return it->choice;
    }
    return -1;
}
inline void DrawGraphPicker(ImDrawList& draw,const Switcher& picker,const Layout& layout,
    const Editor::RawLayerToolPickerCache& graph,const Header::Palette& palette,int active) {
    const float alpha=picker.Amount(),r=layout.radius;
    const ImRect bounds({layout.center.x-r,layout.center.y-r*.7f},{layout.center.x+r,layout.center.y+r*.7f});
    const auto tint=[&](ImVec4 color,float opacity) {color.w*=opacity*alpha;return ImGui::GetColorU32(color);};
    // Fixed, bounded geometry; this exists only while the picker is visible.
    for(int i=12;i>0;--i) {
        const float inset=float(12-i)*2.f;
        draw.AddRectFilled({bounds.Min.x+inset,bounds.Min.y+inset},
            {bounds.Max.x-inset,bounds.Max.y-inset},tint(palette.caption,.15f),24.f);
    }
    draw.PushClipRect(bounds.Min,bounds.Max,true);
    const int first=draw.VtxBuffer.Size;
    for(const auto& wire:graph.wires) {
        const auto a=GraphPickerPoint(picker,layout,wire.from),b=GraphPickerPoint(picker,layout,wire.to);
        const float bend=std::max(24.f,std::abs(b.x-a.x)*.5f);
        const ImRect wireBounds({std::min(a.x,b.x-bend),std::min(a.y,b.y)},
            {std::max(a.x+bend,b.x),std::max(a.y,b.y)+1.f});
        if (!bounds.Overlaps(wireBounds)) continue;
        draw.AddBezierCubic(a,{a.x+bend,a.y},{b.x-bend,b.y},b,tint(palette.mutedText,.45f),1.3f);
    }
    for(const auto& card:graph.cards) {
        const auto p=GraphPickerPoint(picker,layout,card.position);
        const ImRect rect(p,{p.x+190*picker.graphZoom,p.y+60*picker.graphZoom});
        if(!bounds.Overlaps(rect)) continue;
        const bool selected=card.choice>=0 && card.choice==picker.hovered;
        draw.AddRectFilled(rect.Min,rect.Max,tint(selected ? palette.selected : palette.activeTab,
            card.choice>=0 ? .98f : .5f),5.f);
        draw.AddRect(rect.Min,rect.Max,tint(selected ? palette.focus : palette.mutedText,
            selected ? .9f : card.choice==active ? .6f : .25f),5.f,0,selected ? 1.5f : 1.f);
        if(rect.GetWidth()>65 && rect.GetHeight()>ImGui::GetFontSize()+8) {
            draw.PushClipRect({rect.Min.x+6,rect.Min.y},{rect.Max.x-6,rect.Max.y},true);
            draw.AddText({rect.Min.x+8,rect.GetCenter().y-ImGui::GetFontSize()*.5f},
                tint(palette.text,card.choice>=0 ? 1.f : .5f),card.label.c_str());
            draw.PopClipRect();
        }
    }
    for(int i=first;i<draw.VtxBuffer.Size;++i) {
        auto& vertex=draw.VtxBuffer[i];
        const float edge=std::min({vertex.pos.x-bounds.Min.x,bounds.Max.x-vertex.pos.x,
            vertex.pos.y-bounds.Min.y,bounds.Max.y-vertex.pos.y});
        const float t=std::clamp(edge/28.f,0.f,1.f);
        const unsigned opacity=(vertex.col>>IM_COL32_A_SHIFT)&255;
        vertex.col=(vertex.col&~IM_COL32_A_MASK)|(unsigned(opacity*t*t*(3-2*t))<<IM_COL32_A_SHIFT);
    }
    draw.PopClipRect();
    const auto p=picker.held ? layout.center : ImGui::GetIO().MousePos;
    const auto ink=tint(palette.text,.95f);
    draw.AddLine({p.x-10,p.y},{p.x-3,p.y},ink,1.5f);draw.AddLine({p.x+3,p.y},{p.x+10,p.y},ink,1.5f);
    draw.AddLine({p.x,p.y-10},{p.x,p.y-3},ink,1.5f);draw.AddLine({p.x,p.y+3},{p.x,p.y+10},ink,1.5f);
    const char* hint=picker.held ? "Move to navigate · Wheel to zoom · Release Alt to select" : "Wheel to zoom · Click a node";
    const auto text=ImGui::CalcTextSize(hint);
    draw.AddText({layout.center.x-text.x*.5f,bounds.Max.y+8},tint(palette.mutedText,.9f),hint);
}
}
