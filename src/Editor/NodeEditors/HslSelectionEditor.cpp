#include "Editor/NodeEditors/HslSelectionEditor.h"
#include "Editor/NodeEditors/HslSelectionControls.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Notifications/NotificationPresenter.h"
#include <cstdio>

namespace Stack::Editor {
using namespace RawRecipe;
namespace {
std::string NewId(const RawColorWarpRecipe& recipe) {
    for(int i=1;;++i) {auto id="hsl-"+std::to_string(i);if(std::none_of(recipe.hslSelections.begin(),recipe.hslSelections.end(),[&](const auto& s){return s.id==id;}))return id;}
}
void Select(HslSelectionEditorState& state,const std::string& id) {
    state.EndGesture();state.selected=id;state.range=-1;state.status.clear();state.reveal.Reset(0);
    if(!id.empty())state.reveal.Retarget(1,Animation::MotionSpec::Tween(.2f));else state.EndInteraction();
}
bool ToneStrip(HslSelection& s,HslSelectionEditorState& state,std::uint64_t revision) {
    bool changed=false;const float scale=ImGui::GetFontSize()/13.f;
    const auto origin=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,height=74*scale;
    const float left=origin.x+7*scale,right=origin.x+width-7*scale,top=origin.y+23*scale,bottom=origin.y+57*scale;
    const auto x=[&](float ev){return left+(ev-state.viewLow)/(state.viewHigh-state.viewLow)*(right-left);};
    const auto ev=[&](float px){return state.viewLow+(px-left)/(right-left)*(state.viewHigh-state.viewLow);};
    auto& bands=state.excludeRange?s.excludedBands:s.bands;
    ImGui::InvisibleButton("Brightness ranges",{width,height});const bool active=ImGui::IsItemActive(),hovered=ImGui::IsItemHovered();
    int nearest=-1,index=-1;float distance=10*scale;
    if(hovered && !state.drawing)for(int i=0;i<int(bands.size());++i) {
        const auto& b=bands[i];const float points[]={b.low,b.high,b.low-b.softness,b.high+b.softness};
        for(int h=0;h<4;++h) {const float y=h<2?top:bottom;const float d=std::hypot(ImGui::GetIO().MousePos.x-x(points[h]),ImGui::GetIO().MousePos.y-y);if(d<distance){distance=d;nearest=h;index=i;}}
    }
    if(ImGui::IsItemActivated() && nearest>=0){state.range=index;state.handle=nearest;state.bandBefore=bands[index];state.pressEv=ev(ImGui::GetIO().MousePos.x);}
    if(active && state.handle>=0 && state.range>=0 && state.range<int(bands.size())) {
        auto& b=bands[state.range];const auto before=b;const float delta=ev(ImGui::GetIO().MousePos.x)-state.pressEv;
        if(state.handle==0)b.low=std::clamp(state.bandBefore.low+delta,-32.f,b.high);
        else if(state.handle==1)b.high=std::clamp(state.bandBefore.high+delta,b.low,32.f);
        else b.softness=std::clamp(state.bandBefore.softness+(state.handle==2?-delta:delta),0.f,8.f);
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){b=state.bandBefore;state.handle=-1;ImGui::ClearActiveID();}
        changed=before.low!=b.low || before.high!=b.high || before.softness!=b.softness;
    }
    if(!active)state.handle=-1;
    if(hovered && ImGui::IsMouseDoubleClicked(0) && nearest<0 && bands.size()<kMaxHslBands) {
        const float center=std::clamp(ev(ImGui::GetIO().MousePos.x),-31.f,31.f);bands.push_back({center-.35f,center+.35f,.35f});state.range=int(bands.size())-1;changed=true;
    }
    if(state.profileRevision!=revision || state.profileSelection!=s.id || changed) {
        for(int i=0;i<96;++i)state.profile[i]=HslBrightnessWeight(s,state.viewLow+(state.viewHigh-state.viewLow)*i/95.f);
        state.profileRevision=revision;state.profileSelection=s.id;
    }
    if(ImGui::IsItemVisible()) {
        auto* draw=ImGui::GetWindowDrawList();const auto ink=ImGui::GetColorU32(ImGuiCol_Text),muted=ImGui::GetColorU32(ImGuiCol_Text,.55f);
        draw->AddText(origin,muted,"Dark");const auto bright=ImGui::CalcTextSize("Bright");draw->AddText({right-bright.x,origin.y},muted,"Bright");
        draw->AddLine({left,bottom},{right,bottom},ImGui::GetColorU32(ImGuiCol_Text,.15f));
        for(int i=0;i<95;++i){const float a=left+(right-left)*i/95.f,b=left+(right-left)*(i+1)/95.f;const float y=bottom-state.profile[i]*(bottom-top);draw->AddRectFilled({a,y},{b,bottom},ImGui::GetColorU32(ImGuiCol_SliderGrab,.20f));draw->AddLine({a,y},{b,bottom-state.profile[i+1]*(bottom-top)},ink,1.6f*scale);}
        draw->PushClipRect({left-5*scale,top-6*scale},{right+5*scale,bottom+7*scale},true);
        for(int i=0;i<int(bands.size());++i) {
            const auto& b=bands[i];const float points[]={b.low,b.high,b.low-b.softness,b.high+b.softness};
            for(int h=0;h<4;++h)draw->AddCircleFilled({x(points[h]),h<2?top:bottom},(i==index&&h==nearest?4.5f:3.f)*scale,h<2?ink:muted,12);
        }draw->PopClipRect();
    }
    return changed;
}
}
bool HslNeedsAnalysis(const HslSelectionEditorState& state) {return !state.selected.empty() && (state.tool==HslImageTool::Sample || state.tool==HslImageTool::Exclude || state.preview!=0 || (ImGui::IsKeyDown(ImGuiKey_H) && !ImGui::GetIO().WantTextInput));}
bool CommitHslImagePick(RawWorkspaceEditContext& context) {
    auto& state=context.State<HslSelectionEditorState>();
    if(!state.pending || !context.canEdit)return false;
    bool changed=false;
    auto& recipe=context.recipe.colorWarp;
    const auto find=[&](){return std::find_if(recipe.hslSelections.begin(),recipe.hslSelections.end(),[&](const auto& s){return s.id==state.selected;});};
    if(state.pending) {
        const auto found=find();
        if(found!=recipe.hslSelections.end() && state.draft && state.draft->id==found->id && state.gestureRevision==context.nodeRevision) {
            // Only selection fields come from image gestures; retain any edits
            // made to the adjustment while the gesture was active.
            found->sourceChroma=state.draft->sourceChroma;found->chromaReach=state.draft->chromaReach;found->warpMask=state.draft->warpMask;
            found->neutralOnly=state.draft->neutralOnly;found->allColors=state.draft->allColors;
            found->hue=state.draft->hue;found->hueWidth=state.draft->hueWidth;
            found->bands=std::move(state.draft->bands);found->excludedBands=std::move(state.draft->excludedBands);
            found->outline=std::move(state.draft->outline);found->areaEnabled=state.draft->areaEnabled;changed=true;
        }else state.status="The edit changed before the selection finished. Draw it again.";
        state.pending=false;state.draft.reset();state.profileRevision=~std::uint64_t(0);
    }
    return changed;
}
bool DrawHslSelections(RawWorkspaceEditContext& context) {
    auto& recipe=context.recipe.colorWarp;auto& state=context.State<HslSelectionEditorState>();bool changed=false;
    if(!context.canEdit){state.EndInteraction();return false;}
    const auto find=[&](){return std::find_if(recipe.hslSelections.begin(),recipe.hslSelections.end(),[&](const auto& s){return s.id==state.selected;});};
    changed|=CommitHslImagePick(context);
    if(state.reveal.IsActive())state.reveal.Advance(ImGui::GetIO().DeltaTime,Notifications::SystemReducedMotion());
    ImGui::Spacing();ImGui::TextDisabled("SELECTIONS");ImGui::SameLine();
    ImGui::BeginDisabled(recipe.version<5 || recipe.hslSelections.size()>=kMaxHslSelections);
    if(ImGui::SmallButton("+ Add")) {
        HslSelection s;s.id=NewId(recipe);
        const int family=std::clamp(context.State<PanelControlState>().selectedFamily,0,7);
        const auto& anchor=ColorWarpHslAnchors()[family];s.hue=ColorWarpHue(anchor.sourceA,anchor.sourceB);
        s.name=std::string(ColorWarpFamilyName(family))+" selection";
        recipe.hslSelections.push_back(s);recipe.version=6;Select(state,s.id);changed=true;
    }ImGui::EndDisabled();
    if(recipe.version<5){ImGui::TextWrapped("Use improved HSL before adding selections.");return changed;}
    for(auto& s:recipe.hslSelections) {
        ImGui::PushID(s.id.c_str());bool toggle=false;
        if(HslSelectionRow(s,state.selected==s.id,toggle))Select(state,state.selected==s.id?"":s.id);
        if(toggle){s.enabled=!s.enabled;changed=true;}ImGui::PopID();
    }
    if(!recipe.hslSelections.empty() || recipe.limitHslToSelections)
        if(ImGui::Checkbox("Limit global to selections",&recipe.limitHslToSelections)){recipe.version=6;changed=true;}
    if(recipe.hslSelections.empty()){ImGui::TextWrapped("Add a selection to shape where global HSL applies.");return changed;}
    auto found=find();if(found==recipe.hslSelections.end()){state.EndInteraction();state.selected.clear();state.preview=0;return changed;}
    auto& s=*found;ImGui::PushID(s.id.c_str());ImGui::Separator();
    const auto header=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
    const float reveal=state.reveal.Value();
    ImGui::GetWindowDrawList()->AddRectFilled(header,{header.x+width,header.y+ImGui::GetFrameHeight()},ImGui::GetColorU32(ImGuiCol_Header,.25f),ImGui::GetFrameHeight()*(.5f-.3f*reveal));
    char name[160];std::snprintf(name,sizeof(name),"%s",s.name.c_str());ImGui::SetNextItemWidth(width-30);
    if(ImGui::InputText("##name",name,sizeof(name))) {s.name=name;changed=true;}
    ImGui::SameLine();if(ImGui::SmallButton("..."))ImGui::OpenPopup("Selection actions");
    if(ImGui::BeginPopup("Selection actions")) {
        const auto index=std::size_t(found-recipe.hslSelections.begin());
        if(ImGui::MenuItem("Duplicate",nullptr,false,recipe.hslSelections.size()<kMaxHslSelections)) {
            auto copy=s;copy.id=NewId(recipe);copy.name+=" copy";recipe.hslSelections.insert(found+1,copy);Select(state,copy.id);ImGui::EndPopup();ImGui::PopID();return true;
        }
        if(ImGui::MenuItem("Move earlier",nullptr,false,index>0)){std::swap(recipe.hslSelections[index],recipe.hslSelections[index-1]);ImGui::EndPopup();ImGui::PopID();return true;}
        if(ImGui::MenuItem("Move later",nullptr,false,index+1<recipe.hslSelections.size())){std::swap(recipe.hslSelections[index],recipe.hslSelections[index+1]);ImGui::EndPopup();ImGui::PopID();return true;}
        if(ImGui::MenuItem("Follow global again")){s.own={};s.influence=1;changed=true;}
        if(ImGui::MenuItem("Delete")){recipe.hslSelections.erase(found);Select(state,"");ImGui::EndPopup();ImGui::PopID();return true;}
        ImGui::EndPopup();
    }
    ImGui::BeginDisabled(!s.enabled || !recipe.enabled);
    ImGui::TextDisabled("SELECT");
    const char* tools[]={"Sample tones","Draw area"};int tool=state.tool==HslImageTool::Sample?0:state.tool==HslImageTool::Outline?1:-1;
    if(HslSegmented("Image selection tool",tools,2,tool,state.toolPosition,true)){state.EndGesture();state.tool=tool==0?HslImageTool::Sample:tool==1?HslImageTool::Outline:HslImageTool::None;state.status.clear();}
    if(state.tool==HslImageTool::Sample)ImGui::TextWrapped("Draw over wanted tones. Separate strokes keep brightness gaps.");
    else if(state.tool==HslImageTool::Outline)ImGui::TextWrapped("Draw an outline around the area. Release to close it. Escape cancels.");
    else if(state.tool==HslImageTool::Exclude)ImGui::TextWrapped("Draw over tones to exclude. Escape ends sampling.");
    else if(state.tool==HslImageTool::EditOutline)ImGui::TextWrapped("Drag outline points. Escape cancels the current drag.");
    if(s.areaEnabled) {
        if(ImGui::SmallButton("Edit outline")){state.EndGesture();state.tool=HslImageTool::EditOutline;}
        ImGui::SameLine();if(ImGui::SmallButton("Whole image")){s.areaEnabled=false;s.outline.clear();state.EndGesture();changed=true;}
        float softness=s.areaSoftness*100;
        if(PanelRoller("Edge softness","edge",softness,0,25,.05f,1,"%.2f%%",state.rollers)){s.areaSoftness=softness*.01f;changed=true;}
    }else ImGui::TextDisabled("Area: whole image");
    if(ImGui::TreeNode("Color range")) {
        changed|=ImGui::Checkbox("All colors",&s.allColors);ImGui::BeginDisabled(s.allColors);
        changed|=PanelRoller("Center","center",s.hue,-180,180,.4f,0,"%.1f deg",state.rollers);
        changed|=PanelRoller("Width","width",s.hueWidth,0,180,.25f,25,"%.1f deg",state.rollers);
        changed|=PanelRoller("Softness","color-soft",s.hueSoftness,0,180,.25f,15,"%.1f deg",state.rollers);
        ImGui::EndDisabled();ImGui::TreePop();
    }
    ImGui::TextUnformatted("Brightness selection");
    changed|=ToneStrip(s,state,context.nodeRevision);
    const char* rangeModes[]={"Include","Exclude"};int rangeMode=state.excludeRange?1:0;
    if(HslSegmented("Range operation",rangeModes,2,rangeMode,state.rangePosition)){state.excludeRange=rangeMode==1;state.range=-1;}
    if(ImGui::SmallButton("+ Range")) {
        auto& bands=state.excludeRange?s.excludedBands:s.bands;
        if(bands.size()<kMaxHslBands){bands.push_back({-1,1,.5f});state.range=int(bands.size())-1;changed=true;}
        else state.status="16 ranges reached. Remove a range before adding another.";
    }
    ImGui::TextDisabled(state.excludeRange?"Editing excluded ranges":"Editing included ranges");
    if(ImGui::TreeNode("Exact ranges")) {
        if(ImGui::DragFloatRange2("View stops",&state.viewLow,&state.viewHigh,.1f,-32,32,"%.1f","%.1f"))state.profileRevision=~std::uint64_t(0);
        state.viewHigh=std::max(state.viewHigh,state.viewLow+.1f);
        auto& bands=state.excludeRange?s.excludedBands:s.bands;
        for(std::size_t i=0;i<bands.size();++i) {
            ImGui::PushID(int(i));auto& b=bands[i];ImGui::Text("Range %zu",i+1);
            changed|=PanelRoller("From","low",b.low,-32,b.high,.025f,-2,"%+.2f EV",state.rollers);
            changed|=PanelRoller("To","high",b.high,b.low,32,.025f,2,"%+.2f EV",state.rollers);
            changed|=PanelRoller("Soft edge","soft",b.softness,0,8,.025f,.5f,"%.2f EV",state.rollers);
            if(ImGui::SmallButton("Remove")){bands.erase(bands.begin()+i);changed=true;ImGui::PopID();break;}ImGui::PopID();
        }
        if(ImGui::SmallButton("Sample excluded tones")){state.EndGesture();state.tool=HslImageTool::Exclude;}
        ImGui::TreePop();
    }
    ImGui::Spacing();ImGui::TextDisabled("ADJUST");
    float influence=s.influence*100;
    ImGui::BeginDisabled(s.own[0]&&s.own[1]&&s.own[2]);
    if(PanelRoller("Global influence","influence",influence,0,200,.4f,100,"%.1f%%",state.rollers)){s.influence=influence*.01f;changed=true;}
    ImGui::EndDisabled();
    if(state.referenceRevision!=context.nodeRevision || state.referenceSelection!=s.id || changed) {
        state.globalReference=EvaluateColorWarpHsl(PrepareColorWarpHsl(recipe),s.hue);
        state.ownReference=EvaluateColorWarpHsl(HslOwnCoefficients(s),s.hue);
        state.referenceRevision=context.nodeRevision;state.referenceSelection=s.id;
    }
    const auto& global=state.globalReference;
    const char* labels[]={"Hue","Saturation","Brightness"};
    for(int c=0;c<3;++c) {
        ImGui::PushID(c);bool own=s.own[c];
        if(ImGui::Checkbox(labels[c],&own)){if(own)HslDetachChannel(s,c,recipe);else s.own[c]=false;state.ownReference=EvaluateColorWarpHsl(HslOwnCoefficients(s),s.hue);changed=true;}
        ImGui::SameLine();ImGui::TextDisabled(own?"Own value":"Follows global");
        if(own) {
            const auto old=state.ownReference;float value=c==0?old[c]:(old[c]+(c==1?1.f:0.f))*100;
            const float reference=c==0?global[c]:(global[c]+(c==1?1.f:0.f))*100;
            ImGui::TextDisabled("Global reference: %+.1f%s",reference,c==1?"%":"");
            if(PanelRoller("Local value","own",value,c==0?-180.f:c==1?0.f:-100.f,c==0?180.f:c==1?600.f:100.f,.4f,c==1?100.f:0.f,c==1?"%.1f%%":"%+.1f",state.rollers)) {
                const float desired=c==0?value:value*.01f-(c==1?1.f:0.f);for(auto& v:s.values)v[c]+=desired-old[c];changed=true;
            }
        }ImGui::PopID();
    }
    ImGui::TextDisabled("Preview");const char* modes[]={"Off","Overlay","Mask"};
    HslSegmented("Selection preview",modes,3,state.preview,state.previewPosition);
    if(state.preview && state.overlaySelection==s.id && state.overlayRevision==state.coverageKey && state.matching==0)ImGui::TextWrapped("No matching pixels");
    if(!state.status.empty())ImGui::TextWrapped("%s",state.status.c_str());
    ImGui::EndDisabled();ImGui::PopID();
    if(changed){state.profileRevision=~std::uint64_t(0);recipe.version=6;}
    return changed;
}
}
