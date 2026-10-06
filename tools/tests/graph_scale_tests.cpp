#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Project/GraphSnapshotLookup.h"
#include "Editor/Internal/EditorRenderWorkerTileGraph.h"
#include "NodeMath/FirstClassValue.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RenderTiling.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

void RequireScale(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

int AddReformat(EditorNodeGraph::Graph& graph, float x) {
    const EditorNodeGraph::Node* node = graph.AddReformatNode({ x, 0.0f });
    RequireScale(node != nullptr, "graph-scale Reformat allocation failed");
    return node->id;
}

void AddImageLink(EditorNodeGraph::Graph& graph, int fromNodeId, int toNodeId) {
    graph.EditLinks().push_back(EditorNodeGraph::Link{
        fromNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        toNodeId,
        EditorNodeGraph::kImageInputSocketId
    });
}

void TestDeepCycleCheckUsesBoundedCallStack() {
    constexpr int kDepth = 8192;

    EditorNodeGraph::Graph graph;
    std::vector<int> nodeIds;
    nodeIds.reserve(kDepth);
    for (int index = 0; index < kDepth; ++index) {
        nodeIds.push_back(AddReformat(graph, static_cast<float>(index)));
        if (index > 0) {
            AddImageLink(graph, nodeIds[index - 1], nodeIds[index]);
        }
    }

    std::string error;
    RequireScale(
        !graph.CanConnectSockets(
            nodeIds.back(),
            EditorNodeGraph::kImageOutputSocketId,
            nodeIds.front(),
            EditorNodeGraph::kImageInputSocketId,
            nullptr,
            &error),
        "a deep back-edge should be rejected as a cycle");
    RequireScale(
        error == "That connection would create a cycle.",
        "deep cycle rejection should retain the normal user-facing diagnostic");

    graph.EditLinks().push_back(EditorNodeGraph::Link{
        nodeIds.back(),
        EditorNodeGraph::kImageOutputSocketId,
        nodeIds.front(),
        EditorNodeGraph::kImageInputSocketId
    });
    const EditorNodeGraph::ValidationResult validation = graph.Validate();
    RequireScale(
        !validation.valid &&
            std::find(
                validation.messages.begin(),
                validation.messages.end(),
                "Render chain contains a cycle.") !=
                validation.messages.end(),
        "validation should detect a deep cycle without recursive traversal");
}

void TestDeepRegionPlanningUsesBoundedCallStack() {
    constexpr int kPointwiseDepth = 8190;
    constexpr int kWidth = 64;
    constexpr int kHeight = 32;

    RenderGraphSnapshot graph;
    graph.nodes.reserve(static_cast<std::size_t>(kPointwiseDepth) + 2u);
    graph.links.reserve(static_cast<std::size_t>(kPointwiseDepth) + 1u);

    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::Image;
    source.definitionId = "test:region/source";
    source.image.width = kWidth;
    source.image.height = kHeight;
    graph.nodes.push_back(std::move(source));

    int upstreamNodeId = 1;
    for (int index = 0; index < kPointwiseDepth; ++index) {
        RenderGraphNode pointwise;
        pointwise.nodeId = index + 2;
        pointwise.kind = RenderGraphNodeKind::TechnicalImage;
        pointwise.definitionId = "test:region/pointwise";
        graph.nodes.push_back(std::move(pointwise));
        graph.links.push_back({
            upstreamNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            index + 2,
            EditorNodeGraph::kImageInputSocketId
        });
        upstreamNodeId = index + 2;
    }

    RenderGraphNode output;
    output.nodeId = kPointwiseDepth + 2;
    output.kind = RenderGraphNodeKind::Output;
    output.definitionId = "test:region/output";
    graph.outputNodeId = output.nodeId;
    graph.nodes.push_back(std::move(output));
    graph.links.push_back({
        upstreamNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        graph.outputNodeId,
        EditorNodeGraph::kImageInputSocketId
    });

    const RenderGraphRegionPlan plan =
        RenderTiling::PlanGraphRegions(graph, kWidth, kHeight);
    RequireScale(
        plan.valid && plan.tileable && !plan.requiresFullFrame,
        "deep pointwise region planning should remain valid and tileable");
    RequireScale(
        plan.stages.size() == graph.nodes.size() &&
            plan.requiredHaloX == 0 &&
            plan.requiredHaloY == 0,
        "deep pointwise region planning should visit every reachable node exactly once");
    RequireScale(
        plan.outputSpatial.fullWindow ==
            Stack::NodeMath::Rect{ 0, 0, kWidth, kHeight },
        "deep pointwise region planning should preserve the source extent");

    graph.links.push_back({
        graph.outputNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        2,
        EditorNodeGraph::kImageInputSocketId
    });
    const RenderGraphRegionPlan cyclic =
        RenderTiling::PlanGraphRegions(graph, kWidth, kHeight);
    RequireScale(
        !cyclic.valid &&
            cyclic.reason.find("cycle") != std::string::npos,
        "deep region planning should reject a reachable cycle without recursive traversal");
}

void TestSharedTreeCompletionAndCacheReuse() {
    constexpr int kSharedDepth = 384;
    constexpr int kOutputCount = 24;

    EditorNodeGraph::Graph graph;
    const EditorNodeGraph::Node* source = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor,
        { 0.0f, 0.0f });
    RequireScale(source != nullptr, "graph-scale image generator allocation failed");
    const int sourceNodeId = source->id;

    int terminalNodeId = sourceNodeId;
    for (int index = 0; index < kSharedDepth; ++index) {
        const int reformatNodeId = AddReformat(graph, static_cast<float>(index + 1));
        AddImageLink(graph, terminalNodeId, reformatNodeId);
        terminalNodeId = reformatNodeId;
    }

    for (int index = 0; index < kOutputCount; ++index) {
        const EditorNodeGraph::Node* output = graph.AddOutputNode(
            { static_cast<float>(kSharedDepth + 2), static_cast<float>(index) },
            index == 0);
        RequireScale(output != nullptr, "graph-scale output allocation failed");
        AddImageLink(graph, terminalNodeId, output->id);
    }

    const auto& first = graph.GetCompletedChains();
    RequireScale(
        first.size() == kOutputCount,
        "every output sharing a valid upstream tree should complete");
    for (const EditorNodeGraph::CompletedChainInfo& chain : first) {
        RequireScale(
            chain.sourceNodeId == sourceNodeId &&
                chain.terminalNodeId == terminalNodeId &&
                chain.nodeIds.size() == static_cast<std::size_t>(kSharedDepth + 1),
            "shared-tree completion should retain the full chain and reference source");
    }

    const auto* firstAddress = &first;
    const auto& cached = graph.GetCompletedChains();
    RequireScale(
        &cached == firstAddress,
        "an unchanged completed-chain query should reuse the owned cache");
    RequireScale(
        graph.ResolveReferenceSourceNodeIdForOutput(first.front().outputNodeId) ==
            sourceNodeId,
        "reference-source resolution should traverse Reformat nodes");
}

