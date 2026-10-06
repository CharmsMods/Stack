#include "Editor/EditorModule.h"
#include "Editor/Bracketing/BracketingGallery.h"

#include "Async/TaskSystem.h"

#include <algorithm>
#include <utility>

const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
EditorModule::ResolveRawWorkspaceFilmstripStacks() const {
    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (m_RawWorkspaceFilmstripStacksCacheWorkspaceKey == workspaceKey &&
        m_RawWorkspaceFilmstripStacksCacheGalleryRevision == m_RawWorkspaceGalleryRevision &&
        m_RawWorkspaceFilmstripStacksCacheSimilarityGeneration ==
            m_RawWorkspaceSimilarityPublishedGeneration &&
        m_RawWorkspaceFilmstripStacksCacheOrganizationRevision ==
            m_RawWorkspaceFilmstripOrganizationRevision) {
        return m_RawWorkspaceFilmstripStacksCache;
    }
    std::vector<Stack::RawWorkspace::RawGallerySimilarityInput> catalog;
    catalog.reserve(m_RawWorkspace.sources.size());
    for (std::size_t index = 0; index < m_RawWorkspace.sources.size(); ++index) {
        const auto& source = m_RawWorkspace.sources[index];
        Stack::RawWorkspace::RawGallerySimilarityInput input;
        input.sourceKey = source.relativePathKey;
        input.folderKey = source.parentFolderKey;
        input.catalogIndex = index;
        catalog.push_back(std::move(input));
    }

    const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack> empty;
    const auto& automaticStacks =
        m_RawWorkspaceSimilarityWorkspaceKey == workspaceKey
            ? m_RawWorkspaceSimilarityStacks
            : empty;
    const auto manual = m_RawWorkspaceManualGroupings.find(workspaceKey);
    const Stack::RawWorkspace::RawGalleryManualGrouping emptyManual;
    const auto& organization = manual != m_RawWorkspaceManualGroupings.end()
        ? manual->second
        : emptyManual;
    auto stacks = Stack::RawWorkspace::ApplyRawGalleryManualGrouping(
        automaticStacks,
        catalog,
        organization);
    // The source gallery and automatic queue share the original grouping.
    // Saved results are presented in the Bracket category.
    std::vector<Stack::RawWorkspace::RawGalleryFilmstripSourceSortInfo>
        sortSources;
    sortSources.reserve(m_RawWorkspace.sources.size());
    for (std::size_t index = 0; index < m_RawWorkspace.sources.size(); ++index) {
        const auto& source = m_RawWorkspace.sources[index];
        Stack::RawWorkspace::RawGalleryFilmstripSourceSortInfo sortSource;
        sortSource.sourceKey = source.relativePathKey;
        sortSource.fileName = source.fileName;
        sortSource.folderKey = source.parentFolderKey;
        sortSource.modifiedTimeTicks = source.modifiedTimeTicks;
        sortSource.modifiedUnixSeconds = source.modifiedUnixSeconds;
        sortSource.captureTimestamp = source.captureTimestamp;
        sortSource.catalogIndex = index;
        sortSources.push_back(std::move(sortSource));
    }
    m_RawWorkspaceFilmstripStacksCache = Stack::RawWorkspace::SortRawGalleryFilmstripStacks(
        std::move(stacks), sortSources, organization);
    m_RawWorkspaceFilmstripMaximumStackSizeCache = 1u;
    for (const auto& stack : m_RawWorkspaceFilmstripStacksCache) {
        m_RawWorkspaceFilmstripMaximumStackSizeCache = std::max(
            m_RawWorkspaceFilmstripMaximumStackSizeCache,
            stack.sourceKeys.size());
    }
    m_RawWorkspaceFilmstripStacksCacheWorkspaceKey = workspaceKey;
    m_RawWorkspaceFilmstripStacksCacheGalleryRevision = m_RawWorkspaceGalleryRevision;
    m_RawWorkspaceFilmstripStacksCacheSimilarityGeneration =
        m_RawWorkspaceSimilarityPublishedGeneration;
    m_RawWorkspaceFilmstripStacksCacheOrganizationRevision =
        m_RawWorkspaceFilmstripOrganizationRevision;
    return m_RawWorkspaceFilmstripStacksCache;
}

