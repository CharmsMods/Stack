#pragma once
#include <imgui.h>
#include <algorithm>

namespace Stack::Navigation {
inline float ProjectEntryHeight(float width,float scale,int imageWidth,int imageHeight) {
    if (imageWidth<=0 || imageHeight<=0) return width;
    const float contentWidth=std::max(1.f,width-8.f*scale);
    return std::clamp(contentWidth*imageHeight/imageWidth+4.f*scale,24.f*scale,2.f*width);
}

inline void DrawProjectEntry(ImDrawList* draw, ImVec2 min, ImVec2 max, float scale,
    unsigned texture,int width,int height,const char* fallback,bool selected,bool dirty) {
    const ImVec2 lo(min.x+2.f*scale,min.y+2.f*scale), hi(max.x-2.f*scale,max.y-2.f*scale);
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    if (texture && width>0 && height>0) {
        const float fit=std::min((hi.x-lo.x)/width,(hi.y-lo.y)/height);
        const ImVec2 pos((lo.x+hi.x-width*fit)*.5f,(lo.y+hi.y-height*fit)*.5f);
        draw->AddImageRounded((ImTextureID)(intptr_t)texture,pos,{pos.x+width*fit,pos.y+height*fit},
            {0,1},{1,0},IM_COL32_WHITE,1.5f*scale);
    } else {
        draw->PushClipRect(lo,hi,true);
        const float fontSize=ImGui::GetFontSize()*.85f;
        const ImVec2 labelPos(lo.x,(lo.y+hi.y-fontSize)*.5f);
        draw->AddText(ImGui::GetFont(),fontSize,labelPos,ink,fallback);
        draw->PopClipRect();
    }
    if (selected) {
        const float cy=(min.y+max.y)*.5f;
        draw->AddLine({min.x,cy-4.f*scale},{min.x,cy+4.f*scale},ink,1.5f*scale);
    }
    if (dirty) draw->AddCircleFilled({hi.x-3.f*scale,lo.y+3.f*scale},2.f*scale,ink);
}
}
