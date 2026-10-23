#include "Editor/EditorModule.h"
#include "Project/GraphSnapshotBuilder.h"
#include "Project/GraphSnapshotCache.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Project/GraphImageContracts.h"
#include "Project/RawNodeEditing.h"

namespace {
EditorNodeGraph::GraphOutputContext RawLayerRootOutputContext(
    const EditorNodeGraph::Graph& graph, int background, Raw::RawWorkingSpace space) {
    EditorNodeGraph::GraphOutputContext context;
    const auto scene = Stack::Project::RawLayerSceneDescriptor(space);
    context.sourceDescriptors[EditorNodeGraph::GraphOutputIdentity(background, "imageOut")] = scene;
    return context;
}
} // namespace

std::vector<std::shared_ptr<LayerBase>> EditorModule::BuildGraphRenderLayers() const {
    return Stack::Project::BuildGraphRenderLayers(m_Project->graph, m_Project->layers);
}

std::vector<RenderLayerStep> EditorModule::BuildGraphRenderSteps() const {
    return Stack::Project::BuildGraphRenderSteps(m_Project->graph, m_Project->layers);
}

std::vector<RenderMaskSource> EditorModule::BuildGraphRenderMasks() const {
    return Stack::Project::BuildGraphRenderMasks(m_Project->graph, m_Project->layers);
}

RenderGraphSnapshot EditorModule::BuildGraphSnapshot() const {
    return BuildGraphSnapshotForTimelineFrame(m_TimelineUi.currentFrame);
}

RenderGraphSnapshot EditorModule::BuildGraphSnapshotForTimelineFrame(int timelineFrame, int stageOutputNodeId) const {
    const auto result = EnsureCompiledGraphSnapshot(timelineFrame,stageOutputNodeId);
    if (stageOutputNodeId <= 0 || result->completeRawLayerSemantics) PublishCompiledGraphSemantics(result);
    auto snapshot = result->snapshot;
    // Save/history revisions also advance for placement. Worker feedback belongs
    // to the current request while the retained executable remains unchanged.
    for (auto& node : snapshot.nodes) node.authoredRevision = m_Project->editRevision;
    return snapshot;
}

