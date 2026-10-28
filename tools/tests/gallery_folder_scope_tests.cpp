// Native aggregate + navigator regression. Named scan/storage doubles isolate
// folder-key presentation from disk discovery, image decoding and project IO.
#include "Raw/RawSourceFolders.h"
#include "Editor/Internal/RawLab/RawLabGalleryFolderTree.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace Gallery = Stack::RawWorkspace;
using Tree = Stack::Editor::RawLabInternal::GalleryFolderTree;

namespace Stack::Project {
std::filesystem::path ResolveProjectStoreRoot(const std::filesystem::path& path) { return path; }
bool IsDirectoryProjectBundle(const std::filesystem::path&) { return false; }
}

namespace Stack::RawWorkspace {
ManagedLayout BuildManagedLayout(const std::filesystem::path& path) {
    ManagedLayout layout;
    layout.workspaceRoot = std::filesystem::absolute(path).lexically_normal();
    layout.dataDirectory = layout.workspaceRoot / "Closet";
    return layout;
}
bool LoadCatalogSnapshot(const ManagedLayout&, std::vector<SourceRecord>&,
    std::string*, std::string*, std::vector<SourceSetProjectCatalogEntry>*) { return false; }
bool DefaultRawPathPredicate(const std::filesystem::path&) { return true; }
ThumbnailStatus ClassifyThumbnailMetadata(const ManagedLayout&, SourceRecord&, int) {
    return ThumbnailStatus::Missing;
}
bool DiscoverWorkspaceProjects(const ManagedLayout&, std::vector<SourceRecord>&,
    std::vector<SourceSetProjectCatalogEntry>&, CancellationPredicate) { return true; }
ScanResult ScanWorkspace(const std::filesystem::path& root, RawPathPredicate,
    ScanProgressCallback, CancellationPredicate, ScanSourceReadyCallback, SourceIdentityReuseCallback) {
    ScanResult result;
    result.success = true;
    result.layout = BuildManagedLayout(root);
    for (const char* folder : {"", "Day1", "Day1/Nested", "Day10"}) {
        SourceRecord source;
        source.relativePath = std::filesystem::path(folder) / "stack-gallery-key-fixture.dng";
        source.absolutePath = root / source.relativePath;
        source.relativePathKey = source.relativePath.generic_u8string();
        source.parentFolderKey = folder;
        result.sources.push_back(std::move(source));
    }
    return result;
}
}

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckScope(const std::filesystem::path& root) {
    const auto result = Gallery::AggregateSourceFolders({root});
    Require(result.success && result.sources.size() == 4, "Aggregate fixture was not published.");
    Gallery::GalleryPresentation presentation;
    for (const auto& source : result.sources) {
        Gallery::GalleryFolderGroup group;
        group.folderKey = source.parentFolderKey;
        Gallery::GallerySourceView view;
        view.relativePathKey = source.relativePathKey;
        group.sources.push_back(std::move(view));
        presentation.groups.push_back(std::move(group));
    }
    presentation.totalSources = 4;
    Tree tree;
    tree.Prepare(root, presentation, {}, 1, 0, true, {root});
    const auto rootKey = root.generic_u8string();
    const auto dayKey = (root / "Day1").generic_u8string();
    const auto* day = tree.Find(dayKey);
    Require(day && day->counts[0] == 2, "Navigator omitted nested captures.");
    std::size_t matched = 0;
    for (const auto& source : result.sources) {
        Require(tree.Find(source.parentFolderKey) != nullptr,
            "Aggregate folder key differs from the navigator's selectable key.");
        Require(Tree::ContainsFolderKey(rootKey, source.parentFolderKey),
            "Selecting a separator-ended source root hides descendants.");
        if (Tree::ContainsFolderKey(dayKey, source.parentFolderKey)) ++matched;
        const auto native = Gallery::NativeSourceRecord(source);
        Require(native.relativePathKey == native.relativePath.generic_u8string(),
            "Display folder qualification changed native source identity.");
        Require(source.sourceWorkspaceRoot == root, "Display folder qualification changed source ownership.");
    }
    Require(matched == day->counts[0], "Selected folder contents differ from its sidebar count.");
}
}

int main() try {
    const auto root = (std::filesystem::temp_directory_path() / "stack-gallery-scope-no-files").lexically_normal();
    CheckScope(root);
    CheckScope(root / "");
    CheckScope(root.root_path());
    Require(Tree::ContainsFolderKey("Day1", "Day1/Nested"), "Relative subfolder omitted.");
    Require(!Tree::ContainsFolderKey("Day1", "Day10"), "Sibling prefix admitted.");
    Require(!Tree::ContainsFolderKey("Day1", "Day"), "Parent admitted as a descendant.");
    Require(Tree::ContainsFolderKey("C:/", "C:/Day1"), "Drive-root display key omitted descendants.");
    Require(!Tree::ContainsFolderKey("C:/", "D:/Day1"), "Different drive admitted.");
    Require(Tree::ContainsFolderKey("//server/share/", "//server/share/Day1"), "UNC display root omitted descendants.");
    Require(!Tree::ContainsFolderKey("//server/share", "//server/share2/Day1"), "Different UNC share admitted.");
    Require(Tree::ContainsFolderKey("", "") && !Tree::ContainsFolderKey("", "Day1"),
        "Empty display key unexpectedly includes every folder.");
    std::cout << "Gallery folder scope tests passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
