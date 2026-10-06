#include "Editor/Bracketing/BracketingSession.h"
#include "Editor/EditorModule.h"
#include "BracketingRawToolValidation.h"
#include "BracketingRawLayoutValidation.h"
#include "BracketingPresentationValidation.h"
#include "Renderer/GLLoader.h"
#include "Raw/RawGpuPipeline.h"
#include "ThirdParty/stb_image_write.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace Stack::Validation {
bool ValidateBracketingUi(const std::filesystem::path& directory) {
    if(!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(1440,900,"Bracketing validation",nullptr,nullptr);
    if(!window) {glfwTerminate();return false;}
    glfwMakeContextCurrent(window);if(!LoadGLFunctions()) {glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
    ImGui_ImplGlfw_InitForOpenGL(window,false);ImGui_ImplOpenGL3_Init("#version 430");
    bool success=ValidateBracketingDraftLayout(window,directory);
    success&=BracketingPresentationValidationAccess::Run(window,directory);
    glfwSetWindowSize(window,1440,900);glfwPollEvents();
    {
        EditorModule editor;editor.SetDocumentPersistenceEnabled(false);
        try {BracketingRawToolValidationAccess::Selection(editor);}
        catch(const std::exception& error){std::cerr<<error.what()<<'\n';success=false;}
        BracketingRawToolValidationAccess::Panel(editor);
        for(float scale:{1.f,1.5f,2.f})for(float width:{340.f,520.f}) {
            ImGui::GetIO().FontGlobalScale=scale;
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
            ImGui::SetNextWindowPos({0,50});ImGui::SetNextWindowSize({width*scale,800});
            ImGui::Begin("RAW Bracketing tool",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
            editor.RenderBracketingUI();ImGui::End();ImGui::Render();
            ImGuiWindow* controls=nullptr;
            for(auto* candidate:ImGui::GetCurrentContext()->Windows)
                if(std::strstr(candidate->Name,"RAW Bracketing tool/BracketToolControls"))controls=candidate;
            success&=controls&&controls->Pos.y>90&&controls->Pos.x>=0;
            glViewport(0,0,1440,900);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());glFinish();
            std::vector<unsigned char> pixels(1440*900*4);glReadPixels(0,0,1440,900,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for(int y=0;y<450;++y)for(int x=0;x<1440*4;++x)std::swap(pixels[y*1440*4+x],pixels[(899-y)*1440*4+x]);
            std::filesystem::create_directories(directory);
            stbi_write_png((directory/("raw-bracket-"+std::to_string(int(width))+"-"+std::to_string(int(scale*100))+".png")).string().c_str(),1440,900,4,pixels.data(),1440*4);
        }
        std::cout<<"RAW bracket selection and tool layout: "<<(success?"passed":"failed")<<'\n';
        ImGui::GetIO().FontGlobalScale=1;
        const auto galleryFrame=[&](bool filmstrip,ImVec2 mouse,bool down,bool ctrl=false,bool shift=false) {
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();
            auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);
            io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);io.AddKeyEvent(ImGuiMod_Shift,shift);io.AddMouseButtonEvent(0,down);
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({900,500});
            ImGui::Begin("Bracket capture selection",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
            BracketingRawToolValidationAccess::Gallery(editor,filmstrip);ImGui::End();ImGui::Render();
            glViewport(0,0,1440,900);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        };
        galleryFrame(false,{80,60},false);galleryFrame(false,{80,60},true);galleryFrame(false,{80,60},false);
        success&=BracketingRawToolValidationAccess::SelectionCount(editor)==1;
        galleryFrame(false,{254,60},true,true);galleryFrame(false,{254,60},false,true);
        success&=BracketingRawToolValidationAccess::SelectionCount(editor)==2;
        galleryFrame(false,{80,60},true);galleryFrame(false,{80,60},false);
        galleryFrame(false,{602,60},true,false,true);galleryFrame(false,{602,60},false,false,true);
        success&=BracketingRawToolValidationAccess::SelectionCount(editor)==4;
        galleryFrame(true,{-100,-100},false);
        std::cout<<"Gallery click, Ctrl-toggle, Shift-range and filmstrip rendering: "<<(success?"passed":"failed")<<'\n';
    }
    {
        Raw::RawImageData raw;auto& m=raw.metadata;
        m.rawWidth=m.visibleWidth=16;m.rawHeight=m.visibleHeight=16;
        m.pixelLayout=Raw::RawPixelLayout::MosaicBayer;m.cfaPattern=Raw::CfaPattern::RGGB;m.mosaiced=true;m.orientation=1;
        raw.normalizedMosaicInputContract=Raw::NormalizedMosaicInputContract::BracketingPreGain;
        auto mosaic=std::make_shared<std::vector<float>>(256);
        for(unsigned p=0;p<256;++p)mosaic->at(p)=p<128?-.1f:1.4f;
        raw.normalizedMosaicBuffer=mosaic;
        auto sidecars=std::make_shared<Raw::RawImageData::MultiFrameMeasurementSidecars>();
        sidecars->variance=std::make_shared<std::vector<float>>(256,.001f);raw.multiFrameMeasurementSidecars=sidecars;
        Raw::RawDevelopSettings settings;settings.debugView=Raw::RawDebugView::PreDenoiseMosaic;
        Raw::RawGpuPipeline pipeline;const auto texture=pipeline.Render(raw,settings);
        success&=texture!=0&&pipeline.GetLastPreprocessTelemetry().varianceGenerated;
        if(texture) {
            std::vector<float> rgba(256*4);glBindTexture(GL_TEXTURE_2D,texture);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,rgba.data());glBindTexture(GL_TEXTURE_2D,0);
            // GL readback starts at the bottom. The sensor's first row is the top.
            success&=std::abs(rgba[0]-1.4f)<.002f&&std::abs(rgba[255*4]+.1f)<.002f;
            success&=pipeline.Render(raw,settings)!=0;
        }
        std::cout<<"Bracket RAW orientation, signed range, and variance upload: "<<(success?"passed":"failed")<<'\n';
    }
    {
        Editor::BracketingSession ui;ui.recipe.originFrameId="a";
        ui.recipe.groups={{"one","Bright exposure",true,{{"a"}}},{"two","Middle exposure",true,{{"b"}}},{"three","Dark exposure",true,{{"c"}}}};
        ui.recipe.knots=Raw::Bracketing::EqualCurves(3);
        ui.recipe.knots.front().share=ui.recipe.knots.front().left=ui.recipe.knots.front().right={.8,.2,0};
        ui.recipe.knots.back().share=ui.recipe.knots.back().left=ui.recipe.knots.back().right={0,.2,.8};
        Raw::Bracketing::InsertKnot(ui.recipe.knots,-2);
        ui.recipe.automatic=false;ui.selectedPoint=0;
        auto result=std::make_shared<Raw::Bracketing::BracketingResult>();auto analysis=std::make_shared<Raw::Bracketing::BracketingAnalysis>();
        analysis->suggestion=ui.recipe.knots;for(unsigned i=0;i<256;++i)analysis->histogram[i]=std::exp(-std::pow((static_cast<float>(i)-128)/60,2));
        result->analysis=analysis;ui.result=result;
        Raw::Bracketing::Preview p;p.width=256;p.height=128;p.samples.resize(256*128*4*3);p.guideEv.resize(256*128);
        for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
            const auto index=y*256+x;const float value=.01f+2.f*x/255;
            p.guideEv[index]=std::log2(value/.18f);
            for(unsigned c=0;c<4;++c) for(unsigned g=0;g<3;++g) p.samples[(index*4+c)*3+g]={value,.001f,1,1,value,1,true,false};
        }
        ui.preview=Raw::Bracketing::ReblendPreview(p,ui.recipe,*analysis);
        for(unsigned g=0;g<3;++g) {
            auto original=std::make_shared<Raw::Bracketing::CapturePreview>();original->width=p.width;original->height=p.height;
            original->exposureScale=std::exp2(static_cast<double>(g));original->rgb=ui.preview.resultRgb;original->clipped.resize(p.width*p.height);
            for(auto& v:original->rgb)v/=static_cast<float>(original->exposureScale);
            analysis->originals[std::string(1,'a'+g)]=original;ui.frameLabels[std::string(1,'a'+g)]="Capture "+std::to_string(g+1);
            for(unsigned other=0;other<g;++other)success&=ImGui::ColorConvertFloat4ToU32(Editor::BracketColor(ui.recipe,g))!=ImGui::ColorConvertFloat4ToU32(Editor::BracketColor(ui.recipe,other));
        }
        std::filesystem::create_directories(directory);
        for(int mode:{0,1,2,3,5,6}) for(const auto dimensions: {ImVec2(960,640),ImVec2(1440,900),ImVec2(1920,1080)}) {
            ui.view=mode;ui.hoverFrame=mode==1?"b":"";
            const int w=static_cast<int>(dimensions.x),h=static_cast<int>(dimensions.y);glfwSetWindowSize(window,w,h);glfwPollEvents();
            ImGui::GetIO().FontGlobalScale=w==960?1.f:w==1440?1.5f:2.f;
            for(int frame=0;frame<3;++frame) {
                ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
                ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(dimensions);
                ImGui::Begin("Bracketing",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
                ImGui::TextUnformatted("Bracketing");ImGui::SameLine();ImGui::TextDisabled("RAW alignment, denoise groups, and HDR blend");
                ImGui::TextDisabled("Alignment: Automatic local");
                ImGui::BeginChild("Sources",{dimensions.x*.23f,0},true);
                ImGui::TextUnformatted("Exposure groups");
                for(unsigned g=0;g<3;++g) {ImGui::PushStyleColor(ImGuiCol_Text,Editor::BracketColor(ui.recipe,g));ImGui::Selectable(ui.recipe.groups[g].name.c_str(),ui.selectedGroup==g);ImGui::PopStyleColor();ImGui::TextDisabled("2 captures / ISO 100");ImGui::Separator();}
                ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Editor",{0,0},false);
                Editor::DrawBracketingPreview(ui,{ImGui::GetContentRegionAvail().x,dimensions.y*.52f});
                const auto before=Raw::Bracketing::Identity(ui.recipe);
                Editor::DrawBracketingCurves(ui,{ImGui::GetContentRegionAvail().x-16,dimensions.y*.22f});
                success&=before==Raw::Bracketing::Identity(ui.recipe);
                ImGui::EndChild();ImGui::End();ImGui::Render();
                glViewport(0,0,w,h);glClearColor(.1f,.1f,.12f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());glFinish();
            }
            std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for(int y=0;y<h/2;++y)for(int x=0;x<w*4;++x)std::swap(pixels[static_cast<std::size_t>(y)*w*4+x],pixels[static_cast<std::size_t>(h-1-y)*w*4+x]);
            success&=stbi_write_png((directory/("bracketing-"+std::to_string(w)+"-view-"+std::to_string(mode)+".png")).string().c_str(),w,h,4,pixels.data(),w*4)!=0;
        }
        // Drive the widget through ImGui input events. No desktop automation is involved.
        const auto frame=[&](ImVec2 mouse,int button) {
            ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();
            auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);
            if(button>=0) io.AddMouseButtonEvent(0,button!=0);
            ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1920,1080});
            ImGui::Begin("Curve interaction",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
            Editor::DrawBracketingCurves(ui,{1500,500});ImGui::End();ImGui::Render();
        };
        frame({-100,-100},0);frame({-100,-100},0);
        const auto position=[&](double ev,double share) {return ImVec2(ui.curveOrigin.x+static_cast<float>((ev-ui.viewMin)/(ui.viewMax-ui.viewMin))*ui.curveSize.x,
            ui.curveOrigin.y+static_cast<float>(1-share)*ui.curveSize.y);};
        const auto drag=[&](ImVec2 a,ImVec2 b) {frame(a,-1);frame(a,1);frame(b,-1);frame(b,0);};
        auto before=Raw::Bracketing::Identity(ui.recipe);
        drag(position(ui.recipe.knots[1].ev,ui.recipe.knots[1].share[0]),position(-1,.65));
        success&=before!=Raw::Bracketing::Identity(ui.recipe)&&ui.selectedPoint==1;
        before=Raw::Bracketing::Identity(ui.recipe);
        drag(position(ui.recipe.knots[1].outgoing,ui.recipe.knots[1].right[0]),position(3,.3));
        success&=before!=Raw::Bracketing::Identity(ui.recipe)&&ui.selectedHandle==1;
        std::string error;success&=Raw::Bracketing::Validate(ui.recipe,error);
        for(double ev=-12;ev<=8;ev+=.025) {
            const auto shares=Raw::Bracketing::Evaluate(ui.recipe.knots,ev);double sum=0;
            for(auto share:shares) {success&=share>=0&&share<=1;sum+=share;}
            success&=std::abs(sum-1)<1e-9;
        }
        before=Raw::Bracketing::Identity(ui.recipe);ui.viewMin=-20;ui.viewMax=12;frame({-100,-100},0);
        success&=before==Raw::Bracketing::Identity(ui.recipe);
        std::cout<<"Curve point/handle interactions and view-only range: "<<(success?"passed":"failed")<<'\n';
    }
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();
    std::cout<<(success?"Bracketing UI rendering passed.\n":"Bracketing UI rendering failed.\n");return success;
}
} // namespace Stack::Validation
