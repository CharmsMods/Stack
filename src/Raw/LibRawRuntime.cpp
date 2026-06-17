#include "LibRawRuntime.h"

#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_MSC_VER)
#include <delayimp.h>
#endif
#endif

namespace Raw {
namespace {

LibRawRuntimeStatus BuildLibRawRuntimeStatus() {
    LibRawRuntimeStatus status;

#ifndef STACK_ENABLE_LIBRAW
    status.availability = LibRawRuntimeAvailability::DisabledInBuild;
    status.compiledWithLibRaw = false;
    status.runtimeAvailable = false;
    status.message = "RAW support is unavailable in this build because LibRaw support is disabled.";
    return status;
#else
    status.compiledWithLibRaw = true;

#if defined(_WIN32) && defined(_MSC_VER)
    const HRESULT hr = __HrLoadAllImportsForDll("libraw.dll");
    if (SUCCEEDED(hr)) {
        status.availability = LibRawRuntimeAvailability::Available;
        status.runtimeAvailable = true;
        status.message.clear();
        return status;
    }

    status.availability = LibRawRuntimeAvailability::MissingOrUnloaded;
    status.runtimeAvailable = false;
    status.message = "RAW support is unavailable because libraw.dll is missing or could not be loaded. Restore the DLL next to Stack and relaunch.";
    return status;
#elif defined(_WIN32)
    HMODULE module = LoadLibraryA("libraw.dll");
    if (module != nullptr) {
        status.availability = LibRawRuntimeAvailability::Available;
        status.runtimeAvailable = true;
        status.message.clear();
        return status;
    }

    status.availability = LibRawRuntimeAvailability::MissingOrUnloaded;
    status.runtimeAvailable = false;
    status.message = "RAW support is unavailable because libraw.dll is missing or could not be loaded. Restore the DLL next to Stack and relaunch.";
    return status;
#else
    status.availability = LibRawRuntimeAvailability::Available;
    status.runtimeAvailable = true;
    status.message.clear();
    return status;
#endif
#endif
}

} // namespace

const LibRawRuntimeStatus& GetLibRawRuntimeStatus() {
    static std::once_flag once;
    static LibRawRuntimeStatus status;
    std::call_once(once, []() {
        status = BuildLibRawRuntimeStatus();
    });
    return status;
}

bool IsLibRawRuntimeAvailable() {
    return GetLibRawRuntimeStatus().runtimeAvailable;
}

} // namespace Raw
