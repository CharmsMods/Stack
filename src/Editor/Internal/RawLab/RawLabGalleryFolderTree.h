#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::RawWorkspace { struct GalleryPresentation; struct GalleryProjectView; }

namespace Stack::Editor::RawLabInternal {

// Derived presentation data, rebuilt only when the folder/catalog changes.
struct GalleryFolderTree {
    struct Folder {
        std::map<std::string, Folder> children;
        std::string key, label, fullPath;
        Folder* parent = nullptr;
        std::array<std::size_t, 3> counts{};
        std::array<std::string, 3> countLabels;
        bool expanded = true;
    };
    Folder root;
    std::filesystem::path workspaceRoot;
    std::string workspaceKey, sourceName, parentPath, fullPath;
    std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t queueRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t serial = 0;
    bool allSourceFolders = false;
    // Rebuild only for a catalog/queue publication. No filesystem access.
    void Prepare(const std::filesystem::path& workspace,
        const RawWorkspace::GalleryPresentation& all,
        const std::vector<RawWorkspace::GalleryProjectView>& brackets,
        std::uint64_t catalogRevision, std::uint64_t bracketRevision,
        bool aggregate = false, const std::vector<std::filesystem::path>& sourceRoots = {});
    const Folder* Find(const std::string& key) const;
    // Lexical display keys only; no filesystem queries or ownership inference.
    static bool ContainsFolderKey(const std::string& folder, const std::string& key);

private:
    std::unordered_map<std::string, Folder*> m_Folders;
};

} // namespace Stack::Editor::RawLabInternal
