#include "Queue/RenderQueueModel.h"

#include "Persistence/ProjectIndex.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/LoadedProjectData.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Stack::Queue {
namespace {

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::string PathKey(const std::filesystem::path& path) {
    std::string value = NormalizePath(path).generic_u8string();
#if defined(_WIN32)
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
#endif
    return value;
}

std::vector<unsigned char> ReadThumbnailBytes(
    const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

Item MakeProjectItem(const std::filesystem::path& path) {
    Item item;
    item.kind = ItemKind::Project;
    item.path = NormalizePath(path);
    item.displayName = item.path.stem().u8string();
    Stack::Project::ProjectRecord record;
    if (Stack::Project::ProjectIndex::Get().FindByPath(item.path, record)) {
        item.displayName = record.displayName.empty()
            ? item.displayName : record.displayName;
        item.projectId = record.projectId;
        if (!record.coverThumbnailBytes.empty())
            item.thumbnailBytes = std::make_shared<const std::vector<unsigned char>>(
                std::move(record.coverThumbnailBytes));
    }
    return item;
}

Item MakeSourceItem(const Stack::RawGalleryQueue::SourceRequest& source) {
    Item item;
    item.kind = ItemKind::SourceImage;
    item.path = NormalizePath(source.sourcePath);
    item.displayName = item.path.stem().u8string();
    auto thumbnail = ReadThumbnailBytes(source.thumbnailPath);
    if (!thumbnail.empty())
        item.thumbnailBytes = std::make_shared<const std::vector<unsigned char>>(
            std::move(thumbnail));
    return item;
}

} // namespace

Stack::RawGalleryQueue::Result RenderQueueModel::AddGalleryRequest(
    const Stack::RawGalleryQueue::Request& request) {
    Stack::RawGalleryQueue::Result result;
    const auto record = [&](AddStatus status) {
        switch (status) {
        case AddStatus::Added: ++result.added; break;
        case AddStatus::AlreadyPresent: ++result.alreadyPresent; break;
        case AddStatus::Skipped: ++result.skipped; break;
        }
    };
    const auto skip = [&](std::string message) {
        ++result.skipped;
        result.errors.push_back(std::move(message));
    };
    for (const auto& requestedPath : request.projectPaths) {
        if (requestedPath.empty()) {
            skip("The selected project has no location.");
            continue;
        }
        auto projectPath = NormalizePath(requestedPath);
        if (Stack::Project::IsDirectoryProjectBundle(projectPath)) {
            projectPath = Stack::Project::ResolveProjectStoreRoot(projectPath);
        } else if (!Stack::Project::IsPortableV3Project(projectPath)) {
            skip("The selected project is unavailable or unsupported: " +
                requestedPath.u8string());
            continue;
        }
        record(AddItem(MakeProjectItem(projectPath)));
    }
    for (const auto& source : request.sources) {
        std::error_code error;
        if (source.sourcePath.empty() ||
            !std::filesystem::is_regular_file(source.sourcePath, error) || error) {
            std::string message = source.sourcePath.empty()
                ? "The selected image has no location."
                : "The selected image is unavailable: " + source.sourcePath.u8string();
            if (error) message += "\n" + error.message();
            skip(std::move(message));
            continue;
        }
        record(AddItem(MakeSourceItem(source)));
    }
    return result;
}

Stack::RawGalleryQueue::Result RenderQueueModel::AddProjectSnapshot(
    const ProjectRequest& request) {
    Stack::RawGalleryQueue::Result result;
    if (!request.snapshot) {
        result.skipped = 1;
        result.errors.push_back("The project could not be captured for Queue.");
        return result;
    }
    Item item;
    item.kind = ItemKind::ProjectSnapshot;
    item.projectSnapshot = request.snapshot;
    item.path = std::filesystem::path(request.snapshot->projectFileName);
    item.projectId = request.snapshot->projectId;
    item.displayName = request.displayName.empty()
        ? request.snapshot->projectName : request.displayName;
    if (item.displayName.empty()) item.displayName = "Untitled Project";
    if (!request.thumbnailBytes.empty())
        item.thumbnailBytes = std::make_shared<const std::vector<unsigned char>>(
            request.thumbnailBytes);
    // Each click captures an intentional authored revision/output, including
    // unsaved projects. Path deduplication would discard that distinction.
    result.added = AddItem(std::move(item), false) == AddStatus::Added ? 1u : 0u;
    return result;
}

std::size_t RenderQueueModel::AddProjects(
    const std::vector<std::filesystem::path>& projectPaths) {
    std::size_t added = 0;
    for (const std::filesystem::path& path : projectPaths) {
        if (path.empty()) continue;
        added += AddItem(MakeProjectItem(path)) == AddStatus::Added;
    }
    return added;
}

std::size_t RenderQueueModel::AddSources(
    const std::vector<std::filesystem::path>& sourcePaths) {
    std::size_t added = 0;
    for (const std::filesystem::path& path : sourcePaths) {
        if (path.empty()) continue;
        added += AddItem(MakeSourceItem({path, {}})) == AddStatus::Added;
    }
    return added;
}

std::size_t RenderQueueModel::AddSourceRequests(
    const std::vector<Stack::RawGalleryQueue::SourceRequest>& sources) {
    std::size_t added = 0;
    for (const Stack::RawGalleryQueue::SourceRequest& source : sources) {
        if (source.sourcePath.empty()) continue;
        added += AddItem(MakeSourceItem(source)) == AddStatus::Added;
    }
    return added;
}

RenderQueueModel::AddStatus RenderQueueModel::AddItem(Item item, bool deduplicate) {
    const std::string key = deduplicate
        ? std::to_string(static_cast<int>(item.kind)) + ":" + PathKey(item.path)
        : std::string();
    if (deduplicate && item.path.empty()) return AddStatus::Skipped;
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto existingId = deduplicate ? m_PathItems.find(key) : m_PathItems.end();
    if (existingId != m_PathItems.end()) {
        const auto existing = std::find_if(m_Items.begin(), m_Items.end(),
            [&](const Item& candidate) { return candidate.id == existingId->second; });
        if (existing != m_Items.end() && existing->state == ItemState::Complete) {
            // A deliberate new Gallery request may export the source/latest
            // saved edit again. Retain the old outcome and deduplicate against
            // the newly staged job from here onward.
            m_ItemKeys.erase(existing->id);
            m_PathItems.erase(existingId);
        } else {
            if (existing != m_Items.end() && !existing->selected) {
                existing->selected = true;
                ++m_Revision;
                ++m_PresentationRevision;
            }
            return AddStatus::AlreadyPresent;
        }
    }
    item.id = m_NextId++;
    item.selected = true;
    item.state = ItemState::Waiting;
    item.progress = 0.0f;
    if (deduplicate) {
        m_PathItems.emplace(key, item.id);
        m_ItemKeys.emplace(item.id, key);
    }
    m_Items.push_back(std::move(item));
    ++m_Revision;
    ++m_PresentationRevision;
    return AddStatus::Added;
}

std::vector<Item> RenderQueueModel::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Items;
}

