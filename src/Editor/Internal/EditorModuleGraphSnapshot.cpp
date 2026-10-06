#include "Editor/EditorModule.h"
#include "Project/GraphSnapshotBuilder.h"
#include "Project/RawLayerStackSnapshot.h"

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
    Stack::Project::GraphSnapshotInputs inputs{
        m_Project->graph, m_Project->layers, m_Project->timeline, m_NodeDirtyGenerations,
        m_Project->rawRecipe, m_Project->singleRawSource, m_TimelineUi.liveEditPreviewTargets};
    inputs.frame = {timelineFrame, m_TimelineUi.durationFrames, m_TimelineUi.framesPerSecond};
    inputs.liveEditPreviewFrame = m_TimelineUi.liveEditPreviewFrame;
    inputs.rawProject = m_Project->snapshot.get();
    inputs.mfdResult = m_MfdAdoptedRawResult ? &*m_MfdAdoptedRawResult : nullptr;
    inputs.hdrResult = m_HdrAdoptedRawResult ? &*m_HdrAdoptedRawResult : nullptr;
    inputs.stageOutputNodeId = stageOutputNodeId;
    inputs.executionInspectionEnabled = m_ShowGraphPerformancePopup;
    EditorNodeGraph::GraphOutputContext layerContext;
    if ((IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive())) {
        layerContext = RawLayerRootOutputContext(m_Project->graph,
            ResolveRawWorkspaceStageOutputNodeId(), m_Project->rawRecipe.technical.workingSpace);
        inputs.outputContextOverrides = &layerContext;
    }
    auto result = Stack::Project::BuildGraphSnapshot(inputs);
    if ((IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive())) {
        const int background = ResolveRawWorkspaceStageOutputNodeId();
        const bool includesBackground = std::any_of(result.snapshot.nodes.begin(), result.snapshot.nodes.end(),
            [background](const auto& node) { return node.nodeId == background; });
        if (includesBackground) {
            std::string error;
            if (!Stack::Project::LowerRawLayerStack(result.snapshot, m_Project->rawLayers.State(),
                    background, m_Project->rawLayers.ProcessingRevision(), error, timelineFrame, m_TimelineUi.durationFrames, m_TimelineUi.framesPerSecond, &m_Project->graph)) {
                Stack::NodeMath::Diagnostic diagnostic;
                diagnostic.ruleId = "RAW-LAYER-LOWERING";
                diagnostic.stage = Stack::NodeMath::DiagnosticStage::Lowering;
                diagnostic.severity = Stack::NodeMath::DiagnosticSeverity::HardError;
                diagnostic.message = std::move(error);
                result.snapshot.semanticDiagnostics.push_back(std::move(diagnostic));
                result.snapshot.nodes.clear();
                result.snapshot.outputNodeId = -1;
            }
        }
    }
    if (result.publishSemantics) {
        m_LastGraphOutputSemanticDescriptor = result.snapshot.outputDescriptor;
        m_LastGraphOutputSemanticDescriptorIdentity = result.snapshot.outputDescriptorIdentity;
        m_LastGraphSemanticDiagnostics = result.snapshot.semanticDiagnostics;
        m_GraphOutputDescriptions = std::move(result.outputDescriptions);
        for (const auto& owner : result.snapshot.rawLayerMaskNodeIds) {
            const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),owner.first);
            if (!layer) continue;
            for (const auto& binding : owner.second) {
                const auto* authored = layer->graph.FindNode(binding.first);
                const auto compiled = std::find_if(result.snapshot.nodes.begin(),result.snapshot.nodes.end(),[&](const auto& node) { return node.nodeId == binding.second; });
                if (!authored || compiled == result.snapshot.nodes.end()) continue;
                const auto save = [&](const std::string& port) {
                    auto descriptor = compiled->semanticDescriptor;
                    for (const auto& link : result.snapshot.links)
                        if (link.fromNodeId == binding.second && link.fromSocketId == port) { descriptor = link.semanticDescriptor; break; }
                    m_GraphOutputDescriptions["raw/"+owner.first+"/"+EditorNodeGraph::GraphOutputIdentity(binding.first,port)].descriptor = std::move(descriptor);
                };
                for (const auto& socket : layer->graph.GetSockets(*authored,false))
                    if (socket.direction == EditorNodeGraph::SocketDirection::Output) save(socket.id);
                if (authored->kind == EditorNodeGraph::NodeKind::Output) save("imageOut");
            }
        }
        m_GraphOutputDescriptionRenderRevision = m_RenderRevision;
        m_GraphOutputDescriptionStructureRevision = m_Project->graph.GetStructureRevision();
    }
    return std::move(result.snapshot);
}

bool EditorModule::TryGetGraphOutputSemanticDescriptor(
    Stack::NodeMath::ValueDescriptor& descriptor) const {
    if (m_LastGraphOutputSemanticDescriptorIdentity.empty()) return false;
    descriptor = m_LastGraphOutputSemanticDescriptor;
    return true;
}

const EditorNodeGraph::GraphOutputDescription* EditorModule::GetGraphOutputDescription(
    int nodeId, const std::string& socketId) const {
    if (IsEditingRawLayerMaskGraph()) {
        if (m_GraphOutputDescriptionRenderRevision != m_RenderRevision) BuildGraphSnapshot();
        const auto key = "raw/" + m_RawLayerMaskWorkspace->layerId + "/" + EditorNodeGraph::GraphOutputIdentity(nodeId,socketId);
        const auto found = m_GraphOutputDescriptions.find(key);
        return found == m_GraphOutputDescriptions.end() ? nullptr : &found->second;
    }
    if (m_GraphOutputDescriptionRenderRevision != m_RenderRevision ||
        m_GraphOutputDescriptionStructureRevision != m_Project->graph.GetStructureRevision()) {
        const auto context = !(IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive())
            ? EditorNodeGraph::GraphOutputContext{}
            : RawLayerRootOutputContext(m_Project->graph,
                ResolveRawWorkspaceStageOutputNodeId(), m_Project->rawRecipe.technical.workingSpace);
        m_GraphOutputDescriptions = EditorNodeGraph::DescribeGraphOutputs(m_Project->graph, context);
        m_GraphOutputDescriptionRenderRevision = m_RenderRevision;
        m_GraphOutputDescriptionStructureRevision = m_Project->graph.GetStructureRevision();
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
