#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketPresentation.h"

#include <utility>

void EditorModule::SetWorkspaceDetachedWindowsVisible(bool visible) {
    if (m_WorkspaceDetachedWindowsVisible == visible) return;
    m_WorkspaceDetachedWindowsVisible = visible;
    if (m_AutoBracketPresentation) m_AutoBracketPresentation->SetForeground(visible);
    if (!visible) {
        // A hidden workspace may wait several frames for its next budgeted
        // pump. Retire obsolete presentation demand at deactivation so it
        // cannot occupy the shared thumbnail lane ahead of the new view.
        CancelRawWorkspaceThumbnailDemand();
        m_Viewport.SuspendComparison();
        // These owners retain borrows from the current submitted frame.
        m_NodeOpenTransition.ReleaseSubmittedDraw();
        m_Viewport.FrameTransition().ReleaseSubmittedDraw();
        m_RestoreWorkspaceDetachedPreview = m_DetachedPreviewActive;
        m_RestoreWorkspaceNativeGallery =
            m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow;
        // The shell only routes native window events to the foreground
        // editor. Release its handles before another editor takes ownership.
        CloseDetachedPreviewFullscreen();
        if (m_RestoreWorkspaceNativeGallery) CloseRawWorkspaceLabNativeGallery();
        return;
    }
    if (std::exchange(m_RestoreWorkspaceDetachedPreview, false)) {
        ToggleDetachedPreviewFullscreen();
    }
    if (std::exchange(m_RestoreWorkspaceNativeGallery, false)) {
        OpenRawWorkspaceLabNativeGallery();
    }
}
