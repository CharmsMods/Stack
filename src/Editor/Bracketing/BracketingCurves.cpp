#include "BracketingSession.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
namespace {
ImVec4 ColorForSlot(int slot) {
    static const ImVec4 palette[]={{.25f,.85f,1.f,1},{1.f,.61f,.22f,1},{.9f,.38f,.85f,1},{.4f,.9f,.42f,1},{1.f,.87f,.3f,1},{.6f,.55f,1.f,1},{1.f,.4f,.4f,1},{.45f,1.f,.8f,1}};
    if(slot<8)return palette[std::max(0,slot)];
    float r,g,b;ImGui::ColorConvertHSVtoRGB(std::fmod(slot*.61803398875f,1.f),.5f+(slot%2)*.35f,.95f,r,g,b);return {r,g,b,1};
}

void PrepareCurvePlot(BracketingSession& ui, bool force) {
    auto& cache=ui.curvePlot;
    const auto analysis=ui.result?ui.result->analysis:nullptr;
    if(!force&&cache.revision==ui.controlsRevision&&cache.analysis.lock()==analysis&&
        cache.minimum==ui.viewMin&&cache.maximum==ui.viewMax&&cache.automatic==ui.recipe.automatic)return;
    const auto count=ui.recipe.groups.size();
    const bool currentSuggestion=analysis&&!analysis->suggestion.empty()&&analysis->suggestion.front().share.size()==count;
    auto suggestedRecipe=ui.recipe;
    suggestedRecipe.knots=currentSuggestion?analysis->suggestion:Raw::Bracketing::EqualCurves(count);
    Raw::Bracketing::ConstrainEnabledCurves(suggestedRecipe);
    cache.suggestion=std::move(suggestedRecipe.knots);
    const auto& visible=ui.recipe.automatic?cache.suggestion:ui.recipe.knots;
    for(std::size_t x=0;x<cache.visible.size();++x) {
        const double ev=ui.viewMin+(ui.viewMax-ui.viewMin)*x/256;
        Raw::Bracketing::Evaluate(visible,ev,cache.visible[x]);
        Raw::Bracketing::Evaluate(cache.suggestion,ev,cache.suggested[x]);
    }
    cache.revision=ui.controlsRevision;cache.analysis=analysis;
    cache.minimum=ui.viewMin;cache.maximum=ui.viewMax;cache.automatic=ui.recipe.automatic;
}
}

ImVec4 BracketColor(const Raw::Bracketing::BracketingRecipe& recipe,std::size_t group) {
    const auto slots=Raw::Bracketing::ColorSlots(recipe);
    return ColorForSlot(group<slots.size()?slots[group]:0);
}

void PrepareBracketingControls(BracketingSession& ui) {
    auto& cache=ui.controls;
    if(cache.revision==ui.controlsRevision)return;
    cache.sourceIndices.clear();cache.readingCaptions.clear();
    for(std::size_t source=0;source<ui.sources.size();++source) {
        cache.sourceIndices.emplace(ui.sources[source].frameId,source);
        if(!ui.sources[source].inspected)cache.readingCaptions.push_back(
            ui.sources[source].path.filename().string()+" / reading capture settings...");
    }
    cache.colors.clear();
    for(const auto slot:Raw::Bracketing::ColorSlots(ui.recipe))cache.colors.push_back(ColorForSlot(slot));
    cache.hasContributions=false;
    // Groups can be split manually without creating another exposure level.
    // Use the recipe's capture EVs, rather than noisy measured calibration or
    // group count, and the same 0.15 EV scale used for capture grouping.
    if(ui.recipe.reconstruction!=Raw::Bracketing::ReconstructionMode::Panorama) {
        bool haveFirst=false;double minimum=0,maximum=0;std::size_t enabledGroups=0;
        for(const auto& group:ui.recipe.groups) {
            if(!group.enabled)continue;
            bool active=false;
            for(const auto& frame:group.frames) {
                if(!frame.enabled||!std::isfinite(frame.relativeEv))continue;
                active=true;
                if(!haveFirst){minimum=maximum=frame.relativeEv;haveFirst=true;}
                else {minimum=std::min(minimum,frame.relativeEv);maximum=std::max(maximum,frame.relativeEv);}
            }
            enabledGroups+=active;
        }
        cache.hasContributions=enabledGroups>1&&maximum-minimum>.15+1e-6;
    }
    cache.revision=ui.controlsRevision;
}

