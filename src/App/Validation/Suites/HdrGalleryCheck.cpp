#include "App/Validation/Suites/HdrGalleryCheck.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Hdr/Presentation.h"
#include "Renderer/GLStateGuards.h"
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace Stack::Validation {
namespace {
struct Pixels { int w=0,h=0; std::vector<float> rgba; };
Pixels Read(unsigned texture) {
    Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
    Renderer::GLState::PixelPackState pack;
    pack.ConfigureTightCpuReadback();
    glBindTexture(GL_TEXTURE_2D,texture);
    Pixels p;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&p.w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&p.h);
    if(p.w>0 && p.h>0) {
        p.rgba.resize(size_t(p.w)*p.h*4);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,p.rgba.data());
    }
    pack.Restore(); binding.Restore();
    if(p.rgba.empty() || !std::all_of(p.rgba.begin(),p.rgba.end(),[](float v){return std::isfinite(v);}))
        throw std::runtime_error("HDR photo texture is missing or contains nonfinite values.");
    return p;
}
// Area-average the two raster resolutions into the same small color patches.
double Compare(const Pixels& a,const Pixels& b) {
    const auto mean=[](const Pixels& p,int x,int y) {
        std::array<double,3> rgb{};
        const int x0=x*p.w/16,x1=(x+1)*p.w/16,y0=y*p.h/16,y1=(y+1)*p.h/16;
        for(int row=y0;row<y1;++row) for(int col=x0;col<x1;++col)
            for(int c=0;c<3;++c) rgb[c]+=p.rgba[4*(size_t(row)*p.w+col)+c];
        for(auto& v:rgb) v/=std::max(1,(x1-x0)*(y1-y0));
        return rgb;
    };
    double error=0;
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        const auto u=mean(a,x,y),v=mean(b,x,y);
        for(int c=0;c<3;++c) error+=std::abs(u[c]-v[c]);
    }
    return error/(16*16*3);
}
}

void CheckHdrGallery(GLFWwindow* window,const std::filesystem::path& original) {
    if(!Hdr::PreviewActive()) throw std::runtime_error("The Gallery hardware check needs active Windows HDR.");
    const auto input=std::filesystem::absolute(original);
    const auto prior=ImGui::GetCurrentContext();
    auto* ctx=ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGui::GetIO().IniFilename=nullptr;
    ImGui::GetIO().DisplaySize={900,650};
    ImGui::GetIO().DeltaTime=1.f/60;
    ImGui::GetMainViewport()->PlatformHandle=window;
    ImGui_ImplOpenGL3_Init("#version 430");
    auto& tasks=Async::TaskSystem::Get();
    tasks.Initialize();
    auto editor=std::make_unique<EditorModule>();
    editor->SetDocumentPersistenceEnabled(false);
    editor->SetWorkspaceAppStatePersistenceEnabled(false);
    editor->Initialize(window,nullptr,false,true);
    std::exception_ptr failure;
    try {
        Stack::RawWorkspace::SourceRecord source;
        source.absolutePath=input; source.relativePathKey=input.filename().u8string();
        source.fileSizeBytes=std::filesystem::file_size(input);
        source.browsePreview=true;
        bool opening=false;
        unsigned tile=0,quick=0,master=0; int w=0,h=0;
        const auto frame=[&] {
            glfwPollEvents();
            tasks.PumpMainThreadTasks(8);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            editor->PumpNonRenderingWork(2.5);
            ImGui::SetNextWindowPos({0,0});
            ImGui::SetNextWindowSize({900,650});
            ImGui::Begin("HDR photo validation",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar);
            tile=editor->GetRawWorkspaceThumbnailTexture(source,&w,&h);
            editor->PumpRawWorkspaceThumbnailTextureUploads();
            if(opening) {
                if(editor->ConsumeOpenRawLabTabRequest())
                    editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::View);
                editor->RenderUI();
            }
            ImGui::End();
            ImGui::Render();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        };
        const auto wait=[&](auto predicate,const char* error) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
            do {
                frame();
                if(predicate()) return;
            } while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error(std::string(error)+" "+editor->GetActiveRawWorkspacePresentationDiagnosticForValidation()+
                " load="+editor->GetRawWorkspaceProjectLoadStatusText()+
                " pending transition="+std::to_string(editor->IsWorkspaceTransitionPending()));
        };
        wait([&]{return tile!=0;},"Gallery did not publish its quick RAW preview.");
        quick=tile;
        wait([&]{return tile && tile!=quick;},"Gallery did not refine the visible RAW to HDR.");
        const auto refined=tile;
        auto gallery=Read(refined);
        for(int i=0;i<12;++i) {
            frame();
            if(tile!=refined) throw std::runtime_error("A stationary Gallery tile regenerated.");
        }
        // AppShell creates a clean destination tab before dispatching the
        // Gallery open. Changing its authored workspace mode now would mark
        // the empty editor dirty and park that open behind the save dialog.
        if(!editor->EnterRawWorkspaceRootTab())
            throw std::runtime_error("Could not activate the RAW workspace for the Gallery open.");
        editor->OpenGalleryOriginalInNewTab(input.parent_path(),input);
        opening=true;
        wait([&] {
            if(editor->GalleryLoadFailed()) throw std::runtime_error("RAW failed to open in View.");
            unsigned base=0; int width=0,height=0;
            if(!editor->TryGetActiveRawWorkspacePresentationTexture(base,width,height)) return false;
            master=editor->DisplayOutputTexture(base,true);
            return master && master!=base;
        },"The View viewport did not publish an HDR companion.");
        const auto view=Read(master);
        const double error=Compare(gallery,view);
        std::cout<<"HDR Gallery quick and refined handles "<<quick<<", "<<refined
            <<", settled tile "<<gallery.w<<"x"<<gallery.h<<", View viewport "<<view.w<<"x"<<view.h
            <<", mean normalized patch error "<<error<<"\n"<<std::flush;
        if(error>.045) throw std::runtime_error("The Gallery and View HDR renditions disagree.");
    } catch(...) { failure=std::current_exception(); }
    editor->RequestWorkerShutdownForAppClose();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!editor->IsWorkerShutdownReadyForAppClose() && std::chrono::steady_clock::now()<deadline) {
        tasks.PumpMainThreadTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    editor->Shutdown();
    tasks.Shutdown();
    editor.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prior);
    if(failure) std::rethrow_exception(failure);
}
}
