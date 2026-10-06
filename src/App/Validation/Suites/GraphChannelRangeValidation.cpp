#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace Stack::Validation {
namespace {
RenderGraphNode Node(int id, RenderGraphNodeKind kind) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = kind;
    node.definitionId = "stack:test/channel-range";
    node.definitionHash = "channel-range-v1";
    return node;
}
float ReadRed(RenderPipeline& pipeline, const RenderGraphSnapshot& graph) {
    pipeline.ExecuteGraph(graph);
    const unsigned int texture = pipeline.GetOutputTexture();
    if (!texture) return std::numeric_limits<float>::quiet_NaN();
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    std::array<float, 4> pixel{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixel.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previous));
    return pixel[0];
}
bool Near(float observed, float expected, const char* message) {
    if (std::isfinite(observed) && std::abs(observed - expected) < 0.02f) return true;
    std::cerr << "Channel range validation: " << message << "; expected " << expected << ", observed " << observed << '\n';
    return false;
}
}

bool ValidateGraphChannelRangesWithContext() {
    using namespace EditorNodeGraph;
    RenderPipeline pipeline;
    pipeline.Initialize();
    const std::array<unsigned char, 4> pixel{ 128, 128, 128, 255 };
    pipeline.LoadSourceFromPixels(pixel.data(), 1, 1, 4);
    const float original = 128.0f / 255.0f;

    auto image = Node(1, RenderGraphNodeKind::Image);
    auto hdr = Node(2, RenderGraphNodeKind::DataMath);
    hdr.dataMathMode = RenderDataMathMode::Multiply;
    hdr.dataMathSettings.constantB = 16;
    auto split = Node(3, RenderGraphNodeKind::ChannelSplit);
    auto utility = Node(4, RenderGraphNodeKind::MaskUtility);
    utility.maskUtilityKind = RenderMaskUtilityKind::Levels;
    utility.maskUtilitySettings.blackPoint = 0;
    utility.maskUtilitySettings.whitePoint = 16;
    utility.maskUtilitySettings.gamma = 1;
    RenderGraphSnapshot graph;
    graph.outputNodeId = 4;
    graph.outputSocketId = kMaskOutputSocketId;
    graph.nodes = { image, hdr, split, utility };
    graph.links = {
        { 1, kImageOutputSocketId, 2, kMixInputASocketId },
        { 2, kImageOutputSocketId, 3, kImageInputSocketId },
        { 3, "r", 4, kMaskUtilityInputSocketId }
    };
    bool ok = Near(ReadRed(pipeline, graph), original, "Levels must map the full HDR input range before clamping");
    graph.nodes[3].maskUtilityKind = RenderMaskUtilityKind::Invert;
    graph.nodes[3].maskUtilitySettings.enabled = false;
    ok &= Near(ReadRed(pipeline, graph), original * 16, "bypassed channel Invert must preserve values above one");
    graph.nodes[3].maskUtilitySettings.enabled = true;
    ok &= Near(ReadRed(pipeline, graph), 1 - original * 16, "channel Invert must preserve signed results");

    auto processed = hdr;
    processed.nodeId = 5;
    processed.dataMathSettings.constantB = 24;
    auto amount = Node(6, RenderGraphNodeKind::ConstantChannel);
    auto average = Node(7, RenderGraphNodeKind::DataMath);
    average.dataMathMode = RenderDataMathMode::ImageAverage;
    auto output = Node(8, RenderGraphNodeKind::Output);
    graph.outputNodeId = 8;
    graph.outputSocketId = kImageOutputSocketId;
    graph.nodes = { image, hdr, split, processed, amount, average, output };
    graph.links = {
        { 1, kImageOutputSocketId, 2, kMixInputASocketId },
        { 2, kImageOutputSocketId, 3, kImageInputSocketId },
        { 1, kImageOutputSocketId, 5, kMixInputASocketId },
        { 3, "r", 6, kMatchExtentInputSocketId },
        { 5, kImageOutputSocketId, 7, kMixInputASocketId },
        { 5, kImageOutputSocketId, 7, kMixInputBSocketId },
        { 2, kImageOutputSocketId, 7, kDataMathBaseInputSocketId },
        { 6, kChannelOutputSocketId, 7, kMaskInputSocketId },
        { 7, kImageOutputSocketId, 8, kImageInputSocketId }
    };
    for (float influence : { -0.5f, 0.5f, 2.0f }) {
        graph.nodes[4].constantChannelValue = influence;
        ok &= Near(ReadRed(pipeline, graph), original * (16 + 8 * std::clamp(influence, 0.0f, 1.0f)),
            "coverage clamps influence while retaining HDR image values");
    }
    if (ok) std::cout << "Channel range validation passed: HDR remapping, signed channels, and coverage application.\n";
    return ok;
}
} // namespace Stack::Validation
