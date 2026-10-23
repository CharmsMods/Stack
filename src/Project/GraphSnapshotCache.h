#pragma once

#include "Project/GraphSnapshotBuilder.h"

#include <optional>
#include <tuple>

namespace Stack::Project {

// Cheap identities of inputs to compilation. Authored/save revision is omitted:
// placement changes it without changing executable content. Requests stamp that
// revision on their snapshot copy, while the compiled result remains immutable.
struct GraphSnapshotCacheKey {
    std::shared_ptr<const void> producer;
    std::string documentId, sourceKey, activeSourceSetId, rawProjectId;
    std::string mfdProjectId, mfdSourceSetId, hdrProjectId, hdrSourceSetId;
    std::uint64_t renderRevision = 0, structureRevision = 0, rawProcessingRevision = 0, dirtyGeneration = 0;
    int frame = 0, durationFrames = 0, framesPerSecond = 0, stageOutputNodeId = 0, liveEditPreviewFrame = -1;
    bool inspection = false, rawLayersEnabled = false;
    Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
    std::shared_ptr<const Raw::RawImageData> singleRawSource;
    std::shared_ptr<const RawProjectSnapshot> rawProject;
    std::uint64_t mfdInputRevision = 0, hdrInputRevision = 0, postRecipeRevision = 0, previewSourceHash = 0;
    std::shared_ptr<const Raw::RawImageData> mfdSource, hdrSource;
    std::uint64_t mfdContentHash = 0, hdrContentHash = 0, adoptedMfdRevision = 0, adoptedHdrRevision = 0;

    bool operator==(const GraphSnapshotCacheKey& other) const {
        const auto fields = [](const GraphSnapshotCacheKey& key) {
            return std::tie(key.producer,key.documentId,key.sourceKey,key.activeSourceSetId,key.rawProjectId,
                key.mfdProjectId,key.mfdSourceSetId,key.hdrProjectId,key.hdrSourceSetId,
                key.renderRevision,key.structureRevision,key.rawProcessingRevision,key.dirtyGeneration,
                key.frame,key.durationFrames,key.framesPerSecond,key.stageOutputNodeId,key.liveEditPreviewFrame,
                key.inspection,key.rawLayersEnabled,key.workingSpace,key.singleRawSource,key.rawProject,
                key.mfdInputRevision,key.hdrInputRevision,key.postRecipeRevision,key.previewSourceHash,
                key.mfdSource,key.hdrSource,key.mfdContentHash,key.hdrContentHash,key.adoptedMfdRevision,key.adoptedHdrRevision);
        };
        return fields(*this) == fields(other);
    }
};

// The project retains at most one ordinary result and one stage request. The
// immutable results own/shared-retain their input buffers; no live graph or
// editor reference escapes into a render worker. Failed results are cached too.
struct GraphSnapshotCache {
    struct Entry { GraphSnapshotCacheKey key; std::shared_ptr<const GraphSnapshotResult> result; };
    std::optional<Entry> ordinary, stage;

    std::shared_ptr<const GraphSnapshotResult> Find(const GraphSnapshotCacheKey& key) const {
        const auto& entry = key.stageOutputNodeId > 0 ? stage : ordinary;
        return entry && entry->key == key ? entry->result : nullptr;
    }
    void Store(GraphSnapshotCacheKey key, std::shared_ptr<const GraphSnapshotResult> result) {
        auto& entry = key.stageOutputNodeId > 0 ? stage : ordinary;
        entry = Entry{std::move(key),std::move(result)};
    }
};

} // namespace Stack::Project
