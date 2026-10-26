// SPDX-License-Identifier: GPL-3.0-only
#include "Utils/GlassControls.h"
#include "App/settings/AppearanceTheme.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Stack::Validation {
bool ValidateSidePanelSliders(const std::filesystem::path& preview) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(720,480,"Side panel slider validation",nullptr,nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize={720,480}; io.DeltaTime=1.f/60.f;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    StackAppearance::AppearanceManager appearance;
    appearance.SetupFonts(io); appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
    const bool backend=ImGui_ImplOpenGL3_Init("#version 430");
    bool success=false;
    ImGuiExtras::GlassControlState state;
    if (backend) {
        try {
            std::cout<<"GPU: "<<glGetString(GL_RENDERER)<<'\n';
            const auto require=[](bool ok,const char* message) { if (!ok) throw std::runtime_error(message); };
            struct Item { ImGuiID id; ImRect rect; bool active,edited; };
            std::array<Item,3> items{};
            std::array<Item,3> buttons{};
            std::vector<ImDrawVert> outsideVertices;
            float amount=.25f, logarithmic=.5f, outside=.5f;
            int count=3;
            int actions=0,mode=0;
            bool checked=false;
            bool disabled=false, focus=false;
            const auto frame=[&](bool glass) {
                ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
                state.BeginFrame(glass,"validation-owner");
                ImGui::SetNextWindowPos({20,20}); ImGui::SetNextWindowSize({340,430});
                ImGui::Begin("Side panel",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
                ImGui::TextUnformatted("Side-panel sliders"); ImGui::Spacing();
                ImGui::BeginChild("PanelBody",{0,0},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);
                {
                    ImGuiExtras::GlassControlScope scope(state);
                    ImGui::BeginDisabled(disabled);
                    ImGui::TextUnformatted("Amount"); ImGui::SetNextItemWidth(-1);
                    if (focus) ImGui::SetKeyboardFocusHere();
                    const bool edited=ImGuiExtras::GlassSliderFloat("##amount",&amount,0,1,"%.3f");
                    items[0]={ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()},ImGui::IsItemActive(),edited};
                    ImGui::Spacing(); ImGui::TextUnformatted("Samples"); ImGui::SetNextItemWidth(-1);
                    const bool intEdited=ImGuiExtras::GlassSliderInt("##count",&count,0,10);
                    items[1]={ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()},ImGui::IsItemActive(),intEdited};
                    ImGui::Spacing(); ImGui::TextUnformatted("Logarithmic range"); ImGui::SetNextItemWidth(-1);
                    const bool logEdited=ImGuiExtras::GlassSliderFloat("##log",&logarithmic,.01f,100.f,"%.3f",ImGuiSliderFlags_Logarithmic);
                    items[2]={ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()},ImGui::IsItemActive(),logEdited};
                    ImGui::Spacing();
                    if (ImGuiExtras::GlassButton("Apply")) ++actions;
                    buttons[0]={ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()},ImGui::IsItemActive(),false};
                    ImGui::SameLine(); ImGuiExtras::GlassCheckbox("Enabled",&checked);
                    buttons[1]={ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()},ImGui::IsItemActive(),false};
                    ImGui::SameLine(); ImGuiExtras::GlassRadioButton("B",&mode,1);
                    buttons[2]={ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()},ImGui::IsItemActive(),false};
                    ImGui::EndDisabled();
                    // A slider in a sibling window is outside the presentation scope.
                    ImGui::SetNextWindowPos({390,20}); ImGui::SetNextWindowSize({300,160});
                    ImGui::Begin("Unchanged floating control",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
                    require(!ImGuiExtras::HasGlassControlScope(),"Panel scope leaked to a sibling window.");
                    ImGui::TextUnformatted("Outside the side panel"); ImGui::SetNextItemWidth(-1);
                    ImGuiExtras::GlassSliderFloat("##outside",&outside,0,1);
                    outsideVertices.assign(ImGui::GetWindowDrawList()->VtxBuffer.begin(),ImGui::GetWindowDrawList()->VtxBuffer.end());
                    ImGui::End();
                }
                ImGui::EndChild(); ImGui::End();
                require(!ImGuiExtras::HasGlassControlScope(),"Panel scope was not restored.");
                ImGui::Render();
                glViewport(0,0,720,480); glDisable(GL_SCISSOR_TEST);
                glClearColor(.09f,.09f,.09f,1.f); glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                require(state.material.Error().empty(),state.material.Error().c_str());
                require(glGetError()==GL_NO_ERROR,"Side-panel rendering reported an OpenGL error.");
            };
            io.AddMousePosEvent(-100,-100);
            frame(false); frame(false);
            const auto nativeItems=items;
            const auto nativeButtons=buttons;
            const auto nativeOutside=outsideVertices;
            frame(true);
            for (std::size_t i=0;i<items.size();++i) {
                require(items[i].id==nativeItems[i].id,"Glass changed a native slider ID.");
                require(items[i].rect.Min.x==nativeItems[i].rect.Min.x && items[i].rect.Min.y==nativeItems[i].rect.Min.y &&
                    items[i].rect.Max.x==nativeItems[i].rect.Max.x && items[i].rect.Max.y==nativeItems[i].rect.Max.y,
                    "Glass changed a native slider hit rectangle.");
            }
            for (std::size_t i=0;i<buttons.size();++i)
                require(buttons[i].id==nativeButtons[i].id && buttons[i].rect.Min.x==nativeButtons[i].rect.Min.x &&
                    buttons[i].rect.Min.y==nativeButtons[i].rect.Min.y && buttons[i].rect.Max.x==nativeButtons[i].rect.Max.x &&
                    buttons[i].rect.Max.y==nativeButtons[i].rect.Max.y,"Glass changed native button/toggle identity or bounds.");
            require(nativeOutside.size()==outsideVertices.size(),"Glass changed outside slider geometry.");
            for (std::size_t i=0;i<nativeOutside.size();++i)
                require(nativeOutside[i].pos.x==outsideVertices[i].pos.x && nativeOutside[i].pos.y==outsideVertices[i].pos.y &&
                    nativeOutside[i].col==outsideVertices[i].col,"Glass changed outside slider drawing.");
            const auto point=[&](int row,float ratio) {
                const auto& rect=items[row].rect;
                return ImVec2(rect.Min.x+rect.GetWidth()*ratio,rect.GetCenter().y);
            };
            const auto click=[&](ImVec2 at,bool glass) {
                io.AddMousePosEvent(at.x,at.y); frame(glass);
                io.AddMouseButtonEvent(0,true); frame(glass);
                io.AddMouseButtonEvent(0,false); frame(glass);
            };
            click(point(0,.3f),false);
            const float nativeAmount=amount;
            click(point(0,.3f),true);
            require(amount==nativeAmount,"Glass changed native pointer mapping.");
            io.AddMousePosEvent(point(0,.2f).x,point(0,.2f).y); frame(true);
            io.AddMouseButtonEvent(0,true); frame(true);
            io.AddMousePosEvent(point(0,.8f).x,point(0,.8f).y); frame(true);
            require(items[0].active && amount>.7f,"Glass slider drag did not follow input.");
            io.AddMouseButtonEvent(0,false); frame(true);
            require(!items[0].active,"Glass slider remained active after release.");
            click(point(1,.8f),false); const int nativeCount=count;
            click(point(1,.8f),true); require(count==nativeCount,"Glass changed integer mapping.");
            click(point(2,.65f),false); const float nativeLog=logarithmic;
            click(point(2,.65f),true); require(logarithmic==nativeLog,"Glass changed logarithmic mapping.");
            click(buttons[0].rect.GetCenter(),true); frame(true);
            require(actions==1,"Glass button did not activate exactly once.");
            click(buttons[1].rect.GetCenter(),true);
            require(checked,"Glass checkbox did not toggle.");
            click(buttons[2].rect.GetCenter(),true);
            require(mode==1,"Glass radio button did not select its value.");
            disabled=true; const float beforeDisabled=amount;
            click(point(0,.1f),true);
            require(amount==beforeDisabled && !items[0].active && !items[0].edited,"Disabled slider accepted input.");
            click(buttons[0].rect.GetCenter(),true); click(buttons[1].rect.GetCenter(),true);
            require(actions==1 && checked,"Disabled glass controls accepted input.");
            disabled=false;
            io.AddKeyEvent(ImGuiKey_Tab,true); frame(true);
            io.AddKeyEvent(ImGuiKey_Tab,false); frame(true);
            io.AddMousePosEvent(-100,-100); focus=true; frame(true); focus=false; frame(true);
            // Native tab/programmatic focus prefers exact-value input. Escape
            // exits that input first; Space then activates the slider gesture.
            io.AddKeyEvent(ImGuiKey_Escape,true); frame(true);
            io.AddKeyEvent(ImGuiKey_Escape,false); frame(true);
            io.AddKeyEvent(ImGuiKey_Space,true); frame(true);
            io.AddKeyEvent(ImGuiKey_Space,false); frame(true);
            const float beforeKeyboard=amount;
            io.AddKeyEvent(ImGuiKey_RightArrow,true); frame(true);
            io.AddKeyEvent(ImGuiKey_RightArrow,false); frame(true);
            require(amount>beforeKeyboard,"Glass slider lost native keyboard editing.");
            io.AddKeyEvent(ImGuiKey_Escape,true); frame(true);
            io.AddKeyEvent(ImGuiKey_Escape,false); frame(true);
            require(!items[0].active,"Escape did not end keyboard editing.");
            io.AddKeyEvent(ImGuiMod_Ctrl,true);
            click(point(0,.5f),true);
            io.AddKeyEvent(ImGuiMod_Ctrl,false); frame(true);
            io.AddInputCharactersUTF8("0.42"); frame(true);
            io.AddKeyEvent(ImGuiKey_Enter,true); frame(true);
            io.AddKeyEvent(ImGuiKey_Enter,false); frame(true);
            require(std::abs(amount-.42f)<.0001f,"Glass slider lost native exact-value entry.");
            // Let finite release motion settle, then retain one representative preview.
            io.AddMousePosEvent(-100,-100);
            for (int i=0;i<90;++i) frame(true);
            if (!preview.empty()) {
                std::vector<unsigned char> pixels(720*480*4),topDown(pixels.size());
                glReadPixels(0,0,720,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                for (int row=0;row<480;++row) std::copy_n(pixels.data()+row*720*4,720*4,topDown.data()+(479-row)*720*4);
                std::error_code error;
                if (preview.has_parent_path()) std::filesystem::create_directories(preview.parent_path(),error);
                std::ofstream png(preview,std::ios::binary);
                const auto write=[](void* stream,void* data,int size) { static_cast<std::ofstream*>(stream)->write(static_cast<const char*>(data),size); };
                require(!error && png && stbi_write_png_to_func(write,&png,720,480,4,topDown.data(),720*4) && png,
                    "Could not save side-panel slider preview.");
            }
            frame(false);
            require(state.controls.empty(),"Disabling glass retained control motion state.");
            require(state.feedback.empty(),"Disabling glass retained button/point motion state.");
            std::cout<<"PASS glass float/integer/log mapping, drag/release, disabled input, keyboard/Escape, exact entry, button/checkbox/radio actions, native IDs/bounds and outside-scope drawing.\n";
            success=true;
        } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; }
        state.Shutdown(); ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return success;
}
}
