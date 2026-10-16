#include "SelectionLaunch.h"
#include "ShellIdentities.h"

#include <shlwapi.h>
#include <atomic>
#include <new>

namespace {
HMODULE moduleHandle = nullptr;
std::atomic<ULONG> objectCount{0};

struct Invocation {
    IStream* selection = nullptr;
    HMODULE module = nullptr;
};

DWORD WINAPI LaunchThread(void* parameter) {
    auto* invocation = static_cast<Invocation*>(parameter);
    const HMODULE module = invocation->module;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT result = initialized;
    if (SUCCEEDED(initialized)) {
        IShellItemArray* selection = nullptr;
        result = CoGetInterfaceAndReleaseStream(invocation->selection, IID_PPV_ARGS(&selection));
        invocation->selection = nullptr;
        if (SUCCEEDED(result)) {
            result = Stack::BurstShell::LaunchSelection(module, selection);
            selection->Release();
        }
        CoUninitialize();
    }
    if (invocation->selection) invocation->selection->Release();
    delete invocation;
    if (FAILED(result)) {
        MessageBoxW(nullptr,
            L"Stack Burst could not open this selection. Select at least two supported RAW files and try again. "
            L"If the problem continues, repair or reinstall Stack Burst.",
            L"Stack Burst", MB_OK | MB_ICONERROR);
    }
    --objectCount;
    // Keep this DLL mapped until after the callback's final instruction.
    FreeLibraryAndExitThread(module, static_cast<DWORD>(result));
}

class ExplorerCommand final : public IExplorerCommand {
public:
    explicit ExplorerCommand(REFCLSID identity) : identity_(identity) { ++objectCount; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IExplorerCommand)) return E_NOINTERFACE;
        *value = static_cast<IExplorerCommand*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto result = --references_; if (!result) delete this; return result; }
    HRESULT STDMETHODCALLTYPE GetTitle(IShellItemArray*, PWSTR* title) override {
        return title ? SHStrDupW(L"Burst denoise with Stack", title) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE GetIcon(IShellItemArray*, PWSTR* icon) override {
        if (!icon) return E_POINTER;
        *icon = nullptr;
        try { return SHStrDupW((L"\"" + Stack::BurstShell::ModulePath(moduleHandle) + L"\",-1").c_str(), icon); }
        catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetToolTip(IShellItemArray*, PWSTR* tip) override {
        return tip ? SHStrDupW(L"Group selected RAW captures and denoise with Stack Burst", tip) : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalName(GUID* name) override {
        if (!name) return E_POINTER;
        *name = identity_; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetState(IShellItemArray* selection, BOOL, EXPCMDSTATE* state) override {
        if (!state) return E_POINTER;
        *state = ECS_HIDDEN;
        DWORD count = 0;
        // Menu construction is constant-time: no enumeration, disk access,
        // metadata, image decoding, grouping, engine startup or background job.
        if (selection && SUCCEEDED(selection->GetCount(&count)) && count >= 2 && count <= Stack::BurstShell::MaximumSelectionFiles)
            *state = ECS_ENABLED;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IShellItemArray* selection, IBindCtx*) override {
        if (!selection) return E_INVALIDARG;
        auto* invocation = new (std::nothrow) Invocation;
        if (!invocation) return E_OUTOFMEMORY;
        HRESULT result = CoMarshalInterThreadInterfaceInStream(__uuidof(IShellItemArray), selection, &invocation->selection);
        if (FAILED(result)) { delete invocation; return result; }
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&LaunchThread), &invocation->module)) {
            result = HRESULT_FROM_WIN32(GetLastError());
            CoReleaseMarshalData(invocation->selection);
            invocation->selection->Release(); delete invocation; return result;
        }
        ++objectCount;
        const HANDLE thread = CreateThread(nullptr, 0, LaunchThread, invocation, 0, nullptr);
        if (!thread) {
            result = HRESULT_FROM_WIN32(GetLastError());
            --objectCount;
            FreeLibrary(invocation->module);
            CoReleaseMarshalData(invocation->selection);
            invocation->selection->Release(); delete invocation; return result;
        }
        CloseHandle(thread);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFlags(EXPCMDFLAGS* flags) override {
        if (!flags) return E_POINTER;
        *flags = ECF_DEFAULT; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumSubCommands(IEnumExplorerCommand** commands) override {
        if (!commands) return E_POINTER;
        *commands = nullptr; return E_NOTIMPL;
    }
private:
    ~ExplorerCommand() { --objectCount; }
    std::atomic<ULONG> references_{1};
    CLSID identity_;
};

class CommandFactory final : public IClassFactory {
public:
    explicit CommandFactory(REFCLSID identity) : identity_(identity) { ++objectCount; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *value = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto result = --references_; if (!result) delete this; return result; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* command = new (std::nothrow) ExplorerCommand(identity_);
        if (!command) return E_OUTOFMEMORY;
        const auto result = command->QueryInterface(iid, value);
        command->Release(); return result;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++objectCount; else --objectCount; return S_OK; }
private:
    ~CommandFactory() { --objectCount; }
    std::atomic<ULONG> references_{1};
    CLSID identity_;
};
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) moduleHandle = instance;
    return TRUE;
}

STDAPI DllCanUnloadNow() { return objectCount == 0 ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(REFCLSID classId, REFIID iid, void** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    bool supported = false;
    for (const auto text : StackBurstShellClassIds) {
        CLSID candidate{};
        if (SUCCEEDED(CLSIDFromString(text, &candidate)) && candidate == classId) { supported = true; break; }
    }
    if (!supported) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) CommandFactory(classId);
    if (!factory) return E_OUTOFMEMORY;
    const auto result = factory->QueryInterface(iid, value);
    factory->Release(); return result;
}
