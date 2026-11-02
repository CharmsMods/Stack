#include "Raw/RawWorkspace.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/RawGalleryActions.h"
#include "Raw/RawSourceFolders.h"
#include <fstream>
#include "Persistence/ProjectStore.h"
#include "ThirdParty/stb_image.h"

#include <algorithm>
#include <iostream>
#include <set>

namespace Stack::Validation {

bool ValidateGalleryTrash(const std::filesystem::path& scratch) {
    const auto root = std::filesystem::absolute(scratch) / Project::GenerateStableUuid();
    const auto layout = RawWorkspace::BuildManagedLayout(root);
    std::filesystem::create_directories(root);
    if (!RawWorkspace::EnsureManagedFolders(root)) return false;
    const auto first = root / "one" / "same.dng";
    const auto second = root / "two" / "same.dng";
    const auto asset = layout.dataDirectory / "Assets" / "shared.dng";
    for (const auto& path : {first, second, asset}) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << path.parent_path().filename().string();
    }
    const auto moved = RawGalleryFileActions::DeleteSourcesToTrash(root, {first, asset, second});
    if (moved.moved != 2 || moved.errors.size() != 1 || !std::filesystem::exists(asset) ||
        std::filesystem::exists(first) || std::filesystem::exists(second)) return false;
    for (size_t i = 0; i < moved.destinations.size(); ++i) {
        std::string contents;
        std::ifstream(moved.destinations[i]) >> contents;
        if (moved.destinations[i].parent_path() != layout.sourceTrashDirectory ||
            contents != (i == 0 ? "one" : "two")) return false;
    }
    Project::RawProjectSnapshot snapshot;
    snapshot.projectId = Project::GenerateStableUuid();
    snapshot.projectName = "Trash validation";
    const auto project = layout.projectsDirectory / "saved.stack";
    if (!Project::CreateProjectStore(project, Project::ProjectStorageKind::WorkingFile, snapshot)) return false;
    const auto bundled = RawGalleryFileActions::DeleteProjectsToTrash(root, {project});
    if (!bundled || std::filesystem::exists(project) || !Project::OpenProjectStore(bundled.destinations.front())) return false;
    const auto portable = root / "portable.stack";
    if (!Project::CreateProjectStore(portable, Project::ProjectStorageKind::PortableFile, snapshot)) return false;
    const auto packed = RawGalleryFileActions::DeleteProjectsToTrash(root, {portable});
    if (!packed || std::filesystem::exists(portable) || !Project::OpenProjectStore(packed.destinations.front())) return false;
    RawGalleryActions::Context all;
    all.aggregateView = true;
    all.items.emplace_back();
    if (!RawGalleryActions::GetAvailability(all, RawGalleryActions::Action::MoveToTrash).enabled) return false;
    const auto firstOwner = root.parent_path() / Project::GenerateStableUuid();
    std::filesystem::create_directories(firstOwner);
    const auto other = root.parent_path() / Project::GenerateStableUuid();
    std::filesystem::create_directories(other);
    for (const auto& owner : {firstOwner, other}) std::ofstream(owner / "cross-folder.dng") << owner.filename().string();
    const auto combined = RawWorkspace::AggregateSourceFolders({firstOwner, other});
    std::set<std::filesystem::path> trashFolders;
    for (const auto& source : combined.sources) {
        if (source.absolutePath.filename() != "cross-folder.dng") continue;
        const auto owner = RawWorkspace::ResolveSourceWorkspaceRoot(source, {});
        const auto removed = RawGalleryFileActions::DeleteSourcesToTrash(owner, {source.absolutePath});
        if (!removed || owner != source.absolutePath.parent_path() ||
            removed.destinations.front().parent_path() != RawWorkspace::BuildManagedLayout(owner).sourceTrashDirectory)
            return false;
        trashFolders.insert(removed.destinations.front().parent_path());
    }
    if (trashFolders.size() != 2) return false;
    std::cout << "PASS All Folders captures moved to two distinct owning Closets\n";
    std::cout << "PASS both originals retained with distinct names, shared asset protected, complete trashed project reopened\n";
    return true;
}

