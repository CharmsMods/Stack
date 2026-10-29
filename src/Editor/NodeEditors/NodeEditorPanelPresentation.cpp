#include "Editor/NodeEditors/NodeEditorPanelPresentation.h"
#include "Notifications/NotificationPresenter.h"
#include "Editor/NodeEditors/NodeEditorPanelState.h"
#include "Utils/GlassControls.h"
#include "App/ControlMotionAdapter.h"
#include "App/AppHeaderStyle.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
namespace {
bool ControlHeading(const char* label,float width,float height) {
    const auto transparent=ImVec4(0,0,0,0);
    for (auto color:{ImGuiCol_Button,ImGuiCol_ButtonHovered,ImGuiCol_ButtonActive})
        ImGui::PushStyleColor(color,transparent);
    const bool clicked=ImGui::Button("##control-name",ImVec2(width,height));
    ImGui::PopStyleColor(3);
    if (!ImGui::IsItemVisible()) return clicked;
    const bool hovered=ImGui::IsItemHovered();
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
    const auto textSize=ImGui::CalcTextSize(label);
    const ImVec2 textAt(minimum.x,minimum.y+(height-textSize.y)*.5f);
    auto* draw=ImGui::GetWindowDrawList();
    const auto paint=[&](ImVec2 offset,ImVec4 ink) {
        ImGui::PushStyleColor(ImGuiCol_Text,ink);
        ImGui::RenderTextEllipsis(draw,{textAt.x+offset.x,textAt.y+offset.y},
            {maximum.x+offset.x,maximum.y+offset.y},maximum.x+offset.x,label,nullptr,&textSize);
        ImGui::PopStyleColor();
    };
    if (hovered) {
        // A bounded glyph halo, submitted only for the hovered visible name.
        // Native Button still owns click, focus and keyboard activation.
        constexpr ImVec2 directions[]={{1,0},{-1,0},{0,1},{0,-1},
            {.7071f,.7071f},{-.7071f,.7071f},{.7071f,-.7071f},{-.7071f,-.7071f}};
        const float scale=ImGui::GetFontSize()/13.f;
        for (int ring=3;ring>=1;--ring)
            for (const auto& direction:directions) {
                const float radius=ring*1.6f*scale;
                paint({direction.x*radius,direction.y*radius},
                    {1,1,1,ring==1 ? .22f : ring==2 ? .11f : .055f});
            }
    }
    paint({0,0},hovered ? ImVec4(1,1,1,1) : ImVec4(.82f,.82f,.82f,1));
    return clicked;
}
}