bool EditorModule::MergeRawWorkspaceFilmstripStack(
    const std::string& sourceKey,
    const std::vector<std::string>& targetMembers) {
    return MergeRawWorkspaceFilmstripStackMembers(
        { sourceKey }, targetMembers);
}

bool EditorModule::MergeRawWorkspaceFilmstripStackMembers(
    const std::vector<std::string>& sourceKeys,
    const std::vector<std::string>& targetMembers) {
    if(RequestAutoBracketForeground("change these stacks",[this,sourceKeys,targetMembers]{MergeRawWorkspaceFilmstripStackMembers(sourceKeys,targetMembers);}))return true;
    if (!CanEditRawWorkspaceFilmstripOrganization() ||
        sourceKeys.empty() || targetMembers.empty() ||
        std::any_of(
            sourceKeys.begin(),
            sourceKeys.end(),
            [&](const std::string& key) {
                return key.empty() ||
                    FindRawWorkspaceSourceByKey(key) == nullptr;
            }) ||
        std::any_of(
            targetMembers.begin(),
            targetMembers.end(),
            [&](const std::string& key) {
                return FindRawWorkspaceSourceByKey(key) == nullptr;
            })) {
        return false;
    }
    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (workspaceKey.empty()) return false;
    auto& grouping = m_RawWorkspaceManualGroupings[workspaceKey];
    if (!Stack::RawWorkspace::MergeRawGalleryManualStackMembers(
            grouping, sourceKeys, targetMembers)) {
        return false;
    }
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    SaveRawWorkspaceGalleryGroupingState();
    SaveRawWorkspaceAppState();
    return true;
}

bool EditorModule::ReorderRawWorkspaceFilmstripSources(
    const std::vector<std::string>& sourceKeys,
    const std::vector<Stack::RawWorkspace::RawGallerySimilarityStack>&
        visibleStacks,
    std::size_t insertionStackIndex,
    bool detachSingleMember) {
    if(RequestAutoBracketForeground("reorder these stacks",[this,sourceKeys,visibleStacks,insertionStackIndex,detachSingleMember]{ReorderRawWorkspaceFilmstripSources(sourceKeys,visibleStacks,insertionStackIndex,detachSingleMember);}))return true;
    if (!CanEditRawWorkspaceFilmstripOrganization() || sourceKeys.empty() ||
        std::any_of(
            sourceKeys.begin(),
            sourceKeys.end(),
            [&](const std::string& key) {
                return key.empty() ||
                    FindRawWorkspaceSourceByKey(key) == nullptr;
            })) {
        return false;
    }
    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (workspaceKey.empty()) return false;

    std::vector<std::string> catalogKeys;
    catalogKeys.reserve(m_RawWorkspace.sources.size());
    for (const auto& source : m_RawWorkspace.sources) {
        catalogKeys.push_back(source.relativePathKey);
    }

    auto& organization = m_RawWorkspaceManualGroupings[workspaceKey];
    const std::vector<std::string> order =
        Stack::RawWorkspace::ReorderRawGalleryManualSourceOrder(
            visibleStacks,
            sourceKeys,
            insertionStackIndex,
            catalogKeys);
    if (order.empty()) return false;
    const std::vector<std::string> visibleOrder =
        Stack::RawWorkspace::NormalizeRawGalleryManualSourceOrder(
            Stack::RawWorkspace::SeedRawGalleryManualSourceOrder(
                visibleStacks),
            catalogKeys);
    if (!detachSingleMember && order == visibleOrder) {
        return false;
    }
    if (detachSingleMember && sourceKeys.size() == 1u) {
        Stack::RawWorkspace::DetachRawGalleryManualStackMember(
            organization, sourceKeys.front());
    }
    organization.manualSourceOrder = order;
    organization.sortMode =
        Stack::RawWorkspace::RawGalleryFilmstripSortMode::Manual;
    organization.version = Stack::RawWorkspace::
        kRawGalleryManualGroupingVersion;
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    SaveRawWorkspaceGalleryGroupingState();
    SaveRawWorkspaceAppState();
    return true;
}

