// SPDX-License-Identifier: GPL-3.0-only
#include "Renderer/Glass/MaterialRenderer.h"
#include "Renderer/Glass/glass_presets.hpp"
#include "Renderer/Glass/ControlMaterial.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Utils/GlassControls.h"
#include "Utils/FloatingSlider.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "App/settings/AppearanceTheme.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <backends/imgui_impl_opengl3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace Stack::Validation {
bool ValidateGlassMaterial(const std::filesystem::path& directory){
    namespace sg=stack::glass;
    using Renderer::Glass::MaterialRenderer;
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(960,720,"Liquid Glass validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    MaterialRenderer renderer;
    ImGuiExtras::GlassControlState controls;
    ImGuiExtras::FloatingSliderState floating;
    unsigned copy=0,vao=0,texture=0;
    bool success=false,backend=false;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    try{
        const auto require=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        std::filesystem::create_directories(directory);
        std::cout<<"GPU: "<<glGetString(GL_RENDERER)<<'\n';
        copy=GLHelpers::CreateShaderProgram(R"(#version 430 core
out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2-1,0,1);})",
            R"(#version 430 core
layout(binding=0)uniform sampler2D source;in vec2 uv;out vec4 color;
void main(){color=texture(source,vec2(uv.x,1-uv.y));})");
        require(copy!=0,"Reference copy shader failed.");glGenVertexArrays(1,&vao);glGenTextures(1,&texture);
        int fw=0,fh=0,channels=0;
        auto* pixels=stbi_load("tools/tests/glass/reconstructed-backdrop.png",&fw,&fh,&channels,4);
        require(pixels&&fw==400&&fh==360,"Run from the repository root; the frozen 400x360 backdrop fixture is required.");
        glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,fw,fh,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);stbi_image_free(pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        const auto background=[&](int x=0,int y=0,int w=400,int h=360){
            glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glDisable(GL_FRAMEBUFFER_SRGB);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
            glViewport(x,y,w,h);glUseProgram(copy);glBindVertexArray(vao);glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D,texture);glDrawArrays(GL_TRIANGLES,0,3);
        };
        const auto read=[&](int x,int y,int w,int h){
            std::vector<unsigned char> data(std::size_t(w)*h*4);Renderer::GLState::PixelPackState pack;pack.ConfigureTightCpuReadback();
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);glReadPixels(x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data.data());pack.Restore();return data;
        };
        const auto save=[&](const char* name,int w,int h,const std::vector<unsigned char>& data){
            std::vector<unsigned char> top(data.size());for(int y=0;y<h;++y)std::copy_n(data.data()+std::size_t(h-1-y)*w*4,w*4,top.data()+std::size_t(y)*w*4);
            require(stbi_write_png((directory/name).string().c_str(),w,h,4,top.data(),w*4)!=0,"Capture write failed.");
        };
        background();const auto original=read(0,0,400,360);
        renderer.BeginFrame();const Renderer::GLState::FramebufferState host(true);
        auto* target=renderer.Acquire(400,360);host.Restore(true);require(target,"Material allocation failed.");
        require(renderer.Capture(*target,0,0,1),"Backdrop capture failed.");
        sg::BackdropDescription desc;const sg::ContextKey context{1,{1,1}};
        desc.image={1,{target->allocation,1}};desc.viewport={context,{1,1}};desc.captureScope={1,1};desc.frameSerial=1;
        desc.mapping={{{0,0},{400,360}},{0,0,400,360},{400,360},sg::PixelOrigin::BottomLeft};desc.provenance.complete=true;
        const sg::BackdropSnapshot snapshot(desc);
        sg::GlassPassContract pass;pass.viewport=desc.viewport;pass.group={context,{2,1}};pass.frameSerial=1;
        pass.destination={1,{target->allocation+1,1}};pass.requiredSampleDomainPxTL={{0,0},{400,360}};pass.output=sg::CompositeRepresentation::PremultipliedSurface;
        sg::gl::UniformPacket packet;packet.material=*sg::presets::clearControlV2(sg::presets::clearControlV2ReferenceHalfHeightPx);
        packet.geometry.localOriginFramebufferPx={200.5856700546489f,203.95800513509388f};
        packet.geometry.shapes[0]={{0,0},{120.12570417879074f,sg::presets::clearControlV2ReferenceHalfHeightPx},sg::presets::clearControlV2ReferenceHalfHeightPx};
        packet.source={0,target->mips,float(target->mips-1),{400,360},&snapshot,&pass};
        require(renderer.Draw(*target,packet,0,0,400,360),"Native material draw failed.");
        const auto premult=read(0,0,400,360);save("clear-control-reference.png",400,360,premult);
        background();packet.controls.output=sg::gl::OutputMode::OpaqueReference;pass.output=sg::CompositeRepresentation::OpaqueReplacement;
        require(renderer.Draw(*target,packet,0,0,400,360),"Opaque material draw failed.");const auto opaque=read(0,0,400,360);
        int maxDifference=0;for(std::size_t i=0;i<opaque.size();++i)maxDifference=std::max(maxDifference,std::abs(int(opaque[i])-premult[i]));
        require(maxDifference<=2,"Premultiplied and opaque reference composition disagree.");
        background();packet.material.materialize=0;packet.material.roughnessLod=3;
        require(renderer.Draw(*target,packet,0,0,400,360),"Zero-presence draw failed.");const auto absent=read(0,0,400,360);
        maxDifference=0;for(std::size_t i=0;i<absent.size();++i)maxDifference=std::max(maxDifference,std::abs(int(absent[i])-original[i]));
        require(maxDifference<=1,"Zero presence changed the captured source.");
        pass.destination=desc.image;require(!renderer.Draw(*target,packet,0,0,400,360),"Same-allocation feedback was accepted.");
        pass.destination={1,{target->allocation+1,1}};
        const auto mipBuilds=renderer.Diagnostics().mipBuilds;require(renderer.Capture(*target,0,0,1),"Revision reuse failed.");
        require(renderer.Diagnostics().mipBuilds==mipBuilds,"An unchanged declared source rebuilt mips.");
        // Nonzero crop origin uses the same local optical coordinates.
        background(37,29);require(renderer.Capture(*target,37,29,2),"Cropped source capture failed.");
        packet.material=*sg::presets::clearControlV2(sg::presets::clearControlV2ReferenceHalfHeightPx);
        require(renderer.Draw(*target,packet,37,29,400,360),"Cropped material draw failed.");
        const auto cropped=read(37,29,400,360);maxDifference=0;
        for(std::size_t i=0;i<cropped.size();++i)maxDifference=std::max(maxDifference,std::abs(int(cropped[i])-opaque[i]));
        require(maxDifference<=2,"Crop origin changed the local optical mapping.");

        // Same clear-water material and incident light over three base levels.
        // Encoding remains bounded to this UI validation framebuffer.
        Renderer::Glass::MaterialEffects water;water.clearWater=true;
        packet.material=*Renderer::Glass::ClearControlMaterial(sg::presets::clearControlV2ReferenceHalfHeightPx);
        packet.controls.output=sg::gl::OutputMode::PremultipliedLayer;
        pass.output=sg::CompositeRepresentation::PremultipliedSurface;
        std::array<std::vector<unsigned char>,3> waterLevels;
        const char* waterNames[]={"clear-water-dark.png","clear-water-mid.png","clear-water-light.png"};
        for(int level=0;level<3;++level) {
            const float linear=level*.5f;
            const float encoded=linear<=.0031308f?12.92f*linear:1.055f*std::pow(linear,1.f/2.4f)-.055f;
            glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glDisable(GL_FRAMEBUFFER_SRGB);
            glDisable(GL_SCISSOR_TEST);glClearColor(encoded,encoded,encoded,1);glClear(GL_COLOR_BUFFER_BIT);
            const auto base=read(0,0,400,360);
            require(renderer.Capture(*target,0,0,10+level),"Water source capture failed.");
            water.opacity=0;
            require(renderer.Draw(*target,packet,0,0,400,360,water),"Zero-opacity water draw failed.");
            const auto invisible=read(0,0,400,360);
            int error=0;for(std::size_t i=0;i<base.size();++i)error=std::max(error,std::abs(int(base[i])-invisible[i]));
            require(error<=1,"Layer opacity changed pixels when zero.");
            water.opacity=1;
            require(renderer.Draw(*target,packet,0,0,400,360,water),"Clear-water draw failed.");
            waterLevels[level]=read(0,0,400,360);save(waterNames[level],400,360,waterLevels[level]);
        }
        const auto decode=[](unsigned char value){const float c=value/255.f;return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
        float continuityError=0;
        for(std::size_t i=0;i<waterLevels[0].size();++i)if(i%4!=3 && waterLevels[2][i]<250)
            continuityError=std::max(continuityError,std::abs(decode(waterLevels[1][i])-.5f*(decode(waterLevels[0][i])+decode(waterLevels[2][i]))));
        require(continuityError<.012f,"Clear water changed its lighting with background brightness.");
        water.logStrain={0,.2f};background();require(renderer.Capture(*target,0,0,20),"Tensor source capture failed.");
        require(renderer.Draw(*target,packet,0,0,400,360,water),"Diagonal tensor draw failed.");
        save("clear-water-diagonal.png",400,360,read(0,0,400,360));
        water.logStrain={0,1};require(!renderer.Draw(*target,packet,0,0,400,360,water),"Excessive tensor strain was accepted.");
        std::cout<<"Clear-water lighting continuity error (linear, 8-bit target): "<<continuityError<<"; zero layer opacity and diagonal tensor uploads passed.\n";

        // Overlapping peers must stay in the linear working image until the
        // complete group is presented. The host cannot expose a partial group.
        water.logStrain={};water.opacity=.7f;background();
        require(renderer.Capture(*target,0,0,21),"Group source capture failed.");
        const auto groupBase=read(0,0,400,360);
        const auto encodesBefore=renderer.Diagnostics().encodes;
        require(renderer.BeginComposition(*target,0,0,400,360),"Linear group initialization failed.");
        require(renderer.Composite(*target,packet,0,0,400,360,water),"First peer draw failed.");
        packet.geometry.localOriginFramebufferPx.x+=45.f;
        require(renderer.Composite(*target,packet,0,0,400,360,water),"Overlapping peer draw failed.");
        require(read(0,0,400,360)==groupBase,"A peer encoded before its group was complete.");
        require(renderer.EndComposition(*target),"Linear group presentation failed.");
        require(renderer.Diagnostics().encodes==encodesBefore+1,"A group encoded more than once.");
        const auto overlap=read(0,0,400,360);
        require(overlap!=groupBase,"The completed group did not reach its destination.");
        save("clear-water-overlap.png",400,360,overlap);

        StackAppearance::AppearanceManager appearance;appearance.SetupFonts(io);appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
        backend=ImGui_ImplOpenGL3_Init("#version 430");require(backend,"ImGui renderer initialization failed.");
        Editor::RawLabInternal::CurveCanvasState curveState;
        RawRecipe::CurveAmountState curveAmount;
        std::vector<RawRecipe::RawBezierCurvePoint> points{
            {0,.12f,{1},{1}}, {.32f,.22f,{1},{1}}, {.64f,.72f,{1},{1}}, {1,.9f,{1},{1}}};
        float amount=.4f;
        const auto uiFrame=[&]{
            io.DisplaySize={640,480};io.DisplayFramebufferScale={1.5f,1.5f};io.DeltaTime=1.f/60.f;
            ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();controls.BeginFrame(true,"floating-graph-check");
            ImGui::SetNextWindowPos({50,24});ImGui::SetNextWindowSize({540,300});
            ImGui::Begin("Floating graph",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground);
            {
                ImGuiExtras::GlassControlScope glass(controls,1.f,false,ImGuiExtras::GlassElements::FloatingTools);
                Editor::RawLabInternal::DrawCurveCanvas("curve",points,curveAmount,curveState,
                    false,{}, {520,270},12);
                require(!ImGuiExtras::HasGlassControlScope(),"Point-only presentation enabled control skins.");
            }
            ImGui::End();
            ImGui::SetNextWindowPos({50,330});ImGui::SetNextWindowSize({540,145});
            ImGui::Begin("Floating slider",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground);
            {
                ImGuiExtras::GlassControlScope glass(controls,1.f,false,ImGuiExtras::GlassElements::FloatingTools);
                ImGuiExtras::FloatingSliderScope values(floating);
                ImGuiExtras::FloatingSliderFloat("Amount","water-thumb",&amount,0,1,"%.3f",500);
            }
            ImGui::End();ImGui::Render();background(0,0,960,720);glViewport(0,0,960,720);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            require(controls.material.Error().empty(),controls.material.Error().c_str());
            require(glGetError()==GL_NO_ERROR,"Native floating graph produced an OpenGL error.");
        };
        for(int i=0;i<4;++i)uiFrame();save("floating-graph-points.png",960,720,read(0,0,960,720));
        const auto allocations=controls.material.Diagnostics().allocations;
        const auto joinBuilds=controls.material.JoinBuildCount();
        for(int i=0;i<4;++i)uiFrame();
        require(controls.material.Diagnostics().allocations==allocations,"Stationary graph points reallocated GPU targets.");
        require(controls.material.Diagnostics().draws>0,"No glass graph point was rendered.");
        require(controls.material.Diagnostics().encodes==controls.material.Diagnostics().compositions &&
            controls.material.Diagnostics().encodes<controls.material.Diagnostics().draws,
            "Floating controls did not encode once per complete surface group.");
        require(controls.controls.empty() && controls.feedback.empty(),"Floating tool icons or dials acquired glass motion.");
        require(controls.material.JoinBuildCount()==joinBuilds,"Idle graph rebuilt unchanged proximity groups.");
        points[1].x=.48f;points[1].y=.5f;points[2].x=.54f;points[2].y=.5f;
        const auto beforeJoin=controls.material.Diagnostics().draws;
        uiFrame();
        require(controls.material.Diagnostics().draws==beforeJoin+5,"Touching graph points did not render as one compound.");
        save("floating-graph-joined.png",960,720,read(0,0,960,720));
        const auto joinedBuilds=controls.material.JoinBuildCount(),joinedAllocations=controls.material.Diagnostics().allocations;
        uiFrame();
        require(controls.material.JoinBuildCount()==joinedBuilds && controls.material.Diagnostics().allocations==joinedAllocations,
            "An idle joined graph rebuilt topology or allocated GPU storage.");
        points[2].x=.64f;
        const auto beforeSeparate=controls.material.Diagnostics().draws;
        uiFrame();
        require(controls.material.Diagnostics().draws==beforeSeparate+6,"Separated graph points did not recover individual rendering.");
        save("floating-graph-separated.png",960,720,read(0,0,960,720));
        std::cout<<"Native material: reference, premultiplied equivalence, zero presence, feedback rejection, crop, revision cache, fractional-scale floating graph passed.\n";
        const auto& c=controls.material.Diagnostics();std::cout<<"Graph counters: allocations="<<c.allocations<<" captures="<<c.captures<<" mip builds="<<c.mipBuilds<<" draws="<<c.draws<<" groups="<<c.compositions<<" encodes="<<c.encodes<<'\n';
        success=true;
    }catch(const std::exception& e){std::cerr<<"Liquid Glass validation failed: "<<e.what()<<'\n';}
    ImGuiExtras::SetGlassFrameState(nullptr);controls.Shutdown();renderer.Shutdown();
    if(copy)glDeleteProgram(copy);if(vao)glDeleteVertexArrays(1,&vao);if(texture)glDeleteTextures(1,&texture);
    if(backend)ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
