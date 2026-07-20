#include "App/Validation/ValidationSuites.h"

#include "Video/FFmpegProvider.h"

#include <iostream>

namespace Stack::Validation {

bool ValidateFFmpegProvider() {
    const Video::FFmpegProviderStatus status = Video::ProbeAppLocalFFmpegProvider();

    std::cout << status.message << std::endl;
    std::cout << "Provider directory: " << status.providerDirectory.string() << std::endl;
    if (!status.executablePath.empty()) {
        std::cout << "Executable: " << status.executablePath.string() << std::endl;
    }
    if (!status.providerName.empty()) {
        std::cout << "Provider: " << status.providerName << std::endl;
    }
    if (!status.ffmpegVersion.empty()) {
        std::cout << "FFmpeg version: " << status.ffmpegVersion << std::endl;
    }
    if (!status.license.empty()) {
        std::cout << "License marker: " << status.license << std::endl;
    }

    for (const std::string& warning : status.warnings) {
        std::cout << "Warning: " << warning << std::endl;
    }

    if (status.errors.empty()) {
        std::cout << "FFmpeg provider validation passed." << std::endl;
        return true;
    }

    for (const std::string& error : status.errors) {
        std::cerr << "FFmpeg provider validation failed: " << error << std::endl;
    }
    return false;
}

} // namespace Stack::Validation
