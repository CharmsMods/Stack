#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/GraphWireReadout.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "Graph output semantics: " << message << '\n'; std::exit(1); }
}

WireReadout::Readout Read(const Graph& graph, int node, const std::string& socket) {
    const auto description = DescribeGraphOutput(graph, node, socket);
    WireReadout::Input input;
    graph.FindSocket(node, socket, &input.sourceSocket);
    input.hasDescriptor = true;
    input.descriptor = description.descriptor;
    input.sourceDiagnostics = description.diagnostics;
    return WireReadout::Build(input);
}
}

void RunGraphOutputSemanticsTests() {
    Graph graph;
    ImagePayload pixels;
    pixels.width = 16;
    pixels.height = 8;
    pixels.originalChannels = 3;
    pixels.pixels.resize(16 * 8 * 4, 128);
    const int image = graph.AddImageNode(std::move(pixels), {})->id;
    const int split = graph.AddChannelSplitNode({})->id;
    const int average = graph.AddDataMathNode(DataMathMode::Average, {})->id;
    const int contrast = graph.AddLayerNode(LayerType::Contrast, 0, {})->id;
    const int combine = graph.AddChannelCombineNode({})->id;
    Check(graph.TryConnectSockets(image, kImageOutputSocketId, split, kImageInputSocketId), "image to split");
    Check(graph.TryConnectSockets(split, "r", average, DataMathInputSocketId(0)), "R to Average");
    Check(graph.TryConnectSockets(split, "g", average, DataMathInputSocketId(1)), "G to Average");
    Check(graph.TryConnectSockets(average, kImageOutputSocketId, contrast, kImageInputSocketId), "Average to Contrast");
    const auto carried = DescribeGraphOutput(graph, contrast, kImageOutputSocketId);
    Check(carried.descriptor.logicalType == LogicalValueType::Channel && carried.diagnostics.empty(), "valid channel processing must have a Channel description without a false missing image");
    Check(OutputChannelColor(carried).empty(), "Average must remain neutral through Contrast");
    Check(carried.descriptor.spatial.value.dataWindow.width == 16 && carried.descriptor.spatial.value.dataWindow.height == 8, "channel dimensions must propagate");
    const auto readout = Read(graph, contrast, kImageOutputSocketId);
    Check(readout.primary == "1 channel · 16 × 8" && readout.secondary == "16-bit float", "compact channel readout must describe shape and working precision");
    Check(graph.TryConnectSockets(contrast, kImageOutputSocketId, combine, "g"), "neutral processed Channel must be assignable to any component");
    Check(graph.ResolveSocketChannel(contrast, kImageOutputSocketId).empty(), "destination assignment must not recolor the source");
    const auto assembled = DescribeGraphOutput(graph, combine, kImageOutputSocketId);
    Check(assembled.descriptor.presentImageComponents.state == KnowledgeState::Known &&
        ImageComponentCount(assembled.descriptor.presentImageComponents.value) == 1 &&
        HasImageComponent(assembled.descriptor.presentImageComponents.value, ImageComponent::Green), "assembly must describe precisely the assigned components");

    const int arithmetic = graph.AddDataMathNode(DataMathMode::Add, {})->id;
    Check(graph.TryConnectSockets(split, "r", arithmetic, DataMathInputSocketId(0)), "R arithmetic input");
    Check(graph.TryConnectSockets(split, "g", arithmetic, DataMathInputSocketId(1)), "G arithmetic input");
    Check(graph.ResolveSocketChannel(arithmetic, kImageOutputSocketId).empty(), "R plus G must not inherit the first operand's color");
    graph.RemoveLink(split, "r", arithmetic, DataMathInputSocketId(0));
    graph.RemoveLink(split, "g", arithmetic, DataMathInputSocketId(1));
    Check(graph.TryConnectSockets(split, "g", arithmetic, DataMathInputSocketId(0)), "swap first operand");
    Check(graph.TryConnectSockets(split, "r", arithmetic, DataMathInputSocketId(1)), "swap second operand");
    Check(graph.ResolveSocketChannel(arithmetic, kImageOutputSocketId).empty(), "swapping operands must not change mixed-channel identity");

    const int broadcast = graph.AddDataMathNode(DataMathMode::Add, {})->id;
    Check(graph.TryConnectSockets(split, "r", broadcast, DataMathInputSocketId(0)), "broadcast channel input");
    Check(graph.TryConnectSockets(image, kImageOutputSocketId, broadcast, DataMathInputSocketId(1)), "broadcast RGB input");
    const auto channelFirst = DescribeGraphOutput(graph, broadcast, kImageOutputSocketId);
    Check(channelFirst.descriptor.logicalType == LogicalValueType::ColorImage &&
        ImageComponentCount(channelFirst.descriptor.presentImageComponents.value) == 3,
        "channel plus RGB must produce RGB");
    graph.RemoveLink(split, "r", broadcast, DataMathInputSocketId(0));
    graph.RemoveLink(image, kImageOutputSocketId, broadcast, DataMathInputSocketId(1));
    Check(graph.TryConnectSockets(image, kImageOutputSocketId, broadcast, DataMathInputSocketId(0)), "swap RGB to first operand");
    Check(graph.TryConnectSockets(split, "r", broadcast, DataMathInputSocketId(1)), "swap channel to second operand");
    Check(DescribeGraphOutput(graph, broadcast, kImageOutputSocketId).descriptor == channelFirst.descriptor,
        "broadcast description must not depend on operand order");

    const int unary = graph.AddLayerNode(LayerType::Brightness, 1, {})->id;
    Check(graph.TryConnectSockets(split, "r", unary, kImageInputSocketId), "R into independent processing");
    Check(graph.ResolveSocketChannel(unary, kImageOutputSocketId) == "r", "independent channel processing must preserve R identity");
    const int maskConsumer = graph.AddLayerNode(LayerType::Contrast, 2, {})->id;
    Check(graph.TryConnectSockets(image, kImageOutputSocketId, maskConsumer, kImageInputSocketId), "mask consumer image");
    Check(graph.TryConnectSockets(unary, kImageOutputSocketId, maskConsumer, kMaskInputSocketId), "R can be used for coverage");
    Check(graph.ResolveSocketChannel(unary, kImageOutputSocketId) == "r", "mask use must not rewrite the carried Channel identity");

    const int missing = graph.AddLayerNode(LayerType::Contrast, 3, {})->id;
    Check(!DescribeGraphOutput(graph, missing, kImageOutputSocketId).diagnostics.empty(), "a genuinely missing input needs a diagnostic");
    const auto red = Read(graph, split, "r");
    Check(red.attention == WireReadout::Attention::None, "an absent A component must not taint Split R");
    const auto alpha = DescribeGraphOutput(graph, split, "a");
    Check(!alpha.diagnostics.empty() && alpha.descriptor.logicalType == LogicalValueType::Channel, "absent alpha must preserve its declared shape and report a socket-specific cause");

    GraphOutputContext context;
    auto hdr = DescribeGraphOutput(graph, image, kImageOutputSocketId).descriptor;
    hdr.range = SemanticField<NumericRange>::Known({ -2, 16, true, true, NonFinitePolicy::Preserve });
    context.sourceDescriptors[GraphOutputIdentity(image, kImageOutputSocketId)] = hdr;
    const auto hdrChannel = DescribeGraphOutput(graph, split, "r", context);
    Check(hdrChannel.descriptor.range == hdr.range, "splitting HDR data must not invent a normalized range");
    const auto before = hdrChannel.descriptor;
    WireReadout::Input inspection;
    inspection.hasDescriptor = true;
    inspection.descriptor = before;
    WireReadout::Build(inspection);
    Check(inspection.descriptor == before, "F readout must not modify carried values or their description");

    const int onlyB = graph.AddDataMathNode(DataMathMode::Average, {})->id;
    Check(graph.TryConnectSockets(split, "b", onlyB, DataMathInputSocketId(1)), "Average can start with input B");
    Check(DescribeGraphOutput(graph, onlyB, kImageOutputSocketId).diagnostics.empty(), "input B must satisfy Average without a synthetic missing image input");

    WireReadout::Input neutral;
    neutral.hasDescriptor = true;
    neutral.descriptor = carried.descriptor;
    neutral.sourceSocket.semanticRoleKey = "red-channel";
    Check(WireReadout::DeclaredChannelRole(neutral).empty(), "known neutral must not fall back to a socket color");
    Diagnostic fault;
    fault.severity = DiagnosticSeverity::RuntimeFault;
    fault.message = "Blur failed";
    neutral.sourceDiagnostics = { fault };
    Check(WireReadout::Build(neutral).attention == WireReadout::Attention::Error, "runtime faults must be visible without replacing shape");

    graph.AddImageGeneratorNode(ImageGeneratorKind::SolidColor, {});
    const auto all = DescribeGraphOutputs(graph);
    Check(all.at(GraphOutputIdentity(unary, kImageOutputSocketId)).descriptor ==
        DescribeGraphOutput(graph, unary, kImageOutputSocketId).descriptor, "batched and individual output descriptions must agree");
    for (const auto& pair : all) {
        const auto issues = ValidateDescriptor(pair.second.descriptor);
        for (const auto& issue : issues) std::cerr << pair.first << ": " << issue.field << " " << issue.message << '\n';
        Check(issues.empty(), "every described graph output must satisfy descriptor invariants");
    }
    int compound = -1;
    std::string error;
    const int compoundMath = graph.AddDataMathNode(DataMathMode::Multiply, {})->id;
    const int compoundConsumer = graph.AddLayerNode(LayerType::Contrast, 4, {})->id;
    Check(graph.TryConnectSockets(unary, kImageOutputSocketId, compoundMath, DataMathInputSocketId(0)), "compound arithmetic input");
    Check(graph.TryConnectSockets(compoundMath, kImageOutputSocketId, compoundConsumer, kImageInputSocketId), "compound arithmetic output");
    const bool created = graph.CreateCompoundFromSelection({ compoundMath }, "Red processing", &compound, &error);
    if (!created) std::cerr << error << "\n";
    Check(created, "create channel compound");
    const auto compoundOutputs = DescribeGraphOutputs(graph);
    const Link* outgoing = nullptr;
    graph.ForEachIncomingLink(compoundConsumer, [&](const Link& link) {
        if (link.toSocketId == kImageInputSocketId) outgoing = &link;
    });
    Check(outgoing && outgoing->fromNodeId == compound, "compound rewires existing channel use");
    const auto& compoundChannel = compoundOutputs.at(GraphOutputIdentity(compound, outgoing->fromSocketId));
    Check(compoundChannel.descriptor.logicalType == LogicalValueType::Channel &&
        OutputChannelColor(compoundChannel) == "r" && compoundChannel.descriptor.spatial.value.dataWindow.width == 16,
        "compound output must retain its expanded channel shape, color, and extent");
    Check(graph.ResolveSocketChannel(compound, outgoing->fromSocketId) == "r" &&
        graph.IsScalarSocketStream(compound, outgoing->fromSocketId),
        "compound connection validation must use the same carried output facts");

}
