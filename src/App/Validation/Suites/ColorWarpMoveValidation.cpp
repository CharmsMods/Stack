#include "Project/RawLayerStack.h"
#include "Raw/RawGraphOperation.h"
#include "Raw/ColorWarpHsl.h"
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
bool ValidateHslSelectionMath(unsigned program);
bool CaptureHslSelectionControls();
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
    for(int version:{4,5})for(float saturation:{-1.f,0.f,.2f})for(float brightness:{-1.f,0.f,1.f})for(int selectionMode:{0,1,2})for(bool rec2020:{false,true})for(bool move:{false,true})for(float strength:{0.f,.4f,1.f}) {
        RawRecipe::RawColorWarpRecipe recipe;recipe.version=version;RawRecipe::RawColorWarpPin pin;
        pin.id="test";pin.sourceA=.2f;pin.sourceB=.1f;pin.hueShift=25;pin.saturation=saturation;pin.brightness=brightness;pin.moveColors=move;pin.strength=strength;
        pin.hueRange.enabled=pin.saturationRange.enabled=pin.exposureRange.enabled=false;
        pin.sampleRadius=selectionMode? .4f:0.f;recipe.pins={pin};
        integer("uHslModel",version>=5);
        const auto emptyHsl=RawRecipe::PrepareColorWarpHsl(recipe);
        glUniform4fv(glGetUniformLocation(program,"uHsl[0]"),8,emptyHsl.front().data());
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
        vector("uPinAdjust[0]",move?0:pin.hueShift,move && version<5?0:pin.saturation,pin.brightness,selectionMode==2?1.f:0.f);
        auto destinationPin=pin;if(version>=5)destinationPin.saturation=0;
        const auto destination=RawRecipe::ColorWarpDestination(destinationPin);vector("uPinMove[0]",move?destination[0]-pin.sourceA:0,move?destination[1]-pin.sourceB:0,0,0);
        for(const std::array<float,4> input: {std::array<float,4>{.4f,.12f,.08f,1},std::array<float,4>{2.f,.8f,.2f,.5f},std::array<float,4>{.18f,.18f,.18f,1}}) {
            const auto source=GLHelpers::CreateTextureFromData(input.data(),1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT);
            const auto target=GLHelpers::CreateTextureFromData(nullptr,1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT);
            const auto fbo=GLHelpers::CreateFBO(target);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,1,1);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);glDrawArrays(GL_TRIANGLES,0,3);
            std::array<float,4> actual{};glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,actual.data());
            const auto expected=RawRecipe::ApplyPreparedColorWarp(recipe,{input[0],input[1],input[2]},rec2020?Raw::RawWorkingSpace::LinearRec2020D65:Raw::RawWorkingSpace::LinearSrgbD65,selectionMode==2?&masks:nullptr);
            for(int c=0;c<3;++c){const float error=std::abs(actual[c]-expected[c]);ok &= std::isfinite(actual[c])&&error<.0001f*std::max(1.f,std::abs(expected[c]));worst=std::max(worst,error);}
            if(version==5 && saturation==-1 && strength==1 && selectionMode==0 && input[0]>.2f) {
                const float scale=std::max({actual[0],actual[1],actual[2],1e-8f});
                ok &= std::abs(actual[0]-actual[1])<scale*.00002f && std::abs(actual[1]-actual[2])<scale*.00002f;
            }
            ok &= actual[3]==input[3] && glGetError()==GL_NO_ERROR;
            glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&source);glDeleteTextures(1,&target);
        }
        glDeleteTextures(3,maskTextures);
    }
    std::cout<<"Custom/legacy checks passed="<<ok<<"; worst="<<worst<<'\n';
    // Versioned family curve: ordinary slider edits need no spatial masks.
    integer("uHslModel",1);integer("uUseSpatialMasks",0);integer("uPinCount",0);
    scalar("uStrength",1);
    int hslCases=0;
    for(bool rec2020:{false,true})for(float saturation:{-1.f,-.5f,0.f,1.f,5.f}) {
        RawRecipe::RawColorWarpRecipe recipe;
        for(auto& pin:recipe.pins)pin.saturation=saturation;
        const auto restored=RawRecipe::DeserializeColorWarpRecipe(RawRecipe::SerializeColorWarpRecipe(recipe));
        ok &= restored.version==5 && restored.pins[0].saturation==saturation;
        const auto coefficients=RawRecipe::PrepareColorWarpHsl(restored);
        glUniform4fv(glGetUniformLocation(program,"uHsl[0]"),8,coefficients.front().data());
        integer("uInputIsRec2020",rec2020);
        const auto space=rec2020?Raw::RawWorkingSpace::LinearRec2020D65:Raw::RawWorkingSpace::LinearSrgbD65;
        for(const std::array<float,4> input:{std::array<float,4>{.4f,.12f,.08f,1},
            std::array<float,4>{8.f,2.f,.3f,.5f},std::array<float,4>{.18f,.1801f,.18f,1},
            std::array<float,4>{.0000004f,.00000012f,.00000008f,1},
            std::array<float,4>{-.01f,.2f,.3f,1},std::array<float,4>{0,0,0,1}}) {
            const auto source=GLHelpers::CreateTextureFromData(input.data(),1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT);
            const auto target=GLHelpers::CreateTextureFromData(nullptr,1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT);
            const auto fbo=GLHelpers::CreateFBO(target);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,1,1);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);glDrawArrays(GL_TRIANGLES,0,3);
            std::array<float,4> actual{};glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,actual.data());
            const auto expected=RawRecipe::ApplyPreparedColorWarp(restored,{input[0],input[1],input[2]},space);
            for(int c=0;c<3;++c) {
                const float error=std::abs(actual[c]-expected[c]);worst=std::max(worst,error);
                ok &= std::isfinite(actual[c]) && error<.0001f*std::max(1.f,std::abs(expected[c]));
                if(saturation==0)ok &= actual[c]==input[c] && expected[c]==input[c];
            }
            if(saturation==-1) {
                const float maximum=std::max({actual[0],actual[1],actual[2],1e-8f});
                const bool neutral=std::abs(actual[0]-actual[1])<maximum*.00002f && std::abs(actual[1]-actual[2])<maximum*.00002f;
                if(!neutral)std::cerr<<"Non-neutral endpoint: "<<actual[0]<<","<<actual[1]<<","<<actual[2]<<" space="<<rec2020<<'\n';
                ok &= neutral;
            }
            ok &= actual[3]==input[3] && glGetError()==GL_NO_ERROR;
            ++hslCases;glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&source);glDeleteTextures(1,&target);
        }
    }
    {
        RawRecipe::RawColorWarpRecipe recipe;
        recipe.pins[0].saturation=-1;
        const auto c=RawRecipe::PrepareColorWarpHsl(recipe);
        const auto center=RawRecipe::EvaluateColorWarpHsl(c,c[0][0]);
        ok &= center[1]==-1 && RawRecipe::EvaluateColorWarpHsl(c,c[1][0])[1]==0;
        // The red wrap and its feather remain continuous.
        const auto a=RawRecipe::EvaluateColorWarpHsl(c,-180.f+.0001f);
        const auto b=RawRecipe::EvaluateColorWarpHsl(c,180.f-.0001f);
        ok &= std::abs(a[1]-b[1])<.0001f;
        recipe.pins[0].hueShift=180;recipe.pins[1].hueShift=-180;
        const auto angles=RawRecipe::PrepareColorWarpHsl(recipe);
        const float midpoint=(angles[0][0]+angles[1][0])*.5f;
        ok &= std::abs(RawRecipe::EvaluateColorWarpHsl(angles,midpoint)[0])>179;
        recipe.version=4;
        ok &= RawRecipe::DeserializeColorWarpRecipe(RawRecipe::SerializeColorWarpRecipe(recipe)).version==4;
        recipe.version=5;
        // Adjustment-only edits must not invalidate custom selection analysis.
        const auto before=RawRecipe::ColorWarpSelectionFingerprint(recipe);
        recipe.pins[0].saturation=.5f;recipe.pins[0].hueShift=0;
        ok &= before==RawRecipe::ColorWarpSelectionFingerprint(recipe);
        ok &= !RawRecipe::UsesColorWarpSpatialMasks(recipe);
        // Explicit protection continues to override full family desaturation.
        for(auto& pin:recipe.pins) {pin.saturation=-1;pin.hueShift=0;}
        RawRecipe::RawColorWarpPin protection;protection.id="protect";protection.protectColor=true;
        protection.hueRange.enabled=false;recipe.pins.push_back(protection);
        const std::array<float,3> rgb{.4f,.12f,.08f};
        ok &= RawRecipe::ApplyPreparedColorWarp(recipe,rgb,Raw::RawWorkingSpace::LinearSrgbD65)==rgb;
        // Ordinary family overlap does not depend on the custom-pin blend mode.
        recipe.pins.pop_back();const auto accumulated=RawRecipe::ApplyPreparedColorWarp(recipe,rgb,Raw::RawWorkingSpace::LinearSrgbD65);
        recipe.combine=RawRecipe::RawColorWarpCombine::Blend;
        ok &= RawRecipe::ApplyPreparedColorWarp(recipe,rgb,Raw::RawWorkingSpace::LinearSrgbD65)==accumulated;
    }
    {
        Project::RawLayerStackDocument document;
        auto state=document.State();
        auto* node=Project::FindRawOperation(state.background,RawRecipe::GraphOperationKind::ColorWarp);
        if(!node)ok=false;
        else {
            auto recipe=RawRecipe::ReadGraphOperation(node->rawOperation);
            recipe.colorWarp.version=4;
            RawRecipe::WriteGraphOperation(node->rawOperation,recipe);
            std::string error;ok &= document.Apply(state,error);
            document.BeginGesture();
            for(float value:{-.25f,-.5f,-1.f}) {
                auto edited=document.State();
                auto* target=Project::FindRawOperation(edited.background,RawRecipe::GraphOperationKind::ColorWarp);
                recipe.colorWarp.version=5;recipe.colorWarp.pins[0].saturation=value;
                RawRecipe::WriteGraphOperation(target->rawOperation,recipe);
                ok &= document.Apply(std::move(edited),error);
            }
            document.EndGesture();
            const auto read=[&] {return RawRecipe::ReadGraphOperation(Project::FindRawOperation(document.State().background,RawRecipe::GraphOperationKind::ColorWarp)->rawOperation).colorWarp;};
            ok &= document.Undo(&error) && read().version==4 && read().pins[0].saturation==0;
            ok &= document.Redo(&error) && read().version==5 && read().pins[0].saturation==-1;
            std::cout<<"HSL graph gesture undo/redo passed="<<ok<<'\n';
        }
    }
    std::cout<<"HSL endpoint/identity/HDR/round-trip cases: "<<hslCases<<"; passed="<<ok<<'\n';
    ok &= ValidateHslSelectionMath(program);
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
    ok &= CaptureHslSelectionControls();
    ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();
    std::cout<<"Color Warp Move/Turn CPU-GPU difference: "<<worst<<"; passed="<<ok<<'\n';return ok;
}
}
