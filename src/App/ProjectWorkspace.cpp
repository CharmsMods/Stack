#include "ProjectWorkspace.h"
#include "Editor/EditorModule.h"
#include "Project/ProjectSession.h"

ProjectWorkspace::ProjectWorkspace(std::uint64_t workspaceId)
    : id(workspaceId), project(std::make_shared<Stack::Project::ProjectSession>()),
      editor(std::make_unique<EditorModule>(project)) {}

ProjectWorkspace::~ProjectWorkspace() = default;
