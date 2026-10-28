// Full response wrapper/shared interaction, real curve math/style. Glass-only
// presentation doubles keep the native input test independent of GL services.
#include <imgui.h>
#include <imgui_internal.h>
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include "Utils/GlassControls.h"
#include <iostream>
#include <stdexcept>
namespace ImGuiExtras {
GlassBackgroundScope::GlassBackgroundScope(ImDrawList& d):draw_(d){} GlassBackgroundScope::~GlassBackgroundScope(){} void GlassBackgroundScope::End(){}
bool IsGraphPointPresentation(){return false;}
bool DrawGlassPoint(ImDrawList&,ImVec2,float,ImGuiID,bool,bool,bool,GlassMotionRole,float,const Stack::Animation::ControlInput*,const Stack::Renderer::GlassChrome::JoinGroup*){return false;}
}
namespace {
using namespace Stack::Editor::RawLabInternal;
void Require(bool ok,const char*m){if(!ok)throw std::runtime_error(m);}
struct Fixture {
    std::vector<Stack::RawRecipe::RawBezierCurvePoint> points={{0,0,{1},{1}},{.5f,.5f,{1},{1}},{1,1,{1},{1}}};
    Stack::EditorModuleTypes::RawCurveGraphUiState state;
    int selected=-1;bool disabled=false,show=true,changed=false,modal=false,exposureMode=false;
    ImVec2 min,max;ImGuiID id=0;float exposure=0;
    Fixture(){ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);Frame();Frame();}
    ~Fixture(){ImGui::DestroyContext();}
    void Frame(){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,400});ImGui::Begin("Response");changed=false;
        if(show){ImGui::BeginDisabled(disabled);
            if(exposureMode){ImGui::InvisibleButton("##response",{400,230},ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
                const ImRect box(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());min={box.Min.x+6,box.Min.y+6};max={box.Max.x-6,box.Max.y-6};
                changed=DrawBezierCurveInteraction({min,max},points,state,selected,IM_COL32_WHITE,IM_COL32_WHITE,0,{},true,&exposure);}
            else changed=DrawFramelessResponseBezier("##response",points,state,selected,{}, {400,230},1,min,max,.5f);
            id=ImGui::GetItemID();ImGui::EndDisabled();}
        else ImGui::Button("Other",{400,230});
        if(modal){ImGui::OpenPopup("Modal");modal=false;}if(ImGui::BeginPopupModal("Modal")){ImGui::Button("Modal owner");ImGui::EndPopup();}
        ImGui::End();ImGui::Render();}
    ImVec2 At(float x,float y)const{return {min.x+x*(max.x-min.x),max.y-y*(max.y-min.y)};}
    void Move(ImVec2 p){ImGui::GetIO().AddMousePosEvent(p.x,p.y);Frame();}
    void Press(ImVec2 p,int button=0){Move(p);ImGui::GetIO().AddMouseButtonEvent(button,true);Frame();}
    void Release(int button=0){ImGui::GetIO().AddMouseButtonEvent(button,false);Frame();}
    void Click(ImVec2 p,int button=0){Press(p,button);Release(button);}
    bool Active()const{return id&&ImGui::GetCurrentContext()->ActiveId==id;}
    auto Snapshot()const{nlohmann::json out=nlohmann::json::array();for(const auto&p:points)out.push_back({p.x,p.y,p.incoming.strength,p.incoming.manual,p.incoming.offsetX,p.incoming.offsetY,p.outgoing.strength,p.outgoing.manual,p.outgoing.offsetX,p.outgoing.offsetY});return out;}
    void Stopped(const nlohmann::json&before,const char*m){Require(Snapshot()==before&&!changed&&state.draggingPoint==-1&&state.draggingSegment==-1&&state.draggingSide==-1,m);}
    void Grab(bool segment){if(!segment){Press(At(.5f,.5f));Require(state.draggingPoint==1,"point acquired");return;}
        Click(At(.25f,.25f));Require(state.chooserOpen&&state.selectedSegment==0,"segment chooser opens");
        const auto center=At(.25f,.25f);Click({center.x-25,center.y-22.5f});Require(state.selectedSide==0,"left tangent selected");
        const auto edge=Stack::RawRecipe::BuildRawBezierSegment(points,0);Press(At(edge.leftHandleX,edge.leftHandleY));Require(state.draggingSegment==0&&state.draggingSide==0,"tangent acquired");}
};
void Ordinary(bool segment){Fixture f;f.Grab(segment);const auto before=f.Snapshot();auto p=ImGui::GetIO().MousePos;p.y-=30;f.Move(p);Require(f.changed&&f.Active()&&f.Snapshot()!=before,"ordinary gesture edits");const auto done=f.Snapshot();f.Release();f.Stopped(done,"ordinary release closes without extra edit");Require(!f.Active(),"released interaction inactive");}
void Disabled(bool segment){Fixture f;f.Grab(segment);const auto before=f.Snapshot();auto p=ImGui::GetIO().MousePos;p.y-=30;f.disabled=true;f.Move(p);f.Stopped(before,"disabled gesture must not mutate");Require(!f.Active(),"disabled ends native owner");f.disabled=false;p.y-=10;f.Move(p);f.Stopped(before,"reenable held press cannot replay");f.Release();f.Grab(false);}
void Takeover(bool modal){Fixture f;f.Grab(false);if(modal){f.modal=true;f.Frame();}else ImGui::ClearActiveID();const auto before=f.Snapshot();f.Move(f.At(.5f,.7f));f.Stopped(before,"lost owner cannot edit");Require(!f.Active(),"lost owner inactive");}
void Hidden(bool same,bool valid){Fixture f;f.Grab(false);f.show=false;f.Release();const auto before=f.Snapshot();
    if(same){f.show=true;auto p=valid?f.At(1,1):ImVec2(f.min.x-3,f.min.y+80);ImGui::GetIO().AddMousePosEvent(p.x,p.y);ImGui::GetIO().AddMouseButtonEvent(0,true);f.Frame();f.Frame();if(valid)Require(f.state.draggingPoint==2,"fresh same-ID press reselects point");else f.Stopped(before,"fresh margin press cannot replay");}
    else{f.Press(f.At(.5f,.5f));const auto owner=ImGui::GetCurrentContext()->ActiveId;f.show=true;f.Move(f.At(.5f,.7f));f.Stopped(before,"hidden release then unrelated press cannot edit");Require(ImGui::GetCurrentContext()->ActiveId==owner,"other native owner untouched");}}
