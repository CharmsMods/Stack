#pragma once

#include "Raw/RawGallerySimilarity.h"
#include "RawLabGalleryScroll.h"
#include "RawLabFilmstripSlots.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <cstdint>

namespace Stack::Editor::RawLabInternal {

// Covers are temporary browsing state. Cycling never changes source selection
// or writes the catalog, and each editor owns its own state.
class GalleryGridStacks {
public:
    // Folder/transfer navigation is transient; only a delivered move changes
    // the existing workspace grouping model.
    std::array<std::vector<std::string>, 2> groups;
    std::vector<std::string> dragged;
    std::string scope;
    // The same expansion interaction is used for originals and saved versions.
    // Project cover identities are not members of the original-image catalog.
    bool projectGroups = false;
    std::uint64_t navigationRevision = 0;
    int groupCount = 0, openedFrame = -1, dragGroup = -1, pendingGroup = -1;
    std::vector<std::string> pendingMembers;
    float scaleTarget = -1.f;
    int columns = 0;
    GalleryScrollMotion filmstripScroll;
    FilmstripSlots filmstripSlots;
    void Open(const std::vector<std::string>& first, const std::vector<std::string>& second, int frame,
        bool projects = false) {
        groups = {first, second}; groupCount = second.empty() ? 1 : 2;
        projectGroups = projects;
        openedFrame = frame; ++navigationRevision;
    }
    void Close() {
        if (!groupCount) return;
        groups = {}; groupCount = 0; projectGroups = false; ++navigationRevision;
    }
    void Reconcile(const std::string& workspace,
        const std::vector<RawWorkspace::RawGallerySimilarityStack>& stacks);
    std::string Cover(const std::vector<std::string>& members,
        const std::string& selectedSourceKey);
    void UpdateHover(const std::vector<std::string>& members,
        bool hovered, bool controlDown, bool paused, int frame, double now);

private:
    std::string m_Workspace;
    std::unordered_map<std::string, std::string> m_Covers;
    std::string m_HoveredStack;
    int m_LastHoverFrame = -1;
    double m_NextCycleAt = -1.0;
};

} // namespace Stack::Editor::RawLabInternal
