#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace PlatformHelpers {

// Zero means unavailable; callers must not treat it as an unchanged clipboard.
std::uint32_t ClipboardSequenceNumber();

bool OpenUrl(const std::string& url, std::string* errorMessage = nullptr);
bool OpenPath(const std::filesystem::path& path, std::string* errorMessage = nullptr);
bool RevealPathInExplorer(const std::filesystem::path& path, std::string* errorMessage = nullptr);
bool LaunchElevatedInstaller(
    const std::filesystem::path& executablePath,
    const std::wstring& parameters,
    std::string* errorMessage = nullptr);

} // namespace PlatformHelpers
