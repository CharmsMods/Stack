#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>

namespace Stack::Startup {
// Transform only this frame's submitted main-window geometry. Layout/hit targets
// stay at their final positions; the shell blocks input until the transition ends.
inline void MorphInterface(float amount, bool reducedMotion) {
    if(amount<=0.f || amount>=1.f) return;
    auto& context=*ImGui::GetCurrentContext();
    const auto* viewport=ImGui::GetMainViewport();
    const float scale=reducedMotion?1.f:.96f+.04f*amount;
    const float rise=reducedMotion?0.f:(1.f-amount)*18.f;
    for(auto* window:context.Windows) {
        if(window->LastFrameActive!=context.FrameCount || window->Viewport!=viewport) continue;
        auto* list=window->DrawList;
        const ImVec2 center(window->Pos.x+window->Size.x*.5f,window->Pos.y+window->Size.y*.5f);
        for(auto& vertex:list->VtxBuffer) {
            vertex.pos.x=center.x+(vertex.pos.x-center.x)*scale;
            vertex.pos.y=center.y+(vertex.pos.y-center.y)*scale+rise;
            const auto alpha=static_cast<ImU32>(((vertex.col>>IM_COL32_A_SHIFT)&255)*amount);
            vertex.col=(vertex.col&~IM_COL32_A_MASK)|(alpha<<IM_COL32_A_SHIFT);
        }
        for(auto& command:list->CmdBuffer) {
            command.ClipRect.x=center.x+(command.ClipRect.x-center.x)*scale;
            command.ClipRect.z=center.x+(command.ClipRect.z-center.x)*scale;
            command.ClipRect.y=center.y+(command.ClipRect.y-center.y)*scale+rise;
            command.ClipRect.w=center.y+(command.ClipRect.w-center.y)*scale+rise;
        }
    }
}
}
