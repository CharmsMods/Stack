#include "Library/Internal/LibraryNewAssetWrite.h"
#include "Utils/AtomicExportFile.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>

namespace Stack::Library {
namespace { std::atomic<std::uint64_t> sequence{0}; }
bool WriteNewLibraryAsset(const std::filesystem::path& folder,
    const std::filesystem::path& preferredFileName, const std::vector<unsigned char>& bytes,
    std::filesystem::path& publishedPath, std::string& error) try {
    publishedPath.clear(); error.clear();
    if (bytes.empty() || preferredFileName.empty() || preferredFileName.has_parent_path() ||
        preferredFileName.filename() != preferredFileName) {
        error = "The imported image needs bytes and a filename inside the Library.";
        return false;
    }
    std::string extension = preferredFileName.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char value){return static_cast<char>(std::tolower(value));});
    const std::vector<std::string> formats{".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    if (std::find(formats.begin(),formats.end(),extension)==formats.end()) {
        error = "The imported file must use a supported image extension."; return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder,ec);
    if (ec) {error="Could not access the Library image folder: "+ec.message();return false;}
    for (unsigned attempt=0; attempt<64; ++attempt) {
        const auto name = attempt==0 ? preferredFileName : std::filesystem::u8path(
            preferredFileName.stem().u8string()+"_"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"_"+
            std::to_string(sequence.fetch_add(1))+preferredFileName.extension().u8string());
        const auto candidate=folder/name;
        ec.clear();
        const auto status=std::filesystem::symlink_status(candidate,ec);
        if (ec && ec!=std::errc::no_such_file_or_directory) {
            error="Could not inspect the image destination: "+ec.message();return false;
        }
        if (std::filesystem::exists(status)) continue;
        FileSave::ExportFileTarget target;
        if (!FileSave::PrepareExportFileTarget(candidate,{},formats,target,error)) {
            ec.clear();
            if (std::filesystem::exists(std::filesystem::symlink_status(candidate,ec))) continue;
            return false;
        }
        if (target.approval.existed) continue;
        if (FileSave::WriteExportFileAtomically(target,bytes,error)) {
            publishedPath=target.approval.path;return true;
        }
        ec.clear();
        if (!std::filesystem::exists(std::filesystem::symlink_status(candidate,ec))) return false;
    }
    error="The image destination kept changing. Retry the import.";
    return false;
} catch (const std::exception& exception) {
    error="Could not save the imported image: "+std::string(exception.what());return false;
}
}