void TestDeepCompletedChainUsesBoundedCallStack() {
    constexpr int kDepth = 8192;

    EditorNodeGraph::Graph graph;
    const EditorNodeGraph::Node* source = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor,
        { 0.0f, 0.0f });
    RequireScale(source != nullptr, "deep-chain image generator allocation failed");
    const int sourceNodeId = source->id;

    int terminalNodeId = sourceNodeId;
    for (int index = 0; index < kDepth; ++index) {
        const int reformatNodeId = AddReformat(graph, static_cast<float>(index + 1));
        AddImageLink(graph, terminalNodeId, reformatNodeId);
        terminalNodeId = reformatNodeId;
    }
    const EditorNodeGraph::Node* output = graph.AddOutputNode(
        { static_cast<float>(kDepth + 2), 0.0f },
        true);
    RequireScale(output != nullptr, "deep-chain output allocation failed");
    AddImageLink(graph, terminalNodeId, output->id);

    const auto& chains = graph.GetCompletedChains();
    RequireScale(
        chains.size() == 1 &&
            chains.front().sourceNodeId == sourceNodeId &&
            chains.front().terminalNodeId == terminalNodeId &&
            chains.front().nodeIds.size() == static_cast<std::size_t>(kDepth + 1),
        "a deep valid chain should complete without exhausting the call stack");
}

void TestFrequencyChainRetainsItsReferenceSource() {
    EditorNodeGraph::Graph graph;
    const EditorNodeGraph::Node* source = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor,
        { 0.0f, 0.0f });
    RequireScale(source != nullptr, "frequency reference source allocation failed");
    const int sourceNodeId = source->id;
    const int splitNodeId = graph.AddChannelSplitNode({ 1.0f, 0.0f })->id;
    const int fftNodeId = graph.AddFrequencyFftNode({ 2.0f, 0.0f })->id;
    const int ifftNodeId = graph.AddFrequencyIfftNode({ 3.0f, 0.0f })->id;
    const int outputNodeId = graph.AddOutputNode({ 4.0f, 0.0f }, true)->id;

    std::string error;
    RequireScale(
        graph.TryConnectSockets(
            sourceNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            splitNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &error),
        "frequency reference fixture should connect its image source");
    RequireScale(
        graph.TryConnectSockets(
            splitNodeId,
            "r",
            fftNodeId,
            EditorNodeGraph::kChannelInputSocketId,
            &error),
        "frequency reference fixture should connect Split to FFT");
    RequireScale(
        graph.TryConnectSockets(
            fftNodeId,
            EditorNodeGraph::kSpectrumOutputSocketId,
            ifftNodeId,
            EditorNodeGraph::kSpectrumInputSocketId,
            &error),
        "frequency reference fixture should connect FFT to inverse FFT");
    RequireScale(
        graph.TryConnectSockets(
            ifftNodeId,
            EditorNodeGraph::kChannelOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &error),
        "frequency reference fixture should connect inverse FFT to output");
    RequireScale(
        graph.ResolveReferenceSourceNodeIdForOutput(outputNodeId) == sourceNodeId,
        "frequency-domain chains should resolve the original image reference");
    RequireScale(
        graph.IsScalarSocketStream(
            ifftNodeId,
            EditorNodeGraph::kChannelOutputSocketId),
        "inverse FFT output should retain its scalar channel identity");
}