void EditorModule::SetRawWorkspaceFilmstripSortMode(
    Stack::RawWorkspace::RawGalleryFilmstripSortMode mode) {
    if (!CanEditRawWorkspaceFilmstripOrganization()) return;
    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (workspaceKey.empty()) return;
    auto& organization = m_RawWorkspaceManualGroupings[workspaceKey];
    if (organization.sortMode == mode) return;
    if (mode == Stack::RawWorkspace::RawGalleryFilmstripSortMode::Manual &&
        organization.manualSourceOrder.empty()) {
        organization.manualSourceOrder =
            Stack::RawWorkspace::SeedRawGalleryManualSourceOrder(
                ResolveRawWorkspaceFilmstripStacks());
    }
    organization.sortMode = mode;
    organization.version = Stack::RawWorkspace::
        kRawGalleryManualGroupingVersion;
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    m_RawWorkspaceLabFilmstripScrollTargetX = 0.0f;
    m_RawWorkspaceLabFilmstripScrollLastAppliedX = -1.0f;
    SaveRawWorkspaceGalleryGroupingState();
    SaveRawWorkspaceAppState();
}

void EditorModule::NormalizeRawWorkspaceFilmstripOrganization() {
    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    const auto found = m_RawWorkspaceManualGroupings.find(workspaceKey);
    if (workspaceKey.empty() || found == m_RawWorkspaceManualGroupings.end()) {
        return;
    }
    std::vector<std::string> catalogKeys;
    catalogKeys.reserve(m_RawWorkspace.sources.size());
    for (const auto& source : m_RawWorkspace.sources) {
        catalogKeys.push_back(source.relativePathKey);
    }
    const std::vector<std::string> normalized =
        Stack::RawWorkspace::NormalizeRawGalleryManualSourceOrder(
            found->second.manualSourceOrder,
            catalogKeys);
    if (normalized != found->second.manualSourceOrder) {
        found->second.manualSourceOrder = normalized;
        SaveRawWorkspaceGalleryGroupingState();
        SaveRawWorkspaceAppState();
    }
}

bool EditorModule::DetachRawWorkspaceFilmstripStackMember(
    const std::string& sourceKey) {
    if(RequestAutoBracketForeground("change this stack",[this,sourceKey]{DetachRawWorkspaceFilmstripStackMember(sourceKey);}))return true;
    if (!CanEditRawWorkspaceFilmstripOrganization() || sourceKey.empty() ||
        FindRawWorkspaceSourceByKey(sourceKey) == nullptr) {
        return false;
    }
    const auto stacks = ResolveRawWorkspaceFilmstripStacks();
    const auto current = std::find_if(
        stacks.begin(), stacks.end(), [&](const auto& stack) {
            return stack.sourceKeys.size() > 1u &&
                std::find(
                    stack.sourceKeys.begin(),
                    stack.sourceKeys.end(),
                    sourceKey) != stack.sourceKeys.end();
        });
    if (current == stacks.end()) return false;

    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    auto& grouping = m_RawWorkspaceManualGroupings[workspaceKey];
    if (!Stack::RawWorkspace::DetachRawGalleryManualStackMember(
            grouping, sourceKey)) {
        return false;
    }
    m_RawWorkspaceLabFilmstripDrawerSessionOrders.clear();
    SaveRawWorkspaceGalleryGroupingState();
    SaveRawWorkspaceAppState();
    return true;
}

void EditorModule::ResetRawWorkspaceSimilarityStacks() {
    m_RawWorkspaceSimilarityGeneration.fetch_add(1, std::memory_order_relaxed);
    m_RawWorkspaceSimilarityPublishedGeneration = 0;
    m_RawWorkspaceSimilarityWorkspaceKey.clear();
    m_RawWorkspaceSimilarityStacks.clear();
    m_RawWorkspaceSimilarityRebuildPending = false;
    m_RawWorkspaceSimilarityActiveSourceCount = 0;
    m_RawWorkspaceSimilarityLastStackCount = 0;
}

