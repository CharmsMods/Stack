#include "Queue/RenderQueueModel.h"

#include "Persistence/ProjectIndex.h"
#include "Persistence/ProjectStore.h"

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
    std::string value = NormalizePath(path).generic_string();
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
    item.displayName = item.path.stem().string();
    Stack::Project::ProjectRecord record;
    if (Stack::Project::ProjectIndex::Get().FindByPath(item.path, record)) {
        item.displayName = record.displayName.empty()
            ? item.displayName : record.displayName;
        item.projectId = record.projectId;
        item.thumbnailBytes = std::move(record.coverThumbnailBytes);
    }
    return item;
}

Item MakeSourceItem(const Stack::RawGalleryQueue::SourceRequest& source) {
    Item item;
    item.kind = ItemKind::SourceImage;
    item.path = NormalizePath(source.sourcePath);
    item.displayName = item.path.stem().string();
    item.thumbnailBytes = ReadThumbnailBytes(source.thumbnailPath);
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

RenderQueueModel::AddStatus RenderQueueModel::AddItem(Item item) {
    const std::string key = PathKey(item.path);
    if (key.empty()) return AddStatus::Skipped;
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto existing = std::find_if(
        m_Items.begin(), m_Items.end(), [&](const Item& candidate) {
            return candidate.kind == item.kind &&
                PathKey(candidate.path) == key;
        });
    if (existing != m_Items.end()) {
        existing->selected = true;
        return AddStatus::AlreadyPresent;
    }
    item.id = m_NextId++;
    item.selected = true;
    item.state = ItemState::Waiting;
    item.progress = 0.0f;
    m_Items.push_back(std::move(item));
    return AddStatus::Added;
}

std::vector<Item> RenderQueueModel::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Items;
}

void RenderQueueModel::SetSelected(std::uint64_t id, bool selected) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto item = std::find_if(m_Items.begin(), m_Items.end(),
        [&](const Item& candidate) { return candidate.id == id; });
    if (item != m_Items.end()) item->selected = selected;
}

void RenderQueueModel::SetAllSelected(bool selected) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (Item& item : m_Items) item.selected = selected;
}

void RenderQueueModel::Remove(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Items.erase(
        std::remove_if(m_Items.begin(), m_Items.end(),
            [&](const Item& item) { return item.id == id; }),
        m_Items.end());
}

void RenderQueueModel::ClearCompleted() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Items.erase(
        std::remove_if(m_Items.begin(), m_Items.end(),
            [](const Item& item) {
                return item.state == ItemState::Complete;
            }),
        m_Items.end());
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
    item->state = state;
    item->progress = std::clamp(progress, 0.0f, 1.0f);
    item->status = std::move(status);
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
