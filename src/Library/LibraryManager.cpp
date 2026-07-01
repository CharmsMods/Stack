#include "LibraryManager.h"

#include "App/AppPaths.h"
#include "Renderer/GLHelpers.h"

#include <filesystem>
#include <utility>

LibraryManager::LibraryManager() {
    AppPaths::EnsureRuntimeDirectories();
    m_LibraryPath = AppPaths::GetLibraryDirectory();
    m_AssetsPath = m_LibraryPath / "Assets";
    if (!std::filesystem::exists(m_LibraryPath)) {
        std::filesystem::create_directories(m_LibraryPath);
    }
    if (!std::filesystem::exists(m_AssetsPath)) {
        std::filesystem::create_directories(m_AssetsPath);
    }
}

LibraryManager::~LibraryManager() {
    for (auto& project : m_Projects) {
        ReleaseProjectTextures(project);
    }
    for (auto& asset : m_Assets) {
        ReleaseAssetTextures(asset);
    }
}

void LibraryManager::ProcessDeferredDeletions() {
    if (m_DeferredTextureDeletions.empty()) return;
    glDeleteTextures(static_cast<GLsizei>(m_DeferredTextureDeletions.size()), m_DeferredTextureDeletions.data());
    m_DeferredTextureDeletions.clear();
}

bool LibraryManager::ConsumeSavedProjectEvent(std::string& outFileName, std::string& outProjectKind) {
    if (m_PendingSavedProjectFileName.empty()) {
        outFileName.clear();
        outProjectKind.clear();
        return false;
    }

    outFileName = m_PendingSavedProjectFileName;
    outProjectKind = m_PendingSavedProjectKind;
    m_PendingSavedProjectFileName.clear();
    m_PendingSavedProjectKind.clear();
    return true;
}

bool LibraryManager::ConsumeUiNotification(UiNotificationEvent& outEvent) {
    if (m_UiNotifications.empty()) {
        return false;
    }
    outEvent = std::move(m_UiNotifications.front());
    m_UiNotifications.pop_front();
    return true;
}

void LibraryManager::QueueSavedProjectEvent(const std::string& fileName, const std::string& projectKind) {
    m_PendingSavedProjectFileName = fileName;
    m_PendingSavedProjectKind = projectKind;
}

void LibraryManager::QueueUiNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    if (message.empty()) {
        return;
    }
    m_UiNotifications.push_back(UiNotificationEvent{
        severity,
        std::move(message),
        std::move(dedupeKey)
    });
}
