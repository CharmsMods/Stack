#pragma once

#include "App/ProjectLoadTransition.h"

#include <cstdint>
#include <memory>
#include <string>

class EditorModule;
namespace Stack::Project { class ProjectSession; }

// A workspace owns its editor for its entire lifetime. The permanent Gallery
// uses the same lifetime without a project tab. Workers and save callbacks
// keep targeting their owner when another workspace becomes visible.
struct ProjectWorkspace {
    explicit ProjectWorkspace(std::uint64_t workspaceId);
    ~ProjectWorkspace();
    ProjectWorkspace(const ProjectWorkspace&) = delete;
    ProjectWorkspace& operator=(const ProjectWorkspace&) = delete;

    std::uint64_t id;
    std::shared_ptr<Stack::Project::ProjectSession> project;
    std::unique_ptr<EditorModule> editor;
    int rootTab = 5;
    ProjectLoadTransition loadTransition;
    bool rawCatalogRefreshPending = false;
    std::uint64_t observedProjectCatalogRevision = 0;
    std::string previewDocumentId;
    std::uint64_t previewLoadGeneration = 0;
};
