#pragma once

#include "Raw/RawGallerySimilarity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Editor::RawLabInternal {

// Covers are temporary browsing state. Cycling never changes source selection
// or writes the catalog, and each editor owns its own state.
class GalleryGridStacks {
public:
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
