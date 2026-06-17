#include "UpdateManager.h"

#include "AppPaths.h"
#include "AppVersion.h"
#include "PlatformHelpers.h"
#include "Async/TaskSystem.h"
#include "ThirdParty/json.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace AppUpdate {
namespace {

using json = nlohmann::json;

constexpr const char* kStateFileName = "StackUpdateState.json";
constexpr std::uint64_t kBackgroundCheckIntervalSeconds = 12ull * 60ull * 60ull;

struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool valid = false;
};

struct ReleaseAsset {
    std::string name;
    std::string url;
    std::string digest;
};

struct ReleaseInfo {
    bool ok = false;
    bool updateAvailable = false;
    std::string latestVersion;
    std::string releaseName;
    std::string releaseSummary;
    std::string releasePageUrl;
    ReleaseAsset installerAsset;
    ReleaseAsset hashAsset;
    std::string errorMessage;
};

struct CheckTaskResult {
    ReleaseInfo release;
    bool manual = false;
    std::uint64_t checkedAt = 0;
};

struct DownloadTaskResult {
    bool ok = false;
    bool verificationAvailable = false;
    bool verificationPassed = false;
    std::string downloadedFilePath;
    std::string errorMessage;
    std::string selectedDigest;
};

std::uint64_t GetUnixTimeNow() {
    return static_cast<std::uint64_t>(std::time(nullptr));
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string FormatLocalTimestamp(std::uint64_t unixSeconds) {
    if (unixSeconds == 0) {
        return "Never";
    }

    std::time_t rawTime = static_cast<std::time_t>(unixSeconds);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &rawTime);
#else
    localtime_r(&rawTime, &localTime);
#endif
    char buffer[64] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &localTime) == 0) {
        return "Unknown";
    }
    return buffer;
}

std::filesystem::path GetPersistentStatePath() {
    return AppPaths::GetSettingsDirectory() / kStateFileName;
}

bool ParseSemanticVersion(const std::string& text, SemanticVersion& outVersion) {
    std::string value = Trim(text);
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        value.erase(value.begin());
    }

    std::stringstream stream(value);
    std::string majorText;
    std::string minorText;
    std::string patchText;
    if (!std::getline(stream, majorText, '.') ||
        !std::getline(stream, minorText, '.') ||
        !std::getline(stream, patchText, '.')) {
        return false;
    }

    if (stream.rdbuf()->in_avail() != 0) {
        return false;
    }

    try {
        outVersion.major = std::stoi(majorText);
        outVersion.minor = std::stoi(minorText);
        outVersion.patch = std::stoi(patchText);
        outVersion.valid = true;
        return true;
    } catch (...) {
        return false;
    }
}

int CompareSemanticVersion(const SemanticVersion& lhs, const SemanticVersion& rhs) {
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (lhs.patch != rhs.patch) {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    return 0;
}

std::string BuildReleaseSummary(const std::string& releaseBody) {
    std::stringstream stream(releaseBody);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (!line.empty()) {
            if (line.size() > 240) {
                line.resize(240);
                line += "...";
            }
            return line;
        }
    }
    return {};
}

bool NameContains(const std::string& haystack, const std::string& needle) {
    return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
}

int ScoreInstallerAsset(const ReleaseAsset& asset, const std::string& versionText) {
    const std::string lowered = ToLowerAscii(asset.name);
    if (!NameContains(lowered, ".exe") || lowered.find(".exe") == std::string::npos) {
        return -1000;
    }

    int score = 0;
    if (lowered.find("stacksetup") != std::string::npos) {
        score += 80;
    }
    if (lowered.find("stack") != std::string::npos) {
        score += 20;
    }
    if (lowered.find("win") != std::string::npos || lowered.find("windows") != std::string::npos) {
        score += 20;
    }
    if (lowered.find("x64") != std::string::npos || lowered.find("64") != std::string::npos) {
        score += 12;
    }
    if (lowered.find(ToLowerAscii(versionText)) != std::string::npos) {
        score += 18;
    }
    if (lowered.find("portable") != std::string::npos || lowered.find("source") != std::string::npos) {
        score -= 60;
    }
    return score;
}

