#include "Notifications/NotificationPresenter.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Stack::Notifications {

float Presenter::RenderIndicator(NotificationStore& store,
    const UiActivity::Snapshot& snapshot, UiActivity::Presentation& activity,
    float width, float height, bool openPanel, const PresentationContext& context) {
    activity.Update(snapshot,ImGui::GetTime(),ImGui::GetIO().DeltaTime);
    const auto attention=store.CountNeedsAttention();
    std::string label=activity.busyBlend>.001f?activity.label:attention?"Activity":"";
    if(snapshot.entries.size()>1)label+=" +"+std::to_string(snapshot.entries.size()-1);
    if(attention)label+=" · "+std::to_string(attention);
    m_IndicatorLabel.Update(label,ImGui::GetTime(),context.reducedMotion);
    const float hitWidth=snapshot.Busy()||activity.busyBlend>.001f||attention||m_PanelOpen||m_IndicatorLabel.Visible()?width:36.f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+width-hitWidth);
    const ImVec2 pos=ImGui::GetCursorScreenPos();
    if (context.customIndicatorFocus) ImGui::PushStyleColor(ImGuiCol_NavCursor,ImVec4(0,0,0,0));
    if (ImGui::InvisibleButton("##Activity",ImVec2(hitWidth,height),ImGuiButtonFlags_EnableNav)||openPanel)
        m_PanelOpen=!m_PanelOpen;
    if (context.customIndicatorFocus) ImGui::PopStyleColor();
    m_Anchor=ImVec2(pos.x+hitWidth,pos.y+height);
    m_HasAnchor=true;
    auto* draw=ImGui::GetWindowDrawList();
    if (context.keepFixed) context.keepFixed(draw);
    if (context.beginIndicator) context.beginIndicator(draw,pos,ImVec2(pos.x+hitWidth,pos.y+height),m_PanelOpen);
    if (!context.beginIndicator && (m_PanelOpen||ImGui::IsItemHovered()))
        draw->AddRectFilled(pos,ImVec2(pos.x+hitWidth,pos.y+height),
            ImGui::GetColorU32(ImGuiCol_ButtonHovered),6.f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s",attention?"View activity and items needing attention":"View activity");
    const ImVec4 ink=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImU32 color=ImGui::GetColorU32(ink);
    const ImVec2 center(pos.x+hitWidth-16.f,pos.y+height*.5f);
    constexpr float pi=3.14159265359f;
    if (activity.busyBlend>.001f) {
        const float angle=context.reducedMotion?-.5f*pi:static_cast<float>(std::fmod(ImGui::GetTime()*5.5,2*pi));
        for(int i=0;i<=24;++i) {
            const float theta=angle+float(i)/24.f*pi*1.55f;
            draw->PathLineTo(ImVec2(center.x+std::cos(theta)*7.f,center.y+std::sin(theta)*7.f));
        }
        draw->PathStroke(color,0,1.6f);
    } else if(activity.waiting) {
        draw->AddLine(ImVec2(center.x-6.f,center.y),ImVec2(center.x+6.f,center.y),color,1.6f);
    } else {
        // Resting is idle, not a claim that an operation succeeded.
        draw->AddCircle(center,5.f,color,24,1.4f);
    }
    const float fontSize=ImGui::GetFontSize();
    const float labelLeft=pos.x+6.f, labelRight=center.x-13.f;
    const float labelRoom=labelRight-labelLeft;
    // Compact indicators must not expose a few clipped pixels of a label.
    // A fitting short label is still useful below one em. Account for both
    // its target and animated advance instead of checking the new text alone.
    const bool fitsLabel=!label.empty() && m_IndicatorLabel.Width()<=labelRoom &&
        ImGui::GetFont()->CalcTextSizeA(fontSize,FLT_MAX,0.f,label.c_str()).x<=labelRoom;
    if (labelRoom>=fontSize || fitsLabel) {
        draw->PushClipRect(pos,ImVec2(labelRight,pos.y+height),true);
        m_IndicatorLabel.Draw(*draw,ImVec2(labelLeft,pos.y+(height-fontSize)*.5f),labelRight,color);
        draw->PopClipRect();
    }
    if (context.endIndicator) context.endIndicator(draw);
    if(attention) {
        const ImVec4 bg=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const bool light=bg.x+bg.y+bg.z>1.5f;
        draw->AddCircleFilled(ImVec2(center.x+9.f,center.y-8.f),2.6f,
            ImGui::GetColorU32(light?ImVec4(.53f,.33f,.1f,1):ImVec4(.90f,.73f,.46f,1)));
    }
    return hitWidth;
}

} // namespace Stack::Notifications