std::shared_ptr<const Stack::Project::GraphSnapshotResult> EditorModule::EnsureCompiledGraphSnapshot(
    int timelineFrame, int stageOutputNodeId) const {
    const auto frame = Stack::Timeline::NormalizeTimelineFrameRequest(
        timelineFrame,m_TimelineUi.durationFrames,m_TimelineUi.framesPerSecond);
    Stack::Project::GraphSnapshotCacheKey key;
    key.producer = m_GraphSnapshotProducerIdentity;
    key.documentId = GetProjectDocumentId();
    key.sourceKey = m_Project->rawSourceKey;
    key.renderRevision = m_RenderRevision;
    key.structureRevision = m_Project->graph.GetStructureRevision();
    key.rawProcessingRevision = m_Project->rawLayers.ProcessingRevision();
    key.dirtyGeneration = m_NodeDirtyGenerationCounter;
    key.frame = frame.frame; key.durationFrames = frame.durationFrames; key.framesPerSecond = frame.framesPerSecond;
    key.stageOutputNodeId = stageOutputNodeId;
    key.liveEditPreviewFrame = m_TimelineUi.liveEditPreviewFrame;
    key.inspection = m_ShowGraphPerformancePopup;
    key.rawLayersEnabled = IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive();
    key.workingSpace = m_Project->rawRecipe.technical.workingSpace;
    key.singleRawSource = m_Project->singleRawSource;
    key.rawProject = m_Project->snapshot;
    if (m_Project->snapshot) {
        key.rawProjectId = m_Project->snapshot->projectId;
        key.activeSourceSetId = m_Project->snapshot->activeSourceSetId;
        key.mfdInputRevision = m_Project->snapshot->mfdInputRevision;
        key.hdrInputRevision = m_Project->snapshot->hdrInputRevision;
        key.postRecipeRevision = m_Project->snapshot->postRecipeRevision;
    }
    if (m_MfdAdoptedRawResult) {
        key.mfdSource = m_MfdAdoptedRawResult->rawData;
        key.mfdContentHash = m_MfdAdoptedRawResult->contentHash;
        key.adoptedMfdRevision = m_MfdAdoptedRawResult->inputRevision;
        key.mfdProjectId = m_MfdAdoptedRawResult->projectId;
        key.mfdSourceSetId = m_MfdAdoptedRawResult->sourceSetId;
    }
    if (m_HdrAdoptedRawResult) {
        key.hdrSource = m_HdrAdoptedRawResult->rawData;
        key.hdrContentHash = m_HdrAdoptedRawResult->contentHash;
        key.adoptedHdrRevision = m_HdrAdoptedRawResult->inputRevision;
        key.hdrProjectId = m_HdrAdoptedRawResult->projectId;
        key.hdrSourceSetId = m_HdrAdoptedRawResult->sourceSetId;
    }
    if (key.rawLayersEnabled) key.previewSourceHash = GetActiveRawWorkspacePreviewSourceHash();
    if (!m_Project->compiledGraphCache) m_Project->compiledGraphCache = std::make_unique<Stack::Project::GraphSnapshotCache>();
    // A live timeline draft suppresses selected animated values. It is transient
    // UI state without its own revision, so do not reuse a prior draft result.
    const bool reusable = m_TimelineUi.liveEditPreviewTargets.empty();
    if (reusable) if (auto cached = m_Project->compiledGraphCache->Find(key)) return cached;

    Stack::Project::GraphSnapshotInputs inputs{
        m_Project->graph, m_Project->layers, m_Project->timeline, m_NodeDirtyGenerations,
        m_Project->rawRecipe, m_Project->singleRawSource, m_TimelineUi.liveEditPreviewTargets};
    inputs.frame = frame;
    inputs.documentId = GetProjectDocumentId();
    inputs.authoredRevision = m_Project->editRevision;
    inputs.liveEditPreviewFrame = m_TimelineUi.liveEditPreviewFrame;
    inputs.rawProject = m_Project->snapshot.get();
    inputs.mfdResult = m_MfdAdoptedRawResult ? &*m_MfdAdoptedRawResult : nullptr;
    inputs.hdrResult = m_HdrAdoptedRawResult ? &*m_HdrAdoptedRawResult : nullptr;
    inputs.stageOutputNodeId = stageOutputNodeId;
    inputs.executionInspectionEnabled = m_ShowGraphPerformancePopup;
    EditorNodeGraph::GraphOutputContext layerContext;
    if (key.rawLayersEnabled) {
        layerContext = RawLayerRootOutputContext(m_Project->graph,
            ResolveRawWorkspaceStageOutputNodeId(), m_Project->rawRecipe.technical.workingSpace);
        inputs.outputContextOverrides = &layerContext;
    }
    auto result = Stack::Project::BuildGraphSnapshot(inputs);
    if (key.rawLayersEnabled) {
        const int background = ResolveRawWorkspaceStageOutputNodeId();
        const bool includesBackground = std::any_of(result.snapshot.nodes.begin(), result.snapshot.nodes.end(),
            [background](const auto& node) { return node.nodeId == background; });
        if (includesBackground) {
            std::string error;
            if (!Stack::Project::LowerRawLayerStack(result.snapshot, m_Project->rawLayers.State(),
                    background, m_Project->rawLayers.ProcessingRevision(), error, frame.frame, frame.durationFrames,
                    frame.framesPerSecond, &m_Project->graph, &result.outputDescriptions)) {
                Stack::NodeMath::Diagnostic diagnostic;
                diagnostic.ruleId = "RAW-LAYER-LOWERING";
                diagnostic.stage = Stack::NodeMath::DiagnosticStage::Lowering;
                diagnostic.severity = Stack::NodeMath::DiagnosticSeverity::HardError;
                diagnostic.message = std::move(error);
                result.snapshot.semanticDiagnostics.push_back(std::move(diagnostic));
                result.snapshot.nodes.clear();
                result.snapshot.outputNodeId = -1;
                result.outputDescriptions.clear();
            } else if (stageOutputNodeId == background) {
                result.completeRawLayerSemantics = true;
            }
        }
    }
    const auto uniformSource=UniformSourceIdentityForFrame(frame.frame);
    for (auto& node:result.snapshot.nodes) node.uniformSourceIdentity=uniformSource;
    auto retained = std::make_shared<const Stack::Project::GraphSnapshotResult>(std::move(result));
    if (reusable) m_Project->compiledGraphCache->Store(std::move(key),retained);
    return retained;
}

