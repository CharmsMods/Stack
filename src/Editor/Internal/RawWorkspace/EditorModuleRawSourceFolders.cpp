#include "Editor/EditorModule.h"
#include "Raw/RawSourceFolders.h"
#include "Raw/RawLoader.h"
#include "Persistence/ProjectIndex.h"
#include "Async/TaskSystem.h"

#include <algorithm>

namespace {
Stack::RawWorkspace::SourceRecord FolderPreview(const Stack::RawWorkspace::SourceRecord& source) {
    Stack::RawWorkspace::SourceRecord preview;
    preview.absolutePath = source.absolutePath;
    preview.relativePath = source.relativePath;
    preview.relativePathKey = source.relativePathKey;
    preview.sourceWorkspaceRoot = source.sourceWorkspaceRoot;
    preview.sourceRelativePathKey = source.sourceRelativePathKey;
    preview.fileName = source.fileName;
    preview.fileSizeBytes = source.fileSizeBytes;
    preview.modifiedTimeTicks = source.modifiedTimeTicks;
    preview.fingerprint = source.fingerprint;
    preview.thumbnail = source.thumbnail;
    preview.transientThumbnail = source.transientThumbnail;
    return preview;
}
}

bool EditorModule::FindRawWorkspaceGalleryProjectRecord(const std::filesystem::path& path,
    Stack::Project::ProjectRecord& record) const {
    const auto normalized = path.lexically_normal();
    const auto found = std::find_if(m_RawWorkspace.sourceSetProjects.begin(), m_RawWorkspace.sourceSetProjects.end(),
        [&](const auto& project) { return project.absolutePath.lexically_normal() == normalized; });
    if (found != m_RawWorkspace.sourceSetProjects.end() && found->projectMetadata) {
        record = *found->projectMetadata;
        return true;
    }
    // A warm native catalog may not yet have full metadata. Aggregate records
    // must never depend on which unrelated folder rebuilt the global index last.
    return !m_RawWorkspace.allSourceFolders && Stack::Project::ProjectIndex::Get().FindByPath(path, record);
}

std::string EditorModule::GetRawWorkspaceGalleryScopeKey() const {
    return m_RawWorkspace.allSourceFolders ? "@all-source-folders" :
        m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
}

std::filesystem::path EditorModule::ResolveRawWorkspaceSourceFolder(
    const std::filesystem::path& path) const {
    if (path.empty()) return {};
    const auto normalized = path.lexically_normal();
    for (const auto& source : m_RawWorkspace.sources)
        if (source.absolutePath.lexically_normal() == normalized)
            return Stack::RawWorkspace::ResolveSourceWorkspaceRoot(source, m_RawWorkspace.workspaceRoot);
    std::filesystem::path browseFallback;
    for (const auto& project : m_RawWorkspace.sourceSetProjects) {
        if (project.absolutePath.lexically_normal() != normalized) continue;
        browseFallback = project.sourceWorkspaceRoot.empty()
            ? m_RawWorkspace.workspaceRoot : project.sourceWorkspaceRoot;
        break;
    }
    // Recursive discovery is not storage ownership: a nested child's Closet
    // remains that saved project's owner even when its parent was scanned first.
    const auto owner = Stack::RawWorkspace::ResolveManagedProjectSourceFolder(path);
    return owner.empty() ? browseFallback : owner;
}

void EditorModule::RememberRawWorkspaceFolderCards(bool complete) {
    if (m_RawWorkspace.allSourceFolders || (!complete && !GetRawWorkspaceScanSnapshot().completedSuccessfully)) return;
    std::unordered_map<std::string, Stack::RawWorkspace::SourceFolderCard> cards;
    if (!m_RawWorkspace.allSourceFolders && !m_RawWorkspace.workspaceRoot.empty()) {
        auto& card = cards[m_RawWorkspace.workspaceRoot.lexically_normal().generic_u8string()];
        card.root = m_RawWorkspace.workspaceRoot;
        card.countKnown = true;
    }
    for (const auto& source : m_RawWorkspace.sources) {
        const auto root = Stack::RawWorkspace::ResolveSourceWorkspaceRoot(source, m_RawWorkspace.workspaceRoot);
        if (root.empty()) continue;
        auto& card = cards[root.lexically_normal().generic_u8string()];
        card.root = root;
        card.countKnown = true;
        ++card.sourceCount;
        if (card.previews.size() < 4u) card.previews.push_back(FolderPreview(source));
    }
    for (auto& card : cards) m_RawGallerySourceFolderCards[card.first] = std::move(card.second);
    ++m_RawGalleryFolderCardRevision;
}

