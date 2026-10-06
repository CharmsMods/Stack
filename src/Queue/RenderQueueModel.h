#pragma once

#include "Raw/RawGalleryQueueRequest.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Stack::Queue {

enum class ItemKind {
    Project,
    SourceImage
};

enum class ItemState {
    Waiting,
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
    std::vector<unsigned char> thumbnailBytes;
    bool selected = true;
    ItemState state = ItemState::Waiting;
    float progress = 0.0f;
    std::string status;
};

class RenderQueueModel {
public:
    Stack::RawGalleryQueue::Result AddGalleryRequest(
        const Stack::RawGalleryQueue::Request& request);
    std::size_t AddProjects(
        const std::vector<std::filesystem::path>& projectPaths);
    std::size_t AddSources(
        const std::vector<std::filesystem::path>& sourcePaths);
    std::size_t AddSourceRequests(
        const std::vector<Stack::RawGalleryQueue::SourceRequest>& sources);

    std::vector<Item> Snapshot() const;
    void SetSelected(std::uint64_t id, bool selected);
    void SetAllSelected(bool selected);
    void Remove(std::uint64_t id);
    void ClearCompleted();
    void UpdateState(
        std::uint64_t id,
        ItemState state,
        float progress,
        std::string status = {});
    bool HasSelectedWaitingItems() const;

private:
    enum class AddStatus { Added, AlreadyPresent, Skipped };
    AddStatus AddItem(Item item);

    mutable std::mutex m_Mutex;
    std::vector<Item> m_Items;
    std::uint64_t m_NextId = 1;
};

} // namespace Stack::Queue