bool TryExtractHashForAsset(const std::string& shaText, const std::string& assetName, std::string& outHash) {
    std::stringstream stream(shaText);
    std::string line;
    const std::string targetName = ToLowerAscii(assetName);
    while (std::getline(stream, line)) {
        const std::size_t firstSpace = line.find_first_of(" \t");
        if (firstSpace == std::string::npos) {
            continue;
        }

        std::string hash = Trim(line.substr(0, firstSpace));
        std::string remainder = Trim(line.substr(firstSpace));
        while (!remainder.empty() && (remainder.front() == '*' || remainder.front() == ' ' || remainder.front() == '\t')) {
            remainder.erase(remainder.begin());
        }

        if (ToLowerAscii(remainder) == targetName && hash.size() == 64) {
            outHash = ToLowerAscii(hash);
            return true;
        }
    }
    return false;
}

#if defined(_WIN32)
struct HttpResponse {
    bool ok = false;
    DWORD statusCode = 0;
    std::string body;
};

bool CrackUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& secure) {
    const std::wstring wideUrl(url.begin(), url.end());
    URL_COMPONENTSW components {};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    std::wstring mutableUrl = wideUrl;
    if (!WinHttpCrackUrl(mutableUrl.data(), 0, 0, &components)) {
        return false;
    }

    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    port = components.nPort;
    secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

HttpResponse HttpGetText(const std::string& url, const std::vector<std::wstring>& extraHeaders) {
    HttpResponse response;

    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
    if (!CrackUrl(url, host, path, port, secure)) {
        return response;
    }

    HINTERNET session = WinHttpOpen(L"StackUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        return response;
    }

    HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return response;
    }

    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    std::wstring headerBlock = L"User-Agent: StackUpdater/" + std::wstring(AppVersion::kVersionString, AppVersion::kVersionString + std::strlen(AppVersion::kVersionString)) + L"\r\n";
    for (const std::wstring& header : extraHeaders) {
        headerBlock += header + L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(
        request,
        headerBlock.c_str(),
        static_cast<DWORD>(headerBlock.size()),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &statusCode, &statusSize, nullptr);
    response.statusCode = statusCode;

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }

        std::string chunk(static_cast<std::size_t>(available), '\0');
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &downloaded)) {
            break;
        }
        chunk.resize(static_cast<std::size_t>(downloaded));
        body += chunk;
    }

    response.ok = statusCode >= 200 && statusCode < 300;
    response.body = std::move(body);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

bool DownloadUrlToFile(const std::string& url, const std::filesystem::path& destinationPath, std::string& outError) {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
    if (!CrackUrl(url, host, path, port, secure)) {
        outError = "The update download URL was invalid.";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"StackUpdater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        outError = "Stack could not start the download session.";
        return false;
    }

    HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        outError = "Stack could not connect to the download server.";
        return false;
    }

    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "Stack could not open the update download request.";
        return false;
    }

    const std::wstring headers = L"Accept: application/octet-stream\r\nUser-Agent: StackUpdater\r\n";
    const BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "The update download failed before any data was received.";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &statusCode, &statusSize, nullptr);
    if (statusCode < 200 || statusCode >= 300) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "GitHub returned an unexpected response while downloading the update.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destinationPath.parent_path(), ec);
    std::ofstream output(destinationPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "Stack could not create the local update cache folder.";
        return false;
    }

    bool readOk = true;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            readOk = false;
            break;
        }
        if (available == 0) {
            break;
        }

        std::vector<char> chunk(static_cast<std::size_t>(available));
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &downloaded)) {
            readOk = false;
            break;
        }

        output.write(chunk.data(), static_cast<std::streamsize>(downloaded));
        if (!output.good()) {
            readOk = false;
            outError = "Stack could not write the downloaded update file.";
            break;
        }
    }

    output.close();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!readOk) {
        std::filesystem::remove(destinationPath, ec);
        if (outError.empty()) {
            outError = "The update download was interrupted.";
        }
        return false;
    }

    return true;
}

bool ComputeFileSha256(const std::filesystem::path& path, std::string& outDigest) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD bytesWritten = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }

    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytesWritten, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<unsigned char> objectBuffer(static_cast<std::size_t>(objectLength));
    if (BCryptCreateHash(algorithm, &hash, objectBuffer.data(), objectLength, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    while (file.good()) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0) {
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) != 0) {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return false;
            }
        }
    }

    unsigned char digest[32] = {};
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream output;
    output << std::hex;
    for (unsigned char byte : digest) {
        output.width(2);
        output.fill('0');
        output << static_cast<int>(byte);
    }
    outDigest = ToLowerAscii(output.str());
    return true;
}
#endif

