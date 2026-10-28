// Full production editor + actual scene-tone/recipe math; tooltip is display only.
#include <imgui.h>
#include <imgui_internal.h>
#include "Editor/Internal/RawLab/SceneToneEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <iostream>
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
run("ordinary",Ordinary);run("disabled",Disabled);run("lost owner",LostOwner);run("hidden unrelated",[]{Hidden(false,false);});run("same ID margin",[]{Hidden(true,false);});run("same ID point",[]{Hidden(true,true);});run("modal",Modal);run("Escape",Escape);run("add/delete",[]{AddDelete(false);});run("disabled add/delete",[]{AddDelete(true);});
if(!failures)std::cout<<"Scene tone native ownership passed\n";return failures?1:0;}