void TestScalarBlendRetainsChannelIdentity() {
    EditorNodeGraph::Graph graph;
    const int splitNodeId =
        graph.AddChannelSplitNode({ 0.0f, 0.0f })->id;
    const int mixNodeId = graph.AddMixNode({ 1.0f, 0.0f })->id;
    graph.EditLinks() = {
        {
            splitNodeId,
            "r",
            mixNodeId,
            EditorNodeGraph::kMixInputASocketId
        },
        {
            splitNodeId,
            "g",
            mixNodeId,
            EditorNodeGraph::kMixInputBSocketId
        }
    };

    RequireScale(
        graph.IsScalarSocketStream(
            mixNodeId,
            EditorNodeGraph::kImageOutputSocketId),
        "blending scalar channel inputs should retain scalar output identity");
}

void TestDeepScalarAndChannelPropagationUsesBoundedCallStack() {
    constexpr int kDepth = 8192;

    EditorNodeGraph::Graph graph;
    const EditorNodeGraph::Node* split =
        graph.AddChannelSplitNode({ 0.0f, 0.0f });
    RequireScale(split != nullptr, "deep scalar Split allocation failed");
    const int splitNodeId = split->id;
    const EditorNodeGraph::Node* viewTransform =
        graph.AddLayerNode(
            LayerType::ViewTransform,
            0,
            { 1.0f, 0.0f });
    RequireScale(
        viewTransform != nullptr,
        "deep scalar View Transform allocation failed");
    graph.EditLinks().push_back(EditorNodeGraph::Link{
        splitNodeId,
        "r",
        viewTransform->id,
        EditorNodeGraph::kImageInputSocketId
    });
    int previousNodeId = viewTransform->id;
    std::string previousSocketId = EditorNodeGraph::kImageOutputSocketId;

    for (int index = 0; index < kDepth; ++index) {
        const EditorNodeGraph::Node* operation =
            graph.AddTechnicalImageNode(
                Stack::NodeMath::TechnicalImageOperation::Exposure,
                { static_cast<float>(index + 2), 0.0f });
        RequireScale(
            operation != nullptr,
            "deep scalar Technical Image allocation failed");
        graph.EditLinks().push_back(EditorNodeGraph::Link{
            previousNodeId,
            previousSocketId,
            operation->id,
            EditorNodeGraph::kImageInputSocketId
        });
        previousNodeId = operation->id;
        previousSocketId = EditorNodeGraph::kImageOutputSocketId;
    }

    RequireScale(
        graph.IsScalarSocketStream(previousNodeId, previousSocketId),
        "a deep channel-derived image chain should remain scalar");
    RequireScale(
        graph.ResolveSocketChannel(previousNodeId, previousSocketId) == "r",
        "a deep channel-derived image chain should retain its channel role");
    const EditorNodeGraph::ScenePathInfo scenePath =
        EditorNodeGraph::AnalyzeScenePath(graph, previousNodeId);
    RequireScale(
        !scenePath.sceneReferred && scenePath.hasViewTransform,
        "deep scene-path analysis should retain the upstream View Transform");
}

void TestFrequencyScenePathUsesTypedInputs() {
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawSourcePayload rawPayload;
    const int rawSourceNodeId =
        graph.AddRawSourceNode(std::move(rawPayload), { 0.0f, 0.0f })->id;
    EditorNodeGraph::RawDecodePayload decodePayload;
    const int decodeNodeId =
        graph.AddRawDecodeNode(std::move(decodePayload), { 1.0f, 0.0f })->id;
    const int viewTransformNodeId =
        graph.AddLayerNode(
            LayerType::ViewTransform,
            0,
            { 2.0f, 0.0f })->id;
    const int splitNodeId =
        graph.AddChannelSplitNode({ 3.0f, 0.0f })->id;
    const int fftNodeId =
        graph.AddFrequencyFftNode({ 4.0f, 0.0f })->id;
    const int ifftNodeId =
        graph.AddFrequencyIfftNode({ 5.0f, 0.0f })->id;
    const int outputNodeId =
        graph.AddOutputNode({ 6.0f, 0.0f }, true)->id;

    graph.EditLinks() = {
        {
            rawSourceNodeId,
            EditorNodeGraph::kRawOutputSocketId,
            decodeNodeId,
            EditorNodeGraph::kRawInputSocketId
        },
        {
            decodeNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            viewTransformNodeId,
            EditorNodeGraph::kImageInputSocketId
        },
        {
            viewTransformNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            splitNodeId,
            EditorNodeGraph::kImageInputSocketId
        },
        {
            splitNodeId,
            "r",
            fftNodeId,
            EditorNodeGraph::kChannelInputSocketId
        },
        {
            fftNodeId,
            EditorNodeGraph::kSpectrumOutputSocketId,
            ifftNodeId,
            EditorNodeGraph::kSpectrumInputSocketId
        },
        {
            ifftNodeId,
            EditorNodeGraph::kChannelOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId
        }
    };

    const EditorNodeGraph::ScenePathInfo scenePath =
        EditorNodeGraph::AnalyzeScenePath(graph, outputNodeId);
    RequireScale(
        scenePath.sceneReferred && scenePath.hasViewTransform,
        "frequency scene-path analysis should follow channel and spectrum inputs");
}

