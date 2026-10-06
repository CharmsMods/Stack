#include "Editor/EditorModule.h"

namespace {
// Copy presentation choices only. GPU caches and in-flight work remain owned
// by the live UI and must never be copied or rolled back after a preview.
void CopyLayout(Stack::EditorModuleTypes::RawWorkspaceLabUiState& to,
                const Stack::EditorModuleTypes::RawWorkspaceLabUiState& from) {
#define COPY(field) to.field = from.field
    COPY(activeTool); COPY(lastCurvesTool); COPY(lastColorTool);
    COPY(galleryHost); COPY(lastGalleryHost);
    COPY(galleryNavigationMode); COPY(galleryProjectId);
    COPY(toolRailWidth); COPY(toolRailWidthUserAdjusted); COPY(toolRailOnRight);
    COPY(lowerShelfHeight); COPY(filmstripHeight); COPY(galleryThumbnailScale);
    COPY(lowerShelfOpen); COPY(gradingScopesShowInput); COPY(settingsTabActive);
    COPY(previewActualPixels); COPY(previewZoom); COPY(previewZoomTarget);
    COPY(previewZoomAnimating); COPY(previewZoomFocusScreen); COPY(previewZoomFocusUv);
    COPY(previewPanX); COPY(previewPanY); COPY(previewViewSourceKey);
    COPY(previewPanning);
    COPY(previewLayout);
    COPY(activePointCurve); COPY(selectedZonePoint); COPY(selectedTonePoint);
    COPY(selectedColorWarpPin); COPY(selectedColorWarpGroupId); COPY(colorWarpLiveCloud);
#undef COPY
}
}
void EditorModule::BeginWorkspacePreview() {
    if (m_WorkspacePreviewUi) return;
    EnsureRawWorkspaceLoaded();
    m_WorkspacePreviewUi.emplace();
    CopyLayout(*m_WorkspacePreviewUi, m_RawWorkspaceLabUi);
    m_WorkspacePreviewPaneWidth = m_LeftPaneWidth;
    m_WorkspacePreviewActiveSubWindow = m_ActiveSubWindow;
    m_WorkspacePreviewTargetSubWindow = m_TargetSubWindow;
}
void EditorModule::RestoreWorkspacePreviewLayout() {
    if (!m_WorkspacePreviewUi) return;
    CopyLayout(m_RawWorkspaceLabUi, *m_WorkspacePreviewUi);
    m_LeftPaneWidth = m_WorkspacePreviewPaneWidth;
    m_ActiveSubWindow = m_WorkspacePreviewActiveSubWindow;
    m_TargetSubWindow = m_WorkspacePreviewTargetSubWindow;
}
void EditorModule::EndWorkspacePreview() {
    RestoreWorkspacePreviewLayout();
    m_WorkspacePreviewUi.reset();
}