ReleaseInfo FetchLatestRelease() {
    ReleaseInfo result;

    SemanticVersion currentVersion;
    if (!ParseSemanticVersion(AppVersion::kVersionString, currentVersion)) {
        result.errorMessage = "The current app version is missing or invalid.";
        return result;
    }

#if defined(_WIN32)
    HttpResponse response = HttpGetText(
        AppVersion::kReleaseApiUrl,
        {L"Accept: application/vnd.github+json"});
    if (!response.ok) {
        if (response.statusCode == 403 || response.statusCode == 429) {
            result.errorMessage = "GitHub rate limited the update check. Try again later.";
        } else {
            result.errorMessage = "Could not check for updates. Check your internet connection and try again.";
        }
        return result;
    }

    json root = json::parse(response.body, nullptr, false);
#else
    json root = json();
#endif
    if (root.is_discarded() || !root.is_object()) {
        result.errorMessage = "GitHub returned an invalid update response.";
        return result;
    }

    if (root.value("draft", false) || root.value("prerelease", false)) {
        result.errorMessage = "No normal release is currently available.";
        return result;
    }

    result.releaseName = root.value("name", std::string());
    result.releaseSummary = BuildReleaseSummary(root.value("body", std::string()));
    result.releasePageUrl = root.value("html_url", std::string(AppVersion::kReleasesPageUrl));

    const std::string tagName = root.value("tag_name", std::string());
    SemanticVersion latestVersion;
    if (!ParseSemanticVersion(tagName, latestVersion)) {
        result.errorMessage = "The latest release tag is not a valid semantic version.";
        return result;
    }

    result.latestVersion = tagName;
    const int comparison = CompareSemanticVersion(latestVersion, currentVersion);
    result.ok = true;
    result.updateAvailable = comparison > 0;
    if (!result.updateAvailable) {
        return result;
    }

    std::vector<ReleaseAsset> installerCandidates;
    ReleaseAsset hashAsset;
    if (root.contains("assets") && root["assets"].is_array()) {
        for (const auto& assetJson : root["assets"]) {
            if (!assetJson.is_object()) {
                continue;
            }

            ReleaseAsset asset;
            asset.name = assetJson.value("name", std::string());
            asset.url = assetJson.value("browser_download_url", std::string());
            asset.digest = assetJson.value("digest", std::string());
            if (asset.name.empty() || asset.url.empty()) {
                continue;
            }

            const std::string lowered = ToLowerAscii(asset.name);
            if (lowered == "sha256sums.txt" || lowered == "sha256sum.txt" || lowered.find("sha256") != std::string::npos) {
                hashAsset = asset;
            }
            installerCandidates.push_back(asset);
        }
    }

    int bestScore = -1000;
    for (const ReleaseAsset& asset : installerCandidates) {
        const int score = ScoreInstallerAsset(asset, tagName);
        if (score > bestScore) {
            bestScore = score;
            result.installerAsset = asset;
        }
    }

    result.hashAsset = hashAsset;
    if (bestScore < 0 || result.installerAsset.url.empty()) {
        result.ok = false;
        result.errorMessage = "No compatible Windows update asset was found for this release.";
        result.updateAvailable = false;
        return result;
    }

    return result;
}

DownloadTaskResult DownloadReleaseInstaller(
    const ReleaseInfo& release,
    const std::filesystem::path& downloadDirectory) {
    DownloadTaskResult result;
    if (release.installerAsset.url.empty() || release.installerAsset.name.empty()) {
        result.errorMessage = "No compatible installer asset is available to download.";
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(downloadDirectory, ec);
    if (ec) {
        result.errorMessage = "Stack could not create the local update cache folder.";
        return result;
    }

    const std::filesystem::path tempPath = downloadDirectory / (release.installerAsset.name + ".partial");
    const std::filesystem::path finalPath = downloadDirectory / release.installerAsset.name;
    std::filesystem::remove(tempPath, ec);

#if defined(_WIN32)
    if (!DownloadUrlToFile(release.installerAsset.url, tempPath, result.errorMessage)) {
        return result;
    }
#else
    result.errorMessage = "In-app downloading is only implemented on Windows in this build.";
    return result;
#endif

    std::string expectedDigest = release.installerAsset.digest;
    if (!expectedDigest.empty()) {
        const std::string prefix = "sha256:";
        const std::string lowered = ToLowerAscii(expectedDigest);
        if (lowered.rfind(prefix, 0) == 0) {
            expectedDigest = lowered.substr(prefix.size());
        } else {
            expectedDigest = lowered;
        }
    } else if (!release.hashAsset.url.empty()) {
#if defined(_WIN32)
        const HttpResponse hashResponse = HttpGetText(release.hashAsset.url, {});
        if (hashResponse.ok) {
            TryExtractHashForAsset(hashResponse.body, release.installerAsset.name, expectedDigest);
        }
#endif
    }

    if (!expectedDigest.empty()) {
        result.verificationAvailable = true;
#if defined(_WIN32)
        std::string actualDigest;
        if (!ComputeFileSha256(tempPath, actualDigest) || actualDigest != ToLowerAscii(expectedDigest)) {
            std::filesystem::remove(tempPath, ec);
            result.errorMessage = "The downloaded update could not be verified.";
            return result;
        }
#endif
        result.verificationPassed = true;
        result.selectedDigest = expectedDigest;
    }

    std::filesystem::remove(finalPath, ec);
    std::filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        std::filesystem::copy_file(tempPath, finalPath, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tempPath, ec);
    }

    result.ok = true;
    result.downloadedFilePath = finalPath.string();
    return result;
}

} // namespace