void EditorModule::RequestRawWorkspaceFolderCardPreviews() {
    std::vector<std::filesystem::path> roots;
    for (const auto& root : m_RawWorkspace.registeredSourceFolders) {
        const auto card = m_RawGallerySourceFolderCards.find(root.lexically_normal().generic_u8string());
        if (card == m_RawGallerySourceFolderCards.end() || !card->second.countKnown) roots.push_back(root);
    }
    if (roots.empty()) return;
    const auto generation = ++m_RawGalleryFolderCardLoadGeneration;
    ProjectTasks().Submit("Loading source folder cards", [this, roots, generation] {
        std::vector<Stack::RawWorkspace::SourceFolderCard> cards;
        for (const auto& root : roots) {
            Stack::RawWorkspace::SourceFolderCard card;
            card.root = root;
            std::error_code error;
            if (std::filesystem::is_directory(root, error) && !error) {
                std::vector<Stack::RawWorkspace::SourceRecord> sources;
                if (Stack::RawWorkspace::LoadCatalogSnapshot(Stack::RawWorkspace::BuildManagedLayout(root), sources)) {
                    card.countKnown = true;
                    card.sourceCount = sources.size();
                    for (auto& source : sources) {
                        if (card.previews.size() == 4u) break;
                        source.sourceWorkspaceRoot = root;
                        card.previews.push_back(FolderPreview(source));
                    }
                }
            }
            cards.push_back(std::move(card));
        }
        ProjectTasks().PostToMain([this, generation, cards = std::move(cards)]() mutable {
            if (generation != m_RawGalleryFolderCardLoadGeneration) return;
            for (auto& card : cards) {
                const auto key = card.root.lexically_normal().generic_u8string();
                const auto existing = m_RawGallerySourceFolderCards.find(key);
                // A newer accepted live scan is more authoritative than this cache.
                if (existing == m_RawGallerySourceFolderCards.end() || !existing->second.countKnown)
                    m_RawGallerySourceFolderCards[key] = std::move(card);
            }
            ++m_RawGalleryFolderCardRevision;
        });
    });
}

void EditorModule::UpdateRawWorkspaceFolderCardThumbnail(const Stack::RawWorkspace::SourceRecord& source) {
    const auto root = Stack::RawWorkspace::ResolveSourceWorkspaceRoot(source, m_RawWorkspace.workspaceRoot);
    const auto found = m_RawGallerySourceFolderCards.find(root.lexically_normal().generic_u8string());
    if (found == m_RawGallerySourceFolderCards.end()) return;
    for (auto& preview : found->second.previews)
        if (preview.absolutePath == source.absolutePath) { preview = FolderPreview(source); ++m_RawGalleryFolderCardRevision; return; }
}

const std::vector<Stack::RawWorkspace::SourceFolderCard>& EditorModule::GetRawWorkspaceSourceFolderCards() const {
    if (m_RawGalleryFolderCardViewRevision == m_RawGalleryFolderCardRevision)
        return m_RawGalleryFolderCardView;
    m_RawGalleryFolderCardView.clear();
    m_RawGalleryFolderCardView.reserve(m_RawWorkspace.registeredSourceFolders.size());
    for (const auto& root : m_RawWorkspace.registeredSourceFolders) {
        const auto key = root.lexically_normal().generic_u8string();
        const auto cached = m_RawGallerySourceFolderCards.find(key);
        Stack::RawWorkspace::SourceFolderCard card;
        if (cached != m_RawGallerySourceFolderCards.end()) card = cached->second;
        card.root = root;
        // This is the chooser's revisioned display snapshot, not the native
        // source catalog. Prepare its cache namespace once per publication so
        // drawing a resident cover needs no SourceRecord or path-string copies.
        for (auto& preview : card.previews)
            preview.relativePathKey = "folder-cover:" + key + "|" +
                preview.absolutePath.lexically_normal().generic_u8string();
        m_RawGalleryFolderCardView.push_back(std::move(card));
    }
    m_RawGalleryFolderCardViewRevision = m_RawGalleryFolderCardRevision;
    return m_RawGalleryFolderCardView;
}

