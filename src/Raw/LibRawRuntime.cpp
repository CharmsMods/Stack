#include "LibRawRuntime.h"

#include "App/AppPaths.h"

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
    const std::filesystem::path libraryPath =
        AppPaths::GetRuntimeDirectory() / "libraw.dll";
    HMODULE module = LoadLibraryExW(
        libraryPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        status.availability = LibRawRuntimeAvailability::MissingOrUnloaded;
        status.runtimeAvailable = false;
        status.message =
            "RAW support is unavailable because Stack\\App\\Runtime\\libraw.dll "
            "is missing or could not be loaded.";
        return status;
    }
    const HRESULT hr = __HrLoadAllImportsForDll("libraw.dll");
    if (SUCCEEDED(hr)) {
        status.availability = LibRawRuntimeAvailability::Available;
        status.runtimeAvailable = true;
        status.message.clear();
        return status;
    }

    status.availability = LibRawRuntimeAvailability::MissingOrUnloaded;
    status.runtimeAvailable = false;
    status.message = "RAW support is unavailable because the managed LibRaw runtime could not be initialized.";
    return status;
#elif defined(_WIN32)
    const std::filesystem::path libraryPath =
        AppPaths::GetRuntimeDirectory() / "libraw.dll";
    HMODULE module = LoadLibraryExW(
        libraryPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module != nullptr) {
        status.availability = LibRawRuntimeAvailability::Available;
        status.runtimeAvailable = true;
        status.message.clear();
        return status;
    }

    status.availability = LibRawRuntimeAvailability::MissingOrUnloaded;
    status.runtimeAvailable = false;
    status.message = "RAW support is unavailable because Stack\\App\\Runtime\\libraw.dll is missing or could not be loaded.";
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
