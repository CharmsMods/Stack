#pragma once

#include "Utils/UiNotifications.h"

#include <cstdint>
#include <functional>
#include <string>

namespace AppUpdate {

enum class UpdateState {
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    DownloadFailed,
    Downloaded,
    Verifying,
    VerificationFailed,
    ReadyToInstall,
    Installing,
    InstallFailed,
    RestartRequired,
    UpdateCheckFailed
};

struct Snapshot {
    UpdateState state = UpdateState::Idle;
    std::string currentVersion;
    std::string latestVersion;
    std::string releaseName;
    std::string releaseSummary;
    std::string statusMessage;
    std::string lastCheckDisplay;
    std::string selectedAssetName;
    std::string downloadedFilePath;
    bool isInstalledBuild = false;
    bool updateAvailable = false;
    bool downloadReady = false;
    bool verificationAvailable = false;
    bool verificationPassed = false;
};

class UpdateManager {
public:
    using NotificationSink = std::function<void(UiNotificationSeverity, const std::string&, const std::string&)>;
    using CloseRequest = std::function<void()>;

    UpdateManager(NotificationSink notificationSink = {}, CloseRequest closeRequest = {});

    void Initialize();

    const Snapshot& GetSnapshot() const;
    bool CanCheckForUpdates() const;
    bool CanDownloadUpdate() const;
    bool CanInstallUpdate() const;

    void StartBackgroundCheck();
    void StartManualCheck();
    void DownloadUpdate();
    bool InstallAndRestart(std::string* errorMessage = nullptr);

    bool OpenReleasesPage(std::string* errorMessage = nullptr) const;
    bool OpenWebsiteDownloadPage(std::string* errorMessage = nullptr) const;
    bool RevealDownloadedUpdate(std::string* errorMessage = nullptr) const;

private:
    void SavePersistentState() const;
    void LoadPersistentState();
    void QueueCheck(bool manual);
    void QueueNotification(UiNotificationSeverity severity, const std::string& message, const std::string& dedupeKey) const;

    Snapshot m_Snapshot;
    NotificationSink m_NotificationSink;
    CloseRequest m_CloseRequest;
    std::uint64_t m_LastCheckUnixSeconds = 0;
    std::string m_ReleasePageUrl;
    std::string m_WebsiteDownloadPageUrl;
    std::string m_SelectedAssetUrl;
    std::string m_SelectedAssetDigest;
    std::string m_SelectedHashAssetUrl;
};

const char* UpdateStateLabel(UpdateState state);

} // namespace AppUpdate
