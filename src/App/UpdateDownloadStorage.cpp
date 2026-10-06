#include "UpdateDownloadStorage.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <system_error>

namespace AppUpdate {

bool CommitDownloadedInstaller(
    const std::filesystem::path& partialPath,
    const std::filesystem::path& finalPath,
    std::string& errorMessage) {
    errorMessage.clear();
    std::error_code error;
    if (partialPath.empty() || finalPath.empty() || partialPath == finalPath ||
        partialPath.parent_path() != finalPath.parent_path() ||
        !std::filesystem::is_regular_file(partialPath, error) || error) {
        errorMessage = "Stack could not find the completed update download.";
        return false;
    }

#if defined(_WIN32)
    if (!MoveFileExW(partialPath.c_str(), finalPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    std::filesystem::rename(partialPath, finalPath, error);
#endif
    if (error) {
        errorMessage = "Stack could not save the downloaded installer: " + error.message();
        return false;
    }
    return true;
}

} // namespace AppUpdate
