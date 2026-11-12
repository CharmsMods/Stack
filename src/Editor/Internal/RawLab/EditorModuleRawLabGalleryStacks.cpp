#include "Editor/EditorModule.h"
#include "Raw/RawGalleryCoverCache.h"
#include "Raw/GalleryDateLabels.h"
#include "Editor/Internal/RawLab/RawLabGalleryFolderTree.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
EditorModule::ResolveRawWorkspaceVisibleGalleryStacks() {
    const auto& resolved = ResolveRawWorkspaceFilmstripStacks();
    const auto calendar=Stack::RawWorkspace::GalleryCalendarRevision();
    const bool dateChanged=calendar!=m_RawGalleryCalendarRevision;
    m_RawGalleryCalendarRevision=calendar;
    if (!dateChanged && m_RawWorkspaceLabVisibleGalleryWorkspaceKey == m_RawWorkspaceFilmstripStacksCacheWorkspaceKey &&
        m_RawWorkspaceLabVisibleGalleryRevision == m_RawWorkspaceGalleryRevision &&
        m_RawWorkspaceLabVisibleGalleryOrganizationRevision == m_RawWorkspaceFilmstripOrganizationRevision) {
        return m_RawWorkspaceLabVisibleGalleryStacksCache;
    }
    const auto& presentation = GetRawWorkspaceGalleryPresentation();
    const auto folderFilter = m_PermanentGalleryWorkspace
        ? m_RawGalleryFolderFilter : std::optional<std::string>{};
    const bool orderCurrent = !dateChanged &&
        m_RawWorkspaceLabVisibleGalleryWorkspaceKey == m_RawWorkspaceFilmstripStacksCacheWorkspaceKey &&
        m_RawWorkspaceLabVisibleGalleryContentRevision == m_RawWorkspaceNavigatorRevision &&
        m_RawWorkspaceLabVisibleGalleryOrganizationRevision == m_RawWorkspaceFilmstripOrganizationRevision &&
        m_RawWorkspaceLabVisibleGalleryFolderFilter == folderFilter;
    const auto folderVisible = [&](const std::string& key) {
        if(m_PermanentGalleryWorkspace && !m_RawGalleryIncludeSubfolders) {
            const auto selected=m_RawGalleryFolderFilter.value_or("");
            if(key!=selected && !(selected.empty() && key=="."))return false;
        }
        if (!m_PermanentGalleryWorkspace || !m_RawGalleryFolderFilter) return true;
        const auto& folder = *m_RawGalleryFolderFilter;
        return Stack::Editor::RawLabInternal::GalleryFolderTree::ContainsFolderKey(folder, key);
    };
    auto& views = m_RawWorkspaceLabGalleryViewsBySource;
    views.clear();
    views.reserve(static_cast<std::size_t>(presentation.totalSources));
    for (const auto& group : presentation.groups) {
        if (!folderVisible(group.folderKey)) continue;
        for (const auto& view : group.sources)
            if (!m_PermanentGalleryWorkspace || Stack::Editor::RawLabInternal::GalleryFileTypeMatches(
                    view.relativePathKey, m_RawGalleryFileTypes))
                views.emplace(view.relativePathKey, &view);
    }
    const auto updateCompletion = [&](auto& stacks) {
        std::unordered_set<std::string> completed;
        for (const auto& set : presentation.completedBurstSourceSets)
            completed.insert(set.sourceContentKey);
        std::unordered_map<std::string, std::string> contents;
        for (const auto& source : m_RawWorkspace.sources) if (!source.fingerprint.empty())
            contents.emplace(source.relativePathKey, source.fingerprint + ':' + std::to_string(source.fileSizeBytes));
        std::unordered_map<std::string, bool> originalCompletion;
        for (const auto& original : resolved) {
            std::vector<std::string> identities;
            bool known = true;
            for (const auto& key : original.sourceKeys) {
                const auto source = contents.find(key);
                if (source == contents.end()) { known = false; break; }
                identities.push_back(source->second);
            }
            const bool finished = known && completed.count(
                Stack::RawWorkspace::BuildGallerySourceIdentityKey(std::move(identities))) != 0;
            for (const auto& key : original.sourceKeys) originalCompletion.emplace(key, finished);
        }
        for (auto& stack : stacks) {
            // Folder filtering can expose only part of a manual stack. Its
            // completion always describes the full original grouping.
            const auto original = stack.sourceKeys.empty() ? originalCompletion.end() :
                originalCompletion.find(stack.sourceKeys.front());
            stack.hasSavedBracketingResult = original != originalCompletion.end() && original->second;
        }
    };
    // Selection/thumbnail changes replace borrowed presentation views without
    // changing the order or regrouping/sorting the source catalog.
    if (orderCurrent) {
        m_RawWorkspaceLabVisibleGalleryRevision = m_RawWorkspaceGalleryRevision;
        return m_RawWorkspaceLabVisibleGalleryStacksCache;
    }
    auto& stacks = m_RawWorkspaceLabVisibleGalleryStacksCache;
    m_RawFilmstripHeight.first = -1;
    stacks = resolved;
    std::unordered_set<std::string> assigned;
    assigned.reserve(views.size());
    bool anchorChanged = false;
    stacks.erase(std::remove_if(stacks.begin(), stacks.end(), [&](auto& stack) {
        const auto anchor = stack.sourceKeys.empty() ? std::string{} : stack.sourceKeys.front();
        stack.sourceKeys.erase(std::remove_if(stack.sourceKeys.begin(), stack.sourceKeys.end(),
            [&](const auto& key) {
                return views.find(key) == views.end() || !assigned.insert(key).second;
            }), stack.sourceKeys.end());
        if (!stack.sourceKeys.empty() && stack.sourceKeys.front() != anchor) {
            const auto index = views.at(stack.sourceKeys.front())->sourceIndex;
            stack.anchorCatalogIndex = index;
            stack.folderKey = m_RawWorkspace.sources[index].parentFolderKey;
            anchorChanged = true;
        }
        return stack.sourceKeys.empty();
    }), stacks.end());
    for (const auto& group : presentation.groups) {
        if (!folderVisible(group.folderKey)) continue;
        for (const auto& view : group.sources) {
            if (views.find(view.relativePathKey) == views.end()) continue;
            if (!assigned.insert(view.relativePathKey).second) continue;
            Stack::RawWorkspace::RawGallerySimilarityStack stack;
            stack.folderKey = group.folderKey;
            stack.anchorCatalogIndex = view.sourceIndex;
            stack.sourceKeys.push_back(view.relativePathKey);
            stacks.push_back(std::move(stack));
        }
    }
    const auto organization = m_RawWorkspaceManualGroupings.find(
        m_RawWorkspaceFilmstripStacksCacheWorkspaceKey);
    const Stack::RawWorkspace::RawGalleryManualGrouping defaultOrganization;
    const auto& ordering = organization == m_RawWorkspaceManualGroupings.end()
        ? defaultOrganization : organization->second;
    // A manual stack can span folders. If filtering removes its anchor, sort
    // the surviving stack by its new first member for both views.
    if (anchorChanged) stacks = Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
        std::move(stacks), m_RawWorkspaceFilmstripSortSources, ordering);
    updateCompletion(stacks);
    m_RawWorkspaceLabVisibleGalleryWorkspaceKey = m_RawWorkspaceFilmstripStacksCacheWorkspaceKey;
    m_RawWorkspaceLabVisibleGalleryRevision = m_RawWorkspaceGalleryRevision;
    m_RawWorkspaceLabVisibleGalleryContentRevision = m_RawWorkspaceNavigatorRevision;
    m_RawWorkspaceLabVisibleGalleryFolderFilter = folderFilter;
    m_RawWorkspaceLabVisibleGalleryOrganizationRevision = m_RawWorkspaceFilmstripOrganizationRevision;
    m_RawWorkspaceLabVisibleGallerySections = Stack::RawWorkspace::BuildRawGalleryStackSections(
        stacks, ordering.sortMode, m_RawWorkspaceFilmstripSortSources);
    m_RawWorkspaceLabFilmstripStackTimelineActive = false;
    m_RawWorkspaceGalleryGridStacks.Reconcile(m_RawWorkspaceLabVisibleGalleryWorkspaceKey, stacks);
    return stacks;
}

