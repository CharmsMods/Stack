// Full production editor + actual scene-tone/recipe math; tooltip is display only.
#include <imgui.h>
#include <imgui_internal.h>
#include "Editor/Internal/RawLab/SceneToneEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
ImRect graphItem;ImGuiID graphId=0;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
namespace ImGui {
bool RecordedInvisibleButton(const char* id,const ImVec2& size,ImGuiButtonFlags flags) {
    const bool result=InvisibleButton(id,size,flags);
    graphItem={GetItemRectMin(),GetItemRectMax()};graphId=GetItemID();return result;
}
}
namespace Stack::Editor::RawLabInternal {void LabTooltip(const char*,ImGuiHoveredFlags){}}
#ifndef STACK_SCENE_TONE_SOURCE
#define STACK_SCENE_TONE_SOURCE "Editor/Internal/RawLab/SceneToneEditor.cpp"
#endif
#define InvisibleButton RecordedInvisibleButton
#include STACK_SCENE_TONE_SOURCE
#undef InvisibleButton
namespace {
using namespace Stack::Editor::RawLabInternal;
Stack::RawRecipe::SceneTone CompressedTone() {
    Stack::RawRecipe::SceneTone tone;
    // Identity below middle gray, then four scene stops per output stop.
    tone.curve={{0,0},{.5f,.5f},{1,.625f}};
    return tone;
}
void LocalTonePersistence() {
    using namespace Stack::RawRecipe;
    auto tone=CompressedTone();
    auto legacy=SerializeSceneTone(tone);
    for(const char* key:{"localAmount","localRadius","localRangeEv"}) legacy.erase(key);
    const auto old=ReadSceneTone(nlohmann::json::parse(legacy.dump()));
    Require(old.localAmount==0 && old.localRadius==.04f && old.localRangeEv==.5f &&
        !IsLocalSceneToneActive(old),"existing pointwise edits must leave local contrast off");
    for(float ev:{-12.f,0.f,4.f,12.f,24.f})
        Require(EvaluateLocalSceneToneEv(old,ev,6)==EvaluateSceneToneEv(tone,ev),
            "missing local settings changed an existing curve's output");
    tone.localAmount=.85f;tone.localRadius=.12f;tone.localRangeEv=.9f;
    const auto saved=SerializeSceneTone(tone);
    const auto restored=ReadSceneTone(nlohmann::json::parse(saved.dump()));
    Require(restored.localAmount==tone.localAmount && restored.localRadius==tone.localRadius &&
        restored.localRangeEv==tone.localRangeEv && SerializeSceneTone(restored)==saved,
        "local tone settings or authored points did not survive save/reopen");
}
void LocalToneMapping() {
    using namespace Stack::RawRecipe;
    auto tone=CompressedTone();
    const float low=5.75f,high=6.25f,base=6;
    const float pointwiseLow=EvaluateSceneToneEv(tone,low),pointwiseHigh=EvaluateSceneToneEv(tone,high);
    Require(pointwiseHigh-pointwiseLow<.2f,"local contrast fixture must compress pointwise detail");
    tone.localAmount=1;
    const float localLow=EvaluateLocalSceneToneEv(tone,low,base),localHigh=EvaluateLocalSceneToneEv(tone,high,base);
    Require(IsLocalSceneToneActive(tone) && std::abs(localHigh-localLow-(high-low))<2e-5f,
        "regional tone mapping must retain local scene contrast");
    Require(EvaluateLocalSceneToneEv(tone,base,base)==EvaluateSceneToneEv(tone,base),
        "a uniform region must match the authored pointwise curve");
    tone.localAmount=.25f;
    Require(std::abs(EvaluateLocalSceneToneEv(tone,high,base)-
        (pointwiseHigh+.25f*(localHigh-pointwiseHigh)))<2e-5f,
        "partial local preservation must blend in scene EV");
    Require(EvaluateLocalSceneToneEv(tone,high,std::numeric_limits<float>::quiet_NaN())==pointwiseHigh,
        "an unavailable regional guide must retain the pointwise curve");
    const std::array<float,3> rgb{3.f,1.5f,-.2f},weights{.2627f,.678f,.0593f};
    const auto pointwiseRgb=ApplySceneTone(tone,rgb,weights);
    tone.localAmount=0;
    Require(ApplySceneTone(tone,rgb,weights)==pointwiseRgb &&
        EvaluateLocalSceneToneEv(tone,high,base)==pointwiseHigh,
        "single-pixel processing must remain explicitly pointwise");
    SceneTone identity;identity.localAmount=1;
    Require(!IsLocalSceneToneActive(identity),"an identity curve must not require a spatial guide");
    tone.localAmount=1;tone.enabled=false;
    Require(!IsLocalSceneToneActive(tone) && EvaluateLocalSceneToneEv(tone,high,base)==high,
        "disabled local tone must preserve input");
    tone.enabled=true;tone.direct=false;tone.contrast=.75f;
    Require(IsLocalSceneToneActive(tone) && std::abs(EvaluateLocalSceneToneEv(tone,high,base)-
        EvaluateLocalSceneToneEv(tone,low,base)-(high-low))<2e-5f,
        "the slope curve representation must retain the same local-detail contract");
}
void LocalToneBounds() {
    using namespace Stack::RawRecipe;
    SceneTone tone;
    tone.localAmount=std::numeric_limits<float>::quiet_NaN();tone.localRadius=100;tone.localRangeEv=0;
    const auto bounded=SanitizeSceneTone(tone);
    Require(bounded.localAmount==0 && bounded.localRadius==.25f && bounded.localRangeEv==.05f,
        "local contrast settings must sanitize nonfinite and out-of-range values");
    tone.localAmount=2;tone.localRadius=-1;tone.localRangeEv=std::numeric_limits<float>::infinity();
    const auto restored=ReadSceneTone(SerializeSceneTone(tone));
    Require(restored.localAmount==1 && restored.localRadius==.002f && restored.localRangeEv==.5f,
        "serialized local contrast settings must retain finite supported limits");
}
struct Fixture {
    Stack::RawRecipe::SceneTone tone;
    int selected=-1,dragging=-1;float lo=0,hi=2;
    bool disabled=false,show=true,changed=false,openModal=false;
    Fixture(){tone.direct=false;ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.DisplaySize={800,600};io.DeltaTime=1.f/60;unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);Frame();Frame();}
    ~Fixture(){ImGui::DestroyContext();}
    void Frame(){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,400});ImGui::Begin("Tone");changed=false;
        if(show){ImGui::BeginDisabled(disabled);changed=DrawSceneToneEditor(tone,true,selected,dragging,lo,hi,{}, {},RawLabControlSection::Graph);ImGui::EndDisabled();}
        else ImGui::Button("Other",{480,230});
        if(openModal){ImGui::OpenPopup("Modal");openModal=false;}
        if(ImGui::BeginPopupModal("Modal")){ImGui::Button("Modal owner");ImGui::EndPopup();}
        ImGui::End();ImGui::Render();}
    ImVec2 Point(int index=3)const {const auto&p=tone.points[index];return {graphItem.Min.x+30+(graphItem.GetWidth()-38)*(p.ev+16)/32,
        graphItem.Min.y+10+(graphItem.GetHeight()-38)*(hi-Stack::RawRecipe::EvaluateSceneToneContrast(tone,p.ev))/(hi-lo)};}
    void Move(ImVec2 p){ImGui::GetIO().AddMousePosEvent(p.x,p.y);Frame();}
    void Press(ImVec2 p,int button=0){Move(p);ImGui::GetIO().AddMouseButtonEvent(button,true);Frame();}
    void Release(int button=0){ImGui::GetIO().AddMouseButtonEvent(button,false);Frame();}
    bool Active()const{return graphId!=0&&ImGui::GetCurrentContext()->ActiveId==graphId;}
    auto Snapshot()const{return Stack::RawRecipe::SerializeSceneTone(tone);}
    void Stopped(const nlohmann::json& before,const char* message){Require(Snapshot()==before&&!changed&&dragging==-1,message);}
};
void Ordinary(){Fixture f;f.Press(f.Point());Require(f.selected==3&&f.dragging==3&&f.Active(),"point press selects native owner");auto before=f.Snapshot();auto p=f.Point();p.y-=20;f.Move(p);Require(f.changed&&f.Snapshot()!=before,"ordinary drag edits");before=f.Snapshot();f.Release();f.Stopped(before,"release closes without edit");Require(!f.Active(),"host sees released interaction");}
void Disabled(){Fixture f;f.Press(f.Point());auto before=f.Snapshot();f.disabled=true;auto p=f.Point();p.y-=40;f.Move(p);f.Stopped(before,"disabled drag must not mutate");Require(!f.Active(),"disabled ends native interaction");f.disabled=false;p.y-=10;f.Move(p);f.Stopped(before,"reenable cannot replay");f.Release();f.Press(f.Point(5));Require(f.dragging==5,"fresh press after reenable works");}
void LostOwner(){Fixture f;f.Press(f.Point());auto before=f.Snapshot();ImGui::ClearActiveID();auto p=f.Point();p.y-=40;f.Move(p);f.Stopped(before,"lost owner cannot mutate");Require(!f.Active(),"host sees lost owner inactive");}
void Hidden(bool same,bool valid){Fixture f;f.Press(f.Point());f.show=false;f.Release();auto before=f.Snapshot();
    if(same){f.show=true;auto p=valid?f.Point(5):ImVec2(graphItem.Min.x+4,graphItem.Min.y+80);
        ImGui::GetIO().AddMousePosEvent(p.x,p.y);ImGui::GetIO().AddMouseButtonEvent(0,true);f.Frame();f.Frame();
        if(valid)Require(f.dragging==5,"same ID fresh press selects new point");else f.Stopped(before,"same ID margin cannot inherit point");}
    else{f.Press(f.Point());const auto owner=ImGui::GetCurrentContext()->ActiveId;f.show=true;auto p=f.Point();p.y-=40;f.Move(p);f.Stopped(before,"hidden release and unrelated press cannot edit");Require(ImGui::GetCurrentContext()->ActiveId==owner,"other owner preserved");}}