bool RenderQueueModel::SnapshotIfChanged(
    std::vector<Item>& items, std::uint64_t& revision,
    std::uint64_t* presentationRevision) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (presentationRevision) *presentationRevision = m_PresentationRevision;
    if (revision == m_Revision) return false;
    items = m_Items;
    revision = m_Revision;
    return true;
}

std::vector<Item> RenderQueueModel::ClaimForExport(const std::vector<Item>& requested) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::vector<Item> claimed;
    claimed.reserve(requested.size());
    for (const Item& request : requested) {
        const auto item = std::find_if(m_Items.begin(), m_Items.end(),
            [&](const Item& candidate) { return candidate.id == request.id; });
        if (item == m_Items.end() ||
            (item->state != ItemState::Waiting && item->state != ItemState::Failed)) continue;
        item->state = ItemState::Queued;
        item->progress = 0.0f;
        item->status = "Queued for export";
        item->outputPath.clear();
        claimed.push_back(*item);
    }
    if (!claimed.empty()) {
        ++m_Revision;
        ++m_PresentationRevision;
    }
    return claimed;
}

void RenderQueueModel::ReleaseQueued(const std::vector<std::uint64_t>& ids) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    bool changed = false;
    for (Item& item : m_Items) {
        if (item.state != ItemState::Queued ||
            std::find(ids.begin(), ids.end(), item.id) == ids.end()) continue;
        item.state = ItemState::Waiting;
        item.progress = 0.0f;
        item.status.clear();
        changed = true;
    }
    if (changed) {
        ++m_Revision;
        ++m_PresentationRevision;
    }
}

