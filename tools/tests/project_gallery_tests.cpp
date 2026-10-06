#include "Persistence/ProjectCatalogChanges.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawWorkspace.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

void TestProjectGalleryDiscovery() {
    namespace Project = Stack::Project;
    namespace Gallery = Stack::RawWorkspace;
    const auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    const auto root = std::filesystem::temp_directory_path() /
        ("stack-gallery-discovery-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto folder = root / "Photos";
    const auto layout = Gallery::BuildManagedLayout(folder);
    const auto create = [&](const auto& path, const char* id, bool raw, bool graph,
                            Project::ProjectStorageKind kind = Project::ProjectStorageKind::DirectoryBundle) {
        Project::RawProjectSnapshot snapshot;
        snapshot.projectId = snapshot.projectName = id;
        snapshot.projectKindHint = raw ? StackBinaryFormat::kRawProjectKind : StackBinaryFormat::kEditorProjectKind;
        if (graph) {
            EditorNodeGraph::Graph nodes;
            nodes.AddReformatNode({20, 30});
            snapshot.pipelineData = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), nodes);
        }
        const auto before = Project::ProjectCatalogRevision();
        const auto created = Project::CreateProjectStore(path, kind, snapshot);
        require(bool(created), "Could not save Gallery discovery fixture.");
        require(Project::ProjectCatalogRevision() > before, "A successful save did not invalidate browser catalogs.");
        const auto opened = Project::OpenProjectStore(path);
        require(opened && opened.snapshot.pipelineData == snapshot.pipelineData,
            "The saved Graph/RAW project did not retain its authored data.");
    };
    create(layout.projectsDirectory / "Raw", "raw", true, false);
    create(folder / "Day 1" / "Nested" / "Graph", "graph", false, true);
    create(folder / "Day 2" / "Closet" / "Projects" / "Combined", "raw-graph", true, true);
    create(folder / "Day 1" / "portable.stack", "portable", false, true, Project::ProjectStorageKind::PortableFile);
    create(root / "Other Photos" / "Outside", "outside", true, true);
    create(layout.projectTrashDirectory / "Deleted", "deleted", true, false);
    std::vector<Gallery::SourceRecord> sources;
    std::vector<Gallery::SourceSetProjectCatalogEntry> projects;
    require(Gallery::DiscoverSourceSetProjects(layout, sources, projects), "Gallery discovery failed.");
    require(projects.size() == 4, "Gallery omitted, duplicated or included an out-of-folder/Trash project.");
    for (const char* id : {"raw", "graph", "raw-graph", "portable"})
        require(std::count_if(projects.begin(), projects.end(), [&](const auto& p) { return p.projectId == id; }) == 1,
            "Gallery did not discover each saved project exactly once.");

    // Exercise the same category filter used by the filmstrip for all seven
    // nonempty combinations of Bracket, RAW and Graph editing tools.
    Gallery::GalleryPresentation presentation;
    for (unsigned tools = 1; tools < 8; ++tools) {
        Gallery::GalleryProjectView card;
        card.projectId = std::to_string(tools);
        card.bracketingProject = (tools & 1) != 0;
        presentation.projects.push_back(std::move(card));
    }
    Gallery::FilterGalleryProjectCards(presentation, Gallery::GalleryContentMode::Projects);
    require(presentation.projects.size() == 7, "Projects hid a saved document because of the tools it uses.");
    Gallery::FilterGalleryProjectCards(presentation, Gallery::GalleryContentMode::Bracket);
    require(presentation.projects.size() == 4, "Bracket did not retain just the bracket project combinations.");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "Folder-scoped recursive project discovery, portable/Graph saves, catalog invalidation and all tool combinations passed.\n";
}
