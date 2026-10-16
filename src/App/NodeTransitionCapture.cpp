#include "App/NodeTransitionCapture.h"
#include "Async/TaskSystem.h"
#include "Editor/EditorModule.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ViewportTextureCopy.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Stack::UiDiagnostics {
namespace {
struct CaptureState {
    std::filesystem::path directory;
    std::string document, node;
    int sequence=0, phase=3;
    double started=0;
    CaptureState() {
        if(const auto* value=std::getenv("STACK_NODE_TRANSITION_CAPTURE_DIR"))
            if(*value) directory=std::filesystem::u8path(value);
    }
};
CaptureState& State() { static CaptureState state; return state; }
void WritePng(const std::filesystem::path& path, int width, int height, std::vector<unsigned char>& pixels) {
    // Inspect RGB even when a source texture's stored alpha is undefined.
    for(std::size_t i=3;i<pixels.size();i+=4) pixels[i]=255;
    const auto stride=std::size_t(width)*4;
    for(int y=0;y<height/2;++y)
        for(std::size_t x=0;x<stride;++x) std::swap(pixels[y*stride+x],pixels[(height-1-y)*stride+x]);
    std::ofstream file(path,std::ios::binary);
    if(!file) return;
    stbi_write_png_to_func([](void* context,void* data,int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data),size);
    },&file,width,height,4,pixels.data(),width*4);
}
void QueuePng(std::filesystem::path path, int width, int height, std::vector<unsigned char> pixels) {
    Async::TaskSystem::Get().Submit([path=std::move(path),width,height,pixels=std::move(pixels)]() mutable {
        WritePng(path,width,height,pixels);
    });
}
}
void ArmNodeOpenCapture(const std::string& document, const std::string& node) {
    auto& state=State();
    if(state.directory.empty() || state.sequence>=24) return;
    state.document=document; state.node=node; state.started=ImGui::GetTime();
    ++state.sequence; state.phase=0;
}
void CaptureNodeOpenIfDue(EditorModule& editor, const ImDrawData& data) {
    auto& state=State();
    if(state.directory.empty() || state.phase>=3) return;
    if(state.document!=editor.GetProjectDocumentId()) { state.phase=3; return; }
    constexpr double delays[]{0,.85,1.8};
    if(ImGui::GetTime()-state.started<delays[state.phase]) return;
    const auto phase=state.phase++;
    try {
        std::filesystem::create_directories(state.directory);
        const auto prefix="node-"+std::to_string(state.sequence)+"-"+std::to_string(phase);
        std::ofstream report(state.directory/(prefix+".txt"));
        report<<"document="<<state.document<<" node="<<state.node<<" elapsed="<<ImGui::GetTime()-state.started
            <<" view="<<editor.IsViewMode()<<" transition="<<editor.IsNodeOpenTransitionActive()<<'\n'
            <<editor.GetActiveRawWorkspacePresentationDiagnosticForValidation()<<'\n';
        struct RestoreState {
            Stack::Renderer::GLState::FramebufferState frame;
            Stack::Renderer::GLState::PixelPackState pack;
            Stack::Renderer::GLState::TextureBinding texture{GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D};
            ~RestoreState() { texture.Restore(); pack.Restore(); frame.Restore(); }
        } saved;
        saved.pack.ConfigureTightCpuReadback();
        const int width=int(data.DisplaySize.x*data.FramebufferScale.x);
        const int height=int(data.DisplaySize.y*data.FramebufferScale.y);
        if(width>0 && height>0) {
            std::vector<unsigned char> screen(std::size_t(width)*height*4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glReadBuffer(GL_BACK);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,screen.data());
            QueuePng(state.directory/(prefix+"-screen.png"),width,height,std::move(screen));
        }
        const auto* photo=editor.GetRawImageBackdrop();
        if(photo) {
            report<<"photo="<<photo->imageMinimum.x<<','<<photo->imageMinimum.y<<' '<<photo->imageMaximum.x<<','<<photo->imageMaximum.y
                <<" opacity="<<photo->extensionOpacity<<" fingerprint="<<photo->presentationFingerprint<<'\n';
            const Stack::Renderer::GLState::TextureBinding textureBinding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
            int index=0;
            for(const auto& patch:photo->patches) {
                glBindTexture(GL_TEXTURE_2D,patch.texture);
                GLint w=0,h=0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
                report<<"patch="<<index<<" texture="<<patch.texture<<" extent="<<w<<'x'<<h
                    <<" rect="<<patch.minimum.x<<','<<patch.minimum.y<<' '<<patch.maximum.x<<','<<patch.maximum.y
                    <<" uv="<<patch.uvMinimum.x<<','<<patch.uvMinimum.y<<' '<<patch.uvMaximum.x<<','<<patch.uvMaximum.y
                    <<" previous="<<patch.previousTexture<<" fade="<<patch.fadeAmount<<'\n';
                const float scale=std::min(1.f,256.f/std::max(1,std::max(w,h)));
                const int sw=std::max(1,int(w*scale)),sh=std::max(1,int(h*scale));
                const auto sample=Stack::Renderer::CopyViewportTexture(patch.texture,w,h,sw,sh);
                if(sample) {
                    std::vector<unsigned char> pixels(std::size_t(sw)*sh*4);
                    glBindTexture(GL_TEXTURE_2D,sample);
                    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    QueuePng(state.directory/(prefix+"-texture-"+std::to_string(index)+".png"),sw,sh,std::move(pixels));
                    glDeleteTextures(1,&sample);
                }
                ++index;
            }
            textureBinding.Restore();
        }
    } catch(const std::exception&) {
        // A diagnostic must not stop the user's editing session.
        state.phase=3;
    }
}
}
