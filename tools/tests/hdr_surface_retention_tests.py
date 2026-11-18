#!/usr/bin/env python3
"""Check the production HDR Begin method with platform/resource stand-ins.

This exercises retained capability, extent, pending display notification and
per-window resource ownership. The shell's suspended-window pacing body is also
extracted to check detached-window and shutdown admission. It does not execute
a Windows swap chain, GL or an actual OS event wait.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'src/Hdr/WindowPresenter.cpp').read_text()
header = (root / 'src/Hdr/WindowPresenter.h').read_text()
shell = (root / 'src/App/AppShell.cpp').read_text()


def method(text, signature):
    start = text.index(signature)
    pos = text.index('{', start) + 1
    depth = 1
    while depth:
        if text[pos] == '{': depth += 1
        elif text[pos] == '}': depth -= 1
        pos += 1
    return text[start:pos] + '\n'


pre = r'''
#include "Hdr/WindowSurfaceExtent.h"
#include "Async/UiInteraction.h"
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
struct GLFWwindow {
    bool iconified=false,visible=true;
    int width=2560,height=1440;
};
constexpr int GLFW_TRUE=1,GLFW_ICONIFIED=2,GLFW_VISIBLE=3;
int glfwGetWindowAttrib(GLFWwindow* window,int attribute) {
    return attribute==GLFW_ICONIFIED ? window->iconified : window->visible;
}
void glfwGetFramebufferSize(GLFWwindow* window,int* width,int* height) {
    *width=window->width;*height=window->height;
}
int eventWaits=0;
double eventWaitSeconds=0;
void glfwWaitEventsTimeout(double seconds) {++eventWaits;eventWaitSeconds=seconds;}
namespace ImGui {
struct Viewport {void* PlatformHandle=nullptr;};
struct PlatformIO {std::vector<Viewport*> Viewports;};
PlatformIO platform;
PlatformIO& GetPlatformIO() {return platform;}
}
using HRESULT=long;
constexpr int DXGI_FORMAT_UNKNOWN=0;
#define FAILED(value) ((value)<0)
#define IID_PPV_ARGS(value) value
unsigned long long GetTickCount64() { return 100; }
void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
namespace Stack::Hdr {
struct Factory { bool IsCurrent() const {return true;} };
struct Swap {
    int resizes=0;
    HRESULT ResizeBuffers(int,int,int,int,int) {++resizes;return 0;}
};
template<class T> struct Ref {
    std::shared_ptr<T> value;
    explicit operator bool() const {return bool(value);}
    T* Get() const {return value.get();}
    T* operator->() const {return value.get();}
    void Reset() {value.reset();}
};
HRESULT CreateDXGIFactory1(Ref<Factory>* target) {
    target->value=std::make_shared<Factory>();return 0;
}
struct DisplayInfo {
    int monitor=1,adapter=1;
    bool hdr=true;
    std::string reason="SDR";
};
DisplayInfo observedDisplay;
int displayReads=0;
DisplayInfo ReadDisplay(int,Factory*) {++displayReads;return observedDisplay;}
class WindowPresenter {
public:
    unsigned Begin(int w,int h,bool forceSdr);
'''
accessors = method(header, 'bool Active() const') + method(header, 'bool Suspended() const')
stub = r'''
    int hwnd=1,width=0,height=0;
    DisplayInfo display;
    Ref<Factory> factory;
    Ref<Swap> swap;
    std::shared_ptr<std::atomic<bool>> dirty=std::make_shared<std::atomic<bool>>(true);
    bool advancedEvents=true,failed=false,force=false,suspended=false;
    unsigned target=0,nextTarget=100;
    unsigned long long retryDeviceAt=0,nextRefresh=0;
    int resets=0,releases=0,creates=0;
    std::string status;
    void Reset() {++resets;target=0;swap.Reset();}
    void ReleaseBuffers() {++releases;target=0;}
    void BindBuffers() {target=++nextTarget;}
    void Create() {++creates;swap.value=std::make_shared<Swap>();BindBuffers();}
    void Fail(const char* text,HRESULT) {Reset();failed=true;status=text;}
};
'''
pace = r'''
} // namespace Stack::Hdr
void PaceWindows(GLFWwindow* m_Window,bool m_CloseRequested,Async::UiInteraction::Lease& m_UiInteraction) {
''' + method(shell, 'if (!m_CloseRequested && glfwGetWindowAttrib(m_Window,GLFW_ICONIFIED)==GLFW_TRUE)') + r'''
}
void CheckWindowPacing() {
    GLFWwindow main{true,true,2560,1440},detached{false,true,800,600};
    ImGui::Viewport mainViewport{&main},otherViewport{&detached};
    ImGui::platform.Viewports={&mainViewport};
    Async::UiInteraction::Lease interaction;
    interaction.Update(true,true,0);
    PaceWindows(&main,false,interaction);
    Require(eventWaits==1 && eventWaitSeconds==.05 && !Async::UiInteraction::Active(),
        "A fully minimized app must yield and clear its interaction hint");
    ImGui::platform.Viewports.push_back(&otherViewport);
    interaction.Update(true,true,0);
    PaceWindows(&main,false,interaction);
    Require(eventWaits==1 && Async::UiInteraction::Active(),
        "A drawable detached window must keep normal interactive pacing");
    detached.iconified=true;
    PaceWindows(&main,false,interaction);
    Require(eventWaits==2,"Two minimized windows must not spin without a swap");
    detached.iconified=false;detached.visible=false;
    PaceWindows(&main,false,interaction);
    Require(eventWaits==3,"A hidden detached window must not prevent the event wait");
    detached.visible=true;detached.width=0;
    PaceWindows(&main,false,interaction);
    Require(eventWaits==4,"A detached window without a drawable must not prevent the event wait");
    PaceWindows(&main,true,interaction);
    Require(eventWaits==4,"Closing must retain the existing shutdown loop without a new wait");
    main.iconified=false;
    PaceWindows(&main,false,interaction);
    Require(eventWaits==4,"Restored main windows must immediately return to normal pacing");
    ImGui::platform.Viewports.clear();
}
'''
checks = r'''
int main() {
    using namespace Stack::Hdr;
    try {
        WindowPresenter first;
        const auto target=first.Begin(2560,1440,false);
        Require(target && first.Active() && !first.Suspended(),"A drawable must establish HDR capability");
        const int resets=first.resets,reads=displayReads;
        first.dirty->store(true);
        Require(!first.Begin(0,0,false) && first.Active() && first.Suspended(),
            "Minimizing must retain active HDR capability so image caches stay valid");
        Require(first.target==target && first.resets==resets && first.width==2560 && first.height==1440,
            "Minimizing must retain the allocated frame and dimensions");
        Require(displayReads==reads && first.dirty->load(),
            "Suspension must retain pending display changes for restoration");
        Require(!first.Begin(0,1440,false) && !first.Begin(2560,0,false) && first.Active(),
            "Partially absent drawable extents must retain capability too");
        WindowPresenter other;
        const auto otherTarget=other.Begin(800,600,false);
        Require(otherTarget && other.Active() && first.Active() && first.target==target,
            "Another window must not replace the suspended owner's image");
        Require(first.Begin(2560,1440,false)==target && !first.Suspended() && first.resets==resets &&
                first.swap->resizes==0 && !first.dirty->load(),
            "Restore must reuse the same surface and apply pending display information");
        Require(first.Begin(1920,1080,false)!=target && first.Active() && first.swap->resizes==1 &&
                first.releases==1 && other.target==otherTarget,
            "A real resize must replace only that window's buffers");
        first.Begin(0,0,false);
        observedDisplay.hdr=false;first.dirty->store(true);
        Require(!first.Begin(0,0,false) && first.Active(),
            "Display changes remain pending while there is no drawable");
        Require(!first.Begin(1920,1080,false) && !first.Active() && !first.dirty->load(),
            "A real HDR disable must take effect when the window is restored");
        Require(other.Active() && other.target==otherTarget,
            "Refreshing one window cannot discard another retained surface");
        Require(!other.Begin(800,600,true) && !other.Active(),
            "Explicit SDR mode must still release the live HDR surface");
        WindowPresenter initial;
        const int initialReads=displayReads;
        Require(initial.Begin(0,0,false)==0 && initial.Suspended() && !initial.Active() && displayReads==initialReads,
            "An initially absent drawable must not allocate or consume display notification");
        CheckWindowPacing();
        std::cout<<"Production HDR retention and suspended-window pacing passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
'''
args.output.write_text(pre + accessors + stub + method(source, 'unsigned WindowPresenter::Begin(') + pace + checks)