void EditorModule::PublishCompiledGraphSemantics(
    const std::shared_ptr<const Stack::Project::GraphSnapshotResult>& retained) const {
    if (m_PublishedGraphSnapshot.lock() == retained) return;
    const auto& result = *retained;
    {
        m_LastGraphOutputSemanticDescriptor = result.snapshot.outputDescriptor;
        m_LastGraphOutputSemanticDescriptorIdentity = result.snapshot.outputDescriptorIdentity;
        m_LastGraphSemanticDiagnostics = result.snapshot.semanticDiagnostics;
        m_GraphOutputDescriptions = result.outputDescriptions;
        Stack::Editor::RawOperationInputCache inputInterpretations;
        for (const auto& owner : result.snapshot.rawLayerMaskNodeIds) {
            const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),owner.first);
            if (!layer) continue;
            for (const auto& binding : owner.second) {
                const auto* authored = layer->graph.FindNode(binding.first);
                const auto compiled = std::find_if(result.snapshot.nodes.begin(),result.snapshot.nodes.end(),[&](const auto& node) { return node.nodeId == binding.second; });
                if (!authored || compiled == result.snapshot.nodes.end()) continue;
                if (authored->kind == EditorNodeGraph::NodeKind::RawOperation) {
                    const Stack::Project::NodeEditTarget target{GetProjectDocumentId(),owner.first,authored->instanceUuid};
                    const auto endpoint = Stack::Project::RawNodeInput(m_Project->rawLayers.State(),target,GetProjectDocumentId(),"imageIn");
                    const auto input = std::find_if(result.snapshot.links.begin(),result.snapshot.links.end(),[&](const auto& link) {
                        return link.toNodeId == binding.second && link.toSocketId == "imageIn";
                    });
                    if (endpoint && input != result.snapshot.links.end())
                        inputInterpretations.Store(target,*endpoint,Stack::Project::RawOperationInputWorkingSpace(input->semanticDescriptor));
                }
            }
        }
        m_GraphOutputDescriptionRenderRevision = m_RenderRevision;
        m_GraphOutputDescriptionStructureRevision = m_Project->graph.GetStructureRevision();
        m_RawOperationInputCache = std::move(inputInterpretations);
        m_PublishedGraphSnapshot = retained;
    }
}

bool EditorModule::TryGetGraphOutputSemanticDescriptor(
    Stack::NodeMath::ValueDescriptor& descriptor) const {
    if (m_LastGraphOutputSemanticDescriptorIdentity.empty()) return false;
    descriptor = m_LastGraphOutputSemanticDescriptor;
    return true;
}

