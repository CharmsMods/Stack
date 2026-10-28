#include "Editor/EditorModule.h"

#include <algorithm>

EditorModule::RawWorkspacePresentationPreferences
EditorModule::GetRawWorkspacePresentationPreferences() const {
    RawWorkspacePresentationPreferences preferences;
    preferences.controlsPanelWidth = m_RawWorkspaceLayoutUi.controlsPanelWidth;
    preferences.rawLabToolRailWidth = m_RawWorkspaceLabUi.toolRailWidth;
    preferences.rawLabLowerShelfHeight = m_RawWorkspaceLabUi.lowerShelfHeight;
    preferences.rawLabFilmstripHeight = m_RawWorkspaceLabUi.filmstripHeight;
    preferences.rawLabGalleryThumbnailScale = m_RawWorkspaceLabUi.galleryThumbnailScale;
    preferences.rawLabLowerShelfOpen = m_RawWorkspaceLabUi.lowerShelfOpen;
    preferences.rawLabToolRailOnRight = m_RawWorkspaceLabUi.toolRailOnRight;
    // Node selection belongs to its document, never application preferences.
    preferences.rawLabActiveTool = static_cast<int>(
        m_RawWorkspaceLabUi.activeTool == RawLabTool::GraphNode
            ? RawLabTool::Zones : m_RawWorkspaceLabUi.activeTool);
    preferences.rawLabActivePointCurve = m_RawWorkspaceLabUi.activePointCurve;
    preferences.rawLabColorWarpLiveCloud = m_RawWorkspaceLabUi.colorWarpLiveCloud;
    preferences.viewSectionTab=m_ViewSectionTab;
    preferences.viewSectionOpen=m_ViewSectionOpen;
    preferences.rawLabLastGalleryHost = static_cast<int>(m_RawWorkspaceLabUi.lastGalleryHost);
    return preferences;
}

void EditorModule::ApplyRawWorkspacePresentationPreferences(
    const RawWorkspacePresentationPreferences& preferences, bool persist) {
    // Explicit scalar application only. In particular, do not copy galleryHost,
    // galleryWorkspaceOpen, category/display mode, folder scope, or interactions.
    m_RawWorkspaceLayoutUi.controlsPanelWidth = preferences.controlsPanelWidth;
    m_RawWorkspaceLabUi.toolRailWidth = preferences.rawLabToolRailWidth;
    m_RawWorkspaceLabUi.lowerShelfHeight = preferences.rawLabLowerShelfHeight;
    m_RawWorkspaceLabUi.filmstripHeight = preferences.rawLabFilmstripHeight;
    m_RawWorkspaceLabUi.galleryThumbnailScale = preferences.rawLabGalleryThumbnailScale;
    m_RawWorkspaceLabUi.lowerShelfOpen = preferences.rawLabLowerShelfOpen;
    m_RawWorkspaceLabUi.toolRailOnRight = preferences.rawLabToolRailOnRight;
    m_RawWorkspaceLabUi.activeTool = static_cast<RawLabTool>(preferences.rawLabActiveTool);
    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::GraphNode)
        m_RawWorkspaceLabUi.activeTool = RawLabTool::Zones;
    m_RawWorkspaceLabUi.lastCurvesTool =
        Stack::EditorModuleTypes::RawLabDrawerTool(m_RawWorkspaceLabUi.activeTool) == RawLabTool::Zones
            ? m_RawWorkspaceLabUi.activeTool : RawLabTool::Zones;
    m_RawWorkspaceLabUi.lastColorTool = m_RawWorkspaceLabUi.activeTool == RawLabTool::Calibration
        ? RawLabTool::Calibration : RawLabTool::Color;
    m_RawWorkspaceLabUi.activePointCurve = std::clamp(preferences.rawLabActivePointCurve, 0, 3);
    m_RawWorkspaceLabUi.colorWarpLiveCloud = preferences.rawLabColorWarpLiveCloud;
    m_ViewSectionTab=std::clamp(preferences.viewSectionTab,0,3);
    m_ViewSectionOpen=preferences.viewSectionOpen;
    m_RawWorkspaceLabUi.settingsTabActive=m_ViewSectionTab==2;
    m_RawAssetCapture.assetsTab=m_ViewSectionTab==3;
    m_RawWorkspaceLabUi.lastGalleryHost =
        preferences.rawLabLastGalleryHost == static_cast<int>(RawGalleryHost::NativeWindow)
            ? RawGalleryHost::NativeWindow : RawGalleryHost::Filmstrip;
    // Do not invoke the forwarding handler again. Only Gallery may queue this
    // debounced save; project initialization applies the same values in memory.
    if (persist) QueueRawWorkspaceAppStateSave();
}
