#include "GraphWireRoutingRuntime.h"
#include <filesystem>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <delayimp.h>
#endif
namespace Stack::GraphRouting {
void EnsureRuntime() {
#if defined(_WIN32) && defined(_MSC_VER)
    static const bool loaded=[] {
        std::vector<wchar_t> buffer(32768);
        const auto size=GetModuleFileNameW(nullptr,buffer.data(),DWORD(buffer.size()));
        if(!size || size>=buffer.size())return false;
        const auto path=std::filesystem::path(std::wstring(buffer.data(),size)).parent_path()/L"Stack"/L"App"/L"Runtime"/L"libavoid.dll";
        const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)return false;
        for(const auto* symbol:{"StackAvoidCreate","StackAvoidDestroy","StackAvoidUpdate"})if(!GetProcAddress(module,symbol)) {
            FreeLibrary(module);return false;
        }
        if(FAILED(__HrLoadAllImportsForDll("libavoid.dll"))){FreeLibrary(module);return false;}
        return true; // Process-lifetime lease for the delay-loaded imports.
    }();
    if(!loaded)throw std::runtime_error("Graph wire routing requires Stack/App/Runtime/libavoid.dll.");
#endif
}
}
