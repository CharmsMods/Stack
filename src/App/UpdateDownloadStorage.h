#pragma once

#include <filesystem>
#include <string>

namespace AppUpdate {

// The partial file must be beside its destination so replacement needs no copy.
bool CommitDownloadedInstaller(
    const std::filesystem::path& partialPath,
    const std::filesystem::path& finalPath,
    std::string& errorMessage);

} // namespace AppUpdate
