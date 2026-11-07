#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma comment(lib, "dxgi.lib")
#endif

#include "Renderer/GpuMemoryBudget.h"
#include "Renderer/GpuMemoryBudgetSelection.h"
#include "Renderer/GlDeviceIdentity.h"
#include <cstring>
#include <mutex>
namespace Stack::Renderer {
Raw::RawGpuMemoryBudgetInput QueryGpuMemoryBudget() {
#if defined(_WIN32)
    using Microsoft::WRL::ComPtr;
    // DXGI objects outlive individual GL workers. Recreating/releasing them
    // during thread-local teardown can contend with the driver loader lock.
    static std::mutex queryMutex;
    struct Adapter {
        ComPtr<IDXGIAdapter3> device;
        std::array<unsigned char, 8> luid{};
    };
    static std::vector<Adapter> adapters;
    static bool adapterSearchComplete = false;
    const auto identity = CurrentGlDeviceIdentity();
    const std::lock_guard<std::mutex> lock(queryMutex);
    if (!adapterSearchComplete) {
        adapterSearchComplete = true;
        ComPtr<IDXGIFactory4> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
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
                Adapter adapter;
                if (FAILED(candidate.As(&adapter.device))) continue;
                static_assert(sizeof(description.AdapterLuid) == sizeof(adapter.luid));
                std::memcpy(adapter.luid.data(), &description.AdapterLuid, adapter.luid.size());
                adapters.push_back(std::move(adapter));
            }
        }
    }
    const auto query = [](const Adapter& adapter, unsigned int mask) {
        // Linked adapters can have several rendering nodes. Reserve against
        // the least headroom among the nodes actually used by this context.
        Raw::RawGpuMemoryBudgetInput memory;
        for (unsigned int node = 0; node < 32; ++node) {
            if ((mask & (1u << node)) == 0) continue;
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (FAILED(adapter.device->QueryVideoMemoryInfo(node, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) ||
                info.Budget == 0) continue;
            const Raw::RawGpuMemoryBudgetInput candidate{info.Budget, info.CurrentUsage, true};
            if (!memory.queryAvailable || Raw::ResolveRawGpuMemoryBudget(candidate).workingBudgetBytes <
                    Raw::ResolveRawGpuMemoryBudget(memory).workingBudgetBytes) memory = candidate;
        }
        return memory;
    };
    if (identity.known) for (const auto& adapter : adapters) {
        if (identity.luid != adapter.luid) continue;
        const auto memory = query(adapter, identity.nodeMask);
        if (memory.queryAvailable) return memory;
    }
    std::vector<GpuAdapterBudget> budgets;
    budgets.reserve(adapters.size());
    for (const auto& adapter : adapters) budgets.push_back({adapter.luid, query(adapter, 1u)});
    return SelectGpuMemoryBudget(budgets, std::nullopt);
#else
    return {};
#endif
}
}
