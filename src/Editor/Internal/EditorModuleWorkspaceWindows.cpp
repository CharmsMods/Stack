#include "Editor/EditorModule.h"

#include <utility>

void EditorModule::SetWorkspaceDetachedWindowsVisible(bool visible) {
    if (m_WorkspaceDetachedWindowsVisible == visible) return;
    m_WorkspaceDetachedWindowsVisible = visible;
    if (!visible) {
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