void TestMfsrRejectsMixedFamiliesThroughFrequencyChain() {
    EditorNodeGraph::Graph graph;
    const int rasterNodeId =
        graph.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor,
            { 0.0f, -1.0f })->id;
    const int mfsrNodeId =
        graph.AddMfsrNode({}, { 6.0f, 0.0f })->id;

    EditorNodeGraph::RawSourcePayload rawPayload;
    const int rawSourceNodeId =
        graph.AddRawSourceNode(std::move(rawPayload), { 0.0f, 1.0f })->id;
    EditorNodeGraph::RawDecodePayload decodePayload;
    const int decodeNodeId =
        graph.AddRawDecodeNode(std::move(decodePayload), { 1.0f, 1.0f })->id;
    const int splitNodeId =
        graph.AddChannelSplitNode({ 2.0f, 1.0f })->id;
    const int fftNodeId =
        graph.AddFrequencyFftNode({ 3.0f, 1.0f })->id;
    const int ifftNodeId =
        graph.AddFrequencyIfftNode({ 4.0f, 1.0f })->id;
    const int combineNodeId =
        graph.AddChannelCombineNode({ 5.0f, 1.0f })->id;

    std::string error;
    RequireScale(
        graph.TryConnectSockets(
            rasterNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            mfsrNodeId,
            EditorNodeGraph::kMfsrReferenceInputSocketId,
            &error),
        "MFSR mixed-family fixture should connect its raster reference");
    RequireScale(
        graph.TryConnectSockets(
            rawSourceNodeId,
            EditorNodeGraph::kRawOutputSocketId,
            decodeNodeId,
            EditorNodeGraph::kRawInputSocketId,
            &error) &&
        graph.TryConnectSockets(
            decodeNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            splitNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &error) &&
        graph.TryConnectSockets(
            splitNodeId,
            "r",
            fftNodeId,
            EditorNodeGraph::kChannelInputSocketId,
            &error) &&
        graph.TryConnectSockets(
            fftNodeId,
            EditorNodeGraph::kSpectrumOutputSocketId,
            ifftNodeId,
            EditorNodeGraph::kSpectrumInputSocketId,
            &error) &&
        graph.TryConnectSockets(
            ifftNodeId,
            EditorNodeGraph::kChannelOutputSocketId,
            combineNodeId,
            "r",
            &error),
        "MFSR mixed-family fixture should build its RAW frequency chain");

    error.clear();
    RequireScale(
        !graph.TryConnectSockets(
            combineNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            mfsrNodeId,
            EditorNodeGraph::MfsrInputSocketId(1),
            &error),
        "MFSR should reject RAW-derived frequency output beside a raster reference");
    RequireScale(
        error.find("cannot mix RAW-derived and raster-derived") !=
            std::string::npos,
        "MFSR mixed-family rejection should explain the conflicting origins");
}

void TestDeepRenderScheduleUsesTopologicalOrder() {
    constexpr int kDepth = 8192;
    RenderGraphSnapshot snapshot;
    snapshot.nodes.reserve(kDepth + 2);
    snapshot.links.reserve(kDepth + 1);

    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::Image;
    snapshot.nodes.push_back(source);

    int previousNodeId = source.nodeId;
    for (int index = 0; index < kDepth; ++index) {
        RenderGraphNode operation;
        operation.nodeId = index + 2;
        operation.kind = RenderGraphNodeKind::Reformat;
        snapshot.nodes.push_back(operation);
        snapshot.links.push_back(
            RenderGraphLink{
                previousNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                operation.nodeId,
                EditorNodeGraph::kImageInputSocketId
            });
        previousNodeId = operation.nodeId;
    }

    RenderGraphNode output;
    output.nodeId = kDepth + 2;
    output.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(output);
    snapshot.links.push_back(
        RenderGraphLink{
            previousNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            output.nodeId,
            EditorNodeGraph::kImageInputSocketId
        });

    const auto schedule =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            snapshot,
            {
                output.nodeId,
                EditorNodeGraph::kImageInputSocketId
            });
    RequireScale(
        schedule.valid &&
            schedule.outputs.size() ==
                static_cast<std::size_t>(kDepth + 2) &&
            schedule.outputs.front().nodeId == source.nodeId &&
            schedule.outputs.back().nodeId == output.nodeId,
        "deep render scheduling should produce an upstream-first iterative order");

    snapshot.links.push_back(
        RenderGraphLink{
            output.nodeId,
            EditorNodeGraph::kImageOutputSocketId,
            source.nodeId,
            EditorNodeGraph::kImageInputSocketId
        });
    const auto cyclicSchedule =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            snapshot,
            {
                output.nodeId,
                EditorNodeGraph::kImageInputSocketId
            });
    RequireScale(
        !cyclicSchedule.valid &&
            cyclicSchedule.error.find("cycle") != std::string::npos,
        "render scheduling should reject a deep cycle before recursive evaluation");
}

