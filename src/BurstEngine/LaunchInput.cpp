#include "LaunchInput.h"
#include "Protocol.h"
#include <shobjidl.h>
#include <wrl/client.h>
#include <fstream>

namespace Stack::BurstEngine {
namespace {
bool OwnedManifest(const std::filesystem::path& path){
    // Never clean an arbitrary user-provided manifest or follow a redirected
    // directory. Explorer owns precisely one GUID folder under this temp root.
    const auto root=std::filesystem::temp_directory_path()/L"StackBurst"/L"Selections";
    std::error_code error;const auto actual=std::filesystem::canonical(path,error);
    if(error||actual.filename()!=L"selection.json")return false;
    const auto name=actual.parent_path().filename().wstring();GUID guid{};
    if(name.size()!=36||FAILED(CLSIDFromString((L"{"+name+L"}").c_str(),&guid)))return false;
    const auto expected=std::filesystem::canonical(root,error);
    return !error&&actual.parent_path().parent_path()==expected&&
        std::filesystem::absolute(path).lexically_normal()==actual;
}
}
std::vector<std::filesystem::path> ReadSelectionManifest(const std::filesystem::path& path){
    const bool owned=OwnedManifest(path);
    struct Cleanup{std::filesystem::path path;bool owned;
        ~Cleanup(){if(owned){std::error_code e;std::filesystem::remove(path,e);std::filesystem::remove(path.parent_path(),e);}}
    }cleanup{path,owned};
    if(std::filesystem::file_size(path)>16ull*1024*1024)throw std::runtime_error("The Explorer selection is too large.");
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Could not read the Explorer selection.");
    const auto document=Json::parse(stream);
    if(document.value("schemaVersion",0)!=1||!document.contains("files")||!document["files"].is_array())
        throw std::runtime_error("Unsupported Explorer selection format.");
    if(document["files"].empty()||document["files"].size()>100000)throw std::runtime_error("The selection must contain between 1 and 100000 files.");
    std::vector<std::filesystem::path> result;result.reserve(document["files"].size());
    for(const auto& file:document["files"]){
        const auto text=file.get<std::string>();
        if(text.find('\0')!=std::string::npos)throw std::runtime_error("The selection contains an invalid path.");
        auto source=TextPath(text);if(!source.is_absolute())throw std::runtime_error("Explorer selections require absolute file paths.");
        result.push_back(std::move(source));
    }
    return result;
}
std::vector<std::filesystem::path> PickSelection(void* owner){
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Could not open the Windows file picker.");
    DWORD options=0;dialog->GetOptions(&options);
    dialog->SetOptions(options|FOS_ALLOWMULTISELECT|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST);
    const COMDLG_FILTERSPEC types[]={{L"Camera RAW images",L"*.arw;*.srf;*.sr2;*.raw;*.dng"},{L"All files",L"*.*"}};
    dialog->SetFileTypes(2,types);dialog->SetTitle(L"Choose images for Stack Burst");
    const auto shown=dialog->Show(static_cast<HWND>(owner));
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
    if(FAILED(shown))throw std::runtime_error("The file selection could not be opened.");
    ComPtr<IShellItemArray> items;if(FAILED(dialog->GetResults(&items)))throw std::runtime_error("Could not read the selected files.");
    DWORD count=0;items->GetCount(&count);std::vector<std::filesystem::path> paths;paths.reserve(count);
    for(DWORD i=0;i<count;++i){ComPtr<IShellItem> item;PWSTR path=nullptr;
        if(SUCCEEDED(items->GetItemAt(i,&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){paths.emplace_back(path);CoTaskMemFree(path);}}
    return paths;
}
}