void Escape(bool segment){Fixture f;f.Grab(segment);const auto before=f.Snapshot();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);f.Frame();f.Frame();f.Stopped(before,"Escape cancels curve drag");Require(!f.Active(),"Escape leaves host inactive for rollback");ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);f.Frame();f.Move(f.At(.5f,.7f));f.Stopped(before,"Escape held press cannot replay");}
void InsertDelete(bool disabled){Fixture f;f.disabled=disabled;f.Frame();const auto before=f.Snapshot();f.Click(f.At(.7f,.8f),1);Require(f.points.size()==(disabled?3:4),"native insert respects disabled");f.Click(f.At(.7f,.8f),1);Require(f.points.size()==3,"native delete respects disabled");if(disabled)Require(f.Snapshot()==before,"disabled operations preserve exact recipe");}
void HoverWheel(bool disabled){Fixture f;f.disabled=disabled;f.Move(f.At(.25f,.25f));const auto before=f.Snapshot();ImGui::GetIO().AddMouseWheelEvent(0,1);f.Frame();Require(!f.Active(),"hover-only wheel needs no pressed owner");Require(disabled?(!f.changed&&f.Snapshot()==before):(f.changed&&f.Snapshot()!=before),"wheel behavior preserves disabled gate and hover editing");}
void ExposureControl(){Fixture f;f.exposureMode=true;f.Frame();f.Press(f.At(.25f,.8f));Require(f.state.draggingExposure,"exposure control starts");auto p=ImGui::GetIO().MousePos;p.y-=24;f.Move(p);Require(std::abs(f.exposure-.2f)<.0001f&&!f.changed,"exposure updates separate value, not curve");f.Release();Require(!f.state.draggingExposure&&std::abs(f.exposure-.2f)<.0001f,"exposure release retains final value");
    f.Press(f.At(.25f,.8f));p=ImGui::GetIO().MousePos;p.y-=24;f.Move(p);ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);f.Frame();f.Frame();Require(!f.state.draggingExposure&&std::abs(f.exposure-.2f)<.0001f,"existing exposure Escape restores gesture start");}
}
int main(){int failed=0;const auto run=[&](const char*n,auto test){try{test();}catch(const std::exception&e){++failed;std::cerr<<n<<": "<<e.what()<<'\n';}};
for(bool segment:{false,true}){run(segment?"ordinary tangent":"ordinary point",[&]{Ordinary(segment);});run(segment?"disabled tangent":"disabled point",[&]{Disabled(segment);});run(segment?"Escape tangent":"Escape point",[&]{Escape(segment);});}
run("ClearID",[]{Takeover(false);});run("modal",[]{Takeover(true);});run("hidden unrelated",[]{Hidden(false,false);});run("same-ID margin",[]{Hidden(true,false);});run("same-ID point",[]{Hidden(true,true);});run("insert/delete",[]{InsertDelete(false);});run("disabled insert/delete",[]{InsertDelete(true);});run("hover wheel",[]{HoverWheel(false);});run("disabled wheel",[]{HoverWheel(true);});run("exposure control",ExposureControl);
if(!failed)std::cout<<"Bezier curve native ownership passed\n";return failed?1:0;}