void EditorModule::ResetRawWorkspaceBrowseSources() {
    RememberRawWorkspaceFolderCards();
    m_RawWorkspace.sources.clear();
    m_RawWorkspace.sourceSetProjects.clear();
    m_RawWorkspace.selectedSourceKey.clear();
    m_RawWorkspace.selectedSourceKeys.clear();
    m_RawWorkspaceLabSelectedProjectPaths.clear();
    m_RawWorkspaceLabFocusedProjectPath.clear();
    m_RawWorkspaceLabFocusedProjectName.clear();
    m_RawWorkspaceLabProjectSelectionAnchor.clear();
    m_RawGalleryFolderFilter.reset();
    m_RawWorkspaceLabCollapsedGalleryGroups.clear();
    m_RawWorkspaceLabGalleryScrollTargetY = m_RawWorkspaceLabGalleryScrollCurrentY = 0.f;
    m_RawWorkspaceLabFilmstripScrollTargetX = 0.f;
    m_RawWorkspaceLabFilmstripScrollLastAppliedX = -1.f;
    m_RawWorkspaceGalleryLayoutAnimation.Reset();
    m_RawWorkspacePreviewStageQueued = false;
    m_RawWorkspacePreviewStageSourceKey.clear();
    ClearRawWorkspaceThumbnailTextures();
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceThumbnailMutex);
        ++m_RawWorkspaceThumbnailGeneration;
        m_RawWorkspaceThumbnailSnapshot = {};
        m_RawWorkspaceThumbnailScheduler.Clear();
        m_RawWorkspaceThumbnailWorkerActive = false;
        m_RawWorkspaceThumbnailDecodeRepairAttempts.clear();
    }
    ResetRawWorkspaceCatalogPersistState();
    ResetRawWorkspaceSimilarityStacks();
    InvalidateRawWorkspaceGalleryPresentation();
}

void EditorModule::SetRawWorkspaceAllFolders(bool allFolders) {
    if (!m_PermanentGalleryWorkspace) return;
    EnsureRawWorkspaceLoaded();
    m_RawGallerySourceFoldersOpen = false;
    if (!allFolders) {
        if (!m_RawWorkspace.workspaceRoot.empty()) SetRawWorkspaceFolder(m_RawWorkspace.workspaceRoot);
        return;
    }
    if (m_RawWorkspace.allSourceFolders) return;
    if (m_RawWorkspace.registeredSourceFolders.empty()) { ShowRawWorkspaceSourceFolders(); return; }
    ResetRawWorkspaceBrowseSources();
    m_RawWorkspace.allSourceFolders = true;
    if (m_RawWorkspace.workspaceRoot.empty())
        m_RawWorkspace.workspaceRoot = m_RawWorkspace.registeredSourceFolders.front();
    m_RawWorkspaceGalleryContentMode = Stack::RawWorkspace::GalleryContentMode::Gallery;
    m_RawWorkspaceLabUi.galleryNavigationMode = RawGalleryNavigationMode::ProjectRoot;
    m_RawWorkspaceLabUi.galleryProjectId.clear();
    SaveRawWorkspaceAppState();
    RequestRawWorkspaceScan();
}