int DrawControlsPanelHeader(const NodeEditorPanelGroup& group,float width) {
    if (group.instances.empty()) return -1;
    const float height=ImGui::GetFontSize()+10.f;
    const float step=std::max(height,ImGui::CalcTextSize(group.instances.back().ordinal.c_str()).x+14.f);
    const float gap=8.f;
    const float labelWidth=std::min(ImGui::CalcTextSize(group.label.c_str()).x,
        std::max(1.f,width-step-gap));
    const auto rowStart=ImGui::GetCursorScreenPos();
    const int current=static_cast<int>(std::distance(group.instances.begin(),
        std::find_if(group.instances.begin(),group.instances.end(),
            [&](const auto& instance){return instance.key==group.selectedKey;})));
    int activated=ControlHeading(group.label.c_str(),labelWidth,height) ? current : -1;
    ImGui::SameLine(0,gap);
    const float firstWidth=std::max(step,width-labelWidth-gap);
    auto* draw=ImGui::GetWindowDrawList();
    ImGuiExtras::AutomaticGlassScope glass;
    for (std::size_t start=0;start<group.instances.size();) {
        const float available=start ? width : firstWidth;
        const auto count=std::min(group.instances.size()-start,
            static_cast<std::size_t>(std::max(1.f,std::floor(available/step))));
        const auto origin=ImGui::GetCursorScreenPos();
        const ImRect pill(origin,{origin.x+float(count)*step,origin.y+height});
        ImGui::PushID(static_cast<int>(start));
        const auto selectionId=ImGui::GetID("version-bead");
        {
            ImGuiExtras::GlassBackgroundScope background(*draw);
            draw->AddRectFilled(pill.Min,pill.Max,ImGui::GetColorU32(ImVec4(0,0,0,.16f)),height*.5f);
        }
        bool hovered=false,focused=false,held=false;
        int captured=-1;
        ImGuiID captureId=0;
        for (std::size_t i=0;i<count;++i) {
            if (i) ImGui::SameLine(0,0);
            const auto index=start+i;
            if (ImGui::InvisibleButton(group.instances[index].key.c_str(),{step,height},ImGuiButtonFlags_EnableNav))
                activated=static_cast<int>(index);
            hovered|=ImGui::IsItemHovered();focused|=ImGui::IsItemFocused();held|=ImGui::IsItemActive();
            if (ImGui::IsItemActive() || (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(0))) {
                captured=static_cast<int>(i);captureId=ImGui::GetItemID();
            }
        }
        float position=float((activated>=0 ? activated : current)-int(start));
        bool direct=false,cancelled=false;
        if (captured>=0) {
            const auto& io=ImGui::GetIO();
            cancelled=io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false);
            const float dx=io.MousePos.x-io.MouseClickedPos[0].x,dy=io.MousePos.y-io.MouseClickedPos[0].y;
            direct=!cancelled && ImGui::IsMousePosValid() &&
                (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0)) &&
                dx*dx+dy*dy>=io.MouseDragThreshold*io.MouseDragThreshold;
            if (direct) {
                position=std::clamp(float(captured)+dx/step,0.f,float(count-1));
                if (ImGui::IsMouseReleased(0)) activated=int(start)+int(std::lround(position));
            }
            if (cancelled) {
                if (ImGui::GetActiveID()==captureId) ImGui::ClearActiveID();
                activated=-1;position=float(current-int(start));
            }
        }
        if (pill.Overlaps(ImGui::GetCurrentWindow()->ClipRect)) {
            const int selected=activated>=0 ? activated : current;
            if ((direct && captured>=0) || (selected>=static_cast<int>(start) && selected<static_cast<int>(start+count))) {
                auto input=Stack::UiMotion::LastItemInput(true);
                input.hovered=hovered;input.focused=focused;input.held=held;input.visible=true;
                const ImGuiExtras::GlassSelectionInteraction interaction{input,position,direct,cancelled};
                const ImVec2 first(origin.x+step*.5f,origin.y+height*.5f);
                if (!ImGuiExtras::DrawGlassSelection(*draw,first,step,selected-int(start),
                    height*.46f,selectionId,&interaction)) {
                    ImGuiExtras::GlassBackgroundScope background(*draw);
                    draw->AddCircleFilled({first.x+position*step,first.y},
                        height*.46f,ImGui::GetColorU32(ImGuiCol_Text,.15f));
                }
            }
            for (std::size_t i=0;i<count;++i) {
                const auto& number=group.instances[start+i].ordinal;
                const auto text=ImGui::CalcTextSize(number.c_str());
                draw->AddText({origin.x+float(i)*step+(step-text.x)*.5f,origin.y+(height-text.y)*.5f},
                    ImGui::GetColorU32(ImGuiCol_Text),number.c_str());
            }
        }
        ImGui::PopID();
        start+=count;
        if (start<group.instances.size()) ImGui::SetCursorScreenPos({rowStart.x,ImGui::GetCursorScreenPos().y});
    }
    return activated;
}

bool BeginControlsPanelBody() {
    auto* host=ImGui::GetCurrentWindow();
    const auto windowId=host->GetID("ControlsScrollWindow");
    auto* body=ImGui::FindWindowByID(static_cast<ImGuiID>(host->StateStorage.GetInt(windowId)));
    const auto targetId=ImHashStr("ControlsScrollTarget");
    const auto currentId=ImHashStr("ControlsScrollCurrent");
    const auto appliedId=ImHashStr("ControlsScrollApplied");
    const auto frameId=ImHashStr("ControlsScrollFrame");
    auto& gui=*ImGui::GetCurrentContext();
    bool direct=true;
    if(body) {
        auto& state=body->StateStorage;
        // NewFrame routes wheel input through native item/window ownership.
        // Filter only that accepted request, before BeginChild lays out controls.
        const bool wheel=gui.WheelingWindow==body && gui.WheelingWindowScrolledFrame==gui.FrameCount &&
            body->ScrollTarget.y<FLT_MAX && body->ScrollTargetCenterRatio.y==0.f;
        direct=state.GetInt(frameId,-2)!=gui.FrameCount-1 ||
            body->Scroll.y!=state.GetFloat(appliedId,body->Scroll.y) ||
            (body->ScrollTarget.y<FLT_MAX && !wheel) ||
            gui.ActiveId==host->GetID("ControlsOverlayScroll");
        if(!direct) {
            float target=state.GetFloat(targetId,body->Scroll.y);
            float current=state.GetFloat(currentId,body->Scroll.y);
            if(wheel) {
                const float delta=body->ScrollTarget.y-body->Scroll.y;
                if(delta*(target-current)<0.f)target=current;
                target+=delta;
            }
            target=std::clamp(target,0.f,body->ScrollMax.y);
            current=std::clamp(current,0.f,body->ScrollMax.y);
            if(current!=target) {
                Stack::Header::Approach(current,target,22.f,.05f);
                ImGui::SetScrollY(body,current);
            }
            state.SetFloat(targetId,target);state.SetFloat(currentId,current);
        }
    }
    const bool visible=ImGui::BeginChild("PanelBody",ImVec2(0,0),ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar);
    body=ImGui::GetCurrentWindow();
    host->StateStorage.SetInt(windowId,static_cast<int>(body->ID));
    auto& state=body->StateStorage;
    // Scrollbar/key navigation and reopening take effect immediately, ending
    // any old wheel tail. Fractional wheel motion stays separate from ImGui's
    // pixel-rounded scroll so a small remaining distance cannot stall.
    if(direct || !visible) {state.SetFloat(targetId,body->Scroll.y);state.SetFloat(currentId,body->Scroll.y);}
    state.SetFloat(appliedId,body->Scroll.y);state.SetInt(frameId,gui.FrameCount);
    return visible;
}

