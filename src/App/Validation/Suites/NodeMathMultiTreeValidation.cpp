#include "App/Validation/Suites/NodeMathMultiTreeValidation.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Node Math multi-tree validation failed: "
            << message
            << "\n";
    }
    return condition;
}

RenderGraphNode Node(int id, RenderGraphNodeKind kind, const char* definitionId) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = kind;
    node.definitionId = definitionId;
    node.definitionVersion = 1;
    node.definitionHash = definitionId;
    node.requestRevision = 1;
    return node;
}

RenderGraphLink Link(
    int fromNodeId,
    const char* fromSocketId,
    int toNodeId,
    const char* toSocketId) {
    RenderGraphLink link;
    link.fromNodeId = fromNodeId;
    link.fromSocketId = fromSocketId;
    link.toNodeId = toNodeId;
    link.toSocketId = toSocketId;
    return link;
}

std::vector<unsigned char> ReadOutput(
    RenderPipeline& pipeline,
    int expectedWidth,
    int expectedHeight) {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels =
        pipeline.GetOutputPixels(width, height);
    if (width != expectedWidth || height != expectedHeight) {
        return {};
    }
    return pixels;
}

bool NearByte(unsigned char actual, int expected, int tolerance = 2) {
    return std::abs(
        static_cast<int>(actual) -
        std::clamp(expected, 0, 255)) <= tolerance;
}

} // namespace