void EditorModule::RequestRawWorkspaceSimilarityRebuild() {
    if (m_RawWorkspaceSimilarityWorkerActive) {
        m_RawWorkspaceSimilarityRebuildPending = true;
        return;
    }

    const std::string workspaceKey =
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    const std::uint64_t generation =
        m_RawWorkspaceSimilarityGeneration.fetch_add(
            1,
            std::memory_order_relaxed) +
        1;

    if (workspaceKey.empty() || m_RawWorkspace.sources.empty()) {
        m_RawWorkspaceSimilarityPublishedGeneration = generation;
        m_RawWorkspaceSimilarityWorkspaceKey = workspaceKey;
        m_RawWorkspaceSimilarityStacks.clear();
        return;
    }

    m_RawWorkspaceSimilarityWorkerActive = true;
    m_RawWorkspaceSimilarityRebuildPending = false;
    m_RawWorkspaceSimilarityActiveSourceCount = m_RawWorkspace.sources.size();

    Stack::RawWorkspace::RawGallerySimilarityRequest request;
    request.generation = generation;
    request.workspaceKey = workspaceKey;
    request.inputs.reserve(m_RawWorkspace.sources.size());
    for (std::size_t index = 0;
         index < m_RawWorkspace.sources.size();
         ++index) {
        const Stack::RawWorkspace::SourceRecord& source =
            m_RawWorkspace.sources[index];
        Stack::RawWorkspace::RawGallerySimilarityInput input;
        input.sourceKey = source.relativePathKey;
        input.folderKey = source.parentFolderKey;
        input.catalogIndex = index;
        input.descriptor = source.thumbnail.similarityDescriptor.IsValid()
            ? source.thumbnail.similarityDescriptor
            : source.transientThumbnail.similarityDescriptor;
        request.inputs.push_back(std::move(input));
    }

    bool submitted = false;
    try {
        submitted = ProjectTasks().Submit(
            "Grouping similar RAW images",
            [
                this,
                request = std::move(request)
            ]() mutable {
                Stack::RawWorkspace::RawGallerySimilarityResult result =
                    Stack::RawWorkspace::BuildRawGallerySimilarityResult(
                        request);
                ProjectTasks().PostToMain([
                    this,
                    result = std::move(result)
                ]() mutable {
                    const bool current = Stack::RawWorkspace::
                        IsRawGallerySimilarityResultCurrent(
                            result,
                            m_RawWorkspaceSimilarityGeneration.load(
                                std::memory_order_relaxed),
                            m_RawWorkspace.workspaceRoot
                                .lexically_normal()
                                .generic_string());
                    if (current) {
                        m_RawWorkspaceSimilarityPublishedGeneration =
                            result.generation;
                        m_RawWorkspaceSimilarityWorkspaceKey =
                            result.workspaceKey;
                        m_RawWorkspaceSimilarityStacks =
                            std::move(result.stacks);
                        m_RawWorkspaceSimilarityLastStackCount =
                            m_RawWorkspaceSimilarityStacks.size();
                    }
                    m_RawWorkspaceSimilarityWorkerActive = false;
                    if (m_RawWorkspaceSimilarityRebuildPending) {
                        m_RawWorkspaceSimilarityRebuildPending = false;
                        RequestRawWorkspaceSimilarityRebuild();
                    }
                });
            });
    } catch (...) {
        submitted = false;
    }

    if (!submitted &&
        generation == m_RawWorkspaceSimilarityGeneration.load(
            std::memory_order_relaxed)) {
        m_RawWorkspaceSimilarityWorkerActive = false;
        m_RawWorkspaceSimilarityPublishedGeneration = generation;
        m_RawWorkspaceSimilarityWorkspaceKey = workspaceKey;
        m_RawWorkspaceSimilarityStacks.clear();
    } else if (!submitted) {
        m_RawWorkspaceSimilarityWorkerActive = false;
    }
}