void Modal(){Fixture f;f.Press(f.Point());f.openModal=true;f.Frame();auto before=f.Snapshot();auto p=f.Point();p.y-=40;f.Move(p);f.Stopped(before,"modal takeover cannot edit");Require(!f.Active(),"modal ends graph input");}
void Escape(){Fixture f;f.Press(f.Point());auto before=f.Snapshot();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);f.Frame();f.Frame();f.Stopped(before,"Escape cancels point drag");Require(!f.Active(),"Escape leaves inactive for host rollback");ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);f.Frame();auto p=f.Point();p.y-=40;f.Move(p);f.Stopped(before,"Escape held pointer cannot replay");}
void AddDelete(bool disabled){Fixture f;f.disabled=disabled;f.Frame();const auto before=f.Snapshot();const auto count=f.tone.points.size();
    const ImVec2 empty(graphItem.Min.x+30+(graphItem.GetWidth()-38)*.7f,graphItem.Min.y+35);
    f.Press(empty);f.Release();f.Press(empty); // native double click adds at an empty graph point
    Require(f.tone.points.size()==count+(disabled?0:1),"double click add respects disabled state");f.Release();
    f.Press(f.Point(3),1);Require(f.tone.points.size()==count,"right click delete respects disabled state");f.Release(1);
    if(disabled)Require(f.Snapshot()==before&&f.selected==-1,"disabled add/delete/selection preserve recipe");}
}
int main(){int failures=0;const auto run=[&](const char*name,auto test){try{test();}catch(const std::exception&e){++failures;std::cerr<<name<<": "<<e.what()<<'\n';}};
run("local tone persistence",LocalTonePersistence);run("local tone mapping",LocalToneMapping);run("local tone bounds",LocalToneBounds);
run("ordinary",Ordinary);run("disabled",Disabled);run("lost owner",LostOwner);run("hidden unrelated",[]{Hidden(false,false);});run("same ID margin",[]{Hidden(true,false);});run("same ID point",[]{Hidden(true,true);});run("modal",Modal);run("Escape",Escape);run("add/delete",[]{AddDelete(false);});run("disabled add/delete",[]{AddDelete(true);});
if(!failures)std::cout<<"Scene tone model and native ownership passed\n";return failures?1:0;}