void TestWideSharedRenderScheduleUsesEachDependencyOnce() {
    constexpr int kBranchCount = 512;
    constexpr int kBranchDepth = 8;

    RenderGraphSnapshot snapshot;
    snapshot.nodes.reserve(
        1 + kBranchCount * kBranchDepth + kBranchCount);
    snapshot.links.reserve(
        kBranchCount * kBranchDepth +
        (kBranchCount - 1) * 2 +
        1);

    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::Image;
    snapshot.nodes.push_back(source);

    int nextNodeId = 2;
    std::vector<int> terminals;
    terminals.reserve(kBranchCount);
    for (int branch = 0; branch < kBranchCount; ++branch) {
        int previousNodeId = source.nodeId;
        for (int depth = 0; depth < kBranchDepth; ++depth) {
            RenderGraphNode operation;
            operation.nodeId = nextNodeId++;
            operation.kind = RenderGraphNodeKind::Reformat;
            snapshot.nodes.push_back(operation);
            snapshot.links.push_back(
                RenderGraphLink{
                    previousNodeId,
                    EditorNodeGraph::kImageOutputSocketId,
                    operation.nodeId,
                    EditorNodeGraph::kImageInputSocketId
                });
            previousNodeId = operation.nodeId;
        }
        terminals.push_back(previousNodeId);
    }

    std::vector<int> mergeLevel = terminals;
    while (mergeLevel.size() > 1) {
        std::vector<int> nextLevel;
        nextLevel.reserve((mergeLevel.size() + 1u) / 2u);
        for (std::size_t index = 0;
             index < mergeLevel.size();
             index += 2u) {
            if (index + 1u >= mergeLevel.size()) {
                nextLevel.push_back(mergeLevel[index]);
                continue;
            }
            RenderGraphNode merge;
            merge.nodeId = nextNodeId++;
            merge.kind = RenderGraphNodeKind::Mix;
            snapshot.nodes.push_back(merge);
            snapshot.links.push_back(
                RenderGraphLink{
                    mergeLevel[index],
                    EditorNodeGraph::kImageOutputSocketId,
                    merge.nodeId,
                    EditorNodeGraph::kMixInputASocketId
                });
            snapshot.links.push_back(
                RenderGraphLink{
                    mergeLevel[index + 1u],
                    EditorNodeGraph::kImageOutputSocketId,
                    merge.nodeId,
                    EditorNodeGraph::kMixInputBSocketId
                });
            nextLevel.push_back(merge.nodeId);
        }
        mergeLevel = std::move(nextLevel);
    }
    RequireScale(
        mergeLevel.size() == 1,
        "wide shared render fixture should reduce to one balanced merge root");

    RenderGraphNode output;
    output.nodeId = nextNodeId;
    output.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(output);
    snapshot.links.push_back(
        RenderGraphLink{
            mergeLevel.front(),
            EditorNodeGraph::kImageOutputSocketId,
            output.nodeId,
            EditorNodeGraph::kImageInputSocketId
        });

    const auto schedule =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            snapshot,
            {
                output.nodeId,
                EditorNodeGraph::kImageOutputSocketId
            });
    RequireScale(
        schedule.valid &&
            schedule.outputs.size() == snapshot.nodes.size() &&
            schedule.outputs.front().nodeId == source.nodeId &&
            schedule.outputs.back().nodeId == output.nodeId,
        "wide shared render scheduling should visit every required dependency once in upstream-first order");

    const std::size_t sourceOccurrences =
        static_cast<std::size_t>(std::count_if(
            schedule.outputs.begin(),
            schedule.outputs.end(),
            [&](const auto& scheduled) {
                return scheduled.nodeId == source.nodeId;
            }));
    RequireScale(
        sourceOccurrences == 1,
        "wide shared render scheduling should not duplicate a heavily shared source");
}

void TestManyIndependentRenderTreesStayOutputScoped() {
    constexpr int kTreeCount = 256;
    constexpr int kTreeDepth = 16;

    RenderGraphSnapshot snapshot;
    snapshot.nodes.reserve(kTreeCount * (kTreeDepth + 2));
    snapshot.links.reserve(kTreeCount * (kTreeDepth + 1));
    std::vector<int> outputNodeIds;
    outputNodeIds.reserve(kTreeCount);

    int nextNodeId = 1;
    for (int tree = 0; tree < kTreeCount; ++tree) {
        RenderGraphNode source;
        source.nodeId = nextNodeId++;
        source.kind = RenderGraphNodeKind::Image;
        snapshot.nodes.push_back(source);
        int previousNodeId = source.nodeId;

        for (int depth = 0; depth < kTreeDepth; ++depth) {
            RenderGraphNode operation;
            operation.nodeId = nextNodeId++;
            operation.kind = RenderGraphNodeKind::Reformat;
            snapshot.nodes.push_back(operation);
            snapshot.links.push_back({
                previousNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                operation.nodeId,
                EditorNodeGraph::kImageInputSocketId
            });
            previousNodeId = operation.nodeId;
        }

        RenderGraphNode output;
        output.nodeId = nextNodeId++;
        output.kind = RenderGraphNodeKind::Output;
        outputNodeIds.push_back(output.nodeId);
        snapshot.nodes.push_back(output);
        snapshot.links.push_back({
            previousNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            output.nodeId,
            EditorNodeGraph::kImageInputSocketId
        });
    }

    const auto topology =
        Stack::Renderer::GraphExecution::BuildGraphTopologyIndex(
            snapshot);
    RequireScale(
        topology.valid &&
            topology.nodes.size() == snapshot.nodes.size() &&
            topology.inputLinks.size() ==
                static_cast<std::size_t>(kTreeCount * (kTreeDepth + 1)),
        "one reusable topology index should cover every independent tree");

    const auto singleTree =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            snapshot,
            topology,
            {
                outputNodeIds.front(),
                EditorNodeGraph::kImageOutputSocketId
            });
    RequireScale(
        singleTree.valid &&
            singleTree.outputs.size() ==
                static_cast<std::size_t>(kTreeDepth + 2),
        "one requested output should not activate independent render trees");

    std::vector<Stack::Renderer::GraphExecution::ScheduledGraphOutput>
        extraRoots;
    extraRoots.reserve(outputNodeIds.size() - 1u);
    for (std::size_t index = 1; index < outputNodeIds.size(); ++index) {
        extraRoots.push_back({
            outputNodeIds[index],
            EditorNodeGraph::kImageOutputSocketId
        });
    }
    const auto allTrees =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            snapshot,
            topology,
            {
                outputNodeIds.front(),
                EditorNodeGraph::kImageOutputSocketId
            },
            extraRoots);
    RequireScale(
        allTrees.valid &&
            allTrees.outputs.size() == snapshot.nodes.size(),
        "a multi-root schedule should visit every independent tree exactly once");

    RenderGraphSnapshot copiedSnapshot = snapshot;
    const auto mismatchedSchedule =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            copiedSnapshot,
            topology,
            {
                outputNodeIds.front(),
                EditorNodeGraph::kImageOutputSocketId
            });
    RequireScale(
        !mismatchedSchedule.valid &&
            mismatchedSchedule.error.find("not bound") !=
                std::string::npos,
        "a reusable topology index should reject a different snapshot even when its topology is equal");
}