const std::vector<Stack::RawWorkspace::GalleryProjectStack>&
EditorModule::ResolveRawWorkspaceGalleryProjectStacks() {
    const auto& presentation = GetRawWorkspacePanelGalleryPresentation();
    const auto folder = m_PermanentGalleryWorkspace ? m_RawGalleryFolderFilter : std::optional<std::string>{};
    if (m_RawWorkspaceGalleryProjectStacksRevision == m_RawWorkspaceGalleryRevision &&
        m_RawWorkspaceGalleryProjectStacksQueueRevision == m_RawWorkspaceCategoryQueueRevision &&
        m_RawWorkspaceGalleryProjectStacksOrganizationRevision == m_RawWorkspaceFilmstripOrganizationRevision &&
        m_RawWorkspaceGalleryProjectStacksMode == m_RawWorkspaceGalleryContentMode &&
        m_RawWorkspaceGalleryProjectStacksFolder == folder) return m_RawWorkspaceGalleryProjectStacks;
    m_RawWorkspaceGalleryProjectStacks = Stack::RawWorkspace::BuildGalleryProjectStacks(
        presentation, ResolveRawWorkspaceFilmstripStacks());
    m_RawFilmstripHeight.first = -1;
    m_RawWorkspaceGalleryProjectMaximumVersions = 1u;
    for (const auto& family : m_RawWorkspaceGalleryProjectStacks)
        m_RawWorkspaceGalleryProjectMaximumVersions = std::max(
            m_RawWorkspaceGalleryProjectMaximumVersions, family.projectIndices.size());
    // A preview is disposable. Its absence or decoder failure must never hide
    // an authoritative saved document or prevent the user from opening it.
    auto& tiles = m_RawWorkspaceGalleryProjectTileSources;
    auto& projectIndices = m_RawWorkspaceGalleryProjectIndices;
    tiles.clear();
    projectIndices.clear();
    tiles.reserve(presentation.projects.size());
    for (const auto& project : presentation.projects) {
        projectIndices.emplace(project.projectCoverKey, tiles.size());
        const Stack::RawWorkspace::SourceRecord* reference = nullptr;
        if (project.referenceSourceIndex < m_RawWorkspace.sources.size())
            reference = &m_RawWorkspace.sources[project.referenceSourceIndex];
        else if (project.projectIndex < m_RawWorkspace.sourceSetProjects.size()) {
            const auto& entry = m_RawWorkspace.sourceSetProjects[project.projectIndex];
            reference = entry.thumbnailOriginal ? entry.thumbnailOriginal.get() : entry.linkedOriginal.get();
        }
        tiles.push_back(reference ? *reference : Stack::RawWorkspace::SourceRecord{});
        auto& tile = tiles.back();
        tile.relativePathKey = project.projectCoverKey;
        tile.fileName = project.projectName;
        tile.stem = project.projectName;
        if (!project.coverThumbnailCachePath.empty()) {
            tile.thumbnail.absolutePath = project.coverThumbnailCachePath;
            tile.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Ready;
        }
    }
    auto& covers = m_RawWorkspaceGalleryProjectCoverIds;
    std::unordered_set<std::string> catalogIds;
    for (const auto& project : m_RawWorkspace.sourceSetProjects) catalogIds.insert(Stack::RawWorkspace::ProjectCoverKey(project.absolutePath,project.projectId));
    for (const auto& project : presentation.projects) catalogIds.insert(project.projectCoverKey);
    if (m_RawWorkspaceGalleryGridStacks.projectGroups) {
        auto& browse = m_RawWorkspaceGalleryGridStacks;
        bool changed = false;
        for (auto& group : browse.groups) {
            const auto previous = group.size();
            group.erase(std::remove_if(group.begin(), group.end(),
                [&](const auto& key) { return !catalogIds.count(key); }), group.end());
            changed |= previous != group.size();
        }
        if (browse.groups[0].empty() && browse.groups[1].empty()) browse.Close();
        else if (changed) ++browse.navigationRevision;
    }
    for (auto cover = covers.begin(); cover != covers.end();) {
        if (!catalogIds.count(cover->first) || !catalogIds.count(cover->second)) cover = covers.erase(cover);
        else ++cover;
    }
    for (const auto& family : m_RawWorkspaceGalleryProjectStacks) {
        std::unordered_set<std::string> members;
        for (const auto index : family.projectIndices) members.insert(presentation.projects[index].projectCoverKey);
        std::string chosen;
        for (const auto index : family.projectIndices) {
            const auto existing = covers.find(presentation.projects[index].projectCoverKey);
            if (existing != covers.end() && members.count(existing->second)) {
                chosen = existing->second;
                break;
            }
        }
        if (chosen.empty()) {
            const auto savedCover = std::find_if(family.projectIndices.begin(), family.projectIndices.end(),
                [&](const auto index) { return !presentation.projects[index].coverThumbnailCachePath.empty(); });
            chosen = presentation.projects[savedCover == family.projectIndices.end()
                ? family.projectIndices.front() : *savedCover].projectCoverKey;
            for (const auto index : family.projectIndices) {
                const auto& project = presentation.projects[index];
                if (project.projectPath == m_RawWorkspaceLabFocusedProjectPath ||
                    std::find(m_RawWorkspaceLabSelectedProjectPaths.begin(), m_RawWorkspaceLabSelectedProjectPaths.end(),
                        project.projectPath) != m_RawWorkspaceLabSelectedProjectPaths.end()) {
                    chosen = project.projectCoverKey;
                    break;
                }
            }
        }
        for (const auto& member : members) covers[member] = chosen;
    }
    m_RawWorkspaceGalleryProjectStacksRevision = m_RawWorkspaceGalleryRevision;
    m_RawWorkspaceGalleryProjectStacksQueueRevision = m_RawWorkspaceCategoryQueueRevision;
    m_RawWorkspaceGalleryProjectStacksOrganizationRevision = m_RawWorkspaceFilmstripOrganizationRevision;
    m_RawWorkspaceGalleryProjectStacksMode = m_RawWorkspaceGalleryContentMode;
    m_RawWorkspaceGalleryProjectStacksFolder = folder;
    m_RawWorkspaceLabFilmstripProjectTimelineActive = false;
    return m_RawWorkspaceGalleryProjectStacks;
}

