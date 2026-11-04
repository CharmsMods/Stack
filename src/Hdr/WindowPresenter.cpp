#ifdef _WIN32
#include "WindowPresenter.h"
#include "ScRgbShader.h"
#include "Renderer/GLHelpers.h"
#include <chrono>
#include <cmath>

namespace Stack::Hdr {
namespace {
constexpr char Vertex[]=R"(#version 430 core
void main() {
    vec2 v=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    gl_Position=vec4(v*2.-1.,0.,1.);
})";
const std::string Convert=std::string("#version 430 core\n")+ScRgbConversion+R"(
layout(binding=0) uniform sampler2D frame;
layout(location=0) uniform float whiteScale;
layout(location=1) uniform float headroom;
layout(location=0) out vec4 color;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);
    p.y=textureSize(frame,0).y-1-p.y;
    vec4 s=texelFetch(frame,p,0);
    color=toWindowsColor(s,whiteScale,headroom);
})";
}
WindowPresenter::WindowPresenter(HWND window,bool compatible):hwnd(window),watch(WatchDisplay(window,dirty,advancedEvents)),compatibleOnly(compatible) {}
WindowPresenter::~WindowPresenter(){ Reset(); }
void WindowPresenter::Fail(const char* reason,HRESULT error) {
    // Removal does not always generate a new monitor event. Re-enumerate
    // adapters and retry while the desktop driver comes back.
    if (retryDeviceAt || error==DXGI_ERROR_DEVICE_REMOVED || error==DXGI_ERROR_DEVICE_RESET ||
        (device && FAILED(device->GetDeviceRemovedReason())))
        retryDeviceAt=GetTickCount64()+2000;
    Reset();
    failed=true;
    status=reason;
}
void WindowPresenter::ReleaseBuffers() {
    sharing.reset();
    if(pending) glDeleteSync(pending);
    pending=nullptr; phase=Phase::Ready; hasPrevious=false;
    if(pbo) glDeleteBuffers(1,&pbo);
    if(flag) glDeleteBuffers(1,&flag);
    if(converted[0]) glDeleteTextures(2,converted);
    if(targets[0]) glDeleteFramebuffers(2,targets);
    converted[0]=converted[1]=targets[0]=targets[1]=pbo=flag=0;
    if(target) glDeleteFramebuffers(1,&target);
    if(scene) glDeleteTextures(1,&scene);
    target=scene=0;
    back.Reset();
}
void WindowPresenter::Reset() {
    if(compositionTarget && composition) {compositionTarget->SetRoot(nullptr); composition->Commit();}
    attached=false;
    ReleaseBuffers();
    std::vector<unsigned short>().swap(transferBytes);
    if(compare) glDeleteProgram(compare);
    if(program) glDeleteProgram(program);
    if(vao) glDeleteVertexArrays(1,&vao);
    compare=program=vao=0;
    visual.Reset(); compositionTarget.Reset(); composition.Reset();
    swap.Reset(); context.Reset(); device.Reset();
}
unsigned WindowPresenter::Begin(int w,int h,bool forceSdr) {
    const auto now=GetTickCount64();
    const bool invalidFactory=factory && !factory->IsCurrent();
    const bool retryDevice=retryDeviceAt && now>=retryDeviceAt;
    const bool notified=dirty->exchange(false);
    const bool changed=notified || invalidFactory || retryDevice ||
        (!advancedEvents && display.hdr && now>=nextRefresh);
    if(changed) {
        nextRefresh=now+2000;
        if(!factory || invalidFactory || retryDevice) {factory.Reset(); CreateDXGIFactory1(IID_PPV_ARGS(&factory));}
        auto updated=ReadDisplay(hwnd,factory.Get());
        if(updated.monitor!=display.monitor || updated.hdr!=display.hdr || invalidFactory || retryDevice ||
            (failed && notified)) {
            Reset(); failed=false;
        }
        display=std::move(updated);
        if(retryDevice) retryDeviceAt=now+2000;
        if(display.adapter && !display.hdr) retryDeviceAt=0;
    }
    const bool resized=w!=width || h!=height;
    width=w; height=h;
    if(force!=forceSdr) {force=forceSdr;failed=false;retryDeviceAt=0;}
    if(!display.hdr || force || width<1 || height<1) {
        if(swap) Reset();
        const char* description=force ? "SDR comparison" : display.reason.c_str();
        if(status!=description) status=description;
        return 0;
    }
    if(resized && swap) {
        ReleaseBuffers();
        // D3D11 owns no views of these buffers. Drop the last direct reference
        // above, then let DXGI resize the visual's existing flip chain in place.
        const HRESULT resizedBuffers=swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0);
        if(FAILED(resizedBuffers))
            Fail("SDR fallback: Windows could not resize the HDR surface.",resizedBuffers);
        else BindBuffers();
    } else if(!target && (!failed || resized)) {
        failed=false;
        Create();
        if(!failed) retryDeviceAt=0;
    }
    return target;
}
void WindowPresenter::Create() {
    if(!factory || !display.adapter) return Fail("SDR fallback: the display adapter is unavailable.");
    HRESULT hr=D3D11CreateDevice(display.adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(hr)) return Fail("SDR fallback: Direct3D could not open the display adapter.",hr);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=width; desc.Height=height; desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; desc.Scaling=DXGI_SCALING_STRETCH; desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    // FP16 swap chains default to linear scRGB on Windows. The optional
    // SetColorSpace1 API is needed for an override such as HDR10/PQ.
    hr=factory->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&swap);
    if(FAILED(hr)) return Fail(hr==DXGI_ERROR_UNSUPPORTED
        ? "SDR fallback: the display driver does not support an FP16 HDR surface."
        : "SDR fallback: DXGI could not create the FP16 swap chain.",hr);
    Ptr<IDXGIDevice> dxgi;
    if(FAILED(hr=device.As(&dxgi)) || FAILED(hr=DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition))) ||
        FAILED(hr=composition->CreateTargetForHwnd(hwnd,FALSE,&compositionTarget)) ||
        FAILED(hr=composition->CreateVisual(&visual)) || FAILED(hr=visual->SetContent(swap.Get())))
        return Fail("SDR fallback: Windows composition could not bind the HDR surface to this window.",hr);
    program=GLHelpers::CreateShaderProgram(Vertex,Convert.c_str());
    glGenVertexArrays(1,&vao);
    if(!program || !vao) return Fail("SDR fallback: OpenGL could not initialize the HDR display shader.");
    BindBuffers();
}
void WindowPresenter::BindBuffers() {
    const HRESULT hr=swap->GetBuffer(0,IID_PPV_ARGS(&back));
    if(FAILED(hr)) return Fail("SDR fallback: DXGI could not open the HDR frame buffer.",hr);
    scene=GLHelpers::CreateEmptyTexture(width,height);
    target=scene ? GLHelpers::CreateFBO(scene) : 0;
    if(!target) return Fail("SDR fallback: OpenGL could not allocate the HDR frame buffer.");
    if(!compatibleOnly) sharing=SharedFrame::Open(device.Get(),back.Get());
    status=sharing ? sharing->Status() : "HDR active (compatible transfer)";
}
void WindowPresenter::DrawConverted(unsigned destination) {
    glBindFramebuffer(GL_FRAMEBUFFER,destination); glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,width,height); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
    glUseProgram(program); glUniform1f(0,display.white/80.f); glUniform1f(1,display.peak/display.white);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,scene); glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES,0,3);
    glBindVertexArray(0); glBindTexture(GL_TEXTURE_2D,0); glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
}
bool WindowPresenter::Present() {
    if(!Active()) return false;
    bool ready=false;
    if(sharing) {
        unsigned framebuffer;
        auto result=sharing->Begin(framebuffer);
        if(result==SharedFrame::Result::Ready) {
            if(framebuffer) DrawConverted(framebuffer);
            result=sharing->CopyTo(context.Get(),back.Get());
        }
        if(result==SharedFrame::Result::Pending) return true;
        ready=result==SharedFrame::Result::Ready;
        if(!ready) { sharing.reset(); status="HDR active (compatible transfer)"; }
    }
    if(!ready) ready=Transfer();
    if(failed) return false;
    if(ready) {
        const HRESULT presented=swap->Present(1,0);
        if(FAILED(presented)) {
            Fail("SDR fallback: Windows could not present the HDR frame.",presented);
            return false;
        }
        if(!attached) {
            if(FAILED(compositionTarget->SetRoot(visual.Get())) || FAILED(composition->Commit())) {
                Fail("SDR fallback: Windows could not attach the HDR surface.");return false;
            }
            attached=true;
        }
        ++presentations;
    }
    return true;
}
}
#endif
