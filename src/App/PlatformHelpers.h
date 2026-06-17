#pragma once

#include <filesystem>
#include <string>

namespace PlatformHelpers {

bool OpenUrl(const std::string& url, std::string* errorMessage = nullptr);
bool RevealPathInExplorer(const std::filesystem::path& path, std::string* errorMessage = nullptr);
bool LaunchElevatedInstaller(
    const std::filesystem::path& executablePath,
    const std::wstring& parameters,
    std::string* errorMessage = nullptr);

} // namespace PlatformHelpers
