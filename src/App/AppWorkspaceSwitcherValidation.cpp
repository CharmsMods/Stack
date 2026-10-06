#include "AppShell.h"
#include "Renderer/GLLoader.h"
#include "Renderer/GLStateGuards.h"
#include "ThirdParty/stb_image_write.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

void AppShell::ConfigureDiagnosticWorkspaceSwitcher(std::filesystem::path output) {
    m_WorkspaceDiagnosticOutput=std::move(output);
}
bool AppShell::TickDiagnosticWorkspaceSwitcher() {
    if(m_WorkspaceDiagnosticOutput.empty()) return false;
    if(!m_StartupRevealVisual.AllowsInput()) return true;
    if(m_LegalManager && !m_LegalManager->IsAccepted()) {
        std::cerr << "Workspace validation requires an accepted legal gate.\n";
        m_IsRunning=false; return true;
    }
    auto& s=m_WorkspaceSwitcher;
    const int frame=m_WorkspaceDiagnosticFrame++;
    const int step=frame/48, phase=frame%48;
    constexpr int count = static_cast<int>(Stack::Workspace::Destinations.size());
    if(step>=count*2) {
        m_Editor->EndWorkspacePreview();
        m_WorkspaceDiagnosticPassed=true; m_IsRunning=false;
        std::cout << "Workspace live preview/cancel/commit validation passed.\n";
        return true;
    }
    const auto& destination=Stack::Workspace::Destinations[step%count];
    if(phase==0) {
        if(!m_Editor->FinishWorkspaceInteraction()) throw std::runtime_error("Cannot finish workspace interaction");
        m_Editor->BeginWorkspacePreview();
        m_WorkspaceDiagnosticOrigin=m_CurrentTabId;
        m_WorkspaceDiagnosticDirty=m_Editor->IsDirty();
        m_WorkspaceDiagnosticSource=m_Editor->GetRawWorkspaceState().selectedSourceKey;
        s.Begin(m_CurrentTabId); s.Move(destination.x*75*s.scale/s.config.pointerSensitivity,destination.y*75*s.scale/s.config.pointerSensitivity);
    }
    if(phase<22) {
        if(m_CurrentTabId!=m_WorkspaceDiagnosticOrigin || m_Editor->IsDirty()!=m_WorkspaceDiagnosticDirty ||
           m_Editor->GetRawWorkspaceState().selectedSourceKey!=m_WorkspaceDiagnosticSource)
            throw std::runtime_error("Workspace preview mutated active project state");
    }
    if(phase==22) {
        EndWorkspaceSwitcher(step>=count,false);
        if(step<count && m_CurrentTabId!=m_WorkspaceDiagnosticOrigin)
            throw std::runtime_error("Workspace cancellation committed a destination");
        if(step>=count && m_CurrentTabId!=static_cast<int>(destination.id))
            throw std::runtime_error("Workspace commit did not activate destination");
    }
    s.Tick(1.f/60);
    if(s.Visible()) m_Editor->BeginWorkspacePreview(); else m_Editor->EndWorkspacePreview();
    return true;
}
void AppShell::CaptureDiagnosticWorkspaceSwitcher() {
    if(m_WorkspaceDiagnosticOutput.empty() || m_WorkspaceDiagnosticFrame%48!=20) return;
    const auto* data=ImGui::GetDrawData();
    const int width=int(data->DisplaySize.x*data->FramebufferScale.x);
    const int height=int(data->DisplaySize.y*data->FramebufferScale.y);
    if(width<=0 || height<=0) return;
    std::filesystem::create_directories(m_WorkspaceDiagnosticOutput);
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
    Stack::Renderer::GLState::PixelPackState pack;
    pack.ConfigureTightCpuReadback();
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    pack.Restore();
    for(int y=0;y<height/2;++y)
        for(int x=0;x<width*4;++x) std::swap(pixels[size_t(y)*width*4+x],pixels[size_t(height-1-y)*width*4+x]);
    const auto path=m_WorkspaceDiagnosticOutput/("workspace-"+std::to_string(m_WorkspaceDiagnosticFrame/48)+".png");
    if(!stbi_write_png(path.string().c_str(),width,height,4,pixels.data(),width*4))
        throw std::runtime_error("Could not write workspace validation image");
}
