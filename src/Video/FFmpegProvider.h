#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Video {

struct FFmpegProviderStatus {
    std::filesystem::path providerDirectory;
    std::filesystem::path manifestPath;
    std::filesystem::path executablePath;
    std::string providerName;
    std::string ffmpegVersion;
    std::string license;
    std::string configureLine;
    bool providerDirectoryPresent = false;
    bool manifestPresent = false;
    bool executablePresent = false;
    bool approvedForRedistribution = false;
    bool available = false;
    bool unsafeBuild = false;
    std::string message;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

std::filesystem::path GetAppLocalFFmpegProviderDirectory();
FFmpegProviderStatus ProbeFFmpegProviderDirectory(const std::filesystem::path& providerDirectory);
FFmpegProviderStatus ProbeAppLocalFFmpegProvider();

} // namespace Stack::Video