const EditorNodeGraph::GraphOutputDescription* EditorModule::GetGraphOutputDescription(
    int nodeId, const std::string& socketId) const {
    // Match the actual RAW render request. A generic stage request does not
    // publish composition semantics; this explicit RAW request may publish only
    // after complete layer lowering at the technical background boundary.
    const int stage = IsEditingRawLayerMaskGraph() && UsesRawWorkspaceStageRender()
        ? ResolveRawWorkspaceStageOutputNodeId() : 0;
    const auto compiled = EnsureCompiledGraphSnapshot(m_TimelineUi.currentFrame,stage);
    // Unlike an arbitrary stage-only build, this request is the descriptor
    // owner's current complete render boundary. Publish failures too, replacing
    // old descriptions with the current empty result instead of stale feedback.
    PublishCompiledGraphSemantics(compiled);
    if (IsEditingRawLayerMaskGraph()) {
        std::string key;
        if (m_RawLayerMaskWorkspace->projectCanvas) {
            const auto* selected = GetNodeGraph().FindNode(nodeId);
            if (!selected) return nullptr;
            if(nodeId==m_RawLayerMaskWorkspace->projectCanvas->preparedSource)
                key="raw/$raw-source/technical/"+socketId;
            else {
                if(selected->rawLayerOwnerId.empty())return nullptr;
                key = "raw/" + selected->rawLayerOwnerId + "/" + selected->instanceUuid + "/" + socketId;
            }
        } else {
            key = "raw/" + m_RawLayerMaskWorkspace->layerId + "/" +
                EditorNodeGraph::GraphOutputIdentity(nodeId,socketId);
        }
        const auto found = m_GraphOutputDescriptions.find(key);
        return found == m_GraphOutputDescriptions.end() ? nullptr : &found->second;
    }
    const auto output = m_GraphOutputDescriptions.find(EditorNodeGraph::GraphOutputIdentity(nodeId, socketId));
    return output == m_GraphOutputDescriptions.end() ? nullptr : &output->second;
}

bool EditorModule::TryGetGraphLinkSemanticDescriptor(
    const EditorNodeGraph::Link& link,
    Stack::NodeMath::ValueDescriptor& descriptor) const {
    const auto* output = GetGraphOutputDescription(link.fromNodeId, link.fromSocketId);
    if (!output) return false;
    descriptor = output->descriptor;
    return true;
}

bool EditorModule::TryGetGraphLinkWireReadoutInput(
    const EditorNodeGraph::Link& link,
    EditorNodeGraph::WireReadout::Input& input) const {
    input = {};
    const EditorNodeGraph::Node* source = GetNodeGraph().FindNode(link.fromNodeId);
    if (source == nullptr) return false;

    if (!GetNodeGraph().FindSocket(link.fromNodeId, link.fromSocketId, &input.sourceSocket)) {
        input.sourceSocket.id = link.fromSocketId;
        input.sourceSocket.nodeId = link.fromNodeId;
        input.sourceSocket.direction = EditorNodeGraph::SocketDirection::Output;
        input.sourceSocket.label = "Unknown";
        EditorNodeGraph::SocketPresentation::NormalizeSocketDefinition(
            source->kind,
            input.sourceSocket);
    }

    input.hasDescriptor = TryGetGraphLinkSemanticDescriptor(link, input.descriptor);

    // A Value node owns a declared uniform payload, so presenting it needs no
    // evaluation. Other known values are copied only from an accepted current
    // render result; stale or absent results deliberately fall back to type.
    if (source->kind == EditorNodeGraph::NodeKind::Value &&
        link.fromSocketId == EditorNodeGraph::kValueOutputSocketId) {
        input.value = source->value.value;
    } else if (!IsEditingRawLayerMaskGraph() && !m_RenderDirty &&
               m_LastGraphUniformOutputGeneration == m_LastCompletedRenderGeneration) {
        const auto value = m_LastGraphUniformOutputValues.find(
            EditorNodeGraph::WireReadout::OutputIdentity(
                link.fromNodeId,
                link.fromSocketId));
        if (value != m_LastGraphUniformOutputValues.end()) {
            input.value = value->second;
        }
    }

    if (const auto* output = GetGraphOutputDescription(link.fromNodeId, link.fromSocketId)) {
        input.sourceDiagnostics = output->diagnostics;
    }
    return true;
}
