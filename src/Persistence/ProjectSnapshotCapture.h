#pragma once
#include "Persistence/RawProjectModel.h"
namespace Stack::Project {
inline RawProjectSnapshot CopyProjectSnapshotWithoutPipeline(
    const RawProjectSnapshot& source) {
    RawProjectSnapshot copy;
    copy.schemaVersion = source.schemaVersion;
    copy.projectId = source.projectId;
    copy.projectName = source.projectName;
    copy.projectKindHint = source.projectKindHint;
    copy.lifecycle = source.lifecycle;
    copy.embeddedAssets = source.embeddedAssets;
    copy.sourceSets = source.sourceSets;
    copy.multiFrameGraph = source.multiFrameGraph;
    copy.rawWorkspaceData = source.rawWorkspaceData;
    copy.coverThumbnailBytes = source.coverThumbnailBytes;
    copy.activeSourceSetId = source.activeSourceSetId;
    copy.activeFrameId = source.activeFrameId;
    copy.mfdInputRevision = source.mfdInputRevision;
    copy.hdrInputRevision = source.hdrInputRevision;
    copy.postRecipeRevision = source.postRecipeRevision;
    copy.dirtyRevision = source.dirtyRevision;
    copy.persistedStorageRevision = source.persistedStorageRevision;
    copy.timestamp = source.timestamp;
    copy.sourceWidth = source.sourceWidth;
    copy.sourceHeight = source.sourceHeight;
    copy.adoptedFrom = source.adoptedFrom;
    copy.sourceAssetId = source.sourceAssetId;
    copy.nodeBrowserThumbnails = source.nodeBrowserThumbnails;
    return copy;
}

inline void UpdateWorkspaceManifestFields(RawProjectSnapshot& snapshot) {
    if (!snapshot.rawWorkspaceData.is_object()) {
        snapshot.rawWorkspaceData = nlohmann::json::object();
    }
    snapshot.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
    snapshot.rawWorkspaceData["schemaVersion"] =
        Stack::Project::kRawWorkspaceProjectSchemaVersion;
    snapshot.rawWorkspaceData.erase("rawWorkspaceSchemaVersion");
    snapshot.rawWorkspaceData["rawProjectModel"] =
        Stack::Project::kRawProjectModelSourceSets;
    snapshot.rawWorkspaceData["projectId"] = snapshot.projectId;
    snapshot.rawWorkspaceData["activeSourceSetId"] = snapshot.activeSourceSetId;
    snapshot.rawWorkspaceData["activeFrameId"] = snapshot.activeFrameId;
}

}
