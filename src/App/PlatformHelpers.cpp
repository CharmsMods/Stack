#include "PlatformHelpers.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace PlatformHelpers {
namespace {

std::wstring ToWide(const std::string& value) {
#if defined(_WIN32)
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
    return wide;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

void SetLastErrorMessage(const std::string& fallback, std::string* outErrorMessage) {
    if (outErrorMessage == nullptr) {
        return;
    }

#if defined(_WIN32)
    const DWORD error = GetLastError();
    if (error == ERROR_CANCELLED) {
        *outErrorMessage = "The operation was cancelled.";
        return;
    }
#endif

    *outErrorMessage = fallback;
}

} // namespace

bool OpenUrl(const std::string& url, std::string* errorMessage) {
#if defined(_WIN32)
    const std::wstring wideUrl = ToWide(url);
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32) {
        SetLastErrorMessage("Windows could not open the requested page.", errorMessage);
        return false;
    }
    return true;
#else
    (void)url;
    SetLastErrorMessage("Opening URLs is only implemented on Windows in this build.", errorMessage);
    return false;
#endif
}

bool RevealPathInExplorer(const std::filesystem::path& path, std::string* errorMessage) {
#if defined(_WIN32)
    const std::wstring widePath = path.wstring();
    const std::wstring parameters = L"/select,\"" + widePath + L"\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32) {
        SetLastErrorMessage("Windows could not open File Explorer for the downloaded update.", errorMessage);
        return false;
    }
    return true;
#else
    (void)path;
    SetLastErrorMessage("Revealing downloaded files is only implemented on Windows in this build.", errorMessage);
    return false;
#endif
}

bool LaunchElevatedInstaller(
    const std::filesystem::path& executablePath,
    const std::wstring& parameters,
    std::string* errorMessage) {
#if defined(_WIN32)
    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = nullptr;
    info.lpVerb = L"runas";
    info.lpFile = executablePath.c_str();
    info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        if (GetLastError() == ERROR_CANCELLED) {
            if (errorMessage != nullptr) {
                *errorMessage = "The update requires administrator permission and the UAC prompt was cancelled.";
            }
        } else {
            SetLastErrorMessage("Stack could not launch the installer automatically.", errorMessage);
        }
        return false;
    }

    if (info.hProcess != nullptr) {
        CloseHandle(info.hProcess);
    }
    return true;
#else
    (void)executablePath;
    (void)parameters;
    SetLastErrorMessage("Installer launch is only implemented on Windows in this build.", errorMessage);
    return false;
#endif
}

} // namespace PlatformHelpers
