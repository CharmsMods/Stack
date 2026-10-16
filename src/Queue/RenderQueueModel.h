#pragma once

#include "Raw/RawGalleryQueueRequest.h"
#include "Queue/ProjectQueueRequest.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Queue {

enum class ItemKind {
    Project,
    SourceImage,
    ProjectSnapshot
};

enum class ItemState {
    Waiting,
    Queued,
    Preparing,
    Rendering,
    Writing,
    Complete,
    Failed
};

struct Item {
    std::uint64_t id = 0;
    ItemKind kind = ItemKind::Project;
    std::filesystem::path path;
    std::string displayName;
    std::string projectId;
    std::shared_ptr<const std::vector<unsigned char>> thumbnailBytes;
    std::shared_ptr<const Stack::Project::LoadedProjectData> projectSnapshot;
    std::filesystem::path outputPath;
    bool selected = true;
    ItemState state = ItemState::Waiting;
    float progress = 0.0f;
    std::string status;
};

class RenderQueueModel {
public:
    Stack::RawGalleryQueue::Result AddGalleryRequest(
        const Stack::RawGalleryQueue::Request& request);
    Stack::RawGalleryQueue::Result AddProjectSnapshot(
        const ProjectRequest& request);
    std::size_t AddProjects(
        const std::vector<std::filesystem::path>& projectPaths);
    std::size_t AddSources(
        const std::vector<std::filesystem::path>& sourcePaths);
    std::size_t AddSourceRequests(
        const std::vector<Stack::RawGalleryQueue::SourceRequest>& sources);

    std::vector<Item> Snapshot() const;
    // Progress/status stay live in the snapshot without invalidating prepared
    // membership, inclusion, state counts, and output-location presentation.
    bool SnapshotIfChanged(std::vector<Item>& items, std::uint64_t& revision,
        std::uint64_t* presentationRevision = nullptr) const;
    // Atomically claim still-eligible model entries. Callers receive owned
    // immutable inputs; stale UI snapshots cannot reschedule active jobs.
    std::vector<Item> ClaimForExport(const std::vector<Item>& requested);
    void ReleaseQueued(const std::vector<std::uint64_t>& ids);
    void SetSelected(std::uint64_t id, bool selected);
    void SetAllSelected(bool selected);
    void Remove(std::uint64_t id);
    void ClearCompleted();
    void SetOutputPath(std::uint64_t id, const std::filesystem::path& path);
    void UpdateState(
        std::uint64_t id,
        ItemState state,
        float progress,
        std::string status = {});
    bool HasSelectedWaitingItems() const;

private:
    enum class AddStatus { Added, AlreadyPresent, Skipped };
    AddStatus AddItem(Item item, bool deduplicate = true);

    mutable std::mutex m_Mutex;
    std::vector<Item> m_Items;
    std::unordered_map<std::string, std::uint64_t> m_PathItems;
    std::unordered_map<std::uint64_t, std::string> m_ItemKeys;
    std::uint64_t m_NextId = 1;
    std::uint64_t m_Revision = 1;
    std::uint64_t m_PresentationRevision = 1;
};

} // namespace Stack::Queue
