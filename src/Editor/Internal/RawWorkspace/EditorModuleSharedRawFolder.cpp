#include "Editor/EditorModule.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "Raw/RawSourceFolders.h"

void EditorModule::OpenGalleryOriginalInNewTab(const std::filesystem::path& root, const std::filesystem::path& original) {
    const auto generation = ++m_RawWorkspaceProjectLoadGeneration;
    m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Queued;
    m_RawWorkspaceProjectLoadStatusText = "Opening " + original.filename().u8string() + "...";
    const auto fail = [this, name = original.filename().u8string()](std::string reason) {
        m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Failed;
        m_RawWorkspaceProjectLoadStatusText = "Could not open " + name + ". " + reason;
        PostNotification(UiNotificationSeverity::Error, m_RawWorkspaceProjectLoadStatusText, "gallery-source-open");
    };
    if (!ProjectTasks().SubmitHighPriority("Opening Gallery original", [this, generation, root, original, fail] {
        Stack::RawWorkspace::SourceRecord source;
        std::string error;
        const auto cancelled = [this, generation] { return generation != m_RawWorkspaceProjectLoadGeneration.load(); };
        bool ready = false;
        try {
            ready = Stack::RawWorkspace::ReadGalleryOriginal(root, original, source, error, cancelled);
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        if (cancelled()) return;
        ProjectTasks().PostToMain([this, cancelled, fail, ready, source = std::move(source), error = std::move(error)]() mutable {
            if (cancelled()) return;
            if (!ready) { fail(std::move(error)); return; }
            m_RawWorkspaceProjectLoadTaskState = Async::TaskState::Idle;
            m_RawWorkspaceProjectLoadStatusText.clear();
            m_RawWorkspace.workspaceRoot = source.sourceWorkspaceRoot;
            const auto key = source.relativePathKey;
            const bool raster = Stack::RawWorkspace::IsGalleryRasterPath(source.absolutePath);
            m_RawWorkspace.sources.clear();
            m_RawWorkspace.sources.push_back(std::move(source));
            InvalidateRawWorkspaceGalleryPresentation();
            SelectRawWorkspaceSourceForGallery(key, false, false, true);
            if (!raster) RequestOpenRawLabTab();
        });
    })) fail("The image load could not be scheduled.");
}

void EditorModule::SetRawWorkspaceFolder(const std::filesystem::path& root) {
    m_RawGallerySourceFoldersOpen = false;
    if(m_PermanentGalleryWorkspace && m_GalleryNavigationMotion)
        m_GalleryNavigationMotion->Folders(false,m_GalleryBrowseAnchor);
    if(m_PermanentGalleryWorkspace && !m_RawWorkspace.allSourceFolders && !root.empty() &&
        !m_RawWorkspace.workspaceRoot.empty() && !Async::IsBusy(GetRawWorkspaceScanSnapshot().state)) {
        const auto relative=root.lexically_normal().lexically_relative(m_RawWorkspace.workspaceRoot.lexically_normal());
        if(!relative.empty() && !relative.is_absolute() && *relative.begin()!="..") {
            const auto filter=relative=="."?std::optional<std::string>{}:std::optional<std::string>{relative.generic_u8string()};
            if(filter==m_RawGalleryFolderFilter)return;
            m_RawGalleryFolderFilter=filter;
            m_RawWorkspace.selectedSourceKeys.clear();
            m_RawWorkspace.selectedSourceKey.clear();
            m_RawWorkspaceLabSelectedProjectPaths.clear();
            m_RawWorkspaceLabGalleryScrollTargetY=m_RawWorkspaceLabGalleryScrollCurrentY=0.f;
            m_RawWorkspaceLabFilmstripScrollTargetX=0.f;
            m_RawWorkspaceLabFilmstripScrollLastAppliedX=-1.f;
            m_RawWorkspaceGalleryLayoutAnimation.Reset();
            InvalidateRawWorkspaceGalleryPresentation(false);
            SaveRawWorkspaceAppState();
            return;
        }
    }
    if (!m_PermanentGalleryWorkspace && !m_RawWorkspace.allSourceFolders && root.lexically_normal() == m_RawWorkspace.workspaceRoot.lexically_normal()) return;
    if (root.empty()) ClearRawWorkspace();
    else RequestOpenRawWorkspace(root);
}

void EditorModule::SetWorkspaceAppStatePersistenceEnabled(bool enabled) {
    if (m_WorkspaceAppStatePersistenceEnabled == enabled) return;
    ResetRawWorkspaceAppStatePersistState();
    m_WorkspaceAppStatePersistenceEnabled = enabled;
    if (enabled) SaveRawWorkspaceAppState();
}
