#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma comment(lib, "dxgi.lib")
#endif

#include "Renderer/GpuMemoryBudget.h"
#include <utility>
#include <mutex>
namespace Stack::Renderer {
Raw::RawGpuMemoryBudgetInput QueryGpuMemoryBudget() {
    Raw::RawGpuMemoryBudgetInput input;
#if defined(_WIN32)
    using Microsoft::WRL::ComPtr;
    // DXGI objects outlive individual GL workers. Recreating/releasing them
    // during thread-local teardown can contend with the driver loader lock.
    static std::mutex queryMutex;
    static ComPtr<IDXGIAdapter3> cachedAdapter;
    static bool adapterSearchComplete = false;
    const std::lock_guard<std::mutex> lock(queryMutex);
    if (!adapterSearchComplete) {
        adapterSearchComplete = true;
        ComPtr<IDXGIFactory4> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            SIZE_T bestDedicatedBytes = 0;
            for (UINT index = 0;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                if (FAILED(factory->EnumAdapters1(index, &candidate)) ||
                    !candidate) {
                    break;
                }
                DXGI_ADAPTER_DESC1 description {};
                if (FAILED(candidate->GetDesc1(&description)) ||
                    (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                    continue;
                }
                ComPtr<IDXGIAdapter3> candidate3;
                if (SUCCEEDED(candidate.As(&candidate3)) &&
                    description.DedicatedVideoMemory >= bestDedicatedBytes) {
                    bestDedicatedBytes = description.DedicatedVideoMemory;
                    cachedAdapter = std::move(candidate3);
                }
            }
        }
    }
    if (cachedAdapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info {};
        if (SUCCEEDED(cachedAdapter->QueryVideoMemoryInfo(
                0,
                DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                &info))) {
            input.localBudgetBytes = info.Budget;
            input.localUsageBytes = info.CurrentUsage;
            input.queryAvailable = info.Budget > 0u;
        }
    }
#endif
    return input;
}
}