const char* UpdateStateLabel(UpdateState state) {
    switch (state) {
    case UpdateState::Idle: return "Idle";
    case UpdateState::Checking: return "Checking";
    case UpdateState::UpToDate: return "Up to date";
    case UpdateState::UpdateAvailable: return "Update available";
    case UpdateState::Downloading: return "Downloading";
    case UpdateState::DownloadFailed: return "Download failed";
    case UpdateState::Downloaded: return "Downloaded";
    case UpdateState::Verifying: return "Verifying";
    case UpdateState::VerificationFailed: return "Verification failed";
    case UpdateState::ReadyToInstall: return "Ready to install";
    case UpdateState::Installing: return "Installing";
    case UpdateState::InstallFailed: return "Install failed";
    case UpdateState::RestartRequired: return "Restart required";
    case UpdateState::UpdateCheckFailed: return "Update check failed";
    }
    return "Unknown";
}

UpdateManager::UpdateManager(NotificationSink notificationSink, CloseRequest closeRequest)
    : m_NotificationSink(std::move(notificationSink))
    , m_CloseRequest(std::move(closeRequest)) {
    m_Snapshot.currentVersion = AppVersion::kVersionString;
    m_Snapshot.isInstalledBuild = AppPaths::IsInstalledBuild();
    m_ReleasePageUrl = AppVersion::kReleasesPageUrl;
    m_WebsiteDownloadPageUrl = AppVersion::kWebsiteDownloadPageUrl;
}

void UpdateManager::Initialize() {
    AppPaths::EnsureRuntimeDirectories();
    LoadPersistentState();
}

const Snapshot& UpdateManager::GetSnapshot() const {
    return m_Snapshot;
}

bool UpdateManager::CanCheckForUpdates() const {
    return m_Snapshot.state != UpdateState::Checking &&
           m_Snapshot.state != UpdateState::Downloading &&
           m_Snapshot.state != UpdateState::Verifying &&
           m_Snapshot.state != UpdateState::Installing;
}

bool UpdateManager::CanDownloadUpdate() const {
    return m_Snapshot.updateAvailable &&
           !m_SelectedAssetUrl.empty() &&
           m_Snapshot.state != UpdateState::Downloading &&
           m_Snapshot.state != UpdateState::Installing &&
           m_Snapshot.state != UpdateState::ReadyToInstall;
}

bool UpdateManager::CanInstallUpdate() const {
    return m_Snapshot.downloadReady && !m_Snapshot.downloadedFilePath.empty();
}

void UpdateManager::QueueNotification(
    UiNotificationSeverity severity,
    const std::string& message,
    const std::string& dedupeKey) const {
    if (m_NotificationSink) {
        m_NotificationSink(severity, message, dedupeKey);
    }
}

void UpdateManager::StartBackgroundCheck() {
    if (!CanCheckForUpdates()) {
        return;
    }

    const std::uint64_t now = GetUnixTimeNow();
    if (m_LastCheckUnixSeconds > 0 && now < m_LastCheckUnixSeconds + kBackgroundCheckIntervalSeconds) {
        return;
    }

    QueueCheck(false);
}

void UpdateManager::StartManualCheck() {
    if (!CanCheckForUpdates()) {
        return;
    }
    QueueCheck(true);
}