void TestAmbiguousInputFanInFailsClosed() {
    EditorNodeGraph::Graph graph;
    const int sourceA =
        graph.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor,
            { 0.0f, 0.0f })->id;
    const int sourceB =
        graph.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor,
            { 0.0f, 1.0f })->id;
    const int output =
        graph.AddOutputNode({ 1.0f, 0.0f }, true)->id;
    graph.EditLinks() = {
        {
            sourceA,
            EditorNodeGraph::kImageOutputSocketId,
            output,
            EditorNodeGraph::kImageInputSocketId
        },
        {
            sourceB,
            EditorNodeGraph::kImageOutputSocketId,
            output,
            EditorNodeGraph::kImageInputSocketId
        }
    };
    const EditorNodeGraph::ValidationResult validation = graph.Validate();
    RequireScale(
        !validation.valid &&
            std::find(
                validation.messages.begin(),
                validation.messages.end(),
                "An input socket has more than one incoming link.") !=
                validation.messages.end(),
        "graph validation should reject ambiguous multiple links to one input");

    RenderGraphSnapshot snapshot;
    snapshot.outputNodeId = output;
    RenderGraphNode renderSourceA;
    renderSourceA.nodeId = sourceA;
    renderSourceA.kind = RenderGraphNodeKind::Image;
    RenderGraphNode renderSourceB;
    renderSourceB.nodeId = sourceB;
    renderSourceB.kind = RenderGraphNodeKind::Image;
    RenderGraphNode renderOutput;
    renderOutput.nodeId = output;
    renderOutput.kind = RenderGraphNodeKind::Output;
    snapshot.nodes = {
        std::move(renderSourceA),
        std::move(renderSourceB),
        std::move(renderOutput)
    };
    snapshot.links = {
        {
            sourceA,
            EditorNodeGraph::kImageOutputSocketId,
            output,
            EditorNodeGraph::kImageInputSocketId
        },
        {
            sourceB,
            EditorNodeGraph::kImageOutputSocketId,
            output,
            EditorNodeGraph::kImageInputSocketId
        }
    };
    const auto schedule =
        Stack::Renderer::GraphExecution::BuildGraphEvaluationSchedule(
            snapshot,
            {
                output,
                EditorNodeGraph::kImageOutputSocketId
            });
    RequireScale(
        !schedule.valid &&
            schedule.error.find("same input socket") != std::string::npos,
        "render scheduling should reject ambiguous input fan-in before execution");
}

void TestDeepCompoundDependencyWalkUsesBoundedCallStack() {
    constexpr int kDepth = 2048;
    EditorNodeGraph::Graph graph;
    const int sourceNodeId =
        graph.AddImageGeneratorNode(
            EditorNodeGraph::ImageGeneratorKind::SolidColor,
            { 0.0f, 0.0f })->id;
    std::vector<int> selectedNodeIds;
    selectedNodeIds.reserve(kDepth);

    int previousNodeId = sourceNodeId;
    for (int index = 0; index < kDepth; ++index) {
        const int operationNodeId =
            graph.AddTechnicalImageNode(
                Stack::NodeMath::TechnicalImageOperation::Exposure,
                { static_cast<float>(index + 1), 0.0f })->id;
        selectedNodeIds.push_back(operationNodeId);
        AddImageLink(graph, previousNodeId, operationNodeId);
        previousNodeId = operationNodeId;
    }
    const int outputNodeId =
        graph.AddOutputNode(
            { static_cast<float>(kDepth + 1), 0.0f },
            true)->id;
    AddImageLink(graph, previousNodeId, outputNodeId);

    int compoundNodeId = -1;
    std::string error;
    RequireScale(
        graph.CreateCompoundFromSelection(
            selectedNodeIds,
            "Deep pointwise chain",
            &compoundNodeId,
            &error),
        "deep compound fixture should author successfully");
    const EditorNodeGraph::Node* compound =
        graph.FindNode(compoundNodeId);
    RequireScale(
        compound != nullptr,
        "deep compound fixture should retain its authored instance");

    std::vector<std::string> dependencies;
    RequireScale(
        graph.ResolveCompoundOutputInputDependencies(
            compoundNodeId,
            graph.DefaultOutputSocket(*compound),
            dependencies,
            &error) &&
            dependencies.size() == 1,
        "deep compound dependency resolution should use a bounded call stack");
}

