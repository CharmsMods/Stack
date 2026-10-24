#pragma once
#include <filesystem>
#include <functional>
#include <string>
namespace Stack::Library {
// Strong signatures identify PNG/JPEG/BMP. TGA additionally requires .tga
// provenance because its header has no unique format signature.
std::string LibraryAssetEncodedExtension(const std::filesystem::path& source, std::string& error);
inline std::filesystem::path LibraryAssetExportSuggestedName(
    const std::filesystem::path& source, const std::string& encodedExtension) {
    auto name = source.filename();
    return name.replace_extension(encodedExtension);
}
bool ExportLibraryAsset(const std::filesystem::path& source,
    const std::filesystem::path& destination, std::string& error,
    const std::function<bool()>& isCanceled = {});
}
