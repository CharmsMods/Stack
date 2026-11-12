#pragma once
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>
namespace Stack::Editor::RawLabInternal {
inline const std::vector<std::wstring>& BrowserImageExtensions() {
    static const std::vector<std::wstring> types={L".jpg",L".jpeg",L".png",L".bmp",L".tga",L".tif",L".tiff",
        L".webp",L".avif",L".heic",L".3fr",L".arw",L".cr2",L".cr3",L".dng",L".fff",L".iiq",
        L".nef",L".nrw",L".orf",L".pef",L".raf",L".raw",L".rw2",L".rwl",L".sr2",L".srf"};
    return types;
}
inline bool BrowserImagePath(const std::filesystem::path& path) {
    auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
    const auto& types=BrowserImageExtensions();return std::find(types.begin(),types.end(),ext)!=types.end();
}
}
