#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Raw/WhiteBalanceEstimate.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Editor/WhiteBalanceAnalysis.h"
#include "Editor/RawRenderService.h"
#include "Async/TaskSystem.h"
#include <thread>
#include <chrono>
#include "Project/RawLayerStack.h"
#include "App/settings/AppearanceTheme.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace Stack::Editor::InterpretationUi {
bool InterpretationControls(RawWorkspaceEditContext&,bool,bool&,int&,bool);
}
namespace Stack::Validation {
bool ValidateInterpretation(const std::filesystem::path& output) {
    auto require=[](bool b,const char* reason){if(!b)throw std::runtime_error(reason);};
    try {
        using Mode=RawRecipe::WhiteBalanceMode;
        auto recipe=RawRecipe::MakeDefaultRecipe({});
        require(recipe.whiteBalance.mode==Mode::AsShot,"New recipes must default to Camera.");
        for(auto mode:{Mode::AsShot,Mode::Auto,Mode::CustomMultipliers}) {
            recipe.whiteBalance.mode=mode;recipe.whiteBalance.hasMultipliers=mode!=Mode::AsShot;
            recipe.whiteBalance.multipliers={.5f,1.f,2.f};
            const auto restored=RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(recipe));
            require(restored.whiteBalance.mode==mode,"White-balance mode did not round-trip.");
            require(restored.whiteBalance.multipliers==recipe.whiteBalance.multipliers,"White-balance values did not round-trip.");
        }
        using Mapping=Editor::RawLabInternal::RawLabImageMapping;
        NodeMath::SpatialDescriptor local,displayed;
        auto mapping=Mapping::FromSpatial(local,displayed,{},false);
        require(bool(mapping),"Normalized source picking must not require raster dimensions.");
        auto uv=mapping->Local({.25f,.75f});
        require(std::abs(uv.x-.25f)<1e-6f&&std::abs(uv.y-.75f)<1e-6f,"Source coordinates changed.");
        displayed.sourceTransform={0,0,0,0,0,0,0,0,1};
        require(!Mapping::FromSpatial(local,displayed,{},false),"Invalid transforms must still be rejected.");
        auto legacy=RawRecipe::SerializeRecipe(recipe);legacy.erase("whiteBalance");
        require(RawRecipe::DeserializeRecipe(legacy).whiteBalance.mode==Mode::AsShot,"Missing legacy WB changed behavior.");
        std::vector<float> pixels(8*8*4);
        for(int i=0;i<64;++i){pixels[i*4]=.4f;pixels[i*4+1]=.2f;pixels[i*4+2]=.1f;pixels[i*4+3]=1;}
        std::array<float,3> gains;std::string error;
        for(auto sample:{std::optional<std::array<float,2>>{},std::optional<std::array<float,2>>{{.5f,.5f}}}) {
            require(Raw::EstimateWhiteBalance(pixels,8,8,{1,1,1},sample,gains,error),"Neutral sampling failed.");
            require(std::abs(gains[0]-.5f)<1e-6f&&std::abs(gains[1]-1)<1e-6f&&std::abs(gains[2]-2)<1e-6f,"Incorrect sampled balance.");
        }
        std::vector<float> halves(16*16*4);
        for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
            const int i=(y*16+x)*4;halves[i]=y<8?.4f:.1f;halves[i+1]=.2f;halves[i+2]=y<8?.1f:.4f;halves[i+3]=1;
        }
        require(Raw::EstimateWhiteBalance(halves,16,16,{1,1,1},std::array<float,2>{.5f,.1f},gains,error),"Top sample failed.");
        require(std::abs(gains[0]-2)<1e-6f&&std::abs(gains[2]-.5f)<1e-6f,"Top-left sample coordinates were inverted.");
        std::fill(pixels.begin(),pixels.end(),1.f);
        require(!Raw::EstimateWhiteBalance(pixels,8,8,{1,1,1},std::nullopt,gains,error),"Clipped samples were accepted.");
        Project::RawLayerStackDocument document;
        auto source=RawRecipe::MakeDefaultRecipe({});source.whiteBalance.mode=Mode::AsShot;
        auto next=source;next.whiteBalance.mode=Mode::CustomMultipliers;next.whiteBalance.hasMultipliers=true;next.whiteBalance.multipliers={.5f,1,2};
        require(document.ApplySourceEdit(document.State(),source,next,error),"Source edit failed.");
        require(document.Undo(&error,nullptr,0,&source)&&source.whiteBalance.mode==Mode::AsShot,"WB undo failed.");
        require(document.Redo(&error,nullptr,0,&source)&&source.whiteBalance.multipliers==next.whiteBalance.multipliers,"WB redo failed.");
        require(glfwInit(),"GLFW initialization failed.");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        auto* window=glfwCreateWindow(480,600,"Interpretation preview",nullptr,nullptr);require(window,"No render context.");
        glfwMakeContextCurrent(window);require(LoadGLFunctions(),"GL setup failed.");
        Async::TaskSystem::Get().Initialize();
        const auto client=EditorRendering::RawRenderService::Get().Acquire(window);
        require(client!=0,"White balance worker unavailable.");
        auto raw=std::make_shared<Raw::RawImageData>();
        raw->metadata.rawWidth=raw->metadata.visibleWidth=8;
        raw->metadata.rawHeight=raw->metadata.visibleHeight=8;
        raw->metadata.pixelLayout=Raw::RawPixelLayout::LinearRgb;raw->metadata.mosaiced=false;
        raw->metadata.linearChannels=3;raw->reconstructedCameraRgb=true;
        for(int i=0;i<64;++i){raw->linearFloatBuffer.push_back(.4f);raw->linearFloatBuffer.push_back(.2f);raw->linearFloatBuffer.push_back(.1f);}
        Editor::WhiteBalanceAnalysis analysis;
        auto measured=RawRecipe::MakeDefaultRecipe({});
        for(auto sample:{std::optional<std::array<float,2>>{},std::optional<std::array<float,2>>{{.5f,.5f}}}) {
            error.clear();require(analysis.Start(raw,measured,sample),"Analysis request rejected.");
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!analysis.Poll(gains,error)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            require(!analysis.Busy()&&error.empty(),error.empty()?"Analysis timed out.":error.c_str());
            require(std::abs(gains[0]-.5f)<.002f&&std::abs(gains[2]-2)<.002f,"Worker sampling disagrees with neutral patch.");
        }
        EditorRendering::RawRenderService::Get().Release(client);
        Async::TaskSystem::Get().Shutdown();
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={480,600};io.DeltaTime=1.f/60;
        StackAppearance::AppearanceManager appearance;appearance.SetupFonts(io);appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
        ImGui_ImplOpenGL3_Init("#version 430");
        Editor::RawWorkspaceEditContext context;context.canEdit=true;context.recipe=recipe;
        context.recipe.technical.demosaicMethod=Raw::DemosaicMethod::Lmmse;
        for(int frame=0;frame<3;++frame) {
            ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({480,600});
            ImGui::Begin("Interpretation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
            bool active=false;int request=0;
            require(!Editor::InterpretationUi::InterpretationControls(context,false,active,request,false),"Idle UI authored an edit.");
            ImGui::End();ImGui::Render();glViewport(0,0,480,600);glClearColor(.025f,.025f,.025f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        std::vector<unsigned char> image(480*600*4);glReadPixels(0,0,480,600,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
        for(int y=0;y<300;++y)for(int x=0;x<480*4;++x)std::swap(image[y*480*4+x],image[(599-y)*480*4+x]);
        std::filesystem::create_directories(output);require(stbi_write_png((output/"interpretation.png").string().c_str(),480,600,4,image.data(),480*4),"Could not save preview.");
        ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();
        std::cout<<"Interpretation modes, sampling, source undo/redo, idle UI and native preview passed.\n";return true;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return false;}
}
}

