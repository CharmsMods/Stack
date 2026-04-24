#include "FileDialogs.h"

#ifdef _WIN32
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

namespace FileDialogs {

std::string OpenImageFileDialog(const char* title) {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#else
    (void)title;
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "PNG Image\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "png";

    if (GetSaveFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Modular Studio Library\0*.stacklib\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Modular Studio Library\0*.stacklib\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "stacklib";

    if (GetSaveFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Render Scene Snapshot\0*.renderscene\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "glTF Scene\0*.gltf;*.glb\0glTF JSON\0*.gltf\0Binary glTF\0*.glb\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Render Scene Snapshot\0*.renderscene\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "renderscene";

    if (GetSaveFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Noise Studio Project\0*.mns.json;*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Modular Studio Project\0*.stack;*.comp\0Stack Project\0*.stack\0Composite Project\0*.comp\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameA(&ofn)) {
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
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Modular Studio Project\0*.stack\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "stack";

    if (GetSaveFileNameA(&ofn)) {
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

        if (SUCCEEDED(pfd->Show(NULL))) {
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

} // namespace FileDialogs
