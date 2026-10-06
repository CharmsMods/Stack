#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace Stack::RawGalleryActions {

enum class Action {
    Open,
    CreateCaptureSet,
    AddToQueue,
    CopyEdits,
    PasteEdits,
    ShowInExplorer,
    CopyPath,
    NewSavedVersion,
    TrashProjects,
    RevertProjects
};

enum class ItemKind { Source, Project };

struct Item {
    ItemKind kind = ItemKind::Source;
    std::filesystem::path path;
    std::string sourceKey;
    std::filesystem::path thumbnailPath;
    bool canCopyEdits = false;
    bool canVersion = false;
    bool protectedProject = false;
};

// Requests carry the selection that initiated the operation. Hover state and
// the next active tab are never used to resolve an already captured request.
struct Context {
    std::filesystem::path workspaceRoot;
    std::uint64_t catalogGeneration = 0;
    std::vector<Item> items;
    std::vector<std::filesystem::path> bracketSources;
    std::size_t focusedIndex = std::numeric_limits<std::size_t>::max();
    bool openAvailable = false;
    bool queueAvailable = false;
    bool hasEditClipboard = false;
    bool foregroundBusy = false;
    bool previewOnly = false;
};

struct Availability {
    bool enabled = false;
    std::string reason;
};

Availability GetAvailability(const Context& context, Action action);
const char* Label(Action action);

} // namespace Stack::RawGalleryActions