void EditorModule::OpenRawWorkspaceGalleryStackInFilmstrip(const std::string& sourceKey) {
    m_RawWorkspaceGalleryLayoutAnimation.PrepareSwitch(sourceKey);
    // Focus must also work when a clicked cover is still loading and has no
    // rendered image for the layout animation to use as an anchor.
    m_RawLabGalleryScrollToSourceKey = sourceKey;
    m_RawWorkspaceLabUi.galleryWorkspaceGrid = false;
    m_RawWorkspaceLabFilmstripDrawerState = {};
    m_RawWorkspaceLabFilmstripDrawerKeyboardToggleRequested = true;
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    m_RawWorkspaceGalleryPreviewHovered = false;
    m_RawWorkspaceLabFilmstripHoverSourceKey = sourceKey;
    m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripHoverFrame = ImGui::GetFrameCount();
    m_RawWorkspaceLabFilmstripHoverSuppressed = false;
    m_RawWorkspaceLabFilmstripHoverBlend = 1.0f;
}

std::vector<std::string> EditorModule::BuildRawWorkspaceGallerySourceSelectionOrder(
    bool compactFilmstrip,
    bool expandedFilmstripDrawer,
    float filmstripDrawerExpansion) {
    std::vector<std::string> order;
    const auto navigation = m_RawWorkspaceLabUi.galleryNavigationMode;
    if (navigation == RawGalleryNavigationMode::ProjectFrames ||
        navigation == RawGalleryNavigationMode::MultiFrameCreation) {
        for (const auto& source : m_RawWorkspace.sources) {
            if (navigation == RawGalleryNavigationMode::ProjectFrames &&
                !std::any_of(source.sourceSetProjectMemberships.begin(),
                    source.sourceSetProjectMemberships.end(), [&](const auto& membership) {
                        return membership.projectId == m_RawWorkspaceLabUi.galleryProjectId;
                    })) continue;
            order.push_back(source.relativePathKey);
        }
        return order;
    }

    const auto& presentation = GetRawWorkspacePanelGalleryPresentation();
    if (!compactFilmstrip && (!m_PermanentGalleryWorkspace ||
        m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List)) {
        for (const auto& group : presentation.groups) {
            for (const auto& source : group.sources) order.push_back(source.relativePathKey);
        }
        return order;
    }

    if (!compactFilmstrip && m_RawWorkspaceGalleryGridStacks.groupCount) {
        // Range selection follows the opened grid's member order. Collapsed
        // stack covers omit the other members currently visible to the user.
        for (int group = 0; group < m_RawWorkspaceGalleryGridStacks.groupCount; ++group) {
            for (const auto& key : m_RawWorkspaceGalleryGridStacks.groups[group]) {
                if (m_RawWorkspaceLabGalleryViewsBySource.find(key) != m_RawWorkspaceLabGalleryViewsBySource.end())
                    order.push_back(key);
            }
        }
        return order;
    }

    const auto& stacks = ResolveRawWorkspaceVisibleGalleryStacks();
    const auto appendStack = [&](const auto& stack) {
        if (stack.sourceKeys.empty()) return;
        if (!compactFilmstrip) {
            order.push_back(m_RawWorkspaceGalleryGridStacks.Cover(
                stack.sourceKeys, {}));
        } else if (expandedFilmstripDrawer && filmstripDrawerExpansion > 0.001f) {
            order.insert(order.end(), stack.sourceKeys.begin(), stack.sourceKeys.end());
        } else {
            order.push_back(stack.sourceKeys.front());
        }
    };
    for (const auto& stack : stacks) appendStack(stack);
    return order;
}
