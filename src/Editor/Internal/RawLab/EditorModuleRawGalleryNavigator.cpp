#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketCoordinator.h"
#include "App/AppHeaderStyle.h"

using namespace Stack::Editor::RawLabInternal;

void EditorModule::SynchronizeRawGalleryFolderScope() {
    if (m_RawGalleryFolderWorkspaceRoot == m_RawWorkspace.workspaceRoot &&
        m_RawGalleryFolderTree.workspaceKey == GetRawWorkspaceGalleryScopeKey()) return;
    m_RawGalleryFolderWorkspaceRoot = m_RawWorkspace.workspaceRoot;
    m_RawGalleryFolderFilter.reset();
    m_RawGalleryFilteredRevision = std::numeric_limits<std::uint64_t>::max();
    m_RawGalleryFolderTree.revision = std::numeric_limits<std::uint64_t>::max();
}

void EditorModule::PrepareRawGalleryFolderTree() {
    SynchronizeRawGalleryFolderScope();
    auto& tree = m_RawGalleryFolderTree;
    const auto queueRevision = m_AutoBracket ? m_AutoBracket->Revision() : 0;
    if (tree.serial && tree.revision == m_RawWorkspaceNavigatorRevision &&
        tree.queueRevision == queueRevision) return;
    const auto& all = GetRawWorkspaceGalleryPresentation();
    std::vector<Stack::RawWorkspace::GalleryProjectView> brackets;
    for (const auto& project : all.projects)
        if (project.bracketingProject) brackets.push_back(project);
    // Reuse the Brackets view's eligibility/deduplication, without resolving
    // project paths or touching disk just to display a count.
    AppendRawWorkspaceQueuedBrackets(brackets, false);
    tree.Prepare(m_RawWorkspace.workspaceRoot, all, brackets, m_RawWorkspaceNavigatorRevision,
        queueRevision, IsRawWorkspaceAllFolders(), GetRawWorkspaceSourceFolders());
}

void EditorModule::RenderRawGalleryFolderPanel() {
    PrepareRawGalleryFolderTree();
    const auto action = m_RawGalleryNavigator.Render(m_RawGalleryFolderTree,
        m_RawWorkspaceGalleryContentMode, m_RawGalleryFolderFilter,
        Stack::Header::ResolvePanelColor(GetWorkspaceBaseColor()),
        [this] { RenderRawGallerySidebarControls(); });
    if (action.category || action.folderChanged) m_RawGallerySourceFoldersOpen = false;
    if (action.category && *action.category != m_RawWorkspaceGalleryContentMode) {
        m_RawWorkspaceGalleryContentMode = *action.category;
        SaveRawWorkspaceAppState();
    }
    if (action.folderChanged && action.folder != m_RawGalleryFolderFilter) {
        m_RawGalleryFolderFilter = action.folder;
        InvalidateRawWorkspaceGalleryPresentation(false);
        m_RawWorkspaceLabGalleryScrollTargetY = 0.f;
        m_RawWorkspaceLabGalleryScrollCurrentY = 0.f;
        m_RawWorkspaceLabFilmstripScrollTargetX = 0.f;
        m_RawWorkspaceGalleryLayoutAnimation.Reset();
    }
    if (action.openFolder) {
        if (m_PermanentGalleryWorkspace) ShowRawWorkspaceSourceFolders();
        else OpenRawWorkspaceFolderDialog();
    }
}