void TestRenderSnapshotLookupScalesWithGraphSize() {
    constexpr int kDepth = 8192;
    EditorNodeGraph::Graph graph;
    std::vector<int> nodeIds;
    nodeIds.reserve(kDepth);
    for (int index = 0; index < kDepth; ++index) {
        nodeIds.push_back(AddReformat(graph, static_cast<float>(index)));
        if (index > 0) {
            AddImageLink(graph, nodeIds[index - 1], nodeIds[index]);
        }
    }

    const int valueNodeId = graph.AddValueNode(
        Stack::NodeMath::MakeUniformScalar(2.5),
        { 0.0f, 160.0f })->id;
    const int technicalNodeId = graph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Exposure,
        { 200.0f, 160.0f })->id;
    const int previewNodeId = graph.AddPreviewNode({ 400.0f, 160.0f })->id;
    graph.EditLinks().push_back(EditorNodeGraph::Link{
        valueNodeId,
        EditorNodeGraph::kValueOutputSocketId,
        technicalNodeId,
        EditorNodeGraph::kExposureValueInputSocketId
    });
    graph.EditLinks().push_back(EditorNodeGraph::Link{
        technicalNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        previewNodeId,
        EditorNodeGraph::kPreviewInputSocketId
    });

    const Stack::Project::GraphSnapshotInternal::Lookup lookup(graph);
    for (std::size_t index = 0; index < nodeIds.size(); index += 257) {
        const EditorNodeGraph::Node* node = lookup.FindNode(nodeIds[index]);
        RequireScale(
            node != nullptr && node->id == nodeIds[index],
            "render-snapshot lookup should resolve nodes in a deep graph");
    }
    double exposure = 0.0;
    RequireScale(
        lookup.TryResolveUniformScalarInput(
            technicalNodeId,
            EditorNodeGraph::kExposureValueInputSocketId,
            exposure) &&
            exposure == 2.5,
        "render-snapshot lookup should preserve typed uniform inputs");
    RequireScale(
        lookup.IsAnalysisLink(graph.GetLinks().back()),
        "render-snapshot lookup should identify preview-only links");
    RequireScale(
        !lookup.IsAnalysisLink(graph.GetLinks().front()),
        "render-snapshot lookup should retain ordinary render links");
}

void TestMutableGraphLookupCacheTracksTopologyChanges() {
    EditorNodeGraph::Graph graph;
    const int sourceNodeId = graph.AddImageGeneratorNode(
        EditorNodeGraph::ImageGeneratorKind::SolidColor,
        { 0.0f, 0.0f })->id;
    const int reformatNodeId = graph.AddReformatNode({ 200.0f, 0.0f })->id;
    const int outputNodeId =
        graph.AddOutputNode({ 400.0f, 0.0f }, true)->id;
    const int previewNodeId = graph.AddPreviewNode({ 400.0f, 160.0f })->id;

    // Prime the cache before using the vector-backed construction path used by
    // serializers, clipboard imports, and scale fixtures.
    RequireScale(
        graph.FindNode(sourceNodeId) != nullptr,
        "mutable graph lookup cache should resolve its initial topology");
    AddImageLink(graph, sourceNodeId, reformatNodeId);
    AddImageLink(graph, reformatNodeId, outputNodeId);
    graph.EditLinks().push_back(EditorNodeGraph::Link{
        reformatNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        previewNodeId,
        EditorNodeGraph::kPreviewInputSocketId
    });

    RequireScale(
        graph.FindInputLink(
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId) != nullptr,
        "lookup cache should detect edited link-vector growth");
    RequireScale(
        graph.GetCompletedChains().size() == 1,
        "lookup-cache fixture should initially contain one completed output");

    graph.EditLinks()[1] = EditorNodeGraph::Link{
        reformatNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        previewNodeId,
        EditorNodeGraph::kPreviewInputSocketId
    };
    RequireScale(
        graph.GetCompletedChains().empty(),
        "same-size link edits should invalidate completed-chain state");
    graph.EditLinks()[1] = EditorNodeGraph::Link{
        reformatNodeId,
        EditorNodeGraph::kImageOutputSocketId,
        outputNodeId,
        EditorNodeGraph::kImageInputSocketId
    };

    std::vector<int> renderDestinations;
    graph.ForEachOutgoingRenderLink(
        reformatNodeId,
        [&](const EditorNodeGraph::Link& link) {
            renderDestinations.push_back(link.toNodeId);
        });
    RequireScale(
        renderDestinations.size() == 1 &&
            renderDestinations.front() == outputNodeId,
        "render adjacency should exclude preview-only analysis links");

    EditorNodeGraph::Graph copied = graph;
    RequireScale(
        copied.FindNode(outputNodeId) != nullptr &&
            copied.FindInputLink(
                outputNodeId,
                EditorNodeGraph::kImageInputSocketId) != nullptr,
        "copied graphs should rebuild pointer-bearing lookup state");

    RequireScale(
        graph.RemoveLink(
            reformatNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId),
        "lookup-cache fixture should remove its render link");
    RequireScale(
        graph.FindInputLink(
            outputNodeId,
            EditorNodeGraph::kImageInputSocketId) == nullptr,
        "lookup cache should not retain removed link pointers");
}

