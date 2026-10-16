#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "App/settings/AppearanceTheme.h"
#include "ThirdParty/stb_image_write.h"
#include <filesystem>
#include <fstream>
#include "Raw/Curves/TonalCurvePoints.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace Stack::Validation {
void ValidateSharedCurveInteractions() {
    using namespace Editor::RawLabInternal;
    using namespace RawRecipe;
    CurveCanvasState state;CurveAmountState amount;
    std::vector<RawBezierCurvePoint> points{{0,.5f,{1},{1}},{.5f,.5f,{1},{1}},{1,.5f,{1},{1}}};
    ImVec2 origin;ImRect plot,lastItem;float exposure=0;bool changed=false;
    bool tonalEnabled=false,disabled=false;
    CurveTonalSliderDomain domain;
    auto& io=ImGui::GetIO();
    const auto frame=[&] {
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,400});
        ImGui::Begin("Curve interaction validation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        origin=ImGui::GetCursorScreenPos();
        const auto style=Editor::GraphUI::ResolveGraphPlotStyle();
        const float graphHeight=250.f-style.controlHeight-ImGui::GetStyle().ItemSpacing.y;
        const float inset=std::min(style.inset,std::min(400.f,graphHeight)*.2f);
        plot={{origin.x+inset,origin.y+inset},{origin.x+400.f-inset,origin.y+graphHeight-inset}};
        ImGui::BeginDisabled(disabled);
        changed=DrawCurveCanvas("curve",points,amount,state,false,{}, {400,250},12,&exposure,
            {},false,nullptr,false,true,tonalEnabled?&domain:nullptr);
        lastItem={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        ImGui::EndDisabled();
        ImGui::End();ImGui::Render();
    };
    const auto require=[](bool valid,const char* reason){if(!valid)throw std::runtime_error(reason);};
    io.AddMousePosEvent(-100,-100);frame();frame();
    const ImVec2 middle(origin.x+200,origin.y+110);
    io.AddMousePosEvent(middle.x,middle.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(middle.x+20,middle.y-30);frame();
    io.AddMouseButtonEvent(0,false);frame();
    require(points[1].x>.5f && points[1].y>.5f,"Direct curve point did not move freely.");
    const auto edited=points;
    state.mode=CurveGestureMode::Select;state.selected={true,false,true};
    const ImVec2 empty(origin.x+300,origin.y+60);
    io.AddMousePosEvent(empty.x,empty.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(empty.x,empty.y-15);frame();io.AddMouseButtonEvent(0,false);frame();
    require(points[0].y>edited[0].y && points[2].y>edited[2].y && points[1].x==edited[1].x && points[1].y==edited[1].y,
        "Selected-point movement affected an unselected point.");
    const auto before=points;
    io.AddMousePosEvent(empty.x,empty.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(empty.x,empty.y+10);frame();io.AddKeyEvent(ImGuiKey_Escape,true);frame();
    io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(0,false);frame();
    require(points[0].y==before[0].y && points[2].y==before[2].y,"Escape did not restore the gesture baseline.");
    state.selected.assign(points.size(),false);
    io.AddMousePosEvent(empty.x,empty.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(empty.x,empty.y+20);frame();io.AddMouseButtonEvent(0,false);frame();
    require(points[0].y==before[0].y && points[1].y==before[1].y,"Empty selection unexpectedly changed the curve.");
    const auto style=Editor::GraphUI::ResolveGraphPlotStyle();
    const ImVec2 lock(lastItem.Min.x-style.controlGap-style.controlHeight*.5f,lastItem.GetCenter().y);
    io.AddMousePosEvent(lock.x,lock.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    require(amount.locked && amount.reference.size()==points.size(),"Curve lock did not capture its reference.");
    const ImVec2 dial=lastItem.GetCenter();
    io.AddMousePosEvent(dial.x,dial.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(dial.x-40,dial.y);frame();io.AddMouseButtonEvent(0,false);frame();
    require(amount.amount<.9f && points[0].y<before[0].y,"Curve amount did not move toward zero delta.");
    const auto softened=points;
    io.AddMousePosEvent(lock.x,lock.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    require(!amount.locked && amount.amount==1 && points[0].y==softened[0].y,"Unlock changed the current image.");
    state.mode=CurveGestureMode::Points;
    io.AddMousePosEvent(empty.x,empty.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(empty.x,empty.y-20);frame();
    require(changed && exposure>0,"Empty-space Local EV exposure drag was not committed.");
    io.AddMouseButtonEvent(0,false);frame();

    // Exercise the same submitted Canvas and slider widgets with ordinary
    // ImGui pointer events; model helpers only seed each authored starting state.
    tonalEnabled=true;state={};amount={};exposure=0;
    const auto resetPoints=[&] {
        points={{0,.5f,{1},{1}},{.65f,.5f,{1},{1}},{1,.5f,{1},{1}}};
        require(SetTonalCurveSlider(points,1,.25f,.5f,12)==TonalCurveEditResult::Changed,
            "Could not seed a tonal point for interaction validation.");
    };
    const auto owned=[&] {return std::find_if(points.begin(),points.end(),[](const auto& p){return p.tonal.region==1;});};
    const auto custom=[&] {return std::find_if(points.begin(),points.end(),[](const auto& p){return !IsTonalCurvePoint(p) && p.x>0 && p.x<1;});};
    const auto screen=[&](float x,float y){return ImVec2(plot.Min.x+x*plot.GetWidth(),plot.Max.y-y*plot.GetHeight());};
    const auto click=[&](ImVec2 position,int button=0) {
        io.AddMousePosEvent(position.x,position.y);frame();
        io.AddMouseButtonEvent(button,true);frame();io.AddMouseButtonEvent(button,false);frame();
    };
    const auto drag=[&](ImVec2 start,ImVec2 finish) {
        io.AddMousePosEvent(start.x,start.y);frame();io.AddMouseButtonEvent(0,true);frame();
        io.AddMousePosEvent(finish.x,finish.y);frame();
        io.AddMouseButtonEvent(0,false);frame();
    };
    resetPoints();frame();
    const auto fixedBefore=*owned();const auto fixedStart=screen(fixedBefore.x,fixedBefore.y);
    drag(fixedStart,{fixedStart.x+24,fixedStart.y-16});
    require(owned()!=points.end() && owned()->x==fixedBefore.x && owned()->y>fixedBefore.y,
        "A tonal point did not retain its x during diagonal dragging.");
    const auto customBefore=*custom();const auto customStart=screen(customBefore.x,customBefore.y);
    drag(customStart,{customStart.x+24,customStart.y-16});
    require(custom()!=points.end() && custom()->x>customBefore.x && custom()->y>customBefore.y,
        "An ordinary point lost free movement beside tonal points.");

    resetPoints();state.mode=CurveGestureMode::Select;state.selected.assign(points.size(),false);
    for(std::size_t i=0;i<points.size();++i)state.selected[i]=IsTonalCurvePoint(points[i]) || points[i].x==.65f;
    frame();const auto groupFixed=*owned();const auto groupCustom=*custom();
    const auto groupStart=screen(.45f,.15f);
    io.AddMousePosEvent(groupStart.x,groupStart.y);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(groupStart.x+24,groupStart.y-16);frame();
    require(owned()->x==groupFixed.x && owned()->y>groupFixed.y && custom()->x>groupCustom.x && custom()->y>groupCustom.y,
        "A mixed selection moved a tonal x or froze its ordinary points.");
    const auto groupResult=points;frame();
    require(!changed && SameCurvePoints(points,groupResult),"An unchanged held drag authored another curve edit.");
    io.AddMouseButtonEvent(0,false);frame();
    state.mode=CurveGestureMode::Points;
    const float retainedCustomX=custom()->x;
    click(screen(owned()->x,owned()->y),1);
    require(owned()==points.end() && points.size()==3 && custom()->x==retainedCustomX,
        "Right-clicking a tonal point did not remove only its ownership point.");

    require(SetTonalCurveSlider(points,1,.25f,.5f,12)==TonalCurveEditResult::Changed,"Could not reseed a tonal point.");
    frame();const float baseline=owned()->tonal.baselineY;const float neutralX=owned()->x;
    const auto zeroStart=screen(owned()->x,owned()->y);
    drag(zeroStart,screen(.3f,baseline));
    // Native pointer coordinates can leave a nonzero subpixel correction. Keep
    // that authored value; the exact-zero slider reset below checks removal.
    const auto nearNeutral=owned();
    require(nearNeutral==points.end() || (nearNeutral->x==neutralX && nearNeutral->y!=baseline &&
        std::abs(nearNeutral->y-baseline)<=1.f/plot.GetHeight()),
        "A near-neutral point drag moved its fixed x or missed the requested vertical position.");

    points={{0,.5f,{1},{1}},{.25f,.5f,{1},{1}},{.65f,.5f,{1},{1}},{1,.5f,{1},{1}}};
    const auto ordinaryBaseline=points;
    require(SetTonalCurveSlider(points,1,.25f,.5f,12)==TonalCurveEditResult::Changed,"Could not seed a borrowed tonal point.");
    frame();const auto toggle=ImVec2(lastItem.Min.x-2*style.controlGap-1.5f*style.controlHeight,lastItem.GetCenter().y);
    const auto beforeVisibility=points;
    click(toggle);
    require(state.showTonalSliders && !changed && SameCurvePoints(points,beforeVisibility),"Showing sliders authored curve points.");
    click(toggle);
    require(!state.showTonalSliders && !changed && SameCurvePoints(points,beforeVisibility),"Hiding sliders authored curve points.");
    click(toggle);require(state.showTonalSliders,"Could not show tonal sliders for widget checks.");
    const float rowStep=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 secondSlider(lastItem.Min.x+lastItem.GetWidth()*.8f,lastItem.GetCenter().y-3*rowStep);
    click(secondSlider);
    require(owned()!=points.end() && TonalCurveSliderValue(points,1)>.1f,"The submitted tonal slider did not adjust its point.");
    click(secondSlider);
    require(owned()==points.end() && SameCurvePoints(points,ordinaryBaseline),
        "Zeroing the submitted slider did not restore the borrowed ordinary point.");
    disabled=true;frame();const auto beforeDisabled=points;
    click(secondSlider);
    require(!changed && SameCurvePoints(points,beforeDisabled),"A disabled tonal slider changed authored curve data.");
    disabled=false;frame();
    std::cout<<"Shared curve point/group, cancel, lock/amount, exposure and tonal slider ownership interactions passed.\n";
}

bool ValidateTonalSliderInteractions(const std::filesystem::path& preview) {
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(1000,700,"Tonal slider interaction validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize={1000,700};io.DeltaTime=1.f/60.f;
    StackAppearance::AppearanceManager appearance;
    appearance.SetupFonts(io);appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
    const bool backend=ImGui_ImplOpenGL3_Init("#version 430");
    bool success=false;
    if(backend) {
        try {
            ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::EndFrame();
            ValidateSharedCurveInteractions();
            if(!preview.empty()) {
                using namespace Editor::RawLabInternal;
                using namespace RawRecipe;
                CurveCanvasState localState,toneState;
                localState.showTonalSliders=toneState.showTonalSliders=true;
                CurveAmountState localAmount,toneAmount;
                float normalization=.35f;
                auto localDomain=ExposureTonalSliderDomain(-8.f,6.f);
                CurveTonalSliderDomain toneDomain;
                std::vector<RawBezierCurvePoint> localPoints{{0,.5f,{1},{1}},{.68f,.54f,{1},{1}},{1,.5f,{1},{1}}};
                std::vector<RawBezierCurvePoint> tonePoints{{0,0,{1},{1}},{.44f,.48f,{1},{1}},{1,1,{1},{1}}};
                SetTonalCurveSlider(localPoints,1,localDomain.positions[1],.3f,12);
                SetTonalCurveSlider(localPoints,3,localDomain.positions[3],-.2f,12);
                SetTonalCurveSlider(tonePoints,0,toneDomain.positions[0],.18f,64);
                SetTonalCurveSlider(tonePoints,3,toneDomain.positions[3],-.2f,64);
                io.AddMousePosEvent(-100,-100);
                ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
                constexpr ImGuiWindowFlags flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|
                    ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize;
                ImGui::SetNextWindowPos({26,24});ImGui::SetNextWindowSize({460,650});
                ImGui::Begin("Local EV slider preview",nullptr,flags);
                ImGui::TextUnformatted("Local EV");ImGui::Spacing();
                DrawCurveCanvas("LocalEV",localPoints,localAmount,localState,false,{},
                    {ImGui::GetContentRegionAvail().x,300},12,nullptr,{},false,&normalization,false,true,&localDomain);
                ImGui::End();
                ImGui::SetNextWindowPos({514,24});ImGui::SetNextWindowSize({460,650});
                ImGui::Begin("Tone slider preview",nullptr,flags);
                ImGui::TextUnformatted("Tone curve");ImGui::Spacing();
                DrawCurveCanvas("Tone",tonePoints,toneAmount,toneState,true,{},
                    {ImGui::GetContentRegionAvail().x,300},64,nullptr,{},false,nullptr,false,true,&toneDomain);
                ImGui::End();ImGui::Render();
                glViewport(0,0,1000,700);glDisable(GL_SCISSOR_TEST);
                glClearColor(.1f,.1f,.1f,1.f);glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                std::vector<unsigned char> pixels(1000*700*4),topDown(pixels.size());
                glReadPixels(0,0,1000,700,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                constexpr std::size_t stride=1000*4;
                for(std::size_t row=0;row<700;++row)
                    std::copy_n(pixels.data()+row*stride,stride,topDown.data()+(699-row)*stride);
                // ImGui's framebuffer is already display encoded. Image
                // processing validation's linear-output writer is unsuitable.
                std::error_code error;
                if(preview.has_parent_path()) std::filesystem::create_directories(preview.parent_path(),error);
                std::ofstream png(preview,std::ios::binary);
                const auto write=[](void* stream,void* data,int size) {
                    static_cast<std::ofstream*>(stream)->write(static_cast<const char*>(data),size);
                };
                if(error || !png || !stbi_write_png_to_func(write,&png,1000,700,4,topDown.data(),1000*4) || !png)
                    throw std::runtime_error("Could not save the native tonal slider preview.");
                std::cout<<"Native Local EV and Tone slider preview: "<<preview.string()<<'\n';
            }
            success=true;
        } catch(const std::exception& error){std::cerr<<error.what()<<'\n';}
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
