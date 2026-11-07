#include "Presentation.h"
#include "Hdr/UiTarget.h"
#include "Hdr/PhotoPairs.h"
#include "Hdr/Color.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <atomic>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "WindowPresenter.h"
#include <dwmapi.h>
#include <unordered_map>
#include <memory>
#endif
namespace Stack::Hdr {
namespace {
bool forced=false;
std::atomic<int> activeCount{0};
std::string mainStatus="SDR";
GLFWwindow* mainWindow=nullptr;
#ifdef _WIN32
std::unordered_map<GLFWwindow*,std::unique_ptr<WindowPresenter>> windows;
using ViewCallback=void(*)(ImGuiViewport*,void*);
ViewCallback renderWindow=nullptr,swapWindow=nullptr;
void (*destroyWindow)(ImGuiViewport*)=nullptr;
void RenderDetached(ImGuiViewport* vp,void* arg) {
    auto* window=static_cast<GLFWwindow*>(vp->PlatformHandle);
    int w=0,h=0; glfwGetFramebufferSize(window,&w,&h);
    auto target=BeginWindow(window,w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    {Ui::TargetScope drawTarget(target!=0); renderWindow(vp,arg);}
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
void SwapDetached(ImGuiViewport* vp,void* arg) {
    auto* window=static_cast<GLFWwindow*>(vp->PlatformHandle);
    glfwMakeContextCurrent(window);
    if(!PresentWindow(window)) swapWindow(vp,arg);
}
void DestroyDetached(ImGuiViewport* vp) {
    auto* window=static_cast<GLFWwindow*>(vp->PlatformHandle);
    const auto found=windows.find(window);
    if(found!=windows.end()) {
        auto* previous=glfwGetCurrentContext(); glfwMakeContextCurrent(window);
        if(found->second->Active()) --activeCount;
        windows.erase(found); glfwMakeContextCurrent(previous==window ? mainWindow : previous);
    }
    if(destroyWindow) destroyWindow(vp);
}
#endif
}
void InstallPlatformHooks() {
#ifdef _WIN32
    auto& io=ImGui::GetPlatformIO();
    if(io.Renderer_RenderWindow && io.Platform_SwapBuffers && !renderWindow) {
        renderWindow=io.Renderer_RenderWindow; io.Renderer_RenderWindow=RenderDetached;
        swapWindow=io.Platform_SwapBuffers; io.Platform_SwapBuffers=SwapDetached;
        destroyWindow=io.Renderer_DestroyWindow;io.Renderer_DestroyWindow=DestroyDetached;
    }
#endif
}
void ShutdownPresentation(GLFWwindow* main) {
#ifdef _WIN32
    for(auto& pair:windows) { glfwMakeContextCurrent(pair.first); pair.second.reset(); }
    windows.clear(); activeCount.store(0); glfwMakeContextCurrent(main);
    if(renderWindow && ImGui::GetCurrentContext()) {
        auto& io=ImGui::GetPlatformIO();
        if(io.Renderer_RenderWindow==RenderDetached) io.Renderer_RenderWindow=renderWindow;
        if(io.Platform_SwapBuffers==SwapDetached) io.Platform_SwapBuffers=swapWindow;
        if(io.Renderer_DestroyWindow==DestroyDetached) io.Renderer_DestroyWindow=destroyWindow;
    }
    renderWindow=nullptr;swapWindow=nullptr;destroyWindow=nullptr;
#endif
    mainWindow=nullptr;
}
unsigned BeginWindow(GLFWwindow* window,int w,int h,bool main,bool opaqueWindow) {
#ifdef _WIN32
    if(main) mainWindow=window;
    if(!opaqueWindow && windows.find(window)==windows.end()) return 0;
    auto& presenter=windows[window];
    if(!presenter) presenter=std::make_unique<WindowPresenter>(glfwGetWin32Window(window));
    const bool previous=presenter->Active();
    auto target=presenter->Begin(w,h,forced || !opaqueWindow);
    if(previous!=presenter->Active()) activeCount.fetch_add(target ? 1 : -1);
    if(main && mainStatus!=presenter->Status()) mainStatus=presenter->Status();
    return target;
#else
    return 0;
#endif
}
bool PresentWindow(GLFWwindow* window) {
#ifdef _WIN32
    const auto found=windows.find(window);
    if(found==windows.end() || !found->second->Active()) return false;
    const auto previous=found->second->SubmittedFrames();
    const bool presented=found->second->Present();
    // The compatibility path may reuse a frame. Keep the main loop paced
    // on those frames just as the ordinary vsynced swap would be.
    if(presented && window==mainWindow && previous==found->second->SubmittedFrames()) DwmFlush();
    if(!found->second->Active()) --activeCount;
    return presented;
#else
    return false;
#endif
}
bool IsActive(GLFWwindow* window) {
#ifdef _WIN32
    const auto it=windows.find(window);return !forced && it!=windows.end() && it->second->Active();
#else
    return false;
#endif
}
bool AnyPreviewActive() {return activeCount.load(std::memory_order_relaxed)>0;}
bool PreviewActive() {return IsActive(mainWindow);}
bool PreviewActiveForCurrentWindow() {
    auto* context=ImGui::GetCurrentContext();
    auto* vp=context && context->CurrentWindow ? ImGui::GetWindowViewport() : nullptr;
    return IsActive(vp && vp->PlatformHandle ? static_cast<GLFWwindow*>(vp->PlatformHandle) : mainWindow);
}
bool PreviewInSdr() {return forced;}
float CurrentTargetHeadroom() {
#ifdef _WIN32
    const auto it=windows.find(glfwGetCurrentContext());
    if(it!=windows.end() && it->second->Active()) {
        const auto& display=it->second->Display();
        return (std::max)(1.f,display.peak/(std::max)(1.f,display.white));
    }
#endif
    return MasterHeadroom;
}
void SetPreviewInSdr(bool value) {forced=value;}
const std::string& MainDisplayStatus(){return mainStatus;}
}
