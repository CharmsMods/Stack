#include "App/Validation/Suites/ProjectGraphSnapshotValidation.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Project/GraphSnapshotBuilder.h"
#include "Project/GraphSnapshotCache.h"
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

struct SnapshotUiContext {
    ImGuiContext* owned = ImGui::GetCurrentContext() ? nullptr : ImGui::CreateContext();
    ~SnapshotUiContext() { if (owned) ImGui::DestroyContext(owned); }
};

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

void CheckCompiledSnapshotReuse(EditorModule& editor,
    const std::shared_ptr<Project::ProjectSession>& project, int rawId) {
    // Match an established document. Its first NoteEdit would otherwise assign
    // a new owner identity as well as changing placement, correctly missing the cache.
    project->EnsureDocumentId();
    const auto* description = editor.GetGraphOutputDescription(rawId,"imageOut");
    RequireSnapshot(description && project->compiledGraphCache && project->compiledGraphCache->ordinary,
        "The descriptor owner did not retain its complete compiled result.");
    const auto retained = project->compiledGraphCache->ordinary->result;
    const auto rendered = editor.BuildGraphSnapshot();
    RequireSnapshot(project->compiledGraphCache->ordinary->result == retained &&
        editor.GetGraphOutputDescription(rawId,"imageOut") == description &&
        rendered.nodes.size() == retained->snapshot.nodes.size(),
        "Descriptor polling and rendering compiled the same ordinary request twice.");

    project->graph.FindNode(rawId)->position.x += 23;
    project->NoteEdit(0);
    const auto afterPlacement = editor.BuildGraphSnapshot();
    RequireSnapshot(project->compiledGraphCache->ordinary->result == retained &&
        std::all_of(afterPlacement.nodes.begin(),afterPlacement.nodes.end(),[&](const auto& node) {
            return node.authoredRevision == project->editRevision;
        }) && FindRawNode(retained->snapshot,rawId).authoredRevision != project->editRevision,
        "Placement recompiled content or returned stale request-owned authored revisions.");

    const auto frame = editor.BuildGraphSnapshotForTimelineFrame(9);
    const auto frameResult = project->compiledGraphCache->ordinary->result;
    RequireSnapshot(frameResult != retained && FindRawNode(frame,rawId).evaluatedFrame == 9 &&
        FindRawNode(frame,rawId).uniformSourceIdentity != FindRawNode(rendered,rawId).uniformSourceIdentity,
        "Frame changes reused a stale compiled evaluation or source identity.");
    editor.BuildGraphSnapshotForTimelineFrame(9);
    RequireSnapshot(project->compiledGraphCache->ordinary->result == frameResult,
        "An unchanged explicit frame request did not reuse compilation.");

    auto replacement = std::make_shared<Raw::RawImageData>();
    replacement->metadata.rawWidth = replacement->metadata.visibleWidth = 64;
    replacement->metadata.rawHeight = replacement->metadata.visibleHeight = 32;
    project->singleRawSource = replacement;
    const auto adopted = editor.BuildGraphSnapshotForTimelineFrame(9);
    RequireSnapshot(project->compiledGraphCache->ordinary->result != frameResult &&
        FindRawNode(adopted,rawId).rawDevelopment.embeddedRawData == replacement &&
        FindRawNode(frameResult->snapshot,rawId).rawDevelopment.embeddedRawData != replacement,
        "Source adoption reused a stale source or mutated a retained snapshot.");

    editor.GetGraphOutputDescription(rawId,"imageOut");
    const auto beforeStage = project->compiledGraphCache->ordinary->result;
    const auto stage = editor.BuildGraphSnapshotForTimelineFrame(0,rawId);
    Stack::NodeMath::ValueDescriptor publishedDescriptor;
    RequireSnapshot(project->compiledGraphCache->ordinary->result == beforeStage &&
        project->compiledGraphCache->stage &&
        !project->compiledGraphCache->stage->result->completeRawLayerSemantics &&
        project->compiledGraphCache->stage->result->outputDescriptions.empty() &&
        editor.TryGetGraphOutputSemanticDescriptor(publishedDescriptor) &&
        stage.outputNodeId == rawId && editor.GetGraphOutputDescription(rawId,"imageOut"),
        "A generic stage request replaced ordinary compilation or published partial descriptions.");

    project->graph.AddValueNode(Stack::NodeMath::MakeUniformScalar(1),{});
    editor.BuildGraphSnapshot();
    RequireSnapshot(project->compiledGraphCache->ordinary->result != beforeStage,
        "A structural change reused stale compilation.");
    const auto beforeEdit = project->compiledGraphCache->ordinary->result;
    auto& authoredRecipe = project->graph.FindNode(rawId)->rawDevelopment.recipe;
    authoredRecipe.preToneExposureEv += .5f;
    editor.MarkRenderDirty(rawId);
    const auto edited = editor.BuildGraphSnapshot();
    RequireSnapshot(project->compiledGraphCache->ordinary->result != beforeEdit &&
        FindRawNode(edited,rawId).rawDevelopment.recipe.preToneExposureEv == authoredRecipe.preToneExposureEv,
        "A content edit reused a stale recipe.");

    const auto firstOwnerResult = project->compiledGraphCache->ordinary->result;
    auto other = std::make_shared<Project::ProjectSession>();
    MakeRawGraph(*other,"snapshot-cache-other.raw",2);
    EditorModule otherEditor(other);
    otherEditor.BuildGraphSnapshot();
    RequireSnapshot(other->compiledGraphCache->ordinary->result != firstOwnerResult &&
        project->compiledGraphCache->ordinary->result == firstOwnerResult,
        "Independent project owners shared a compiled-result slot.");

    const auto& definition = EditorNodeGraphDefinitions::GetShippedCompoundTemplates().front();
    std::string error;
    RequireSnapshot(project->graph.AddCompoundDefinition(definition,&error),
        "Could not add the compound failure fixture.");
    const int compound = project->graph.AddCompoundNode(definition.identity,{})->id;
    project->graph.GetCompoundDefinitions().front().dependencies.push_back(definition.identity);
    RequireSnapshot(!editor.GetGraphOutputDescription(rawId,"imageOut"),
        "Failed current compilation left a previously published description visible.");
    const auto failed = project->compiledGraphCache->ordinary->result;
    RequireSnapshot(!failed->snapshot.semanticDiagnostics.empty() && failed->snapshot.nodes.empty() &&
        editor.BuildGraphSnapshot().nodes.empty() &&
        project->compiledGraphCache->ordinary->result == failed,
        "An unchanged failed compilation was retried or lost its current diagnostic.");
    project->graph.RemoveNode(compound);
    RequireSnapshot(editor.GetGraphOutputDescription(rawId,"imageOut") &&
        project->compiledGraphCache->ordinary->result != failed,
        "Structural repair did not invalidate the negative compilation cache.");
}

