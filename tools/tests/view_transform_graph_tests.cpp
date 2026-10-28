// Native ImGui events through the entire production graph and real tone math.
// JSON is an in-memory edit draft; no project, image, or saved-file operations.
#include <imgui.h>
#include <imgui_internal.h>
#include "ThirdParty/json.hpp"
#include "Raw/ViewTransform.h"
#include "Raw/HdrDisplayMapping.h"
#include <array>
#include <cstdio>
#include <iostream>
#include <stdexcept>
// INSERT_PRODUCTION_HELPERS
namespace {
void Require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
struct Fixture {
    nlohmann::json view={{"blackEv",-8.f},{"whiteEv",4.f},{"contrastPivotEv",0.f}};
    int handle=0;bool disabled=false,show=true,changed=false,active=false,openModal=false;
    ImRect item;
    Fixture() {
        ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.DisplaySize={800,600};io.DeltaTime=1.f/60;
        unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);
        Frame();Frame();
    }
    ~Fixture(){ImGui::DestroyContext();}
    void Frame() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,600});ImGui::Begin("View");
        changed=false;
        if(show) {ImGui::BeginDisabled(disabled);changed=DrawViewTransformGraph(view,{400,200},handle);
            item={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};active=ImGui::IsItemActive();ImGui::EndDisabled();}
        else {ImGui::Button("Other",{400,200});active=false;}
        if(openModal) {ImGui::OpenPopup("Modal");openModal=false;}
        if(ImGui::BeginPopupModal("Modal")) {ImGui::Button("Modal owner");ImGui::EndPopup();}
        ImGui::End();ImGui::Render();
    }
    ImVec2 Marker(int which)const {
        const float fraction=which==1?.2f:which==2?.8f:.6f;
        return {item.Min.x+27+(400-32)*fraction,item.Min.y+80};
    }
    void Move(ImVec2 p){ImGui::GetIO().AddMousePosEvent(p.x,p.y);Frame();}
    void Press(ImVec2 p){Move(p);ImGui::GetIO().AddMouseButtonEvent(0,true);Frame();}
    void Release(){ImGui::GetIO().AddMouseButtonEvent(0,false);Frame();}
    void RequireStopped(const nlohmann::json& before,const char* message) {
        Require(view==before && !changed && handle==0,message);
    }
};
void Ordinary() {
    for(int marker:{1,2,3}) {
        Fixture f;f.Press(f.Marker(marker));Require(f.handle==marker&&f.active,"native press chooses intended marker");
        const auto before=f.view;auto p=f.Marker(marker);p.x+=15;f.Move(p);
        Require(f.changed&&f.active&&f.view!=before,"ordinary drag edits while active");
        const auto released=f.view;f.Release();f.RequireStopped(released,"release ends without extra edit");
        Require(!f.active,"release closes native interaction");
    }
}
void Disabled() {
    Fixture f;f.Press(f.Marker(1));const auto before=f.view;auto p=f.Marker(1);p.x+=60;
    f.disabled=true;f.Move(p);f.RequireStopped(before,"disabled mid-drag must not mutate");Require(!f.active,"disabled ends native interaction");
    f.disabled=false;p.x+=20;f.Move(p);f.RequireStopped(before,"reenable must not replay held drag");
    f.Release();f.Press(f.Marker(2));Require(f.handle==2&&f.active,"reenable fresh press starts normally");
}
void LostOwner() {
    Fixture f;f.Press(f.Marker(1));const auto before=f.view;
    ImGui::ClearActiveID();auto p=f.Marker(1);p.x+=60;f.Move(p);
    f.RequireStopped(before,"cleared native owner cannot mutate");Require(!f.active,"clear owner ends host interaction");
}
void Hidden(bool sameItem,bool margin) {
    Fixture f;f.Press(f.Marker(1));f.show=false;f.Release();const auto before=f.view;
    if(sameItem) {
        f.show=true;
        const auto p=margin?ImVec2(f.item.Min.x+4,f.item.Min.y+80):f.Marker(2);
        // New press arrives on the first frame the graph is submitted again.
        ImGui::GetIO().AddMousePosEvent(p.x,p.y);ImGui::GetIO().AddMouseButtonEvent(0,true);f.Frame();f.Frame();
        if(margin)f.RequireStopped(before,"fresh same-ID margin press cannot inherit hidden handle");
        else Require(f.handle==2,"fresh same-ID valid press reselects marker");
    } else {
        f.Press(f.Marker(1));const auto otherOwner=ImGui::GetCurrentContext()->ActiveId;
        Require(otherOwner!=0,"unrelated button owns press");
        f.show=true;auto p=f.Marker(1);p.x+=60;f.Move(p);
        f.RequireStopped(before,"unrelated held press after hidden release cannot edit");
        Require(ImGui::GetCurrentContext()->ActiveId==otherOwner,"graph cannot clear unrelated native owner");
    }
}
void Modal() {
    Fixture f;f.Press(f.Marker(1));f.openModal=true;f.Frame();const auto before=f.view;
    auto p=f.Marker(1);p.x+=60;f.Move(p);
    f.RequireStopped(before,"modal takeover must stop graph mutation");Require(!f.active,"modal owns native input");
}
void Escape() {
    Fixture f;f.Press(f.Marker(1));const auto before=f.view;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);f.Frame();f.Frame();
    f.RequireStopped(before,"Escape must end graph drag");Require(!f.active,"Escape closes native interaction for host rollback");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);f.Frame();auto p=f.Marker(1);p.x+=60;f.Move(p);
    f.RequireStopped(before,"Escape must not replay while pointer remains held");
}
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto test) {try {test();} catch(const std::exception& e) {
        ++failures;std::cerr<<name<<": "<<e.what()<<'\n';}};
    run("ordinary",Ordinary);run("disabled",Disabled);run("lost owner",LostOwner);
    run("hidden unrelated",[]{Hidden(false,false);});run("same-ID margin",[]{Hidden(true,true);});
    run("same-ID fresh marker",[]{Hidden(true,false);});run("modal",Modal);run("Escape",Escape);
    if(!failures)std::cout<<"View transform graph ownership passed\n";
    return failures?1:0;
}
