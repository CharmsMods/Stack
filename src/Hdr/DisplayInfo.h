#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <string>

namespace Stack::Hdr {
struct DisplayInfo {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    HMONITOR monitor{};
    bool hdr=false;
    float white=80.f,peak=400.f;
    std::string reason="Windows HDR is off.";
};
DisplayInfo ReadDisplay(HWND window,IDXGIFactory1* factory);
// The returned subscription belongs to the HWND's UI thread.
std::shared_ptr<void> WatchDisplay(HWND window,std::shared_ptr<std::atomic<bool>> changed,bool& advancedEvents);
}
#endif