bool DrawBracketingContributionTool(BracketingSession& ui) {
    PrepareBracketingControls(ui);
    if(!ui.controls.hasContributions)return false;
    ui.selectedGroup=std::clamp(ui.selectedGroup,0,static_cast<int>(ui.recipe.groups.size())-1);
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##ContributionGroup",ui.recipe.groups[ui.selectedGroup].name.c_str())) {
        for(std::size_t group=0;group<ui.recipe.groups.size();++group) {
            const auto& candidate=ui.recipe.groups[group];
            const bool active=candidate.enabled&&std::any_of(candidate.frames.begin(),candidate.frames.end(),[](const auto& frame){return frame.enabled;});
            ImGui::BeginDisabled(!active);
            ImGui::PushID(candidate.id.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text,ui.controls.colors[group]);
            if(ImGui::Selectable(candidate.name.c_str(),ui.selectedGroup==static_cast<int>(group))) {
                ui.selectedGroup=static_cast<int>(group);ui.selectedPoint=-1;
            }
            ImGui::PopStyleColor();ImGui::PopID();ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    const float scale=ImGui::GetFontSize()/13.f;
    const float width=std::max(80.f,ImGui::GetContentRegionAvail().x);
    const float height=std::clamp(width*.46f,148.f*scale,240.f*scale);
    return DrawBracketingCurves(ui,{width,height});
}

bool DrawBracketingCurves(BracketingSession& ui,const ImVec2& size) {
    using namespace Raw::Bracketing;
    auto& recipe=ui.recipe;const auto count=recipe.groups.size();if(!count) return false;
    ui.selectedGroup=std::clamp(ui.selectedGroup,0,static_cast<int>(count)-1);
    bool changed=false;
    const bool currentSuggestion=ui.result&&ui.result->analysis&&!ui.result->analysis->suggestion.empty()&&
        ui.result->analysis->suggestion.front().share.size()==count;
    ImGui::BeginDisabled(!currentSuggestion);
    if(ImGui::Button("Use suggestion")) {
        recipe.knots=ui.result->analysis->suggestion;recipe.automatic=false;changed=true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Reset")&&!recipe.automatic) {recipe.automatic=true;ui.selectedPoint=-1;changed=true;}
    ImGui::TextDisabled(recipe.automatic
        ? "Scene luminance (EV) / average automatic contribution (%%)"
        : "Scene luminance (EV) / requested contribution (%%)");
    if(recipe.automatic && ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatic fusion adapts to local noise, alignment and clipping across colors. These curves summarize its choices; Use suggestion turns them into editable requests.");
    PrepareBracketingControls(ui);PrepareCurvePlot(ui,changed);
    const auto origin=ImGui::GetCursorScreenPos();
    ui.curveOrigin=origin;ui.curveSize=size;
    ImGui::InvisibleButton("##BracketCurves",size);
    const bool hovered=ImGui::IsItemHovered();const auto& io=ImGui::GetIO();
    auto* draw=ImGui::GetWindowDrawList();
    const auto point=[&](double ev,double share){return ImVec2(origin.x+static_cast<float>((ev-ui.viewMin)/(ui.viewMax-ui.viewMin))*size.x,origin.y+(1-static_cast<float>(share))*size.y);};
    const auto& suggestion=ui.curvePlot.suggestion;
    const auto& visible=recipe.automatic?suggestion:recipe.knots;
    const auto plotStyle=GraphUI::ResolveGraphPlotStyle();
    GraphUI::DrawGraphBackdrop(*draw,origin,{origin.x+size.x,origin.y+size.y},plotStyle);
    draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
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
        auto color=ui.controls.colors[g];
        if(!recipe.groups[g].enabled) color.w=.25f;
        for(int x=1;x<=256;++x) {
            const double ev0=ui.viewMin+(ui.viewMax-ui.viewMin)*(x-1)/256;
            const double ev1=ui.viewMin+(ui.viewMax-ui.viewMin)*x/256;
            draw->AddLine(point(ev0,ui.curvePlot.visible[x-1][g]),point(ev1,ui.curvePlot.visible[x][g]),
                ImGui::ColorConvertFloat4ToU32(color),g==ui.selectedGroup?plotStyle.selectedWidth:plotStyle.curveWidth);
            if(x%4<2&&!recipe.automatic) {
                const float alpha=color.w;color.w*=.4f;draw->AddLine(point(ev0,ui.curvePlot.suggested[x-1][g]),point(ev1,ui.curvePlot.suggested[x][g]),ImGui::ColorConvertFloat4ToU32(color));color.w=alpha;
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
        draw->AddCircleFilled(p,4,ImGui::ColorConvertFloat4ToU32(ui.controls.colors[ui.selectedGroup]));check(p,static_cast<int>(i),0);
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
    if(changed)ui.curvePlot.revision=std::numeric_limits<std::uint64_t>::max();
    return changed;
}
} // namespace Stack::Editor
