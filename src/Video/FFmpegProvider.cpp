#include "FFmpegProvider.h"

#include "App/AppPaths.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace Stack::Video {
namespace {

constexpr const char* kManifestFileName = "ffmpeg-provider.json";
constexpr const char* kExpectedSchema = "stack.ffmpegProvider";
constexpr int kExpectedSchemaVersion = 1;

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string GetOptionalString(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return {};
    }

    return it->get<std::string>();
}

bool GetOptionalBool(const nlohmann::json& json, const char* key, bool fallback = false) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_boolean()) {
        return fallback;
    }

    return it->get<bool>();
}

int GetOptionalInt(const nlohmann::json& json, const char* key, int fallback = 0) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) {
        return fallback;
    }

    return it->get<int>();
}

std::filesystem::path ResolveProviderRelativePath(
    const std::filesystem::path& providerDirectory,
    const std::string& manifestPathValue) {
    if (manifestPathValue.empty()) {
        return {};
    }

    const std::filesystem::path path(manifestPathValue);
    if (path.is_absolute()) {
        return {};
    }

    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return {};
        }
    }

    return providerDirectory / path;
}

bool LicenseAllowsStackApprovedPackaging(const std::string& licenseText) {
    const std::string lower = ToLowerAscii(licenseText);
    if (lower.empty()) {
        return false;
    }

    if (lower.find("nonfree") != std::string::npos || lower.find("proprietary") != std::string::npos) {
        return false;
    }

    if (lower.find("lgpl") != std::string::npos) {
        return true;
    }

    return lower.find("gpl") == std::string::npos;
}

void ValidateBuildPolicy(FFmpegProviderStatus& status) {
    const std::string lowerConfigureLine = ToLowerAscii(status.configureLine);
    if (lowerConfigureLine.find("--enable-nonfree") != std::string::npos) {
        status.unsafeBuild = true;
        status.errors.push_back("FFmpeg provider configure line includes --enable-nonfree.");
    }

    if (lowerConfigureLine.find("--enable-gpl") != std::string::npos) {
        status.unsafeBuild = true;
        status.errors.push_back("FFmpeg provider configure line includes --enable-gpl.");
    }

    if (!LicenseAllowsStackApprovedPackaging(status.license)) {
        status.unsafeBuild = true;
        status.errors.push_back("FFmpeg provider license marker is not approved for Stack packaging.");
    }
}

void PopulateStatusMessage(FFmpegProviderStatus& status) {
    if (!status.providerDirectoryPresent || !status.executablePresent) {
        status.available = false;
        status.message = "No approved app-local FFmpeg provider was found. Video encoding can remain disabled while Stack continues normally.";
        return;
    }

    if (!status.errors.empty()) {
        status.available = false;
        status.message = "An app-local FFmpeg provider was found, but it is not approved for use.";
        return;
    }

    status.available = true;
    status.message = status.providerName.empty()
        ? "Approved app-local FFmpeg provider is available."
        : "Approved app-local FFmpeg provider is available: " + status.providerName + ".";
}

} // namespace

std::filesystem::path GetAppLocalFFmpegProviderDirectory() {
    return AppPaths::GetExecutableDirectory() / "tools" / "ffmpeg";
}

FFmpegProviderStatus ProbeFFmpegProviderDirectory(const std::filesystem::path& providerDirectory) {
    FFmpegProviderStatus status;
    status.providerDirectory = providerDirectory;
    status.manifestPath = providerDirectory / kManifestFileName;

    std::error_code ec;
    status.providerDirectoryPresent = !providerDirectory.empty() &&
        std::filesystem::is_directory(providerDirectory, ec) &&
        !ec;
    ec.clear();

    status.manifestPresent = std::filesystem::is_regular_file(status.manifestPath, ec) && !ec;
    ec.clear();

    if (!status.providerDirectoryPresent && !status.manifestPresent) {
        PopulateStatusMessage(status);
        return status;
    }

    if (!status.manifestPresent) {
        status.errors.push_back("FFmpeg provider directory exists but ffmpeg-provider.json is missing.");
        status.executablePath = providerDirectory / "ffmpeg.exe";
        status.executablePresent = std::filesystem::is_regular_file(status.executablePath, ec) && !ec;
        PopulateStatusMessage(status);
        return status;
    }

    try {
        std::ifstream input(status.manifestPath);
        const nlohmann::json manifest = nlohmann::json::parse(input);

        if (GetOptionalString(manifest, "schema") != kExpectedSchema) {
            status.errors.push_back("FFmpeg provider manifest schema is not stack.ffmpegProvider.");
        }

        if (GetOptionalInt(manifest, "schemaVersion") != kExpectedSchemaVersion) {
            status.errors.push_back("FFmpeg provider manifest schemaVersion is not 1.");
        }

        status.providerName = GetOptionalString(manifest, "providerName");
        status.ffmpegVersion = GetOptionalString(manifest, "ffmpegVersion");
        status.license = GetOptionalString(manifest, "license");
        status.configureLine = GetOptionalString(manifest, "configureLine");
        status.approvedForRedistribution = GetOptionalBool(manifest, "approvedForRedistribution");

        const std::string binaryFile = GetOptionalString(manifest, "binaryFile");
        if (binaryFile.empty()) {
            status.errors.push_back("FFmpeg provider manifest is missing binaryFile.");
        } else {
            status.executablePath = ResolveProviderRelativePath(providerDirectory, binaryFile);
            if (status.executablePath.empty()) {
                status.errors.push_back("FFmpeg provider binaryFile must be a relative path inside the provider directory.");
            }
        }

        if (status.providerName.empty()) {
            status.warnings.push_back("FFmpeg provider manifest is missing providerName.");
        }

        if (status.ffmpegVersion.empty()) {
            status.warnings.push_back("FFmpeg provider manifest is missing ffmpegVersion.");
        }

        if (status.license.empty()) {
            status.errors.push_back("FFmpeg provider manifest is missing license.");
        }

        if (!status.approvedForRedistribution) {
            status.errors.push_back("FFmpeg provider manifest is not approved for redistribution.");
        }

        if (!status.executablePath.empty()) {
            status.executablePresent = std::filesystem::is_regular_file(status.executablePath, ec) && !ec;
            ec.clear();
            if (!status.executablePresent) {
                status.errors.push_back("FFmpeg provider executable is missing.");
            }
        }

        ValidateBuildPolicy(status);
    } catch (const std::exception& ex) {
        status.errors.push_back(std::string("FFmpeg provider manifest could not be parsed: ") + ex.what());
    }

    PopulateStatusMessage(status);
    return status;
}

FFmpegProviderStatus ProbeAppLocalFFmpegProvider() {
    return ProbeFFmpegProviderDirectory(GetAppLocalFFmpegProviderDirectory());
}

} // namespace Stack::Video
