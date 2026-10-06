#include "BracketingSession.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
ImVec4 BracketColor(const Raw::Bracketing::BracketingRecipe& recipe,std::size_t group) {
    const auto slots=Raw::Bracketing::ColorSlots(recipe);
    const int slot=group<slots.size()?slots[group]:0;
    static const ImVec4 palette[]={{.25f,.85f,1.f,1},{1.f,.61f,.22f,1},{.9f,.38f,.85f,1},{.4f,.9f,.42f,1},{1.f,.87f,.3f,1},{.6f,.55f,1.f,1},{1.f,.4f,.4f,1},{.45f,1.f,.8f,1}};
    if(slot<8)return palette[std::max(0,slot)];
    float r,g,b;ImGui::ColorConvertHSVtoRGB(std::fmod(slot*.61803398875f,1.f),.5f+(slot%2)*.35f,.95f,r,g,b);return {r,g,b,1};
}
bool DrawBracketingCurves(BracketingSession& ui,const ImVec2& size) {
    using namespace Raw::Bracketing;
    auto& recipe=ui.recipe;const auto count=recipe.groups.size();if(!count) return false;
    ui.selectedGroup=std::clamp(ui.selectedGroup,0,static_cast<int>(count)-1);
    bool changed=false;
    const bool currentSuggestion=ui.result&&ui.result->analysis&&!ui.result->analysis->suggestion.empty()&&
        ui.result->analysis->suggestion.front().share.size()==count;
    if(ImGui::Button("Use suggestion")&&currentSuggestion) {
        recipe.knots=ui.result->analysis->suggestion;recipe.automatic=false;changed=true;
    }
    ImGui::SameLine();if(ImGui::Button("Reset")) {recipe.automatic=true;ui.selectedPoint=-1;changed=true;}
    ImGui::TextDisabled(recipe.automatic
        ? "Scene luminance (EV) / average automatic contribution (%%)"
        : "Scene luminance (EV) / requested contribution (%%)");
    if(recipe.automatic && ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatic fusion adapts to local noise, alignment and clipping across colors. These curves summarize its choices; Use suggestion turns them into editable requests.");
    const auto origin=ImGui::GetCursorScreenPos();
    ui.curveOrigin=origin;ui.curveSize=size;
    ImGui::InvisibleButton("##BracketCurves",size);
    const bool hovered=ImGui::IsItemHovered();const auto& io=ImGui::GetIO();
    auto* draw=ImGui::GetWindowDrawList();
    const auto point=[&](double ev,double share){return ImVec2(origin.x+static_cast<float>((ev-ui.viewMin)/(ui.viewMax-ui.viewMin))*size.x,origin.y+(1-static_cast<float>(share))*size.y);};
    auto suggestionRecipe=recipe;suggestionRecipe.knots=currentSuggestion?ui.result->analysis->suggestion:EqualCurves(count);
    ConstrainEnabledCurves(suggestionRecipe);const auto& suggestion=suggestionRecipe.knots;
    const auto& visible=recipe.automatic?suggestion:recipe.knots;
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},ImGui::GetColorU32(ImGuiCol_FrameBg),6);
    draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    for(int i=0;i<=4;++i) {
        const float y=origin.y+size.y*i/4;
        draw->AddLine({origin.x,y},{origin.x+size.x,y},ImGui::GetColorU32(ImGuiCol_Border));
    }
    draw->AddText({origin.x+5,origin.y+3},ImGui::GetColorU32(ImGuiCol_TextDisabled),"100%");
    draw->AddText({origin.x+5,origin.y+size.y-ImGui::GetTextLineHeight()-3},ImGui::GetColorU32(ImGuiCol_TextDisabled),"0%");
    if(ui.result&&ui.result->analysis) {
        const auto& hist=ui.result->analysis->histogram;const float max=*std::max_element(hist.begin(),hist.end());
        for(unsigned b=0;b<256&&max>0;++b) {
            auto a=point(-12+b*20./256,0),z=point(-12+(b+1)*20./256,hist[b]/max*.8);
            draw->AddRectFilled({a.x,z.y},{z.x,a.y},IM_COL32(170,175,190,28));
        }
    }
    for(std::size_t g=0;g<count;++g) {
        auto color=BracketColor(recipe,g);
        if(!recipe.groups[g].enabled) color.w=.25f;
        for(int x=1;x<=256;++x) {
            const double ev0=ui.viewMin+(ui.viewMax-ui.viewMin)*(x-1)/256;
            const double ev1=ui.viewMin+(ui.viewMax-ui.viewMin)*x/256;
            draw->AddLine(point(ev0,Evaluate(visible,ev0)[g]),point(ev1,Evaluate(visible,ev1)[g]),
                ImGui::ColorConvertFloat4ToU32(color),g==ui.selectedGroup?2.5f:1.25f);
            if(x%4<2&&!recipe.automatic) {
                color.w=.4f;draw->AddLine(point(ev0,Evaluate(suggestion,ev0)[g]),point(ev1,Evaluate(suggestion,ev1)[g]),ImGui::ColorConvertFloat4ToU32(color));color.w=1;
            }
        }
    }
    int hit=-1,handle=0;float distance=100;
    const auto check=[&](ImVec2 p,int index,int h) {
        const float d=(p.x-io.MousePos.x)*(p.x-io.MousePos.x)+(p.y-io.MousePos.y)*(p.y-io.MousePos.y);
        if(d<distance) {distance=d;hit=index;handle=h;}
    };
    for(std::size_t i=0;i<visible.size();++i) {
        const auto& k=visible[i];const auto p=point(k.ev,k.share[ui.selectedGroup]);
        draw->AddCircleFilled(p,4,ImGui::ColorConvertFloat4ToU32(BracketColor(recipe,ui.selectedGroup)));check(p,static_cast<int>(i),0);
        if(static_cast<int>(i)==ui.selectedPoint) {
            if(i) {auto h=point(k.incoming,k.left[ui.selectedGroup]);draw->AddLine(p,h,IM_COL32(190,190,200,160));draw->AddCircle(h,3,IM_COL32(230,230,235,255));check(h,static_cast<int>(i),-1);}
            if(i+1<visible.size()) {auto h=point(k.outgoing,k.right[ui.selectedGroup]);draw->AddLine(p,h,IM_COL32(190,190,200,160));draw->AddCircle(h,3,IM_COL32(230,230,235,255));check(h,static_cast<int>(i),1);}
        }
    }
    draw->PopClipRect();
    if(hovered&&ImGui::IsMouseClicked(0)) {
        ui.selectedPoint=hit;ui.selectedHandle=handle;
        if(recipe.automatic) {recipe.knots=suggestion;recipe.automatic=false;changed=true;}
        if(hit<0&&ImGui::IsMouseDoubleClicked(0)&&recipe.knots.size()<128) {
            InsertKnot(recipe.knots,ui.viewMin+(io.MousePos.x-origin.x)/size.x*(ui.viewMax-ui.viewMin));changed=true;
        }
    }
    if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)&&ui.selectedPoint>=0&&ui.selectedPoint<static_cast<int>(recipe.knots.size())) {
        auto& k=recipe.knots[ui.selectedPoint];const auto index=static_cast<std::size_t>(ui.selectedPoint);
        const double ev=ui.viewMin+(io.MousePos.x-origin.x)/size.x*(ui.viewMax-ui.viewMin);
        const double value=1-(io.MousePos.y-origin.y)/size.y;
        auto* shares=ui.selectedHandle<0?&k.left:ui.selectedHandle>0?&k.right:&k.share;
        EditContribution(recipe,*shares,ui.selectedGroup,value,Evaluate(suggestion,ev));
        if(ui.selectedHandle<0&&index) k.incoming=std::clamp(ev,recipe.knots[index-1].outgoing,k.ev);
        else if(ui.selectedHandle>0&&index+1<recipe.knots.size()) k.outgoing=std::clamp(ev,k.ev,recipe.knots[index+1].incoming);
        else if(!ui.selectedHandle) {
            if(index&&index+1<recipe.knots.size()) k.ev=std::clamp(ev,k.incoming,k.outgoing);
            k.left=k.right=k.share;
        }
        changed=true;
    }
    ImGui::TextDisabled("%.1f EV",ui.viewMin);ImGui::SameLine(std::max(0.f,size.x-65));ImGui::TextDisabled("%.1f EV",ui.viewMax);
    if(ui.selectedPoint>=0&&ui.selectedPoint<static_cast<int>(recipe.knots.size())) {
        auto& k=recipe.knots[ui.selectedPoint];float ev=static_cast<float>(k.ev),share=static_cast<float>(k.share[ui.selectedGroup]*100);
        ImGui::SetNextItemWidth(90);
        if(ImGui::InputFloat("EV",&ev,.1f,1,"%.2f")&&ui.selectedPoint>0&&ui.selectedPoint+1<recipe.knots.size()) {k.ev=std::clamp(static_cast<double>(ev),k.incoming,k.outgoing);changed=true;}
        ImGui::SameLine();ImGui::SetNextItemWidth(90);
        if(ImGui::InputFloat("%",&share,1,10,"%.1f")) {EditContribution(recipe,k.share,ui.selectedGroup,share/100,Evaluate(suggestion,k.ev));k.left=k.right=k.share;recipe.automatic=false;changed=true;}
        if(ui.selectedPoint>0&&ui.selectedPoint+1<recipe.knots.size()) {
            if(size.x>360)ImGui::SameLine();if(ImGui::Button("Delete point")) {recipe.knots.erase(recipe.knots.begin()+ui.selectedPoint);ui.selectedPoint=-1;changed=true;}
        }
    }
    ImGui::TextDisabled("Graph range, view only");
    float range[2]={ui.viewMin,ui.viewMax};ImGui::SetNextItemWidth(size.x);
    if(ImGui::DragFloatRange2("##ViewOnly",&range[0],&range[1],.1f,-24,24,"%.1f EV")) {
        ui.viewMin=range[0];ui.viewMax=std::max(range[0]+.5f,range[1]);
    }
    return changed;
}
} // namespace Stack::Editor