void TestTileGraphBatchReusesExactTopology() {
    constexpr int kWidth = 4;
    constexpr int kHeight = 4;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(kWidth) *
        static_cast<std::size_t>(kHeight) * 4u);
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] =
            static_cast<unsigned char>(index & 0xffu);
    }

    RenderGraphNode sourceNode;
    sourceNode.nodeId = 1;
    sourceNode.kind = RenderGraphNodeKind::Image;
    sourceNode.definitionId = "validation:tile-batch/source";
    sourceNode.definitionVersion = 1;
    sourceNode.definitionHash = sourceNode.definitionId;
    sourceNode.image.pixels =
        MakeSharedPixelBufferOwned(source);
    sourceNode.image.width = kWidth;
    sourceNode.image.height = kHeight;
    sourceNode.image.channels = 4;

    RenderGraphSnapshot graph;
    graph.outputNodeId = 1;
    graph.outputSocketId =
        EditorNodeGraph::kImageOutputSocketId;
    graph.nodes.push_back(std::move(sourceNode));

    Stack::EditorRenderWorkerTiles::TileGraphBatch batch(
        graph,
        kWidth,
        kHeight);
    const RenderGraphNode* stableNodeData =
        batch.Graph().nodes.data();

    RenderTileRect firstTile;
    firstTile.x = 0;
    firstTile.y = 0;
    firstTile.width = 2;
    firstTile.height = 2;
    firstTile.haloWidth = 2;
    firstTile.haloHeight = 2;
    RequireScale(
        batch.Prepare(firstTile),
        "tile graph batch should prepare its first crop");
    const SharedPixelBuffer firstCrop =
        batch.Graph().nodes.front().image.pixels;

    RenderTileRect secondTile;
    secondTile.x = 2;
    secondTile.y = 2;
    secondTile.width = 2;
    secondTile.height = 2;
    secondTile.haloX = 2;
    secondTile.haloY = 2;
    secondTile.haloWidth = 2;
    secondTile.haloHeight = 2;
    RequireScale(
        batch.Prepare(secondTile),
        "tile graph batch should prepare its second crop");
    const SharedPixelBuffer secondCrop =
        batch.Graph().nodes.front().image.pixels;

    RequireScale(
        batch.Graph().nodes.data() == stableNodeData &&
            batch.Topology().IsBoundTo(batch.Graph()),
        "tile preparation should preserve exact topology storage");
    RequireScale(
        firstCrop.size() == 2u * 2u * 4u &&
            secondCrop.size() == firstCrop.size() &&
            firstCrop.fingerprint != secondCrop.fingerprint,
        "tile graph batch should replace full-frame image payloads with distinct checked crops");
    RequireScale(
        Stack::EditorRenderWorkerTiles::CropSharedPixelBuffer(
            MakeSharedPixelBufferOwned(source),
            kWidth,
            kHeight,
            5,
            0,
            0,
            2,
            2).empty(),
        "tile crop should reject unsupported channel counts");
    std::vector<unsigned char> truncated(3u, 0u);
    RequireScale(
        Stack::EditorRenderWorkerTiles::CropSharedPixelBuffer(
            MakeSharedPixelBufferOwned(std::move(truncated)),
            kWidth,
            kHeight,
            4,
            0,
            0,
            2,
            2).empty(),
        "tile crop should reject truncated source payloads");
}

} // namespace

void RunGraphScaleTests() {
    TestDeepCycleCheckUsesBoundedCallStack();
    TestDeepRegionPlanningUsesBoundedCallStack();
    TestDeepCompletedChainUsesBoundedCallStack();
    TestSharedTreeCompletionAndCacheReuse();
    TestFrequencyChainRetainsItsReferenceSource();
    TestScalarBlendRetainsChannelIdentity();
    TestDeepScalarAndChannelPropagationUsesBoundedCallStack();
    TestFrequencyScenePathUsesTypedInputs();
    TestMfsrRejectsMixedFamiliesThroughFrequencyChain();
    TestDeepRenderScheduleUsesTopologicalOrder();
    TestWideSharedRenderScheduleUsesEachDependencyOnce();
    TestManyIndependentRenderTreesStayOutputScoped();
    TestAmbiguousInputFanInFailsClosed();
    TestDeepCompoundDependencyWalkUsesBoundedCallStack();
    TestRenderSnapshotLookupScalesWithGraphSize();
    TestMutableGraphLookupCacheTracksTopologyChanges();
    TestTileGraphBatchReusesExactTopology();
}