void UpdateManager::QueueCheck(bool manual) {
    const std::string previouslyDownloadedVersion = m_Snapshot.latestVersion;
    const std::string previouslyDownloadedPath = m_Snapshot.downloadedFilePath;
    const bool previouslyReadyToInstall = m_Snapshot.downloadReady;
    const bool previousVerificationAvailable = m_Snapshot.verificationAvailable;
    const bool previousVerificationPassed = m_Snapshot.verificationPassed;

    m_Snapshot.state = UpdateState::Checking;
    m_Snapshot.statusMessage = "Checking for updates...";

    Async::TaskSystem::Get().Submit([this,
                                     manual,
                                     previouslyDownloadedVersion,
                                     previouslyDownloadedPath,
                                     previouslyReadyToInstall,
                                     previousVerificationAvailable,
                                     previousVerificationPassed]() {
        CheckTaskResult taskResult;
        taskResult.release = FetchLatestRelease();
        taskResult.manual = manual;
        taskResult.checkedAt = GetUnixTimeNow();

        Async::TaskSystem::Get().PostToMain([this,
                                             taskResult,
                                             previouslyDownloadedVersion,
                                             previouslyDownloadedPath,
                                             previouslyReadyToInstall,
                                             previousVerificationAvailable,
                                             previousVerificationPassed]() {
            m_LastCheckUnixSeconds = taskResult.checkedAt;
            m_Snapshot.lastCheckDisplay = FormatLocalTimestamp(taskResult.checkedAt);

            if (!taskResult.release.ok) {
                m_Snapshot.state = UpdateState::UpdateCheckFailed;
                m_Snapshot.statusMessage = taskResult.release.errorMessage.empty()
                    ? "Could not check for updates. Check your internet connection and try again."
                    : taskResult.release.errorMessage;
                SavePersistentState();
                if (taskResult.manual) {
                    QueueNotification(UiNotificationSeverity::Error, m_Snapshot.statusMessage, "update-check-failed");
                }
                return;
            }

            m_ReleasePageUrl = taskResult.release.releasePageUrl.empty()
                ? std::string(AppVersion::kReleasesPageUrl)
                : taskResult.release.releasePageUrl;
            m_Snapshot.releaseName = taskResult.release.releaseName;
            m_Snapshot.releaseSummary = taskResult.release.releaseSummary;
            m_Snapshot.latestVersion = taskResult.release.latestVersion;
            m_Snapshot.selectedAssetName = taskResult.release.installerAsset.name;
            m_SelectedAssetUrl = taskResult.release.installerAsset.url;
            m_SelectedAssetDigest = taskResult.release.installerAsset.digest;
            m_SelectedHashAssetUrl = taskResult.release.hashAsset.url;

            if (!taskResult.release.updateAvailable) {
                m_Snapshot.state = UpdateState::UpToDate;
                m_Snapshot.updateAvailable = false;
                m_Snapshot.downloadReady = false;
                m_Snapshot.downloadedFilePath.clear();
                m_Snapshot.statusMessage = "Stack is up to date.";
                SavePersistentState();
                if (taskResult.manual) {
                    QueueNotification(UiNotificationSeverity::Success, m_Snapshot.statusMessage, "update-up-to-date");
                }
                return;
            }

            m_Snapshot.state = UpdateState::UpdateAvailable;
            m_Snapshot.updateAvailable = true;
            m_Snapshot.downloadReady =
                previouslyReadyToInstall &&
                previouslyDownloadedVersion == taskResult.release.latestVersion &&
                !previouslyDownloadedPath.empty();
            m_Snapshot.downloadedFilePath = m_Snapshot.downloadReady ? previouslyDownloadedPath : std::string();
            m_Snapshot.verificationAvailable = m_Snapshot.downloadReady ? previousVerificationAvailable : false;
            m_Snapshot.verificationPassed = m_Snapshot.downloadReady ? previousVerificationPassed : false;
            m_Snapshot.statusMessage = "Stack " + taskResult.release.latestVersion + " is available.";
            if (m_Snapshot.downloadReady) {
                m_Snapshot.state = m_Snapshot.verificationPassed ? UpdateState::ReadyToInstall : UpdateState::Downloaded;
                m_Snapshot.statusMessage = "The update was already downloaded and is ready to install.";
            }
            SavePersistentState();
            QueueNotification(UiNotificationSeverity::Info, m_Snapshot.statusMessage, "update-available");
        });
    });
}

