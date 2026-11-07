#include "Renderer/ColorWarpShader.h"
#include "Renderer/GLHelpers.h"
#include "Raw/ColorWarpMath.h"
#include "Raw/RawColorWarpSelection.h"
#include <GLFW/glfw3.h>
#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "App/settings/AppearanceTheme.h"
#include "ThirdParty/stb_image_write.h"
#include <backends/imgui_impl_opengl3.h>
#include <filesystem>
#include <iostream>
#include <cmath>
#include "Utils/FloatingSlider.h"
namespace Stack::Validation {
bool ValidateColorWarpMove() {
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(720,860,"Color Warp check",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);if(!LoadGLFunctions())return false;
    const char* vertex=R"(#version 330 core
out vec2 vUV;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);vUV=p;gl_Position=vec4(p*2-1,0,1);})";
    
    const auto program=GLHelpers::CreateShaderProgram(vertex,Renderer::ColorWarp::Fragment);
    bool ok=program!=0;float worst=0;
    GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glUseProgram(program);
    const auto integer=[&](const char* name,int value){glUniform1i(glGetUniformLocation(program,name),value);};
    const auto scalar=[&](const char* name,float value){glUniform1f(glGetUniformLocation(program,name),value);};
    const auto vector=[&](const char* name,float a,float b,float c,float d){glUniform4f(glGetUniformLocation(program,name),a,b,c,d);};
    integer("uInputTex",0);integer("uGateMask",1);integer("uSupportMask",2);integer("uBoundaryMask",3);integer("uMaskGuide",4);
    integer("uUseSpatialMasks",0);integer("uPinCount",1);scalar("uStrength",1);
    vector("uHueRange[0]",-1e20f,-1e20f,1e20f,1e20f);vector("uSaturationRange[0]",-1e20f,-1e20f,1e20f,1e20f);vector("uExposureRange[0]",-1e20f,-1e20f,1e20f,1e20f);
    for(float exposure:{-2.67f,0.f,2.67f})for(int selectionMode:{0,1,2})for(bool rec2020:{false,true})for(bool move:{false,true})for(float strength:{0.f,.4f,1.f}) {
        RawRecipe::RawColorWarpRecipe recipe;RawRecipe::RawColorWarpPin pin;
        pin.id="test";pin.sourceA=.2f;pin.sourceB=.1f;pin.hueShift=25;pin.saturation=.2f;pin.exposureEv=exposure;pin.moveColors=move;pin.strength=strength;
        pin.hueRange.enabled=pin.saturationRange.enabled=pin.exposureRange.enabled=false;
        pin.sampleRadius=selectionMode? .4f:0.f;recipe.pins={pin};
        vector("uPinSelection[0]",pin.sourceA,pin.sourceB,pin.sampleRadius,0);
        integer("uUseSpatialMasks",selectionMode==2);
        vector("uPinFieldFlags[0]",selectionMode==2?1.f:0.f,0,0,0);
        const float maskValues[]={.6f,0.f,1.f};GLuint maskTextures[3]{};
        if(selectionMode==2)for(int i=0;i<3;++i) {
            maskTextures[i]=GLHelpers::CreateTextureArray(1,1,1,0x822A);
            GLHelpers::UploadTextureArrayLayer(maskTextures[i],0,1,1,GL_RED,GL_FLOAT,&maskValues[i]);
            glActiveTexture(GL_TEXTURE0+1+i);glBindTexture(GL_TEXTURE_2D_ARRAY,maskTextures[i]);
        }
        std::vector<RawRecipe::RawColorWarpMaskSample> masks{{.6f,0,1,true}};
        integer("uInputIsRec2020",rec2020);vector("uPinParams[0]",0,strength,0,1);
        vector("uPinAdjust[0]",move?0:pin.hueShift,move?0:pin.saturation,pin.exposureEv,selectionMode==2?1.f:0.f);
        const auto destination=RawRecipe::ColorWarpDestination(pin);vector("uPinMove[0]",move?destination[0]-pin.sourceA:0,move?destination[1]-pin.sourceB:0,0,0);
        for(const std::array<float,4> input: {std::array<float,4>{.4f,.12f,.08f,1},std::array<float,4>{2.f,.8f,.2f,.5f},std::array<float,4>{.18f,.18f,.18f,1}}) {
            const auto source=GLHelpers::CreateTextureFromData(input.data(),1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT);
            const auto target=GLHelpers::CreateTextureFromData(nullptr,1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT);
            const auto fbo=GLHelpers::CreateFBO(target);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,1,1);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);glDrawArrays(GL_TRIANGLES,0,3);
            std::array<float,4> actual{};glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,actual.data());
            const auto expected=RawRecipe::ApplyPreparedColorWarp(recipe,{input[0],input[1],input[2]},rec2020?Raw::RawWorkingSpace::LinearRec2020D65:Raw::RawWorkingSpace::LinearSrgbD65,selectionMode==2?&masks:nullptr);
            for(int c=0;c<3;++c){const float error=std::abs(actual[c]-expected[c]);ok &= std::isfinite(actual[c])&&error<.0001f*std::max(1.f,std::abs(expected[c]));worst=std::max(worst,error);}
            ok &= actual[3]==input[3] && glGetError()==GL_NO_ERROR;
            glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&source);glDeleteTextures(1,&target);
        }
        glDeleteTextures(3,maskTextures);
    }
    glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
    
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={720,860};io.DeltaTime=1.f/60;
    StackAppearance::AppearanceManager appearance;appearance.SetupFonts(io);appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
    ImGui_ImplOpenGL3_Init("#version 330 core");
    {
        EditorModuleTypes::RawWorkspaceLabUiState ui;
        RawRecipe::RawColorWarpRecipe recipe;RawRecipe::RawColorWarpPin sample;
        sample.id="sample";sample.name="Sampled color";
        RawRecipe::SetColorWarpSample(sample,RawRecipe::WorkingRgbToColorWarpCoordinate({.3f,.28f,.25f},Raw::RawWorkingSpace::LinearSrgbD65));
        recipe.pins.push_back(sample);ui.selectedColorWarpPin=int(recipe.pins.size())-1;
        RawDevelopmentGraphScopeReadback scope;std::string identity="validation";
        Editor::ColorWarpEditorState state;state.selectRange=true;
        ImGuiExtras::FloatingSliderState sliders;
        ImGuiExtras::FloatingSliderScope sliderScope(sliders);
        Editor::RawLabInternal::RawLabColorSurfaceArgs args{ui,recipe,Raw::RawWorkingSpace::LinearSrgbD65,scope,0,identity,[]{}};args.state=&state;
        
        for(int frame=0;frame<3;++frame) {
            ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
            ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowSize({320,830});
            ImGui::Begin("Color Warp - Selection",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
            Editor::RawLabInternal::RenderRawLabColorSurface(args);ImGui::End();
            ImGui::Render();glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,720,860);glClearColor(.08f,.08f,.08f,1);glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        std::vector<unsigned char> pixels(720*860*4);glReadPixels(0,0,720,860,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for(int y=0;y<430;++y)for(int x=0;x<720*4;++x)std::swap(pixels[y*720*4+x],pixels[(859-y)*720*4+x]);
        std::filesystem::create_directories("outputs/validation/color-warp-selection");
        ok &= stbi_write_png("outputs/validation/color-warp-selection/controls.png",720,860,4,pixels.data(),720*4)!=0;
    }
    ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();
    std::cout<<"Color Warp Move/Turn CPU-GPU difference: "<<worst<<"; passed="<<ok<<'\n';return ok;
}
}
