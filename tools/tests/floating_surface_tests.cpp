#include "Utils/FloatingSurfaceInput.h"
#include "Utils/ImGuiExtras.h"
#include "App/WorkspaceInputScope.h"
#include "Editor/Internal/RawLab/RawCalibrationEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

void ValidateFloatingSurfaces() {
    using namespace Stack::RawRecipe;
    using namespace Stack::Editor::RawLabInternal;
    const auto require=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
    auto& io=ImGui::GetIO();
    const auto previousConfigFlags=io.ConfigFlags;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    ImGuiExtras::FloatingSurfaceInput input;
    CalibrationEditorState editor;
    RawColorCalibrationRecipe recipe;
    std::array<bool,3> hueDriven{}, saturationDriven{};
    ImVec2 position{};
    int rightActions=0;
    bool temporarilyDisabled=false;
    const auto document=[&] { return SerializeColorCalibration(recipe); };
    const auto frame=[&] {
        io.DisplaySize={600,360}; io.DeltaTime=1.f/60.f;
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("RAW workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("StackWorkingArea",{0,0},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::BeginChild("RawModuleWorkspace",{0,0},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetNextWindowPos(position); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Floating tools",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::SetCursorPos({100,40});
        const bool physicalLeft=io.MouseDown[0], physicalRight=io.MouseDown[1];
        {
            ImGuiExtras::FloatingSurfaceInputScope route(input,ImGui::IsWindowHovered());
            if(input.moving) { position.x+=io.MouseDelta.x; position.y+=io.MouseDelta.y; }
            ImGui::BeginDisabled(temporarilyDisabled);
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ++rightActions;
            DrawCalibrationEditor(recipe,editor,{400,260},hueDriven,saturationDriven);
            ImGui::EndDisabled();
        }
        require(io.MouseDown[0]==physicalLeft && io.MouseDown[1]==physicalRight,
            "Floating tool input leaked into another surface");
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::End();
        {
            Stack::Workspace::InputScope preview(input.OwnsPointer() || ImGui::GetCurrentContext()->HoveredWindow ==
                ImGui::FindWindowByName("Floating tools"),false);
            ImGui::BeginChild("Image",{0,0},0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::InvisibleButton("Preview",ImGui::GetContentRegionAvail());
            ImGui::EndChild();
        }
        ImGui::EndChild(); ImGui::EndChild(); ImGui::End(); ImGui::Render();
    };
    const auto red=[&] {
        const auto p=ColorCalibrationPrimaries(recipe)[0];
        return ImVec2(position.x+300.f+float((p[0]-kCalibrationWhite[0])*116./editor.radius),
            position.y+170.f-float((p[1]-kCalibrationWhite[1])*116./editor.radius));
    };
    io.AddMousePosEvent(-100,-100); frame(); frame();
    const auto initial=document();
    for(int first:{0,1}) {
        ImVec2 p(position.x+20,position.y+20); io.AddMousePosEvent(p.x,p.y); frame();
        io.AddMouseButtonEvent(first,true); frame();
        require(document()==initial && rightActions==0,"The first button edited before a move chord could form");
        io.AddMouseButtonEvent(1-first,true); frame();
        io.AddMousePosEvent(p.x+50,p.y+40); frame();
        require(input.moving && document()==initial && rightActions==0,"Moving a floating tool edited its parameters");
        io.AddMouseButtonEvent(first,false); frame();
        io.AddMousePosEvent(p.x+80,p.y+60); frame();
        require(document()==initial && rightActions==0,"The remaining move button began a parameter edit");
        io.AddMouseButtonEvent(1-first,false); frame(); frame();
        require(!input.OwnsPointer(),"The move chord kept pointer ownership after release");
        // Edit at the moved location; returning the window to its original
        // position first can hide capture or hit-testing failures.
        p=red(); io.AddMousePosEvent(p.x,p.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(p.x+12,p.y); frame();
        io.AddMousePosEvent(p.x+24,p.y); frame();
        require(document()!=initial,"Moving the tool before editing locked its controls");
        io.AddMouseButtonEvent(0,false); frame(); frame();
        recipe={}; editor={}; for(int i=0;i<25;++i) frame();
    }
    position={}; frame(); frame();
    // A RAW operation can become editable while a deferred press is held
    // (for example, when source preparation finishes). The host's background
    // capture must not block delivery of that press to the now-ready control.
    temporarilyDisabled=true;
    auto pendingRed=red(); io.AddMousePosEvent(pendingRed.x,pendingRed.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    temporarilyDisabled=false;
    io.AddMousePosEvent(pendingRed.x+20,pendingRed.y); frame();
    io.AddMousePosEvent(pendingRed.x+40,pendingRed.y); frame();
    require(document()!=initial,"The floating host consumed a deferred control drag");
    io.AddMouseButtonEvent(0,false); frame(); frame();
    recipe={}; editor={}; for(int i=0;i<25;++i) frame();
    // A normal right click is delivered once, on completion, preserving the
    // curve editor's existing add/delete action without a chord side effect.
    io.AddMousePosEvent(120,60); frame();
    io.AddMouseButtonEvent(1,true); frame();
    io.AddMouseButtonEvent(1,false); frame(); frame();
    require(rightActions==1,"An ordinary right click was lost or duplicated");
    auto p=red(); io.AddMousePosEvent(p.x,p.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMousePosEvent(p.x+20,p.y); frame();
    io.AddMousePosEvent(p.x+140,p.y); frame();
    require(recipe.primaries[0].saturation>100,"Calibration drag remained limited to the old slider range");
    const auto edited=document();
    io.AddMouseButtonEvent(1,true); frame();
    require(!input.moving && rightActions==1,"Adding a second button stole an established parameter drag");
    io.AddMouseButtonEvent(0,false); frame();
    io.AddMouseButtonEvent(1,false); frame(); frame();
    require(document()==edited,"Releasing an established edit changed its values");
    require(SerializeColorCalibration(DeserializeColorCalibration(edited))==edited,"Free triangle values did not save/reopen");
    hueDriven[0]=true;
    const float hue=recipe.primaries[0].hue;
    p=red(); io.AddMousePosEvent(p.x,p.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMousePosEvent(p.x-20,p.y+20); frame();
    io.AddMousePosEvent(p.x-60,p.y+50); frame();
    require(recipe.primaries[0].hue==hue,"Dragging a graph-driven primary angle overwrote it");
    require(document()!=edited,"An undriven radius could not be edited independently");
    io.AddKeyEvent(ImGuiKey_Escape,true); frame();
    io.AddKeyEvent(ImGuiKey_Escape,false); io.AddMouseButtonEvent(0,false); frame(); frame();
    require(document()==edited,"Escape did not restore the calibration gesture start");
    recipe.enabled=false;
    const auto disabled=document();
    p=red(); io.AddMousePosEvent(p.x,p.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMousePosEvent(p.x-30,p.y); frame();
    io.AddMousePosEvent(p.x-70,p.y); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
    require(document()==disabled,"A bypassed Calibration remained editable");
    // Pure view changes must not change authored data.
    io.AddMouseWheelEvent(0,-4); frame();
    require(document()==disabled,"Zooming the primary diagram changed calibration");
    recipe.enabled=true; hueDriven[0]=false;
    for(int i=0;i<25;++i) frame();
    p=red(); io.AddMousePosEvent(p.x,p.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
    require(recipe.primaries[0].hue==0 && recipe.primaries[0].saturation==0,
        "Fast double-click reset was lost while releasing the first click");
    io.AddMousePosEvent(120,60); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(1,true); frame();
    io.AddFocusEvent(false); frame();
    io.AddFocusEvent(true); frame(); frame();
    require(!input.moving && !input.OwnsPointer(),"Focus loss left a floating move captured");
    io.ConfigFlags=previousConfigFlags;
    std::cout<<"Floating movement, both button orders, loading handoff, release, calibration edits, driven axes, cancel and save checks passed\n";
}
