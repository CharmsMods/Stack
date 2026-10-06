#include "RawLabGalleryGridStacks.h"

#include <algorithm>
#include <unordered_set>

namespace Stack::Editor::RawLabInternal {

void GalleryGridStacks::Reconcile(const std::string& workspace,
    const std::vector<RawWorkspace::RawGallerySimilarityStack>& stacks) {
    if (m_Workspace != workspace) {
        *this = {};
        m_Workspace = workspace;
    }
    std::unordered_set<std::string> current;
    current.reserve(stacks.size());
    for (const auto& stack : stacks) {
        if (stack.sourceKeys.empty()) continue;
        const auto& key = stack.sourceKeys.front();
        current.insert(key);
        const auto cover = m_Covers.find(key);
        if (cover != m_Covers.end() &&
            std::find(stack.sourceKeys.begin(), stack.sourceKeys.end(),
                cover->second) == stack.sourceKeys.end()) {
            m_Covers.erase(cover);
        }
    }
    for (auto cover = m_Covers.begin(); cover != m_Covers.end();) {
        if (current.find(cover->first) == current.end()) cover = m_Covers.erase(cover);
        else ++cover;
    }
    if (current.find(m_HoveredStack) == current.end()) {
        m_HoveredStack.clear();
        m_LastHoverFrame = -1;
        m_NextCycleAt = -1.0;
    }
}

std::string GalleryGridStacks::Cover(const std::vector<std::string>& members,
    const std::string& selectedSourceKey) {
    if (members.empty()) return {};
    auto [cover, inserted] = m_Covers.try_emplace(members.front());
    if (inserted) {
        cover->second = RawWorkspace::ResolveRawGalleryStackCover(members, selectedSourceKey);
    }
    return cover->second;
}

void GalleryGridStacks::UpdateHover(const std::vector<std::string>& members,
    bool hovered, bool controlDown, bool paused, int frame, double now) {
    if (members.empty()) return;
    const auto& key = members.front();
    if (!hovered || !controlDown || paused || members.size() < 2u) {
        if (m_HoveredStack == key) {
            m_HoveredStack.clear();
            m_LastHoverFrame = -1;
            m_NextCycleAt = -1.0;
        }
        return;
    }
    constexpr double kCycleSeconds = 0.65;
    if (m_HoveredStack != key || m_LastHoverFrame != frame - 1) {
        m_HoveredStack = key;
        m_NextCycleAt = now + kCycleSeconds;
    } else if (now >= m_NextCycleAt) {
        auto& cover = m_Covers[key];
        const auto member = std::find(members.begin(), members.end(), cover);
        const std::size_t next = member == members.end() ? 0u :
            (static_cast<std::size_t>(member - members.begin()) + 1u) % members.size();
        cover = members[next];
        m_NextCycleAt = now + kCycleSeconds;
    }
    m_LastHoverFrame = frame;
}

} // namespace Stack::Editor::RawLabInternal
