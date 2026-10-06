#include "Editor/EditorModule.h"

#include <algorithm>
#include <unordered_set>

const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
EditorModule::ResolveRawWorkspaceVisibleGalleryStacks() {
    const auto& resolved = ResolveRawWorkspaceFilmstripStacks();
    if (m_RawWorkspaceLabVisibleGalleryWorkspaceKey == m_RawWorkspaceFilmstripStacksCacheWorkspaceKey &&
        m_RawWorkspaceLabVisibleGalleryRevision == m_RawWorkspaceGalleryRevision &&
        m_RawWorkspaceLabVisibleGallerySimilarityGeneration == m_RawWorkspaceSimilarityPublishedGeneration &&
        m_RawWorkspaceLabVisibleGalleryOrganizationRevision == m_RawWorkspaceFilmstripOrganizationRevision) {
        return m_RawWorkspaceLabVisibleGalleryStacksCache;
    }
    const auto& presentation = GetRawWorkspaceGalleryPresentation();
    const auto folderVisible = [&](const std::string& key) {
        if (!m_PermanentGalleryWorkspace || !m_RawGalleryFolderFilter) return true;
        const auto& folder = *m_RawGalleryFolderFilter;
        return key == folder || (key.size() > folder.size() &&
            key.compare(0, folder.size(), folder) == 0 && key[folder.size()] == '/');
    };
    auto& views = m_RawWorkspaceLabGalleryViewsBySource;
    views.clear();
    views.reserve(static_cast<std::size_t>(presentation.totalSources));
    for (const auto& group : presentation.groups) {
        if (!folderVisible(group.folderKey)) continue;
        for (const auto& view : group.sources) views.emplace(view.relativePathKey, &view);
    }
    auto& stacks = m_RawWorkspaceLabVisibleGalleryStacksCache;
    stacks = resolved;
    std::unordered_set<std::string> assigned;
    assigned.reserve(views.size());
    stacks.erase(std::remove_if(stacks.begin(), stacks.end(), [&](auto& stack) {
        stack.sourceKeys.erase(std::remove_if(stack.sourceKeys.begin(), stack.sourceKeys.end(),
            [&](const auto& key) {
                return views.find(key) == views.end() || !assigned.insert(key).second;
            }), stack.sourceKeys.end());
        return stack.sourceKeys.empty();
    }), stacks.end());
    for (const auto& group : presentation.groups) {
        if (!folderVisible(group.folderKey)) continue;
        for (const auto& view : group.sources) {
            if (!assigned.insert(view.relativePathKey).second) continue;
            Stack::RawWorkspace::RawGallerySimilarityStack stack;
            stack.folderKey = group.folderKey;
            stack.anchorCatalogIndex = view.sourceIndex;
            stack.sourceKeys.push_back(view.relativePathKey);
            stacks.push_back(std::move(stack));
        }
    }
    m_RawWorkspaceLabVisibleGalleryWorkspaceKey = m_RawWorkspaceFilmstripStacksCacheWorkspaceKey;
    m_RawWorkspaceLabVisibleGalleryRevision = m_RawWorkspaceGalleryRevision;
    m_RawWorkspaceLabVisibleGallerySimilarityGeneration = m_RawWorkspaceSimilarityPublishedGeneration;
    m_RawWorkspaceLabVisibleGalleryOrganizationRevision = m_RawWorkspaceFilmstripOrganizationRevision;
    m_RawWorkspaceLabFilmstripStackTimelineActive = false;
    m_RawWorkspaceGalleryGridStacks.Reconcile(m_RawWorkspaceLabVisibleGalleryWorkspaceKey, stacks);
    return stacks;
}

void EditorModule::OpenRawWorkspaceGalleryStackInFilmstrip(const std::string& sourceKey) {
    m_RawWorkspaceGalleryLayoutAnimation.PrepareSwitch(sourceKey);
    // Focus must also work when a clicked cover is still loading and has no
    // rendered image for the layout animation to use as an anchor.
    m_RawLabGalleryScrollToSourceKey = sourceKey;
    m_RawWorkspaceLabUi.galleryWorkspaceGrid = false;
    m_RawWorkspaceLabFilmstripDrawerState = {};
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    m_RawWorkspaceGalleryDrawerNeedsInitialExpansion = true;
    m_RawWorkspaceGalleryPreviewHovered = false;
    m_RawWorkspaceLabFilmstripHoverSourceKey = sourceKey;
    m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripHoverFrame = ImGui::GetFrameCount();
    m_RawWorkspaceLabFilmstripHoverSuppressed = false;
    m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
}