void RenderQueueModel::SetSelected(std::uint64_t id, bool selected) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto item = std::find_if(m_Items.begin(), m_Items.end(),
        [&](const Item& candidate) { return candidate.id == id; });
    if (item != m_Items.end() && item->selected != selected &&
        (item->state == ItemState::Waiting || item->state == ItemState::Failed)) {
        item->selected = selected;
        ++m_Revision;
        ++m_PresentationRevision;
    }
}

void RenderQueueModel::SetAllSelected(bool selected) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    bool changed = false;
    for (Item& item : m_Items) {
        if (item.state != ItemState::Waiting && item.state != ItemState::Failed) continue;
        if (item.selected == selected) continue;
        item.selected = selected;
        changed = true;
    }
    if (changed) {
        ++m_Revision;
        ++m_PresentationRevision;
    }
}

void RenderQueueModel::Remove(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto item = std::find_if(m_Items.begin(), m_Items.end(),
        [&](const Item& candidate) { return candidate.id == id; });
    if (item == m_Items.end() ||
        (item->state != ItemState::Waiting && item->state != ItemState::Failed &&
         item->state != ItemState::Complete)) return;
    const auto key = m_ItemKeys.find(id);
    if (key != m_ItemKeys.end()) {
        m_PathItems.erase(key->second);
        m_ItemKeys.erase(key);
    }
    m_Items.erase(item);
    ++m_Revision;
    ++m_PresentationRevision;
}

void RenderQueueModel::ClearCompleted() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto previousCount = m_Items.size();
    for (const Item& item : m_Items) {
        if (item.state != ItemState::Complete) continue;
        const auto key = m_ItemKeys.find(item.id);
        if (key != m_ItemKeys.end()) {
            m_PathItems.erase(key->second);
            m_ItemKeys.erase(key);
        }
    }
    m_Items.erase(
        std::remove_if(m_Items.begin(), m_Items.end(),
            [](const Item& item) {
                return item.state == ItemState::Complete;
            }),
        m_Items.end());
    if (m_Items.size() != previousCount) {
        ++m_Revision;
        ++m_PresentationRevision;
    }
}

void RenderQueueModel::SetOutputPath(std::uint64_t id, const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto item = std::find_if(m_Items.begin(), m_Items.end(),
        [&](const Item& candidate) { return candidate.id == id; });
    if (item == m_Items.end() || item->outputPath == path) return;
    item->outputPath = path;
    ++m_Revision;
    ++m_PresentationRevision;
}

void RenderQueueModel::UpdateState(
    std::uint64_t id,
    ItemState state,
    float progress,
    std::string status) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto item = std::find_if(m_Items.begin(), m_Items.end(),
        [&](const Item& candidate) { return candidate.id == id; });
    if (item == m_Items.end()) return;
    progress = std::clamp(progress, 0.0f, 1.0f);
    if (item->state == state && item->progress == progress && item->status == status) return;
    const bool stateChanged = item->state != state;
    item->state = state;
    item->progress = progress;
    item->status = std::move(status);
    ++m_Revision;
    if (stateChanged) ++m_PresentationRevision;
}

bool RenderQueueModel::HasSelectedWaitingItems() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return std::any_of(m_Items.begin(), m_Items.end(), [](const Item& item) {
        return item.selected &&
            (item.state == ItemState::Waiting ||
             item.state == ItemState::Failed);
    });
}

} // namespace Stack::Queue
