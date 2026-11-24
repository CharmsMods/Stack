#include "Editor/EditorModule.h"
#include "Editor/AutoBracket/AutoBracketCoordinator.h"
#include "App/AppHeaderStyle.h"
#include "Editor/Bracketing/BracketingSession.h"

using namespace Stack::Editor::RawLabInternal;

void EditorModule::AppendGalleryFileDependencies(std::vector<std::filesystem::path>& paths) const {
    paths.push_back(m_Project->storePath);
    paths.push_back(m_Project->adoptionSourcePath);
    if (!m_Project->rawRecipe.source.sourcePath.empty())
        paths.push_back(std::filesystem::u8path(m_Project->rawRecipe.source.sourcePath));
    if (m_Project->snapshot) {
        for (const auto& asset : m_Project->snapshot->embeddedAssets) {
            if (!asset.linkedSource) continue;
            if (!asset.originalSourcePath.empty()) paths.push_back(std::filesystem::u8path(asset.originalSourcePath));
            if (!asset.workspaceRelativeSourcePath.empty() && !m_RawWorkspace.workspaceRoot.empty())
                paths.push_back(m_RawWorkspace.workspaceRoot / std::filesystem::u8path(asset.workspaceRelativeSourcePath));
        }
    }
    if (m_Bracketing)
        for (const auto& source : m_Bracketing->sources) paths.push_back(source.path);
    if (m_AutoBracket)
        if (const auto work = m_AutoBracket->Current())
            for (const auto& source : work->item.candidate.sources) paths.push_back(source.path);
}

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
        if (Stack::RawWorkspace::GalleryProjectMatchesContentMode(project,
                Stack::RawWorkspace::GalleryContentMode::Bracket)) brackets.push_back(project);
    // Reuse the Brackets view's eligibility/deduplication, without resolving
    // project paths or touching disk just to display a count.
    AppendRawWorkspaceQueuedBrackets(brackets, false);
    tree.Prepare(m_RawWorkspace.workspaceRoot, all, brackets, m_RawWorkspaceNavigatorRevision,
        queueRevision, IsRawWorkspaceAllFolders(), GetRawWorkspaceSourceFolders());
}

void EditorModule::RenderRawGalleryFolderPanel() {
    if(IsRawWorkspaceAllFolders())SetRawWorkspaceAllFolders(false);
    PrepareRawGalleryFolderTree();
    const auto action = m_RawGalleryNavigator.Render(m_RawGalleryFolderTree,
        m_RawWorkspaceGalleryContentMode, m_RawGalleryFolderFilter,
        Stack::Header::ResolvePanelColor(GetWorkspaceBaseColor()),
        [this] { RenderRawGallerySidebarControls(); }, m_RawGalleryFileTypes);
    if (action.fileTypes && *action.fileTypes != m_RawGalleryFileTypes) {
        m_RawGalleryFileTypes = *action.fileTypes;
        m_RawWorkspace.selectedSourceKeys.clear();
        m_RawWorkspaceLabSelectedProjectPaths.clear();
        InvalidateRawWorkspaceGalleryPresentation(false);
        m_RawWorkspaceLabVisibleGalleryContentRevision = std::numeric_limits<std::uint64_t>::max();
        m_RawWorkspaceLabGalleryScrollTargetY = 0.f;
        m_RawWorkspaceLabGalleryScrollCurrentY = 0.f;
        m_RawWorkspaceLabFilmstripScrollTargetX = 0.f;
        m_RawWorkspaceGalleryGridStacks.gridPosition = {};
        m_RawWorkspaceGalleryGridStacks.filmstripPosition = {};
        m_RawWorkspaceGalleryGridStacks.Close();
        m_RawWorkspaceGalleryLayoutAnimation.Reset();
    }
    auto requestedPath = action.openPath;
    if (!action.fileChanges.empty() && m_PermanentGalleryWorkspace) {
        using namespace Stack::BrowserFiles;
        const auto root = m_RawWorkspace.workspaceRoot;
        const auto visible = m_RawGalleryFolderFilter
            ? root / std::filesystem::u8path(*m_RawGalleryFolderFilter) : root;
        if (!requestedPath && !visible.empty()) {
            auto remapped = RemapLocation(visible, action.fileChanges);
            if (remapped.native() != visible.native()) requestedPath = std::move(remapped);
        }
        bool registrationChanged = false;
        const auto remapFolders = [&](auto& folders) {
            for (auto& folder : folders) {
                auto path = RemapLocation(folder, action.fileChanges);
                registrationChanged |= path.native() != folder.native();
                folder = std::move(path);
            }
        };
        remapFolders(m_RawWorkspace.registeredSourceFolders);
        remapFolders(m_RawWorkspace.recentWorkspaceRoots);
        if (registrationChanged) {
            ++m_RawGalleryFolderCardRevision;
            SaveRawWorkspaceAppState();
        }
        if (!requestedPath && std::any_of(action.fileChanges.begin(), action.fileChanges.end(),
                [&](const auto& change) { return AffectsLocation(change, root); })) {
            {
                std::lock_guard lock(m_RawWorkspaceScanMutex);
                m_RawWorkspaceScanSnapshot.generation = ++m_RawWorkspaceScanGeneration;
                m_RawWorkspaceScanSnapshot.state = Async::TaskState::Idle;
            }
            ++m_RawWorkspaceGalleryGridStacks.navigationRevision;
            InvalidateRawWorkspaceGalleryPresentation();
            RefreshRawGalleryAfterFileMove(root);
        }
    }
    if(requestedPath)RequestOpenRawWorkspace(*requestedPath);
    if(action.openImage && m_RawGalleryOpenSelectionHandler) {
        RawGalleryOpenSelection selection;
        selection.items.push_back({*action.openImage,false,action.openImage->parent_path()});
        m_RawGalleryOpenSelectionHandler(std::move(selection),false);
    }
    if(action.browseMaximum.x>action.browseMinimum.x)
        m_GalleryBrowseAnchor={action.browseMinimum,action.browseMaximum};
    if (action.category || action.folderChanged) {
        m_RawGallerySourceFoldersOpen = false;
        if(m_GalleryNavigationMotion)m_GalleryNavigationMotion->Folders(false,m_GalleryBrowseAnchor);
    }
    if ((action.category && *action.category != m_RawWorkspaceGalleryContentMode) ||
        (action.folderChanged && action.folder != m_RawGalleryFolderFilter)) {
        m_RawWorkspace.selectedSourceKeys.clear();
        m_RawWorkspaceLabSelectedProjectPaths.clear();
        InvalidateRawWorkspaceGalleryPresentation(false);
    }
    if (action.category && *action.category != m_RawWorkspaceGalleryContentMode) {
        m_RawWorkspaceGalleryContentMode = *action.category;
        SaveRawWorkspaceAppState();
    }
    if (action.folderChanged && action.folder != m_RawGalleryFolderFilter) {
        m_RawGalleryFolderFilter = action.folder;
        m_RawWorkspaceLabGalleryScrollTargetY = 0.f;
        m_RawWorkspaceLabGalleryScrollCurrentY = 0.f;
        m_RawWorkspaceLabFilmstripScrollTargetX = 0.f;
        m_RawWorkspaceGalleryLayoutAnimation.Reset();
    }
    if(action.openFolder)OpenRawWorkspaceFolderDialog();
}
