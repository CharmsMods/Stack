#include "Utils/FloatingSlider.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include "ThirdParty/stb_image_write.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

void ValidateFloatingSlider(const char* output) {
    const auto require=[](bool pass,const char* message) { if (!pass) throw std::runtime_error(message); };
    ImGuiExtras::FloatingSliderState state, otherProject;
    float value=0.f;
    bool disabled=false;
    auto& io=ImGui::GetIO();
    ImVec2 low{},high{};
    const auto frame=[&](ImGuiExtras::FloatingSliderState& owner) {
        io.DisplaySize={600,360}; io.DeltaTime=1.f/60.f;
        ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("RAW control",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorPos({120,150});
        ImGui::BeginDisabled(disabled);
        {
            ImGuiExtras::FloatingSliderScope scope(owner);
            ImGuiExtras::NodeSliderFloat("Exposure","exposure",&value,-10.f,10.f,"%.2f EV",360.f);
        }
        low=ImGui::GetItemRectMin(); high=ImGui::GetItemRectMax();
        ImGui::EndDisabled(); ImGui::End(); ImGui::Render();
        glViewport(0,0,600,360); glClearColor(.12f,.12f,.12f,1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        require(!ImGuiExtras::HasFloatingSliderScope(),"RAW slider styling leaked out of its drawing scope");
    };
    io.AddMousePosEvent(-100,-100); frame(state); frame(state);
    require(value==0.f,"Showing a RAW slider changed its authored value");
    const float y=(low.y+high.y)*.5f;
    io.AddMousePosEvent(high.x-12.f,y); frame(state);
    io.AddMouseButtonEvent(0,true); frame(state);
    io.AddMouseButtonEvent(0,false); frame(state);
    require(value>8.f && value<=10.f,"Clicking the RAW slider did not edit its supplied parameter");
    const float before=value;
    disabled=true;
    io.AddMousePosEvent(low.x+12.f,y); frame(state);
    io.AddMouseButtonEvent(0,true); frame(state);
    io.AddMouseButtonEvent(0,false); frame(state);
    require(value==before,"A driven/disabled RAW slider remained editable");
    disabled=false;
    frame(otherProject);
    require(state.readouts.size()==1 && otherProject.readouts.size()==1,
        "RAW readout state was not owned by its editing surface");
    // Check the actual drawn value at several times, including rapid retargeting.
    // The parameter stays exact while only its readout animates.
    const auto pixels=[] {
        std::vector<unsigned char> image(600*360*4);
        glReadPixels(0,0,600,360,GL_RGBA,GL_UNSIGNED_BYTE,image.data()); return image;
    };
    io.AddMousePosEvent(-100,-100);
    value=1.25f; for(int i=0;i<20;++i) frame(state);
    value=-2.75f; frame(state); const auto start=pixels();
    for(int i=0;i<6;++i) frame(state); const auto middle=pixels();
    for(int i=0;i<20;++i) frame(state); const auto end=pixels();
    require(start!=middle && middle!=end,"RAW value readout snapped instead of drawing intermediate morphs");
    require(value==-2.75f,"Readout animation changed the operation value");
    value=3.f; frame(state); value=4.f; frame(state); value=5.f; frame(state);
    for(int i=0;i<20;++i) frame(state);
    require(value==5.f,"Rapid value changes lost the final parameter");
    if(output) {
        const auto image=pixels(); stbi_flip_vertically_on_write(1);
        require(stbi_write_png(output,600,360,4,image.data(),600*4)!=0,"Could not write RAW slider specimen");
    }
    std::cout<<"RAW slider editing, disabled input, scope isolation and rendered number morph checks passed\n";
}