void DrawControlsPanelScrollbar(ImGuiWindow* body,float seamX) {
    auto* host=ImGui::GetCurrentWindow();
    const auto id=host->GetID("ControlsOverlayScroll");
    auto& storage=body->StateStorage;
    const auto positionId=ImHashStr("ControlsScrollPosition");
    const auto timeId=ImHashStr("ControlsScrollTime");
    const float now=static_cast<float>(ImGui::GetTime());
    const bool moved=storage.GetFloat(positionId,body->Scroll.y)!=body->Scroll.y;
    if (moved || ImGui::GetActiveID()==id) storage.SetFloat(timeId,now);
    storage.SetFloat(positionId,body->Scroll.y);
    if (body->ScrollMax.y<=0) return;
    const float age=now-storage.GetFloat(timeId,-100.f);
    if (age>=.43f && ImGui::GetActiveID()!=id) return;
    const bool reduced=Stack::Notifications::SystemReducedMotion();
    const float opacity=reduced ? (age<.35f ? 1.f : 0.f) :
        std::clamp(1.f-(age-.25f)/.18f,0.f,1.f);
    if (opacity<=0 && ImGui::GetActiveID()!=id) return;
    const float scale=ImGui::GetFontSize()/13.f;
    const ImRect rail({seamX-2.f*scale,body->InnerRect.Min.y},
        {seamX+1.f*scale,body->InnerRect.Max.y});
    const auto oldClip=host->ClipRect;
    host->ClipRect.Max.x=std::max(host->ClipRect.Max.x,rail.Max.x);
    host->DrawList->PushClipRect({rail.Min.x-5.f*scale,rail.Min.y},{rail.Max.x+5.f*scale,rail.Max.y},false);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding,0.f);
    const auto touchPadding=ImGui::GetStyle().TouchExtraPadding;
    ImGui::GetStyle().TouchExtraPadding=ImVec2(4.f*scale,0);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding,3.f*scale);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,ImVec4(0,0,0,0));
    const auto ink=ImGui::GetStyleColorVec4(ImGuiCol_Text);
    for (const auto color:{ImGuiCol_ScrollbarGrab,ImGuiCol_ScrollbarGrabHovered,ImGuiCol_ScrollbarGrabActive})
        ImGui::PushStyleColor(color,ImVec4(ink.x,ink.y,ink.z,ink.w*opacity*.42f));
    const auto visible=static_cast<ImS64>(std::ceil(body->InnerRect.GetHeight()));
    ImS64 scroll=static_cast<ImS64>(body->Scroll.y);
    ImGui::ScrollbarEx(rail,id,ImGuiAxis_Y,&scroll,visible,
        visible+static_cast<ImS64>(std::ceil(body->ScrollMax.y)),ImDrawFlags_RoundCornersAll);
    if (static_cast<ImS64>(body->Scroll.y)!=scroll) {
        ImGui::SetScrollY(body,static_cast<float>(scroll));
        storage.SetFloat(timeId,now);
    }
    ImGui::PopStyleColor(4);ImGui::PopStyleVar(2);
    ImGui::GetStyle().TouchExtraPadding=touchPadding;
    host->DrawList->PopClipRect();host->ClipRect=oldClip;
}
}