void CheckRawStageCompilationReuse() {
    auto project = std::make_shared<Project::ProjectSession>();
    const int rawId = MakeRawGraph(*project,"snapshot-raw-stage.raw",0);
    const int outputId = project->graph.ResolvePreviewOutputNodeId();
    project->graph.RemoveLink(rawId,outputId);
    project->rawPipelineActive = true;
    project->rawSourceKey = "snapshot-raw-stage";
    project->storePath = "snapshot-raw-stage.stack";
    EditorModule editor(project);
    editor.OpenRawLayerMaskGraph(Project::kRawBackgroundId,false);
    RequireSnapshot(editor.UsesRawWorkspaceStageRender() && editor.IsEditingRawLayerMaskGraph(),
        "The RAW stage cache fixture did not enter the normal RAW canvas path.");
    const auto operation = std::find_if(editor.GetNodeGraph().GetNodes().begin(),editor.GetNodeGraph().GetNodes().end(),
        [](const auto& node) { return node.kind == EditorNodeGraph::NodeKind::RawOperation; });
    RequireSnapshot(operation != editor.GetNodeGraph().GetNodes().end(),"RAW canvas has no operation to describe.");
    const int projectedId = operation->id;
    RequireSnapshot(editor.GetGraphOutputDescription(projectedId,"imageOut") &&
        project->compiledGraphCache->stage && project->compiledGraphCache->stage->result->completeRawLayerSemantics,
        "RAW canvas descriptions did not use the complete technical-stage compilation.");
    const auto retained = project->compiledGraphCache->stage->result;
    editor.BuildGraphSnapshotForTimelineFrame(0,rawId);
    RequireSnapshot(project->compiledGraphCache->stage->result == retained,
        "RAW descriptor lookup and render submission compiled the same stage twice.");
    const auto advanced = editor.BuildGraphSnapshotForTimelineFrame(3,rawId);
    RequireSnapshot(project->compiledGraphCache->stage->result != retained &&
        project->compiledGraphCache->stage->result->completeRawLayerSemantics &&
        // RAW composition remaps the technical source and retains rawId as a
        // generated result alias; evaluation belongs to the technical producer.
        FindRawNode(advanced,advanced.rawLayerBackgroundNodeId).evaluatedFrame == 3,
        "An explicit RAW stage frame reused a stale evaluation.");
    editor.GetGraphOutputDescription(projectedId,"imageOut");
    const auto beforeRawEdit = project->compiledGraphCache->stage->result;
    auto layers = project->rawLayers.State();
    Project::AddRawAdjustmentLayer(layers,"Cache invalidation");
    std::string error;
    RequireSnapshot(project->rawLayers.Apply(std::move(layers),error),"Could not edit RAW layer processing.");
    editor.BuildGraphSnapshotForTimelineFrame(0,rawId);
    RequireSnapshot(project->compiledGraphCache->stage->result != beforeRawEdit &&
        project->compiledGraphCache->stage->result->completeRawLayerSemantics,
        "A RAW processing revision reused the old technical-stage compilation.");
    project->graph.RemoveNode(rawId);
    RequireSnapshot(!editor.GetGraphOutputDescription(projectedId,"imageOut"),
        "A missing current RAW background left stale RAW descriptions visible.");
}
}

void ValidateProjectGraphSnapshotBuilder() {
    SnapshotUiContext uiContext;
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
    CheckCompiledSnapshotReuse(adapter,first,firstRaw);
    CheckRawStageCompilationReuse();
    std::cout << "PASS independent graph snapshots, immutable compiled reuse, current revisions, source/frame/RAW invalidation and failure publication\n";
}

} // namespace Stack::Validation
