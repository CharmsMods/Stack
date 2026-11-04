#pragma once
#ifdef _WIN32
#include "DisplayInfo.h"
#include "SharedFrame.h"
#include "Renderer/GLLoader.h"
#include <d3d11.h>
#include <dcomp.h>
#include <array>
#include <vector>
namespace Stack::Hdr {
class WindowPresenter {
public:
    explicit WindowPresenter(HWND window,bool compatibleOnly=false);
    ~WindowPresenter();
    unsigned Begin(int w,int h,bool forceSdr);
    bool Present();
    bool Active() const {return target && !failed;}
    unsigned long long SubmittedFrames() const { return presentations; }
    const DisplayInfo& Display() const {return display;}
    const std::string& Status() const {return status;}
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    void Create();
    void BindBuffers();
    void ReleaseBuffers();
    void Reset();
    bool Transfer();
    void DrawConverted(unsigned fbo);
    void Fail(const char* reason, HRESULT error=S_OK);
    HWND hwnd;
    DisplayInfo display;
    Ptr<IDXGIFactory2> factory;
    std::shared_ptr<std::atomic<bool>> dirty=std::make_shared<std::atomic<bool>>(true);
    bool advancedEvents=false;
    std::shared_ptr<void> watch;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> context;
    Ptr<IDXGISwapChain1> swap;
    Ptr<IDCompositionDevice> composition;
    Ptr<IDCompositionTarget> compositionTarget;
    Ptr<IDCompositionVisual> visual;
    Ptr<ID3D11Texture2D> back;
    int width=0,height=0;
    unsigned scene=0,target=0,program=0,vao=0;
    bool failed=false,attached=false,force=false;
    unsigned long long nextRefresh=0,retryDeviceAt=0,presentations=0;
    bool compatibleOnly=false;
    std::string status="Checking display";
    std::unique_ptr<SharedFrame> sharing;
    unsigned converted[2]{},targets[2]{},flag=0,pbo=0,compare=0;
    GLsync pending=nullptr;
    enum class Phase { Ready, Comparing, Reading } phase=Phase::Ready;
    bool hasPrevious=false;
    float previousWhite=0.f,previousPeak=0.f;
    std::vector<unsigned short> transferBytes;
};
}
#endif