void EditorModule::RequestRawWorkspaceAggregateScan() {
    ResetRawWorkspaceSimilarityStacks();
    const auto roots = m_RawWorkspace.registeredSourceFolders;
    std::uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
        generation = ++m_RawWorkspaceScanGeneration;
        m_RawWorkspaceScanSnapshot = {};
        m_RawWorkspaceScanSnapshot.generation = generation;
        m_RawWorkspaceScanSnapshot.state = Async::TaskState::Queued;
        m_RawWorkspaceScanSnapshot.sourcesPublished = !m_RawWorkspace.sources.empty();
        m_RawWorkspaceScanSnapshot.statusText = "Scanning all source folders...";
    }
    const auto submitted = ProjectTasks().Submit("Scanning source folders", [this, roots, generation] {
        const auto canceled = [this, generation] {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
            return generation != m_RawWorkspaceScanGeneration;
        };
        Stack::RawWorkspace::ScanResult result;
        try {
            result = Stack::RawWorkspace::AggregateSourceFolders(roots,
                [](const auto& path) { return Raw::RawLoader::IsRawPath(path.u8string()) ||
                    Stack::RawWorkspace::DefaultRawPathPredicate(path); },
                [this, generation](const auto& progress) {
                    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                    if (generation != m_RawWorkspaceScanGeneration) return;
                    m_RawWorkspaceScanSnapshot.state = Async::TaskState::Running;
                    m_RawWorkspaceScanSnapshot.progress = progress;
                    m_RawWorkspaceScanSnapshot.statusText = progress.statusText;
                }, canceled);
        } catch (const std::exception& error) { result.errorMessage = error.what(); }
        catch (...) { result.errorMessage = "Could not scan the source folders."; }
        if (canceled()) return;
        const bool published = ProjectTasks().PostToMain([this, generation, result = std::move(result)]() mutable {
            {
                std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                if (generation != m_RawWorkspaceScanGeneration || !m_RawWorkspace.allSourceFolders) return;
                m_RawWorkspaceScanSnapshot.state = result.success ? Async::TaskState::Idle : Async::TaskState::Failed;
                m_RawWorkspaceScanSnapshot.completedSuccessfully = result.success;
                m_RawWorkspaceScanSnapshot.sourcesPublished = result.success;
                m_RawWorkspaceScanSnapshot.progress = result.progress;
                m_RawWorkspaceScanSnapshot.errorMessage = result.errorMessage;
                m_RawWorkspaceScanSnapshot.statusText = result.errorMessage.empty()
                    ? "All source folders ready." : result.errorMessage;
            }
            if (!result.success) {
                PostNotification(UiNotificationSeverity::Error, result.errorMessage, "source-folders-scan");
                return;
            }
            const auto latestSelection = m_RawWorkspace.selectedSourceKey;
            m_RawWorkspace.sources = std::move(result.sources);
            m_RawWorkspace.sourceSetProjects = std::move(result.sourceSetProjects);
            m_RawWorkspace.selectedSourceKey.clear();
            // Keep the latest user focus (including cleared focus), not the
            // selection that happened to be active when the scan started.
            if (!latestSelection.empty())
                Stack::RawWorkspace::SelectSourceByKey(m_RawWorkspace, latestSelection);
            m_RawWorkspace.selectedSourceKeys.erase(std::remove_if(m_RawWorkspace.selectedSourceKeys.begin(),
                m_RawWorkspace.selectedSourceKeys.end(), [this](const auto& key) { return !FindRawWorkspaceSourceByKey(key); }),
                m_RawWorkspace.selectedSourceKeys.end());
            const auto projectExists = [this](const auto& path) {
                return std::any_of(m_RawWorkspace.sourceSetProjects.begin(), m_RawWorkspace.sourceSetProjects.end(),
                    [&](const auto& project) { return project.absolutePath.lexically_normal() == path.lexically_normal(); });
            };
            m_RawWorkspaceLabSelectedProjectPaths.erase(std::remove_if(m_RawWorkspaceLabSelectedProjectPaths.begin(),
                m_RawWorkspaceLabSelectedProjectPaths.end(), [&](const auto& path) { return !projectExists(path); }),
                m_RawWorkspaceLabSelectedProjectPaths.end());
            if (!projectExists(m_RawWorkspaceLabFocusedProjectPath)) {
                m_RawWorkspaceLabFocusedProjectPath.clear();
                m_RawWorkspaceLabFocusedProjectName.clear();
            }
            if (!projectExists(m_RawWorkspaceLabProjectSelectionAnchor)) m_RawWorkspaceLabProjectSelectionAnchor.clear();
            for (const auto& root : m_RawWorkspace.registeredSourceFolders) {
                const auto key = root.lexically_normal().generic_u8string();
                auto& card = m_RawGallerySourceFolderCards[key];
                card.root = root;
                card.countKnown = false;
            }
            for (auto& card : result.sourceFolderCards) {
                for (auto& preview : card.previews) preview = FolderPreview(preview);
                m_RawGallerySourceFolderCards[card.root.lexically_normal().generic_u8string()] = std::move(card);
            }
            ++m_RawGalleryFolderCardRevision;
            NormalizeRawWorkspaceFilmstripOrganization();
            InvalidateRawWorkspaceGalleryPresentation();
            RequestRawWorkspaceSimilarityRebuild();
            RequestRawWorkspaceThumbnailGeneration();
            if (!result.errorMessage.empty())
                PostNotification(UiNotificationSeverity::Warning, result.errorMessage, "source-folders-scan");
        });
        if (!published) {
            std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
            if (generation == m_RawWorkspaceScanGeneration) {
                m_RawWorkspaceScanSnapshot.state = Async::TaskState::Failed;
                m_RawWorkspaceScanSnapshot.statusText = "Could not publish the source-folder scan.";
            }
        }
    });
    if (!submitted) {
        std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
        if (generation != m_RawWorkspaceScanGeneration) return;
        m_RawWorkspaceScanSnapshot.state = Async::TaskState::Failed;
        m_RawWorkspaceScanSnapshot.statusText = "Could not queue the source-folder scan.";
    }
}
