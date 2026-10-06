#pragma once

#include "Async/TaskGroup.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "Persistence/ProjectFileStamp.h"
#include "Persistence/ProjectSaveCoordinator.h"
#include "Persistence/ProjectSessionController.h"
#include "Persistence/ProjectStore.h"
#include "Project/FileOperationState.h"
#include "Project/RawLayerStack.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawWorkspaceManagedGraph.h"

#include <memory>
#include <optional>
#include <vector>

class LayerBase;

namespace Stack::Project {

struct RawInteractionDraft {
    std::string sourceKey;
    RawRecipe::RawSourceReference source;
    RawRecipe::RawDevelopmentRecipe recipe;
    bool active = false;
};

// One editable document, independent of the editor that presents it. Shared
// services receive explicit snapshots or work from this instance. They never
// select a document through the active tab.
class ProjectSession {
public:
    ProjectSession();
    ~ProjectSession();
    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;

    const std::string& EnsureDocumentId();
    void NoteEdit(double nowSeconds);
    bool ClearDirtyIfRevision(std::uint64_t expectedRevision);

    EditorNodeGraph::Graph graph;
    std::vector<std::shared_ptr<LayerBase>> layers;
    Timeline::TimelineAnimationState timeline;
    std::string name;
    std::string fileName;
    std::string documentId;
    std::filesystem::path adoptionSourcePath;
    bool dirty = false;
    std::uint64_t editRevision = 0;
    double lastEditTime = 0.0;
    double lastAutosaveTime = -1.0;
    double lastAutosaveAttemptTime = -1.0;

    std::string rawSourceKey;
    std::filesystem::path storePath;
    ProjectStoreHandle store;
    std::shared_ptr<RawProjectSnapshot> snapshot;
    std::shared_ptr<const Raw::RawImageData> singleRawSource;
    RawRecipe::RawDevelopmentRecipe rawRecipe;
    RawLayerStackDocument rawLayers;
    // Per-project presentation state, excluded from authored processing and persistence.
    std::unordered_map<std::string, std::string> rawOperationSelection;
    RawInteractionDraft rawInteractionDraft;
    RawWorkspace::RawProjectMode rawMode = RawWorkspace::RawProjectMode::UnifiedLayers;
    RawWorkspace::ManagedRawSection managedRaw;
    bool rawPipelineActive = false;

    ProjectSessionController lifecycle;
    ProjectSaveCoordinator saves;
    std::shared_ptr<FileOperationState> files = std::make_shared<FileOperationState>();
    std::optional<ProjectFileStamp> savedFileStamp;
    std::uint64_t saveCheckGeneration = 0;
    bool saveCheckBusy = false;
    Async::TaskGroup tasks;
};

} // namespace Stack::Project
