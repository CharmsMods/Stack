#include "Raw/RawWorkspace.h"
#include "Persistence/ProjectStore.h"

#include <iostream>
#include <set>

namespace Stack::Validation {

bool ValidateProjectGallery(const std::filesystem::path& folder) {
    if (!std::filesystem::is_directory(folder)) return false;
    const auto layout = RawWorkspace::BuildManagedLayout(folder);
    std::vector<RawWorkspace::SourceRecord> sources;
    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = layout.workspaceRoot;
    // Read project manifests only. No camera decoding, image processing or
    // project writes are needed to check what the filmstrip will display.
    if (!RawWorkspace::DiscoverSourceSetProjects(layout, sources, state.sourceSetProjects)) return false;
    auto projects = RawWorkspace::BuildGalleryPresentation(state);
    RawWorkspace::FilterGalleryProjectCards(projects, RawWorkspace::GalleryContentMode::Projects);
    if (projects.projects.size() != state.sourceSetProjects.size()) return false;
    std::set<std::filesystem::path> paths;
    bool valid = true;
    for (const auto& project : state.sourceSetProjects) {
        if (!paths.insert(project.absolutePath).second) return false;
        const auto opened = Project::OpenProjectStore(project.absolutePath);
        if (!opened) {
            std::cerr << project.projectName << ": " << opened.message << '\n';
            valid = false;
        } else {
            std::cout << "PASS visible saved project: " << project.projectName
                << (project.bracketingProject ? " [Bracket]" : "") << '\n';
        }
    }
    std::cout << "Projects filmstrip contains " << projects.projects.size()
        << " saved projects from this folder and its subfolders.\n";
    return valid;
}

} // namespace Stack::Validation
