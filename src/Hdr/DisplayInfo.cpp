#ifdef _WIN32
#include "DisplayInfo.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <dispatcherqueue.h>
#include <commctrl.h>
#include <roapi.h>
#include <windows.graphics.display.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>

namespace Stack::Hdr {
namespace {
float SdrWhite(const wchar_t* deviceName) {
    for(int attempt=0;attempt<3;++attempt) {
        UINT32 paths=0,modes=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes)!=ERROR_SUCCESS) break;
        std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
        std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
        const auto status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,p.data(),&modes,m.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER) continue;
        if(status!=ERROR_SUCCESS) break;
        for(UINT32 i=0;i<paths;++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(name),p[i].sourceInfo.adapterId,p[i].sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&name.header) || _wcsicmp(deviceName,name.viewGdiDeviceName)) continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),p[i].targetInfo.adapterId,p[i].targetInfo.id};
            if(!DisplayConfigGetDeviceInfo(&white.header) && white.SDRWhiteLevel)
                return std::clamp(80.f*white.SDRWhiteLevel/1000.f,40.f,1000.f);
        }
        break;
    }
    return 80.f;
}
using Info=winrt::Windows::Graphics::Display::DisplayInformation;
struct Watch {
    HWND hwnd{};
    HMONITOR monitor{};
    bool subscribed=false;
    std::shared_ptr<std::atomic<bool>> changed;
    static LRESULT CALLBACK Messages(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto& self=*reinterpret_cast<Watch*>(data);
        if(msg==WM_WINDOWPOSCHANGED || msg==WM_DPICHANGED) {
            auto monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
            if(monitor!=self.monitor) {self.monitor=monitor;self.changed->store(true);}
        } else if(msg==WM_DISPLAYCHANGE || msg==WM_SETTINGCHANGE || msg==WM_DEVICECHANGE) self.changed->store(true);
        return DefSubclassProc(hwnd,msg,w,l);
    }
    Info info{nullptr};
    winrt::event_token token{};
    ~Watch() {
        if(subscribed) RemoveWindowSubclass(hwnd,Messages,reinterpret_cast<UINT_PTR>(this));
        if(info && token.value) info.AdvancedColorInfoChanged(token);
    }
};
}
DisplayInfo ReadDisplay(HWND window,IDXGIFactory1* factory) {
    DisplayInfo result;
    result.monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
    if(!factory) return result;
    for(UINT a=0;;++a) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if(FAILED(factory->EnumAdapters1(a,&adapter))) break;
        for(UINT o=0;;++o) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            if(FAILED(adapter->EnumOutputs(o,&output))) break;
            Microsoft::WRL::ComPtr<IDXGIOutput6> advanced;
            DXGI_OUTPUT_DESC1 desc{};
            if(FAILED(output.As(&advanced)) || FAILED(advanced->GetDesc1(&desc)) || desc.Monitor!=result.monitor) continue;
            result.adapter=adapter;
            result.hdr=desc.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            if(result.hdr) {
                result.white=SdrWhite(desc.DeviceName);
                result.peak=(std::max)(result.white,std::isfinite(desc.MaxLuminance) && desc.MaxLuminance>0.f ? desc.MaxLuminance : 400.f);
                result.reason.clear();
            }
            return result;
        }
    }
    return result;
}
std::shared_ptr<void> WatchDisplay(HWND window,std::shared_ptr<std::atomic<bool>> changed,bool& advancedEvents) {
    auto watch=std::make_shared<Watch>();
    watch->hwnd=window; watch->monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST); watch->changed=changed;
    watch->subscribed=SetWindowSubclass(window,Watch::Messages,reinterpret_cast<UINT_PTR>(watch.get()),reinterpret_cast<DWORD_PTR>(watch.get()))!=FALSE;
    advancedEvents=false;
    try {
        struct Apartment {
            HRESULT initialized=RoInitialize(RO_INIT_SINGLETHREADED);
            ~Apartment(){if(SUCCEEDED(initialized)) RoUninitialize();}
        };
        static thread_local Apartment apartment;
        using namespace winrt::Windows::System;
        // GLFW owns the message loop. A current-thread dispatcher uses that loop.
        static thread_local DispatcherQueueController controller{nullptr};
        if(!DispatcherQueue::GetForCurrentThread() && !controller) {
            const auto lib=LoadLibraryExW(L"CoreMessaging.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!lib) return watch;
            const auto create=reinterpret_cast<decltype(&CreateDispatcherQueueController)>(GetProcAddress(lib,"CreateDispatcherQueueController"));
            DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_NONE};
            const auto hr=create ? create(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))) : E_NOINTERFACE;
            FreeLibrary(lib);
            if(FAILED(hr)) return watch;
        }
        auto factory=winrt::get_activation_factory<Info,IDisplayInformationStaticsInterop>();
        winrt::check_hresult(factory->GetForWindow(window,winrt::guid_of<Info>(),winrt::put_abi(watch->info)));
        watch->token=watch->info.AdvancedColorInfoChanged([changed](auto&&,auto&&){changed->store(true);});
        advancedEvents=true;
    } catch(const winrt::hresult_error&) {}
    return watch;
}
}
#endif
