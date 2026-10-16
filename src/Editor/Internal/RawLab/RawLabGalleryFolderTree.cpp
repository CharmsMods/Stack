#include "RawLabGalleryFolderTree.h"
#include "Raw/RawWorkspace.h"

#include <algorithm>
#include <unordered_map>

namespace Stack::Editor::RawLabInternal {
namespace {
std::string CountLabel(std::size_t count) {
    std::string label = std::to_string(count);
    for (std::size_t offset = label.size(); offset > 3; offset -= 3)
        label.insert(offset - 3, 1, ',');
    return label;
}
}

void GalleryFolderTree::Prepare(const std::filesystem::path& workspace,
    const RawWorkspace::GalleryPresentation& all,
    const std::vector<RawWorkspace::GalleryProjectView>& brackets,
    std::uint64_t catalogRevision, std::uint64_t bracketRevision) {
    std::unordered_map<std::string, bool> expanded;
    if (workspaceRoot == workspace)
        for (const auto& [key, folder] : m_Folders) expanded.emplace(key, folder->expanded);
    if (workspaceRoot != workspace || serial == 0) {
        workspaceRoot = workspace;
        workspaceKey = workspace.lexically_normal().generic_string();
        fullPath = workspace.u8string();
        sourceName = workspace.filename().u8string();
        if (sourceName.empty()) sourceName = fullPath;
        parentPath = workspace.parent_path().u8string();
    }
    m_Folders.clear();
    root = {};
    root.label = "All folders";
    root.fullPath = fullPath;
    const auto folderFor = [&](const std::string& folderKey) {
        Folder* folder = &root;
        std::filesystem::path relative;
        for (const auto& part : std::filesystem::u8path(folderKey)) {
            if (part.empty() || part == ".") continue;
            relative /= part;
            auto& child = folder->children[part.u8string()];
            if (child.key.empty()) {
                child.parent = folder;
                child.label = part.u8string();
                child.key = relative.generic_u8string();
                child.fullPath = (workspace / relative).u8string();
                if (const auto previous = expanded.find(child.key); previous != expanded.end())
                    child.expanded = previous->second;
                m_Folders.emplace(child.key, &child);
            }
            folder = &child;
        }
        return folder;
    };
    const auto add = [](Folder* folder, std::size_t category, std::size_t count) {
        for (; folder; folder = folder->parent) folder->counts[category] += count;
    };
    std::unordered_map<std::string, Folder*> sourceFolders;
    sourceFolders.reserve(static_cast<std::size_t>(std::max(0, all.totalSources)));
    for (const auto& group : all.groups) {
        auto* folder = folderFor(group.folderKey);
        add(folder, 0, group.sources.size());
        for (const auto& source : group.sources) sourceFolders.emplace(source.relativePathKey, folder);
    }
    const auto projectFolder = [&](const RawWorkspace::GalleryProjectView& project) {
        const auto source = sourceFolders.find(project.referenceSourceKey);
        return source == sourceFolders.end() ? &root : source->second;
    };
    // Saved projects includes saved bracket documents. Pending brackets occur
    // only in the narrower Brackets view, matching its existing presentation.
    for (const auto& project : all.projects) add(projectFolder(project), 1, 1);
    for (const auto& project : brackets) add(projectFolder(project), 2, 1);
    const auto format = [&](const auto& self, Folder& folder) -> void {
        for (std::size_t index = 0; index < folder.counts.size(); ++index)
            folder.countLabels[index] = CountLabel(folder.counts[index]);
        for (auto& [label, child] : folder.children) self(self, child);
    };
    format(format, root);
    revision = catalogRevision;
    queueRevision = bracketRevision;
    ++serial;
}

const GalleryFolderTree::Folder* GalleryFolderTree::Find(const std::string& key) const {
    const auto found = m_Folders.find(key);
    return found == m_Folders.end() ? nullptr : found->second;
}
} // namespace Stack::Editor::RawLabInternal