void UpdateManager::DownloadUpdate() {
    if (!CanDownloadUpdate()) {
        return;
    }

    ReleaseInfo release;
    release.ok = true;
    release.updateAvailable = true;
    release.latestVersion = m_Snapshot.latestVersion;
    release.releaseName = m_Snapshot.releaseName;
    release.releaseSummary = m_Snapshot.releaseSummary;
    release.releasePageUrl = m_ReleasePageUrl;
    release.installerAsset = ReleaseAsset { m_Snapshot.selectedAssetName, m_SelectedAssetUrl, m_SelectedAssetDigest };
    release.hashAsset = ReleaseAsset { "SHA256SUMS.txt", m_SelectedHashAssetUrl, "" };

    m_Snapshot.state = UpdateState::Downloading;
    m_Snapshot.statusMessage = "Downloading update...";

    Async::TaskSystem::Get().Submit([this, release]() {
        const std::string releaseFolderName = release.latestVersion.empty() ? "latest" : release.latestVersion;
        DownloadTaskResult taskResult = DownloadReleaseInstaller(
            release,
            AppPaths::GetUpdateCacheDirectory() / releaseFolderName);

        Async::TaskSystem::Get().PostToMain([this, taskResult]() {
            if (!taskResult.ok) {
                m_Snapshot.state = UpdateState::DownloadFailed;
                m_Snapshot.downloadReady = false;
                m_Snapshot.statusMessage = taskResult.errorMessage.empty()
                    ? "The update download failed."
                    : taskResult.errorMessage;
                QueueNotification(UiNotificationSeverity::Error, m_Snapshot.statusMessage, "update-download-failed");
                return;
            }

            m_Snapshot.downloadedFilePath = taskResult.downloadedFilePath;
            m_Snapshot.verificationAvailable = taskResult.verificationAvailable;
            m_Snapshot.verificationPassed = taskResult.verificationPassed;
            m_Snapshot.downloadReady = true;
            m_Snapshot.state = taskResult.verificationPassed
                ? UpdateState::ReadyToInstall
                : UpdateState::Downloaded;
            m_Snapshot.statusMessage = taskResult.verificationPassed
                ? "The update was downloaded, verified, and is ready to install."
                : "The update was downloaded and is ready to install.";
            if (!taskResult.verificationAvailable) {
                m_Snapshot.statusMessage += " No published SHA-256 file was available for verification.";
            }
            SavePersistentState();
            QueueNotification(UiNotificationSeverity::Success, "The update was downloaded and is ready to install.", "update-downloaded");
        });
    });
}

bool UpdateManager::InstallAndRestart(std::string* errorMessage) {
    if (!CanInstallUpdate()) {
        if (errorMessage != nullptr) {
            *errorMessage = "No downloaded update is ready to install.";
        }
        return false;
    }

    m_Snapshot.state = UpdateState::Installing;
    m_Snapshot.statusMessage = "Stack needs permission to install the update.";

    if (!PlatformHelpers::LaunchElevatedInstaller(m_Snapshot.downloadedFilePath, L"", errorMessage)) {
        m_Snapshot.state = UpdateState::InstallFailed;
        m_Snapshot.statusMessage = errorMessage != nullptr && !errorMessage->empty()
            ? *errorMessage
            : "Stack could not install the update automatically. You can open the release page or website download page instead.";
        QueueNotification(UiNotificationSeverity::Error, m_Snapshot.statusMessage, "update-install-failed");
        return false;
    }

    SavePersistentState();
    if (m_CloseRequest) {
        m_CloseRequest();
    }
    return true;
}

bool UpdateManager::OpenReleasesPage(std::string* errorMessage) const {
    return PlatformHelpers::OpenUrl(m_ReleasePageUrl.empty() ? AppVersion::kReleasesPageUrl : m_ReleasePageUrl, errorMessage);
}

bool UpdateManager::OpenWebsiteDownloadPage(std::string* errorMessage) const {
    return PlatformHelpers::OpenUrl(m_WebsiteDownloadPageUrl, errorMessage);
}

bool UpdateManager::RevealDownloadedUpdate(std::string* errorMessage) const {
    if (m_Snapshot.downloadedFilePath.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "No downloaded update file is available yet.";
        }
        return false;
    }

    return PlatformHelpers::RevealPathInExplorer(m_Snapshot.downloadedFilePath, errorMessage);
}