bool ValidateNodeMathMultiTreeWithCurrentContext() {
    constexpr int kWidth = 4;
    constexpr int kHeight = 4;
    std::vector<unsigned char> source(
        static_cast<std::size_t>(kWidth) *
        static_cast<std::size_t>(kHeight) * 4u);
    for (int pixel = 0; pixel < kWidth * kHeight; ++pixel) {
        const std::size_t offset =
            static_cast<std::size_t>(pixel) * 4u;
        source[offset + 0u] =
            static_cast<unsigned char>(20 + pixel * 3);
        source[offset + 1u] =
            static_cast<unsigned char>(40 + pixel * 5);
        source[offset + 2u] =
            static_cast<unsigned char>(30 + pixel * 4);
        source[offset + 3u] =
            static_cast<unsigned char>(100 + pixel * 7);
    }

    RenderGraphNode exposure = Node(
        3,
        RenderGraphNodeKind::TechnicalImage,
        "validation:multi-tree/exposure");
    exposure.technicalImageOperation =
        Stack::NodeMath::TechnicalImageOperation::Exposure;
    exposure.technicalExposureValue = 1.0f;

    RenderGraphNode halfGreen = Node(
        6,
        RenderGraphNodeKind::DataMath,
        "validation:multi-tree/half-green");
    halfGreen.dataMathMode = RenderDataMathMode::Multiply;
    halfGreen.dataMathSettings.constantB = 0.5f;

    RenderGraphNode halfMask = Node(
        10,
        RenderGraphNodeKind::MaskGenerator,
        "validation:multi-tree/half-mask");
    halfMask.maskKind = RenderMaskGeneratorKind::Solid;
    halfMask.maskSettings.value = 0.5f;

    RenderGraphNode maskDownsample = Node(
        11,
        RenderGraphNodeKind::Reformat,
        "validation:multi-tree/mask-downsample");
    maskDownsample.reformatSettings.width = 2;
    maskDownsample.reformatSettings.height = 2;
    RenderGraphNode maskUpsample = Node(
        12,
        RenderGraphNodeKind::Reformat,
        "validation:multi-tree/mask-upsample");
    maskUpsample.reformatSettings.width = kWidth;
    maskUpsample.reformatSettings.height = kHeight;

    RenderGraphNode maskedBrightness = Node(
        13,
        RenderGraphNodeKind::Layer,
        "validation:multi-tree/masked-brightness");
    maskedBrightness.layerJson = {
        { "type", "Brightness" },
        { "brightness", 0.2f }
    };

    RenderGraphNode measuredExposure = Node(
        17,
        RenderGraphNodeKind::TechnicalImage,
        "validation:multi-tree/measured-exposure");
    measuredExposure.technicalImageOperation =
        Stack::NodeMath::TechnicalImageOperation::Exposure;

    RenderGraphNode cycleA = Node(
        19,
        RenderGraphNodeKind::Reformat,
        "validation:multi-tree/disconnected-cycle-a");
    cycleA.reformatSettings.width = kWidth;
    cycleA.reformatSettings.height = kHeight;
    RenderGraphNode cycleB = Node(
        20,
        RenderGraphNodeKind::Reformat,
        "validation:multi-tree/disconnected-cycle-b");
    cycleB.reformatSettings.width = kWidth;
    cycleB.reformatSettings.height = kHeight;

    RenderGraphSnapshot graph;
    graph.outputNodeId = 2;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.executionInspectionEnabled = true;
    graph.nodes = {
        Node(1, RenderGraphNodeKind::Image, "validation:multi-tree/source"),
        Node(2, RenderGraphNodeKind::Output, "validation:multi-tree/base-output"),
        std::move(exposure),
        Node(4, RenderGraphNodeKind::Output, "validation:multi-tree/exposure-output"),
        Node(5, RenderGraphNodeKind::ChannelSplit, "validation:multi-tree/split"),
        std::move(halfGreen),
        Node(7, RenderGraphNodeKind::FrequencyFft, "validation:multi-tree/fft"),
        Node(8, RenderGraphNodeKind::FrequencyIfft, "validation:multi-tree/ifft"),
        Node(9, RenderGraphNodeKind::Output, "validation:multi-tree/channel-output"),
        Node(21, RenderGraphNodeKind::ChannelCombine, "validation:multi-tree/combine"),
        Node(22, RenderGraphNodeKind::Output, "validation:multi-tree/channel-inspection-output"),
        std::move(halfMask),
        std::move(maskDownsample),
        std::move(maskUpsample),
        std::move(maskedBrightness),
        Node(14, RenderGraphNodeKind::Output, "validation:multi-tree/masked-output"),
        Node(15, RenderGraphNodeKind::ChannelSplit, "validation:multi-tree/measured-split"),
        Node(16, RenderGraphNodeKind::FieldMean, "validation:multi-tree/field-mean"),
        std::move(measuredExposure),
        Node(18, RenderGraphNodeKind::Output, "validation:multi-tree/measured-output"),
        std::move(cycleA),
        std::move(cycleB)
    };
    graph.links = {
        Link(1, EditorNodeGraph::kImageOutputSocketId, 2, EditorNodeGraph::kImageInputSocketId),
        Link(1, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kImageInputSocketId),
        Link(3, EditorNodeGraph::kImageOutputSocketId, 4, EditorNodeGraph::kImageInputSocketId),
        Link(1, EditorNodeGraph::kImageOutputSocketId, 5, EditorNodeGraph::kImageInputSocketId),
        Link(5, "r", 21, "r"),
        Link(5, "g", 6, EditorNodeGraph::kMixInputASocketId),
        Link(6, EditorNodeGraph::kImageOutputSocketId, 21, "g"),
        Link(5, "b", 7, EditorNodeGraph::kChannelInputSocketId),
        Link(7, EditorNodeGraph::kSpectrumOutputSocketId, 8, EditorNodeGraph::kSpectrumInputSocketId),
        Link(8, EditorNodeGraph::kChannelOutputSocketId, 21, "b"),
        Link(5, "a", 21, "a"),
        Link(21, EditorNodeGraph::kImageOutputSocketId, 9, EditorNodeGraph::kImageInputSocketId),
        Link(5, "g", 22, EditorNodeGraph::kImageInputSocketId),
        Link(10, EditorNodeGraph::kMaskOutputSocketId, 11, EditorNodeGraph::kImageInputSocketId),
        Link(11, EditorNodeGraph::kImageOutputSocketId, 12, EditorNodeGraph::kImageInputSocketId),
        Link(1, EditorNodeGraph::kImageOutputSocketId, 13, EditorNodeGraph::kImageInputSocketId),
        Link(12, EditorNodeGraph::kImageOutputSocketId, 13, EditorNodeGraph::kMaskInputSocketId),
        Link(13, EditorNodeGraph::kImageOutputSocketId, 14, EditorNodeGraph::kImageInputSocketId),
        Link(1, EditorNodeGraph::kImageOutputSocketId, 15, EditorNodeGraph::kImageInputSocketId),
        Link(15, "r", 16, EditorNodeGraph::kReductionFieldInputSocketId),
        Link(1, EditorNodeGraph::kImageOutputSocketId, 17, EditorNodeGraph::kImageInputSocketId),
        Link(16, EditorNodeGraph::kValueOutputSocketId, 17, EditorNodeGraph::kExposureValueInputSocketId),
        Link(17, EditorNodeGraph::kImageOutputSocketId, 18, EditorNodeGraph::kImageInputSocketId),
        Link(19, EditorNodeGraph::kImageOutputSocketId, 20, EditorNodeGraph::kImageInputSocketId),
        Link(20, EditorNodeGraph::kImageOutputSocketId, 19, EditorNodeGraph::kImageInputSocketId)
    };
    const Stack::Renderer::GraphExecution::GraphTopologyIndex topology =
        Stack::Renderer::GraphExecution::BuildGraphTopologyIndex(graph);
    bool ok = Check(
        topology.valid,
        "the heterogeneous graph did not produce a reusable topology index");

    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.LoadSourceFromPixels(
        source.data(),
        kWidth,
        kHeight,
        4);

    pipeline.ExecuteGraph(graph, topology);
    const std::vector<unsigned char> baseline =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        baseline.size() == source.size(),
        "the direct source tree produced no complete output");

    graph.outputNodeId = 4;
    pipeline.ExecuteGraph(graph, topology);
    const std::vector<unsigned char> exposed =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        exposed.size() == baseline.size(),
        "the independent exposure tree produced no complete output");
    if (exposed.size() == baseline.size()) {
        for (std::size_t offset = 0;
             offset < baseline.size();
             offset += 4u) {
            ok &= Check(
                NearByte(exposed[offset + 0u], baseline[offset + 0u] * 2) &&
                NearByte(exposed[offset + 1u], baseline[offset + 1u] * 2) &&
                NearByte(exposed[offset + 2u], baseline[offset + 2u] * 2) &&
                NearByte(exposed[offset + 3u], baseline[offset + 3u]),
                "the exposure tree did not preserve its expected RGBA math");
        }
    }

    graph.outputNodeId = 9;
    pipeline.ExecuteGraph(graph, topology);
    const std::vector<unsigned char> recombined =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        recombined.size() == baseline.size(),
        "the heterogeneous channel/frequency tree produced no complete output");
    if (recombined.size() == baseline.size()) {
        for (std::size_t offset = 0;
             offset < baseline.size();
             offset += 4u) {
            ok &= Check(
                NearByte(recombined[offset + 0u], baseline[offset + 0u]) &&
                NearByte(
                    recombined[offset + 1u],
                    static_cast<int>(baseline[offset + 1u]) / 2) &&
                NearByte(recombined[offset + 2u], baseline[offset + 2u], 3) &&
                NearByte(recombined[offset + 3u], baseline[offset + 3u]),
                "Split/Data Math/FFT/IFFT recombination produced an unexpected channel value");
        }
    }

    const auto inspectionOutput = std::find_if(
        graph.nodes.begin(),
        graph.nodes.end(),
        [](const RenderGraphNode& node) {
            return node.nodeId == 22;
        });
    bool inspectionModesValid =
        inspectionOutput != graph.nodes.end();
    const Stack::NodeMath::OutputChannelViewMode inspectionModes[] = {
        Stack::NodeMath::OutputChannelViewMode::Neutral,
        Stack::NodeMath::OutputChannelViewMode::Red,
        Stack::NodeMath::OutputChannelViewMode::Green,
        Stack::NodeMath::OutputChannelViewMode::Blue
    };
    for (const auto mode : inspectionModes) {
        if (inspectionOutput == graph.nodes.end()) {
            break;
        }
        inspectionOutput->outputChannelViewMode = mode;
        graph.outputNodeId = 22;
        pipeline.ExecuteGraph(graph, topology);
        const std::vector<unsigned char> inspected =
            ReadOutput(pipeline, kWidth, kHeight);
        if (inspected.size() != baseline.size()) {
            inspectionModesValid = false;
            continue;
        }
        for (std::size_t offset = 0;
             offset < baseline.size();
             offset += 4u) {
            const int channel = baseline[offset + 1u];
            const int expectedRed =
                mode == Stack::NodeMath::OutputChannelViewMode::Neutral ||
                        mode == Stack::NodeMath::OutputChannelViewMode::Red
                    ? channel
                    : 0;
            const int expectedGreen =
                mode == Stack::NodeMath::OutputChannelViewMode::Neutral ||
                        mode == Stack::NodeMath::OutputChannelViewMode::Green
                    ? channel
                    : 0;
            const int expectedBlue =
                mode == Stack::NodeMath::OutputChannelViewMode::Neutral ||
                        mode == Stack::NodeMath::OutputChannelViewMode::Blue
                    ? channel
                    : 0;
            inspectionModesValid =
                inspectionModesValid &&
                NearByte(inspected[offset + 0u], expectedRed) &&
                NearByte(inspected[offset + 1u], expectedGreen) &&
                NearByte(inspected[offset + 2u], expectedBlue) &&
                inspected[offset + 3u] == 255u;
        }
    }
    ok &= Check(
        inspectionModesValid,
        "direct Channel Output inspection did not produce exact Neutral, Red, Green, Blue, opaque mappings");

    graph.outputNodeId = 14;
    pipeline.ExecuteGraph(graph, topology);
    const std::vector<unsigned char> masked =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        masked.size() == baseline.size(),
        "the mask/Reformat/Layer tree produced no complete output");
    if (masked.size() == baseline.size()) {
        for (std::size_t offset = 0;
             offset < baseline.size();
             offset += 4u) {
            ok &= Check(
                NearByte(masked[offset + 0u], baseline[offset + 0u] + 26) &&
                NearByte(masked[offset + 1u], baseline[offset + 1u] + 26) &&
                NearByte(masked[offset + 2u], baseline[offset + 2u] + 26) &&
                NearByte(masked[offset + 3u], baseline[offset + 3u]),
                "the 0.5 mask through 4x4 -> 2x2 -> 4x4 Reformat did not apply half of the Brightness adjustment");
        }
    }

    double meanRed = 0.0;
    for (std::size_t offset = 0;
         offset < baseline.size();
         offset += 4u) {
        meanRed +=
            static_cast<double>(baseline[offset + 0u]) / 255.0;
    }
    meanRed /=
        static_cast<double>(baseline.size() / 4u);
    const double measuredMultiplier = std::exp2(meanRed);
    graph.outputNodeId = 18;
    pipeline.ExecuteGraph(graph, topology);
    const GraphExecutionStats measuredStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<unsigned char> measured =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        measured.size() == baseline.size() &&
        measuredStats.reductionPasses == 1,
        "the Field Mean-driven exposure tree did not execute one reduction");
    if (measured.size() == baseline.size()) {
        for (std::size_t offset = 0;
             offset < baseline.size();
             offset += 4u) {
            ok &= Check(
                NearByte(
                    measured[offset + 0u],
                    static_cast<int>(std::lround(
                        baseline[offset + 0u] * measuredMultiplier))) &&
                NearByte(
                    measured[offset + 1u],
                    static_cast<int>(std::lround(
                        baseline[offset + 1u] * measuredMultiplier))) &&
                NearByte(
                    measured[offset + 2u],
                    static_cast<int>(std::lround(
                        baseline[offset + 2u] * measuredMultiplier))) &&
                NearByte(measured[offset + 3u], baseline[offset + 3u]),
                "the Field Mean-driven Exposure did not match the measured red-channel mean");
        }
    }
    pipeline.ExecuteGraph(graph, topology);
    const GraphExecutionStats warmMeasuredStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<unsigned char> warmMeasured =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        warmMeasured == measured &&
        warmMeasuredStats.reductionCacheHits > 0,
        "the unchanged reduction-driven tree was not pixel-stable and reduction-cache hot");

    graph.outputNodeId = 19;
    pipeline.ExecuteGraph(graph, topology);
    const GraphExecutionStats cycleStats =
        pipeline.GetLastGraphExecutionStats();
    ok &= Check(
        pipeline.GetOutputTexture() == 0 &&
        cycleStats.lastSpecializedFailure.find("cycle") !=
            std::string::npos,
        "selecting a disconnected cyclic sibling tree did not fail before evaluation");

    graph.outputNodeId = 2;
    pipeline.ExecuteGraph(graph, topology);
    const std::vector<unsigned char> recoveredBaseline =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        recoveredBaseline == baseline,
        "a failed cyclic sibling selection prevented a valid independent tree from recovering");

    graph.outputNodeId = 9;
    pipeline.ExecuteGraph(graph, topology);
    const GraphExecutionStats warmChannelStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<unsigned char> warmRecombined =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        warmRecombined == recombined &&
        warmChannelStats.maskCacheHits > 0 &&
        warmChannelStats.frequencyCacheHits > 0,
        "the unchanged heterogeneous tree was not pixel-stable and cache-hot");

    graph.outputNodeId = 4;
    pipeline.ExecuteGraph(graph, topology);
    const GraphExecutionStats revisitedExposureStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<unsigned char> revisitedExposure =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        revisitedExposure == exposed &&
        revisitedExposureStats.imageCacheHits > 0,
        "switching outputs discarded or changed the independent exposure tree cache");

    graph.outputNodeId = 14;
    pipeline.ExecuteGraph(graph, topology);
    const GraphExecutionStats revisitedMaskStats =
        pipeline.GetLastGraphExecutionStats();
    const std::vector<unsigned char> revisitedMask =
        ReadOutput(pipeline, kWidth, kHeight);
    ok &= Check(
        revisitedMask == masked &&
        revisitedMaskStats.imageCacheHits > 0 &&
        revisitedMaskStats.maskCacheHits > 0,
        "switching outputs discarded or changed the mask/geometry tree caches");

    RenderGraphSnapshot copiedGraph = graph;
    copiedGraph.outputNodeId = 2;
    RenderPipeline staleIndexPipeline;
    staleIndexPipeline.Initialize();
    staleIndexPipeline.LoadSourceFromPixels(
        source.data(),
        kWidth,
        kHeight,
        4);
    staleIndexPipeline.ExecuteGraph(copiedGraph, topology);
    const GraphExecutionStats staleIndexStats =
        staleIndexPipeline.GetLastGraphExecutionStats();
    ok &= Check(
        staleIndexPipeline.GetOutputTexture() == 0 &&
        staleIndexStats.lastSpecializedFailure.find("not bound") !=
            std::string::npos,
        "a topology index reused with a different snapshot did not fail closed");

    if (ok) {
        std::cout
            << "Node Math multi-tree live validation passed: independent "
               "image, channel/frequency, mask/geometry, and measured-reduction "
               "branches remain pixel-correct and cache-stable; a disconnected "
               "cycle fails only when selected.\n";
    }
    return ok;
}

} // namespace Stack::Validation