bool ValidateProjectGallery(const std::filesystem::path& folder) {
    if (!std::filesystem::is_directory(folder)) return false;
    const auto layout = RawWorkspace::BuildManagedLayout(folder);
    std::vector<RawWorkspace::SourceRecord> sources;
    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = layout.workspaceRoot;
    // Read project manifests only. No camera decoding, image processing or
    // project writes are needed to check what the filmstrip will display.
    if (!RawWorkspace::DiscoverSourceSetProjects(layout, sources, state.sourceSetProjects)) return false;
    // Link a distinct probe source to each on-disk copy, including copies
    // that retained the same document ID when saved to different paths.
    for (const auto& project : state.sourceSetProjects) {
        auto& source = state.sources.emplace_back();
        source.relativePathKey = project.absolutePath.generic_u8string();
        auto& membership = source.sourceSetProjectMemberships.emplace_back();
        membership.projectId = project.projectId;
        membership.projectPath = project.absolutePath;
    }
    auto projects = RawWorkspace::BuildGalleryPresentation(state);
    for (const auto& group : projects.groups) {
        for (const auto& source : group.sources) {
            const auto& project = state.sourceSetProjects.at(source.sourceIndex);
            if (!source.representsProject || source.projectPath != project.absolutePath ||
                source.projectCoverThumbnailCachePath != project.coverThumbnailCachePath ||
                source.projectCoverKey != projects.projects.at(source.sourceIndex).projectCoverKey) {
                std::cerr << "FAIL source resolved to another project copy's cover\n";
                return false;
            }
        }
    }
    std::set<std::string> coverKeys;
    for (const auto& project : projects.projects) {
        if (project.projectCoverKey.empty() || !coverKeys.insert(project.projectCoverKey).second) {
            std::cerr << "FAIL shared or missing cover key for " << project.projectPath << '\n';
            return false;
        }
    }
    RawWorkspace::FilterGalleryProjectCards(projects, RawWorkspace::GalleryContentMode::Projects);
    auto brackets = RawWorkspace::BuildGalleryPresentation(state);
    RawWorkspace::FilterGalleryProjectCards(brackets, RawWorkspace::GalleryContentMode::Bracket);
    std::set<std::filesystem::path> paths;
    bool valid = true;
    for (const auto& project : state.sourceSetProjects) {
        if (!paths.insert(project.absolutePath).second) return false;
        const auto opened = Project::OpenProjectStore(project.absolutePath);
        if (!opened) {
            std::cerr << project.projectName << ": " << opened.message << '\n';
            valid = false;
        } else {
            const auto contains = [&](const auto& category) {
                return std::any_of(category.projects.begin(), category.projects.end(),
                    [&](const auto& item) { return item.projectPath == project.absolutePath; });
            };
            if (contains(projects) == project.untouchedAutomaticBracket ||
                contains(brackets) != project.bracketingProject) return false;
            std::cout << (project.untouchedAutomaticBracket ? "PASS Brackets only: " : "PASS visible saved project: ") << project.projectName
                << (project.bracketingProject ? " [Bracket]" : "") << '\n';
            const auto& cover = opened.snapshot.coverThumbnailBytes;
            if (!cover.empty()) {
                int width=0,height=0,channels=0;
                auto* pixels=stbi_load_from_memory(cover.data(),static_cast<int>(cover.size()),&width,&height,&channels,4);
                if (!pixels) { std::cerr << "FAIL saved cover cannot decode\n"; valid=false; }
                else std::cout << "PASS saved cover decodes: " << width << 'x' << height << '\n';
                stbi_image_free(pixels);
            } else std::cout << "Saved project has no embedded cover.\n";
            if (opened.recoveredPreviousManifest) std::cout << "Project opened its previous recovery manifest.\n";
        }
    }
    std::cout << "Projects filmstrip contains " << projects.projects.size()
        << " saved projects from this folder and its subfolders.\n";
    return valid;
}

} // namespace Stack::Validation