void UpdateManager::SavePersistentState() const {
    json root = json::object();
    root["version"] = 1;
    root["lastCheckUnixSeconds"] = m_LastCheckUnixSeconds;
    root["releasePageUrl"] = m_ReleasePageUrl;
    root["websiteDownloadPageUrl"] = m_WebsiteDownloadPageUrl;
    root["selectedAssetUrl"] = m_SelectedAssetUrl;
    root["selectedAssetDigest"] = m_SelectedAssetDigest;
    root["selectedHashAssetUrl"] = m_SelectedHashAssetUrl;
    root["snapshot"] = {
        { "state", UpdateStateLabel(m_Snapshot.state) },
        { "currentVersion", m_Snapshot.currentVersion },
        { "latestVersion", m_Snapshot.latestVersion },
        { "releaseName", m_Snapshot.releaseName },
        { "releaseSummary", m_Snapshot.releaseSummary },
        { "statusMessage", m_Snapshot.statusMessage },
        { "lastCheckDisplay", m_Snapshot.lastCheckDisplay },
        { "selectedAssetName", m_Snapshot.selectedAssetName },
        { "downloadedFilePath", m_Snapshot.downloadedFilePath },
        { "isInstalledBuild", m_Snapshot.isInstalledBuild },
        { "updateAvailable", m_Snapshot.updateAvailable },
        { "downloadReady", m_Snapshot.downloadReady },
        { "verificationAvailable", m_Snapshot.verificationAvailable },
        { "verificationPassed", m_Snapshot.verificationPassed }
    };

    std::ofstream file(GetPersistentStatePath(), std::ios::trunc);
    if (file.is_open()) {
        file << root.dump(2) << '\n';
    }
}

void UpdateManager::LoadPersistentState() {
    m_Snapshot.currentVersion = AppVersion::kVersionString;
    m_Snapshot.isInstalledBuild = AppPaths::IsInstalledBuild();
    m_Snapshot.lastCheckDisplay = "Never";

    std::ifstream file(GetPersistentStatePath());
    if (!file.is_open()) {
        return;
    }

    json root = json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return;
    }

    m_LastCheckUnixSeconds = root.value("lastCheckUnixSeconds", std::uint64_t { 0 });
    m_ReleasePageUrl = root.value("releasePageUrl", std::string(AppVersion::kReleasesPageUrl));
    m_WebsiteDownloadPageUrl = root.value("websiteDownloadPageUrl", std::string(AppVersion::kWebsiteDownloadPageUrl));
    m_SelectedAssetUrl = root.value("selectedAssetUrl", std::string());
    m_SelectedAssetDigest = root.value("selectedAssetDigest", std::string());
    m_SelectedHashAssetUrl = root.value("selectedHashAssetUrl", std::string());
    m_Snapshot.lastCheckDisplay = FormatLocalTimestamp(m_LastCheckUnixSeconds);

    const json snapshot = root.value("snapshot", json::object());
    if (!snapshot.is_object()) {
        return;
    }

    m_Snapshot.latestVersion = snapshot.value("latestVersion", std::string());
    m_Snapshot.releaseName = snapshot.value("releaseName", std::string());
    m_Snapshot.releaseSummary = snapshot.value("releaseSummary", std::string());
    m_Snapshot.statusMessage = snapshot.value("statusMessage", std::string());
    m_Snapshot.selectedAssetName = snapshot.value("selectedAssetName", std::string());
    m_Snapshot.downloadedFilePath = snapshot.value("downloadedFilePath", std::string());
    m_Snapshot.updateAvailable = snapshot.value("updateAvailable", false);
    m_Snapshot.downloadReady = snapshot.value("downloadReady", false);
    m_Snapshot.verificationAvailable = snapshot.value("verificationAvailable", false);
    m_Snapshot.verificationPassed = snapshot.value("verificationPassed", false);

    std::error_code ec;
    if (m_Snapshot.downloadReady &&
        !m_Snapshot.downloadedFilePath.empty() &&
        std::filesystem::exists(m_Snapshot.downloadedFilePath, ec) &&
        !ec) {
        m_Snapshot.state = m_Snapshot.verificationPassed ? UpdateState::ReadyToInstall : UpdateState::Downloaded;
    } else {
        m_Snapshot.downloadReady = false;
        m_Snapshot.downloadedFilePath.clear();
        m_Snapshot.state = UpdateState::Idle;
    }
}

} // namespace AppUpdate
