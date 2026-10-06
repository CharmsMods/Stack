#include "Editor/EditorModule.h"
#include "App/AppPaths.h"
#include "Utils/DisplayRefreshRate.h"
#include <imgui.h>

Raw::ViewportPreferences EditorModule::GetRawViewportPreferences() const {
    if (!m_RawViewportPreferences) {
        m_RawViewportPreferences = Raw::ViewportPreferencesStore::Open(AppPaths::GetSettingsDirectory());
    }
    return m_RawViewportPreferences->Read();
}
void EditorModule::SetRawViewportPreferences(Raw::ViewportPreferences preferences) {
    (void)GetRawViewportPreferences();
    m_RawViewportPreferences->Set(preferences);
    SyncRawViewportPreferences();
}
void EditorModule::SyncRawViewportPreferences() {
    const auto preferences = GetRawViewportPreferences();
    const int fps = GetRawViewportTargetFps();
    const auto revision = m_RawViewportPreferences->Revision();
    if (revision == m_RawViewportPreferencesRevision && fps == m_RawViewportEffectiveFps) return;
    m_RawViewportPreferencesRevision = revision;
    m_RawViewportEffectiveFps = fps;
    CancelRawViewportCalibration();
    if (!preferences.smoothUpdates) ClearRawViewportTransition();
    m_RawWorkspacePreviewHealthyStreak = m_RawWorkspacePreviewSlowSamples = m_RawWorkspacePreviewScaleCooldown = 0;
    const int edge = GetCalibratedRawViewportEdge();
    if (edge > 0) m_RawWorkspaceAdaptivePreviewScale = float(edge) / std::max(1,m_RawWorkspacePhysicalViewportMaxDimension);
}
void EditorModule::SetRawViewportTargetFps(int fps) {
    auto preferences = GetRawViewportPreferences();
    preferences.targetFps = Raw::ClampViewportTargetFps(fps);
    SetRawViewportPreferences(preferences);
}
int EditorModule::GetRawViewportMaximumFps() const {
    return std::max(5,m_RawViewportDisplayRefreshRate);
}
int EditorModule::GetRawViewportRequestedFps() const { return GetRawViewportPreferences().targetFps; }
int EditorModule::GetRawViewportTargetFps() const {
    return Raw::ResolveViewportTargetFps(GetRawViewportRequestedFps(),GetRawViewportMaximumFps());
}
bool EditorModule::GetSmoothRawViewportUpdates() const { return GetRawViewportPreferences().smoothUpdates; }
void EditorModule::SetSmoothRawViewportUpdates(bool enabled) {
    auto preferences = GetRawViewportPreferences();
    preferences.smoothUpdates = enabled;
    SetRawViewportPreferences(preferences);
}
int EditorModule::GetRawViewportFadeBelowFps() const {
    return Raw::ResolveViewportTargetFps(GetRawViewportPreferences().fadeBelowFps,GetRawViewportMaximumFps());
}
void EditorModule::SetRawViewportFadeBelowFps(int fps) {
    auto preferences = GetRawViewportPreferences();
    preferences.fadeBelowFps = Raw::ClampViewportFadeBelowFps(fps);
    SetRawViewportPreferences(preferences);
    ClearRawViewportTransition();
}
void EditorModule::FinishRawViewportTargetFpsEdit() { SyncRawViewportPreferences(); }
