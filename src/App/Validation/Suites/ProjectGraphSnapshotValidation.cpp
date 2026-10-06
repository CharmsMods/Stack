#include "App/Validation/Suites/ProjectGraphSnapshotValidation.h"

#include "Editor/EditorModule.h"
#include "Project/GraphSnapshotBuilder.h"
#include "Project/ProjectSession.h"
#include "Raw/RawImageData.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void RequireSnapshot(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int MakeRawGraph(Project::ProjectSession& project, const char* path, float exposure) {
    project.rawRecipe = RawRecipe::MakeDefaultRecipe(path);
    project.rawRecipe.preToneExposureEv = exposure;
    auto source = std::make_shared<Raw::RawImageData>();
    // Metadata alone exercises lowering; no synthetic image pixels or GPU work.
    source->metadata.rawWidth = source->metadata.visibleWidth = 32;
    source->metadata.rawHeight = source->metadata.visibleHeight = 16;
    project.singleRawSource = source;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = project.rawRecipe;
    const auto* raw = project.graph.AddRawDevelopmentNode(std::move(payload), {20, 20});
    RequireSnapshot(raw != nullptr, "Could not construct a RAW graph for snapshot validation.");
    const int rawId = raw->id;
    const auto* output = project.graph.AddOutputNode({300, 20}, true);
    RequireSnapshot(output && project.graph.TryConnect(rawId, output->id),
        "Could not connect the RAW graph for snapshot validation.");
    return rawId;
}

const RenderGraphNode& FindRawNode(const RenderGraphSnapshot& snapshot, int id) {
    const auto found = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
        [id](const auto& node) { return node.nodeId == id; });
    RequireSnapshot(found != snapshot.nodes.end(), "Snapshot lost its RAW node.");
    return *found;
}
}

void ValidateProjectGraphSnapshotBuilder() {
    auto first = std::make_shared<Project::ProjectSession>();
    Project::ProjectSession second;
    const int firstRaw = MakeRawGraph(*first, "snapshot-owner-a.raw", 0.25f);
    const int secondRaw = MakeRawGraph(second, "snapshot-owner-b.raw", 1.5f);
    std::unordered_map<int, std::uint64_t> firstDirty{{firstRaw, 7}};
    const std::unordered_map<int, std::uint64_t> secondDirty{{secondRaw, 23}};
    const std::vector<Timeline::AnimatableParameterTarget> liveTargets;
    Project::GraphSnapshotInputs firstInputs{first->graph, first->layers, first->timeline,
        firstDirty, first->rawRecipe, first->singleRawSource, liveTargets};
    Project::GraphSnapshotInputs secondInputs{second.graph, second.layers, second.timeline,
        secondDirty, second.rawRecipe, second.singleRawSource, liveTargets};
    firstInputs.frame = {7, 120, 30};
    secondInputs.frame = {30, 120, 30};
    firstInputs.executionInspectionEnabled = true;
    const auto a = Project::BuildGraphSnapshot(firstInputs);
    const auto b = Project::BuildGraphSnapshot(secondInputs);
    const auto& rawA = FindRawNode(a.snapshot, firstRaw);
    const auto& rawB = FindRawNode(b.snapshot, secondRaw);
    RequireSnapshot(a.publishSemantics && b.publishSemantics &&
        !a.outputDescriptions.empty() && !b.outputDescriptions.empty() &&
        a.snapshot.executionInspectionEnabled && !b.snapshot.executionInspectionEnabled &&
        rawA.requestRevision == 7 && rawB.requestRevision == 23 &&
        rawA.rawDevelopment.embeddedRawData == first->singleRawSource &&
        rawB.rawDevelopment.embeddedRawData == second.singleRawSource &&
        rawA.rawDevelopment.embeddedRawData != rawB.rawDevelopment.embeddedRawData &&
        RawRecipe::SerializeRecipe(rawA.rawDevelopment.recipe) == RawRecipe::SerializeRecipe(first->rawRecipe) &&
        RawRecipe::SerializeRecipe(rawB.rawDevelopment.recipe) == RawRecipe::SerializeRecipe(second.rawRecipe),
        "Graph snapshot lowering mixed project recipes, revisions, hints, or RAW buffers.");

    firstInputs.stageOutputNodeId = firstRaw;
    const auto stage = Project::BuildGraphSnapshot(firstInputs);
    RequireSnapshot(!stage.publishSemantics && stage.outputDescriptions.empty() &&
        stage.snapshot.outputNodeId == firstRaw &&
        FindRawNode(stage.snapshot, firstRaw).rawDevelopment.embeddedRawData == first->singleRawSource,
        "Stage-only lowering changed ordinary semantic publication or RAW ownership.");

    // The editor adapter must preserve the same ordinary request contract.
    firstInputs.stageOutputNodeId = 0;
    firstInputs.executionInspectionEnabled = false;
    firstDirty.clear();
    const auto direct = Project::BuildGraphSnapshot(firstInputs);
    EditorModule adapter(first);
    const auto adapted = adapter.BuildGraphSnapshotForTimelineFrame(7);
    RequireSnapshot(direct.snapshot.outputNodeId == adapted.outputNodeId &&
        direct.snapshot.nodes.size() == adapted.nodes.size() &&
        direct.snapshot.links.size() == adapted.links.size() &&
        direct.snapshot.semanticFingerprint == adapted.semanticFingerprint &&
        direct.snapshot.outputDescriptorIdentity == adapted.outputDescriptorIdentity &&
        FindRawNode(adapted, firstRaw).rawDevelopment.embeddedRawData == first->singleRawSource &&
        RawRecipe::SerializeRecipe(FindRawNode(adapted, firstRaw).rawDevelopment.recipe) ==
            RawRecipe::SerializeRecipe(FindRawNode(direct.snapshot, firstRaw).rawDevelopment.recipe),
        "Editor adapter changed the project graph snapshot.");
    std::cout << "PASS independent graph snapshots, shared RAW buffers, stage semantics and editor adapter parity\n";
}

} // namespace Stack::Validation
