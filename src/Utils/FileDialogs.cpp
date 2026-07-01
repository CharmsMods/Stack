#include "FileDialogs.h"

#include "App/AppPaths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#ifdef _WIN32
namespace {

HWND g_DialogOwnerWindow = nullptr;
std::function<void()> g_BeforeDialogCallback;

struct DialogOwnerSelection {
    HWND originalOwner = nullptr;
    HWND effectiveOwner = nullptr;
};

bool IsFileDialogTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("STACK_FILE_DIALOG_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

std::ofstream& FileDialogTraceStream() {
    static std::ofstream stream;
    if (!stream.is_open()) {
        std::error_code ec;
        std::filesystem::create_directories(AppPaths::GetLogsDirectory(), ec);
        stream.open(AppPaths::GetLogsDirectory() / "file_dialog_trace.log", std::ios::app);
    }
    return stream;
}

std::string WindowClassName(HWND hwnd) {
    if (!hwnd) {
        return "";
    }

    char buffer[256] = {};
    const int length = GetClassNameA(hwnd, buffer, static_cast<int>(sizeof(buffer)));
    return length > 0 ? std::string(buffer, static_cast<std::size_t>(length)) : std::string();
}

std::string WindowText(HWND hwnd) {
    if (!hwnd) {
        return "";
    }

    char buffer[256] = {};
    const int length = GetWindowTextA(hwnd, buffer, static_cast<int>(sizeof(buffer)));
    return length > 0 ? std::string(buffer, static_cast<std::size_t>(length)) : std::string();
}

void TraceFileDialog(
    const char* event,
    const char* kind,
    const char* title,
    const DialogOwnerSelection& ownerSelection,
    BOOL result = FALSE,
    DWORD error = 0,
    HRESULT hr = S_OK) {
    if (!IsFileDialogTraceEnabled()) {
        return;
    }

    std::ofstream& stream = FileDialogTraceStream();
    if (!stream.is_open()) {
        return;
    }

    const HWND originalOwner = ownerSelection.originalOwner;
    const HWND effectiveOwner = ownerSelection.effectiveOwner;
    const LONG_PTR originalOwnerStyle = originalOwner ? GetWindowLongPtrW(originalOwner, GWL_STYLE) : 0;
    const LONG_PTR originalOwnerExStyle = originalOwner ? GetWindowLongPtrW(originalOwner, GWL_EXSTYLE) : 0;
    const LONG_PTR effectiveOwnerExStyle = effectiveOwner ? GetWindowLongPtrW(effectiveOwner, GWL_EXSTYLE) : 0;
    stream << "event=" << (event ? event : "unknown")
           << " kind=" << (kind ? kind : "unknown")
           << " title=\"" << (title ? title : "") << "\""
           << " originalOwnerHwnd=" << reinterpret_cast<const void*>(originalOwner)
           << " effectiveOwnerHwnd=" << reinterpret_cast<const void*>(effectiveOwner)
           << " originalOwnerClass=\"" << WindowClassName(originalOwner) << "\""
           << " originalOwnerStyle=0x" << std::hex << static_cast<unsigned long long>(originalOwnerStyle)
           << " originalOwnerExStyle=0x" << static_cast<unsigned long long>(originalOwnerExStyle)
           << " effectiveOwnerExStyle=0x" << static_cast<unsigned long long>(effectiveOwnerExStyle) << std::dec
           << " ownerVisible=" << (originalOwner && IsWindowVisible(originalOwner) ? 1 : 0)
           << " ownerEnabled=" << (originalOwner && IsWindowEnabled(originalOwner) ? 1 : 0)
           << " ownerIconic=" << (originalOwner && IsIconic(originalOwner) ? 1 : 0)
           << " ownerTopMost=" << ((originalOwnerExStyle & WS_EX_TOPMOST) != 0 ? 1 : 0)
           << " foregroundHwnd=" << reinterpret_cast<const void*>(GetForegroundWindow())
           << " activeHwnd=" << reinterpret_cast<const void*>(GetActiveWindow())
           << " focusHwnd=" << reinterpret_cast<const void*>(GetFocus())
           << " result=" << (result ? 1 : 0)
           << " error=" << error
           << " hr=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec
           << '\n';
}

void PrepareDialogOwnerWindow(HWND owner) {
    if (!owner) {
        return;
    }

    if (IsIconic(owner)) {
        ShowWindow(owner, SW_RESTORE);
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(owner, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOPMOST) != 0) {
        SetWindowPos(
            owner,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

DialogOwnerSelection ResolveDialogOwnerWindow(const char* kind, const char* title) {
    if (g_BeforeDialogCallback) {
        g_BeforeDialogCallback();
    }

    HWND owner = nullptr;
    if (g_DialogOwnerWindow && IsWindow(g_DialogOwnerWindow)) {
        owner = g_DialogOwnerWindow;
    }

    if (owner == nullptr) {
        owner = GetActiveWindow();
        if (owner == nullptr) {
            owner = GetForegroundWindow();
        }
        if (owner != nullptr) {
            owner = GetLastActivePopup(owner);
        }
    }

    PrepareDialogOwnerWindow(owner);
    DialogOwnerSelection selection;
    selection.originalOwner = owner;
    selection.effectiveOwner = owner;
    TraceFileDialog("owner-resolved", kind, title, selection);
    return selection;
}

bool RunOpenFileDialog(OPENFILENAMEA& ofn, const char* kind, const char* title) {
    const DialogOwnerSelection ownerSelection = ResolveDialogOwnerWindow(kind, title);
    ofn.hwndOwner = ownerSelection.effectiveOwner;
    TraceFileDialog("begin", kind, title, ownerSelection);
    const BOOL result = GetOpenFileNameA(&ofn);
    TraceFileDialog("end", kind, title, ownerSelection, result, result ? 0 : CommDlgExtendedError());
    return result == TRUE;
}

bool RunSaveFileDialog(OPENFILENAMEA& ofn, const char* kind, const char* title) {
    const DialogOwnerSelection ownerSelection = ResolveDialogOwnerWindow(kind, title);
    ofn.hwndOwner = ownerSelection.effectiveOwner;
    TraceFileDialog("begin", kind, title, ownerSelection);
    const BOOL result = GetSaveFileNameA(&ofn);
    TraceFileDialog("end", kind, title, ownerSelection, result, result ? 0 : CommDlgExtendedError());
    return result == TRUE;
}

HRESULT ShowFileDialog(IFileDialog* dialog, const char* kind, const char* title) {
    const DialogOwnerSelection ownerSelection = ResolveDialogOwnerWindow(kind, title);
    TraceFileDialog("begin", kind, title, ownerSelection);
    const HRESULT hr = dialog ? dialog->Show(ownerSelection.effectiveOwner) : E_POINTER;
    TraceFileDialog("end", kind, title, ownerSelection, SUCCEEDED(hr) ? TRUE : FALSE, 0, hr);
    return hr;
}

} // namespace
#endif

namespace FileDialogs {

void SetOwnerWindow(GLFWwindow* window, std::function<void()> beforeDialog) {
#ifdef _WIN32
    g_DialogOwnerWindow = window ? glfwGetWin32Window(window) : nullptr;
    g_BeforeDialogCallback = window ? std::move(beforeDialog) : std::function<void()>();
#else
    (void)window;
    (void)beforeDialog;
#endif
}

std::string OpenImageFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
#ifdef STACK_ENABLE_LIBRAW
    ofn.lpstrFilter = "Image / RAW Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.arw;*.ARW;*.dng;*.DNG\0All Files\0*.*\0";
#else
    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif\0All Files\0*.*\0";
#endif
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenImageFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string OpenRasterImageFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Raster Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenRasterImageFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string OpenLutFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter =
        "LUT Files\0*.cube;*.3dl;*.spi1d;*.spi3d\0"
        "Cube LUT\0*.cube\0"
        "3DL LUT\0*.3dl\0"
        "SPI1D LUT\0*.spi1d\0"
        "SPI3D LUT\0*.spi3d\0"
        "All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenLutFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string SaveLutFileDialog(const char* title, const char* defaultFileName) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (defaultFileName && defaultFileName[0]) {
        strncpy_s(filename, defaultFileName, _TRUNCATE);
    } else {
        strncpy_s(filename, "generated_lut.cube", _TRUNCATE);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Cube LUT\0*.cube\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "cube";

    if (RunSaveFileDialog(ofn, "SaveLutFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
    (void)defaultFileName;
#endif
    return "";
}

std::string SavePngFileDialog(const char* title, const char* defaultFileName) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (defaultFileName && defaultFileName[0]) {
        strncpy_s(filename, defaultFileName, _TRUNCATE);
    } else {
        strncpy_s(filename, "image.png", _TRUNCATE);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "PNG Image\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "png";

    if (RunSaveFileDialog(ofn, "SavePngFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
    (void)defaultFileName;
#endif
    return "";
}

std::string OpenLibraryBundleFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Modular Studio Library\0*.stacklib\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenLibraryBundleFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string SaveLibraryBundleFileDialog(const char* title, const char* defaultFileName) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (defaultFileName && defaultFileName[0]) {
        strncpy_s(filename, defaultFileName, _TRUNCATE);
    } else {
        strncpy_s(filename, "modular_studio_library.stacklib", _TRUNCATE);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Modular Studio Library\0*.stacklib\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "stacklib";

    if (RunSaveFileDialog(ofn, "SaveLibraryBundleFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
    (void)defaultFileName;
#endif
    return "";
}

std::string OpenRenderSceneFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Render Scene Snapshot\0*.renderscene\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenRenderSceneFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string OpenRenderGltfFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "glTF Scene\0*.gltf;*.glb\0glTF JSON\0*.gltf\0Binary glTF\0*.glb\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenRenderGltfFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string SaveRenderSceneFileDialog(const char* title, const char* defaultFileName) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (defaultFileName && defaultFileName[0]) {
        strncpy_s(filename, defaultFileName, _TRUNCATE);
    } else {
        strncpy_s(filename, "render_scene.renderscene", _TRUNCATE);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Render Scene Snapshot\0*.renderscene\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "renderscene";

    if (RunSaveFileDialog(ofn, "SaveRenderSceneFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
    (void)defaultFileName;
#endif
    return "";
}

std::string OpenWebProjectFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Noise Studio Project\0*.mns.json;*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenWebProjectFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string OpenProjectFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Modular Studio Project\0*.stack;*.comp\0Stack Project\0*.stack\0Composite Project\0*.comp\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenProjectFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string SaveProjectFileDialog(const char* title, const char* defaultFileName) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (defaultFileName && defaultFileName[0]) {
        strncpy_s(filename, defaultFileName, _TRUNCATE);
    } else {
        strncpy_s(filename, "project.stack", _TRUNCATE);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Modular Studio Project\0*.stack\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "stack";

    if (RunSaveFileDialog(ofn, "SaveProjectFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
    (void)defaultFileName;
#endif
    return "";
}

std::string OpenThemePresetFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Stack Theme Preset\0*.stacktheme.json;*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (RunOpenFileDialog(ofn, "OpenThemePresetFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
#endif
    return "";
}

std::string SaveThemePresetFileDialog(const char* title, const char* defaultFileName) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (defaultFileName && defaultFileName[0]) {
        strncpy_s(filename, defaultFileName, _TRUNCATE);
    } else {
        strncpy_s(filename, "theme_preset.stacktheme.json", _TRUNCATE);
    }

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Stack Theme Preset\0*.stacktheme.json;*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "json";

    if (RunSaveFileDialog(ofn, "SaveThemePresetFileDialog", title)) {
        return std::string(filename);
    }
#else
    (void)title;
    (void)defaultFileName;
#endif
    return "";
}

std::string OpenFolderDialog(const char* title) {
#ifdef _WIN32
    std::string result = "";
    IFileOpenDialog *pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
        if (len > 0) {
            std::wstring wtitle(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, title, -1, &wtitle[0], len);
            pfd->SetTitle(wtitle.c_str());
        }

        if (SUCCEEDED(ShowFileDialog(pfd, "OpenFolderDialog", title))) {
            IShellItem *psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                    if (utf8Len > 0) {
                        std::string path(utf8Len - 1, 0);
                        WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &path[0], utf8Len, NULL, NULL);
                        result = path;
                    }
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    return result;
#else
    (void)title;
    return "";
#endif
}

std::vector<std::string> OpenMultipleFilesDialog(const char* title, const char* filter) {
    std::vector<std::string> result;
#ifdef _WIN32
    IFileOpenDialog *pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
        }
        
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
        if (len > 0) {
            std::wstring wtitle(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, title, -1, &wtitle[0], len);
            pfd->SetTitle(wtitle.c_str());
        }

        if (SUCCEEDED(ShowFileDialog(pfd, "OpenMultipleFilesDialog", title))) {
            IShellItemArray *psiaResult;
            if (SUCCEEDED(pfd->GetResults(&psiaResult))) {
                DWORD count;
                if (SUCCEEDED(psiaResult->GetCount(&count))) {
                    for (DWORD i = 0; i < count; i++) {
                        IShellItem *psi;
                        if (SUCCEEDED(psiaResult->GetItemAt(i, &psi))) {
                            PWSTR pszPath;
                            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                                if (utf8Len > 0) {
                                    std::string path(utf8Len - 1, 0);
                                    WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &path[0], utf8Len, NULL, NULL);
                                    result.push_back(path);
                                }
                                CoTaskMemFree(pszPath);
                            }
                            psi->Release();
                        }
                    }
                }
                psiaResult->Release();
            }
        }
        pfd->Release();
    }
#else
    (void)title;
    (void)filter;
#endif
    return result;
}

} // namespace FileDialogs
