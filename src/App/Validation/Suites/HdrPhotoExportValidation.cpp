#include "App/Validation/Suites/HdrPhotoExportValidation.h"
#include "Async/TaskSystem.h"
#include "App/Validation/Suites/HdrFileReopenValidation.h"
#include "Editor/EditorModule.h"
#include "Editor/UI/EditorViewport.h"
#include "Editor/Internal/HdrExportRender.h"
#include "Hdr/Color.h"
#include "Hdr/UiTarget.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Hdr/Presentation.h"
#include "Hdr/RasterSource.h"
#include "Raw/RawLoader.h"
#include "ThirdParty/stb_image.h"
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
namespace {
void Require(bool condition, const std::string& error) {
    if (!condition) throw std::runtime_error(error);
}
std::vector<unsigned char> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("HDR export did not publish " + path.u8string());
    return {(std::istreambuf_iterator<char>(stream)), {}};
}
class EditorCheck {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    const bool compareInSdr = Hdr::PreviewInSdr();
public:
    EditorModule editor;
    bool galleryOpen = false;
    explicit EditorCheck(GLFWwindow* window) {
        ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800,600};
        io.DeltaTime = 1.f/60;
        Require(ImGui_ImplOpenGL3_Init("#version 430"), "Cannot start HDR validation UI renderer.");
        Async::TaskSystem::Get().Initialize();
        editor.Initialize(window, nullptr, false);
        editor.SetDocumentPersistenceEnabled(false);
        editor.SetWorkspaceAppStatePersistenceEnabled(false);
    }
    ~EditorCheck() {
        Hdr::SetPreviewInSdr(compareInSdr);
        editor.RequestWorkerShutdownForAppClose();
        const auto until = std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while (!editor.IsWorkerShutdownReadyForAppClose() && std::chrono::steady_clock::now()<until) {
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        editor.Shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }
    template<class Ready>
    void Wait(Ready ready, const char* operation) {
        const auto begin = std::chrono::steady_clock::now();
        do {
            glfwPollEvents();
            Async::TaskSystem::Get().PumpMainThreadTasks(4);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            editor.PumpNonRenderingWork(2.0, false);
            if (galleryOpen) {
                if (editor.ConsumeOpenRawLabTabRequest())
                    editor.SetWorkspaceMode(Project::WorkspaceMode::View);
                ImGui::SetNextWindowSize({800,600});
                ImGui::Begin("Graph export validation");
                editor.RenderUI();
                ImGui::End();
            }
            ImGui::EndFrame();
            if (ready()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        } while (std::chrono::steady_clock::now()-begin < std::chrono::minutes(5));
        throw std::runtime_error(std::string(operation)+" timed out. "+editor.GetExportStatusText()+
            " "+editor.GetFullQualityRenderDiagnostic());
    }
};

void CheckHdrUiBlend() {
    const Renderer::GLState::FramebufferState host(true);
    Renderer::ScopedGLTexture texture(GLHelpers::CreateEmptyTexture(32,32));
    unsigned target=texture ? GLHelpers::CreateFBO(texture.Get()) : 0;
    Require(target!=0,"Could not allocate the HDR UI blend target.");
    auto& io=ImGui::GetIO();
    const auto oldSize=io.DisplaySize;
    io.DisplaySize={32,32};
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    glViewport(0,0,32,32);glDisable(GL_SCISSOR_TEST);
    glClearColor(4.f,4.f,4.f,1.f);glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame();
    ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},{32,32},IM_COL32(128,128,128,128));
    ImGui::Render();
    {
        Hdr::Ui::TargetScope linear(true);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    float pixel[4]={};
    glReadPixels(16,16,1,1,GL_RGBA,GL_FLOAT,pixel);
    io.DisplaySize=oldSize;
    host.Restore(true);
    glDeleteFramebuffers(1,&target);
    const float alpha=128.f/255.f;
    const float expected=4.f*(1-alpha)+alpha*Hdr::Linear(128.f/255.f);
    Require(std::abs(pixel[0]-expected)<.003f && std::abs(pixel[1]-expected)<.003f &&
        std::abs(pixel[2]-expected)<.003f && pixel[3]==1.f,"HDR overlays must blend in linear light.");
    std::cout<<"HDR half-alpha gray over a 4x-white highlight: "<<pixel[0]<<" (expected "<<expected<<")\n";
}
struct Sample { std::size_t offset; Hdr::Rgb linear; std::array<unsigned char,3> sdr; };
struct Reference { int width, height; std::vector<Sample> samples; };
Reference SampleReference(const RenderGraphSnapshot& graph, int width, int height) {
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.LoadSourceFromPixels(nullptr,width,height,4);
    EditorRenderWorker::Result result;
    Require(EditorRendering::ReadHdrExport(pipeline, graph, Hdr::FileFormat::GainMapJpeg, result),
        "Could not render the HDR reference. "+result.error);
    Reference ref{result.width,result.height,{}};
    for(int y=0;y<ref.height;y+=std::max(1,ref.height/67))
        for(int x=0;x<ref.width;x+=std::max(1,ref.width/73)) {
            const auto i=4*(std::size_t(y)*ref.width+x);
            Hdr::Rgb c{result.hdrExportImage.rgba[i],result.hdrExportImage.rgba[i+1],result.hdrExportImage.rgba[i+2]};
            c=Hdr::To2020(c);
            for(float& v:c) v=std::clamp(v,0.f,10000.f/Hdr::ReferenceWhite);
            ref.samples.push_back({i,Hdr::To709(c),{result.pixels[i],result.pixels[i+1],result.pixels[i+2]}});
        }
    return ref;
}
void CheckFile(const std::filesystem::path& path, Hdr::FileFormat format, const Reference& reference) {
    const auto bytes=ReadFile(path);
    Hdr::Image image;
    Hdr::FileFormat detected{};
    Require(Hdr::DecodeDisplaySource(bytes,image,&detected) && detected==format, "HDR export did not reopen in its declared format.");
    CheckHdrFileReopen(bytes,image);
    Require(image.width==reference.width && image.height==reference.height,"HDR export lost native dimensions.");
    double worst=0,mean=0,peak=0,referencePeak=0;
    for(const auto& sample:reference.samples) for(int k=0;k<3;++k) {
        const float value=image.rgba[sample.offset+k];
        const double delta=std::abs(value-sample.linear[k]);
        Require(std::isfinite(delta),"Non-finite reopened HDR sample.");
        worst=std::max(worst,delta); mean+=delta;
        peak=std::max(peak,double(value));
        referencePeak=std::max(referencePeak,double(sample.linear[k]));
    }
    mean/=3.0*reference.samples.size();
    // An unedited dark capture need not contain above-white pixels. Require
    // them when the actual rendered reference has them; never invent exposure.
    Require(referencePeak<=1.05 || peak>1.0,"The export lost the reference's above-white highlights.");
    std::cout<<path.u8string()<<": "<<image.width<<"x"<<image.height<<", linear RGB mean/worst error "
        <<mean<<"/"<<worst<<", sampled channel peak "<<peak<<" (reference "<<referencePeak<<") x diffuse white\n"<<std::flush;
    if(format==Hdr::FileFormat::PqPng) {
        Require(worst<.005,"Lossless HDR PNG differs from quantized native HDR render.");
        return;
    }
    Require(mean<.09,"Gain-map JPEG differs substantially from the native HDR render.");
    int w=0,h=0,channels=0;
    stbi_set_flip_vertically_on_load_thread(0);
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> sdr(
        stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels,4),stbi_image_free);
    Require(sdr && w==image.width && h==image.height,"Legacy JPEG decoder could not read the SDR base.");
    double baseError=0;
    for(const auto& sample:reference.samples) for(int k=0;k<3;++k)
        baseError+=std::abs(int(sdr.get()[sample.offset+k])-int(sample.sdr[k]));
    baseError/=3.0*reference.samples.size();
    std::cout<<"SDR JPEG base mean error "<<baseError<<" code values\n";
    Require(baseError<8.0,"Gain-map JPEG lost its authored SDR base.");
}
}
void ValidatePhotoExports(GLFWwindow* window,const std::filesystem::path& raw,const std::filesystem::path& folder,
    bool graphOutputOnly) {
    Raw::RawMetadata meta;
    Require(Raw::RawLoader::LoadMetadata(raw.u8string(),meta),"Cannot read the supplied camera RAW.");
    std::filesystem::create_directories(folder);
    EditorCheck check(window);
    if (!graphOutputOnly) CheckHdrUiBlend();
    auto& editor=check.editor;
    int graphOutput = 0;
    if (graphOutputOnly) {
        const auto input=std::filesystem::absolute(raw);
        Require(editor.EnterRawWorkspaceRootTab(),"Could not activate a clean RAW destination tab.");
        editor.OpenGalleryOriginalInNewTab(input.parent_path(),input);
        check.galleryOpen=true;
        check.Wait([&] {
            Require(!editor.GalleryLoadFailed(),"RAW Gallery open failed.");
            unsigned texture=0; int width=0,height=0;
            return !editor.IsRawWorkspaceProjectLoadBusy() &&
                editor.TryGetActiveRawWorkspacePresentationTexture(texture,width,height);
        },"RAW project open");
        editor.OpenRawLayerMaskGraph(Project::kRawBackgroundId,false);
        editor.SetWorkspaceMode(Project::WorkspaceMode::Graph);
        Require(editor.IsRawProjectCanvas(),"The RAW project Graph did not open.");
        for (const auto& node:editor.GetNodeGraph().GetNodes()) {
            if (!editor.CanExportGraphOutput(node.id)) continue;
            Require(!graphOutput,"More than one RAW canvas card can export the project.");
            graphOutput=node.id;
        }
        Require(graphOutput>0,"The final RAW Output is still disabled for export.");
        std::cout<<"RAW Graph final Output "<<graphOutput<<" is exportable.\n"<<std::flush;
    } else {
        editor.RequestLoadSourceImage(std::filesystem::absolute(raw).u8string());
        check.Wait([&]{return !editor.IsSourceLoadBusy();},"RAW source load");
        Require(editor.GetSourceLoadTaskState()!=Async::TaskState::Failed,editor.GetSourceLoadStatusText());
        Require(editor.GetNodeGraph().IsOutputConnected(),"RAW import did not connect an output.");
    }
    if (!graphOutputOnly && Hdr::PreviewActive()) {
        // Submit the real UI surface and inspect its normal ImGui draw stream.
        // Both windows use the same immutable source and accepted render generation.
        EditorViewport viewport;
        viewport.Initialize();
        const auto drawsPhoto = [&](bool viewMode,bool hdr) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({780,560});
            ImGui::Begin("HDR photo");
            if(viewMode) editor.RenderViewWorkspace();
            else viewport.Render(&editor);
            ImGui::End();
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            unsigned base=editor.GetPipeline().GetOutputTexture();
            int w=0,h=0;
            unsigned rawTexture=0;
            if(editor.TryGetActiveRawWorkspacePresentationTexture(rawTexture,w,h)) base=rawTexture;
            const auto companion=editor.DisplayOutputTexture(base,true);
            if(!base || base==companion) return false;
            for(const auto* list:ImGui::GetDrawData()->CmdLists)
                for(const auto& cmd:list->CmdBuffer)
                    if(cmd.ElemCount && static_cast<unsigned>(cmd.GetTexID())==(hdr ? companion : base)) return true;
            return false;
        };
        for(bool viewMode:{false,true}) {
            if(viewMode) editor.SetWorkspaceMode(Project::WorkspaceMode::View);
            for(bool hdr:{true,false}) {
                editor.SetSdrOutputProof(!hdr);
                // Wait ends its own maintenance frame before checking another real UI draw.
                check.Wait([&]{return drawsPhoto(viewMode,hdr);},viewMode ? "View HDR/SDR photo draw" : "Viewport HDR/SDR photo draw");
            }
        }
        Hdr::SetPreviewInSdr(false);
        editor.SetSdrOutputProof(false);
        std::cout<<"Viewport and View drew their accepted HDR texture and SDR counterpart.\n"<<std::flush;
    }
    const auto graph=editor.BuildGraphSnapshot();
    std::vector<std::filesystem::path> paths;
    for(auto format:{Hdr::FileFormat::PqPng,Hdr::FileFormat::GainMapJpeg}) {
        Hdr::SetPreviewInSdr(format==Hdr::FileFormat::PqPng);
        const auto name=raw.stem().u8string()+"-hdr-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+Hdr::Extension(format);
        const auto file=folder/std::filesystem::u8path(name);
        const bool started=graphOutputOnly
            ? editor.RequestExportGraphOutput(graphOutput,file.u8string(),format)
            : editor.RequestQueueExportImage(file.u8string(),format);
        Require(started,"Could not start HDR export. "+editor.GetExportStatusText());
        check.Wait([&]{return !editor.IsExportBusy();},"HDR export");
        Require(editor.GetExportTaskState()!=Async::TaskState::Failed,editor.GetExportStatusText());
        paths.push_back(file);
    }
    const auto reference=SampleReference(graph,Raw::DisplayWidth(meta),Raw::DisplayHeight(meta));
    CheckFile(paths[0],Hdr::FileFormat::PqPng,reference);
    CheckFile(paths[1],Hdr::FileFormat::GainMapJpeg,reference);
    if (graphOutputOnly) {
        const auto file=folder/std::filesystem::u8path(raw.stem().u8string()+"-sdr-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".png");
        Require(editor.RequestExportGraphOutput(graphOutput,file.u8string(),Hdr::FileFormat::SdrPng),
            "Could not start the default SDR graph export. "+editor.GetExportStatusText());
        check.Wait([&]{return !editor.IsExportBusy();},"SDR graph export");
        Require(editor.GetExportTaskState()!=Async::TaskState::Failed,editor.GetExportStatusText());
        const auto bytes=ReadFile(file);
        Require(bytes.size()>25 && bytes[24]==8 && !Hdr::IsPqPng(bytes),
            "The default SDR PNG changed format.");
        int w=0,h=0,channels=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> sdr(
            stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels,4),stbi_image_free);
        Require(sdr && w==reference.width && h==reference.height,"SDR PNG lost native dimensions.");
        double error=0;
        for(const auto& sample:reference.samples) for(int k=0;k<3;++k)
            error+=std::abs(int(sdr.get()[sample.offset+k])-int(sample.sdr[k]));
        error/=3.0*reference.samples.size();
        Require(error<.6,"Default SDR PNG differs from the authored SDR finish.");
        std::cout<<file.u8string()<<": 8-bit SDR PNG, mean SDR error "<<error<<" code values.\n"<<std::flush;
    }
}
}
