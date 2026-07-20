#include "App/Validation/ValidationSuites.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/GraphConnectionPresentation.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/SocketPresentation.h"
#include "Editor/LayerRegistry.h"
#include "NodeMath/TechnicalImageMath.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

struct SocketCatalogNode {
    std::string label;
    std::string category;
    EditorNodeGraph::Node prototype;
};

std::vector<SocketCatalogNode> BuildSocketCatalogNodes() {
    std::vector<SocketCatalogNode> result;
    std::set<std::pair<int, int>> includedKindsAndValues;
    for (const EditorNodeGraphDefinitions::NodeCatalogEntry& entry :
            EditorNodeGraphDefinitions::BuildNodeCatalogEntries()) {
        result.push_back(SocketCatalogNode{
            entry.label,
            entry.category,
            EditorNodeGraphDefinitions::BuildPrototypeNode(entry)
        });
        includedKindsAndValues.emplace(static_cast<int>(entry.kind), entry.value);
    }

    for (const LayerDescriptor& descriptor : LayerRegistry::GetAllDescriptors()) {
        const auto key = std::make_pair(
            static_cast<int>(EditorNodeGraph::NodeKind::Layer),
            static_cast<int>(descriptor.type));
        if (includedKindsAndValues.count(key) != 0) continue;
        EditorNodeGraphDefinitions::NodeCatalogEntry entry;
        entry.kind = EditorNodeGraph::NodeKind::Layer;
        entry.value = static_cast<int>(descriptor.type);
        entry.label = descriptor.displayName ? descriptor.displayName : "Hidden layer";
        entry.category = descriptor.categoryName ? descriptor.categoryName : "Hidden layer";
        result.push_back(SocketCatalogNode{
            entry.label + " (not shown in browser)",
            entry.category,
            EditorNodeGraphDefinitions::BuildPrototypeNode(entry)
        });
    }

    const std::pair<EditorNodeGraph::NodeKind, const char*> nonBrowserKinds[] = {
        { EditorNodeGraph::NodeKind::Image, "Image source" },
        { EditorNodeGraph::NodeKind::RawSource, "RAW source" },
        { EditorNodeGraph::NodeKind::RawDetailAutoMask, "RAW detail auto mask" },
        { EditorNodeGraph::NodeKind::RawDetailFusion, "RAW detail fusion" },
        { EditorNodeGraph::NodeKind::Composite, "Composite scene" }
    };
    for (const auto& [kind, label] : nonBrowserKinds) {
        EditorNodeGraph::Node prototype;
        prototype.kind = kind;
        EditorNodeGraphDefinitions::ApplyNodeMetadata(prototype);
        result.push_back(SocketCatalogNode{ label, "Non-browser / generated", std::move(prototype) });
    }
    return result;
}

std::string EscapeMarkdownCell(std::string value) {
    std::size_t position = 0;
    while ((position = value.find('|', position)) != std::string::npos) {
        value.replace(position, 1, "\\|");
        position += 2;
    }
    return value.empty() ? "—" : value;
}

RenderGraphNode Phase5ImageNode(int id) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::Image;
    node.definitionId = "stack:graph/image";
    return node;
}

RenderGraphNode Phase5TechnicalNode(
    int id,
    Stack::NodeMath::TechnicalImageOperation operation,
    float exposure = 0.0f) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::TechnicalImage;
    node.definitionId = "stack:compound/exposure-then-premultiply/canonical";
    node.technicalImageOperation = operation;
    node.technicalExposureValue = exposure;
    return node;
}

RenderGraphNode Phase5OutputNode(int id) {
    RenderGraphNode node;
    node.nodeId = id;
    node.kind = RenderGraphNodeKind::Output;
    node.definitionId = "stack:graph/output";
    return node;
}

RenderGraphLink Phase5Link(int fromNode, const char* fromSocket, int toNode, const char* toSocket) {
    return { fromNode, fromSocket, toNode, toSocket };
}

RenderGraphSnapshot BuildExposurePremultiplyGraph(bool materializeCanonical, float exposure) {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 4;
    graph.outputSocketId = EditorNodeGraph::kImageOutputSocketId;
    graph.executionInspectionEnabled = true;
    graph.nodes = {
        Phase5ImageNode(1),
        Phase5TechnicalNode(2, Stack::NodeMath::TechnicalImageOperation::Exposure, exposure),
        Phase5TechnicalNode(3, Stack::NodeMath::TechnicalImageOperation::Premultiply),
        Phase5OutputNode(4)
    };
    graph.links = {
        Phase5Link(1, EditorNodeGraph::kImageOutputSocketId, 2, EditorNodeGraph::kImageInputSocketId),
        Phase5Link(2, EditorNodeGraph::kImageOutputSocketId, 3, EditorNodeGraph::kImageInputSocketId),
        Phase5Link(3, EditorNodeGraph::kImageOutputSocketId, 4, EditorNodeGraph::kImageInputSocketId)
    };
    if (materializeCanonical) {
        graph.nodes.push_back(Phase5OutputNode(5));
        graph.links.push_back(Phase5Link(
            2, EditorNodeGraph::kImageOutputSocketId,
            5, EditorNodeGraph::kImageInputSocketId));
    }
    return graph;
}

bool BuildAuthoredCompoundSnapshot(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    bool unpackCanonical,
    RenderGraphSnapshot& snapshot,
    std::string& error) {
    const auto& templates = EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    if (templates.size() < 2) {
        error = "Both shipped compound definitions are required.";
        return false;
    }
    EditorNodeGraph::Graph graph;
    graph.Clear();
    if (!graph.AddCompoundDefinition(templates[0], &error) ||
        !graph.AddCompoundDefinition(templates[1], &error)) {
        return false;
    }

    EditorNodeGraph::ImagePayload source;
    source.label = "Phase 5B Authored Source";
    source.width = width;
    source.height = height;
    source.channels = 4;
    source.originalChannels = 4;
    source.pixels = pixels;
    const int sourceId = graph.AddImageNode(std::move(source), { 0.0f, 0.0f })->id;
    const int addMultiplyId = graph.AddCompoundNode(
        templates[0].identity, { 280.0f, 0.0f })->id;
    const int exposurePremultiplyId = graph.AddCompoundNode(
        templates[1].identity, { 560.0f, 0.0f })->id;
    const int outputId = graph.AddOutputNode({ 840.0f, 0.0f }, true)->id;
    Stack::NodeMath::SetCompoundParameterOverride(
        graph.FindNode(addMultiplyId)->compound.instance, "add", 0.25);
    Stack::NodeMath::SetCompoundParameterOverride(
        graph.FindNode(addMultiplyId)->compound.instance, "multiply", 2.0);
    Stack::NodeMath::SetCompoundParameterOverride(
        graph.FindNode(exposurePremultiplyId)->compound.instance, "exposure-ev", 1.0);
    if (!graph.TryConnectSockets(
            sourceId, EditorNodeGraph::kImageOutputSocketId,
            addMultiplyId, "image-in", &error) ||
        !graph.TryConnectSockets(
            addMultiplyId, "image-out",
            exposurePremultiplyId, "image-in", &error) ||
        !graph.TryConnectSockets(
            exposurePremultiplyId, "image-out",
            outputId, EditorNodeGraph::kImageInputSocketId, &error) ||
        !graph.IsOutputConnected()) {
        if (error.empty()) error = "The authored compound chain was not recognized as connected.";
        return false;
    }

    if (unpackCanonical) {
        EditorNodeGraph::Graph expanded;
        EditorNodeGraph::CompoundExpansionResult expansion;
        if (!graph.ExpandAllCompoundNodes(expanded, &expansion) || !expansion.success) {
            error = expansion.error;
            return false;
        }
        graph = std::move(expanded);
    }

    EditorModule editor;
    editor.GetNodeGraph() = std::move(graph);
    snapshot = editor.BuildGraphSnapshot();
    snapshot.executionInspectionEnabled = true;
    return !snapshot.nodes.empty() && snapshot.outputNodeId == outputId;
}

std::vector<float> ReadPhase5Texture(unsigned int texture, int width, int height) {
    if (texture == 0 || width <= 0 || height <= 0) return {};
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (fbo == 0) return {};
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::vector<float> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    glDeleteFramebuffers(1, &fbo);
    return error == GL_NO_ERROR ? pixels : std::vector<float>{};
}

double Phase5MaximumDifference(
    const std::vector<float>& left,
    const std::vector<float>& right) {
    if (left.size() != right.size() || left.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double maximum = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        maximum = std::max(maximum,
            std::abs(static_cast<double>(left[index]) - static_cast<double>(right[index])));
    }
    return maximum;
}

std::vector<float> EvaluatePhase5CanonicalCpu(
    const std::vector<unsigned char>& pixels,
    float exposure) {
    std::vector<float> result;
    result.reserve(pixels.size());
    for (std::size_t index = 0; index < pixels.size(); index += 4u) {
        const Stack::NodeMath::Rgba32f input {
            pixels[index + 0] / 255.0f,
            pixels[index + 1] / 255.0f,
            pixels[index + 2] / 255.0f,
            pixels[index + 3] / 255.0f
        };
        const Stack::NodeMath::Rgba32f exposed = Stack::NodeMath::ApplyTechnicalImageOperation(
            Stack::NodeMath::TechnicalImageOperation::Exposure, input, exposure);
        const Stack::NodeMath::Rgba32f premultiplied = Stack::NodeMath::ApplyTechnicalImageOperation(
            Stack::NodeMath::TechnicalImageOperation::Premultiply, exposed);
        result.insert(result.end(), premultiplied.begin(), premultiplied.end());
    }
    return result;
}

bool Phase5Check(bool condition, const std::string& message) {
    if (!condition) std::cerr << "Phase 5 validation failed: " << message << '\n';
    return condition;
}

bool ValidatePhase5BSocketPresentation() {
    using namespace EditorNodeGraph::SocketPresentation;
    bool ok = true;
    EditorNodeGraph::Graph compatibilityGraph;
    int prototypeId = 1000;
    for (const SocketCatalogNode& catalogNode : BuildSocketCatalogNodes()) {
        EditorNodeGraph::Node prototype = catalogNode.prototype;
        prototype.id = prototypeId++;
        if (prototype.kind == EditorNodeGraph::NodeKind::Compound) continue;
        for (const EditorNodeGraph::SocketDefinition& socket :
                compatibilityGraph.GetSockets(prototype, false)) {
            ok &= Phase5Check(!socket.id.empty() && !socket.label.empty(),
                "a registered socket is missing its stable ID or short label");
            ok &= Phase5Check(!socket.semanticRoleKey.empty(),
                "a registered socket is missing its normalized semantic role key");
            ok &= Phase5Check(
                socket.logicalType != Stack::NodeMath::LogicalValueType::Invalid,
                "a registered socket has no valid logical type");
            ok &= Phase5Check(
                socket.visibilityTier ==
                    (!socket.visible
                        ? EditorNodeGraph::SocketVisibilityTier::Advanced
                        : (socket.optional
                            ? EditorNodeGraph::SocketVisibilityTier::CommonOptional
                            : EditorNodeGraph::SocketVisibilityTier::Required)),
                "a registered socket has inconsistent optionality/visibility metadata");
        }
    }

    const auto& compounds = EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    std::string error;
    if (compounds.empty()) {
        ok &= Phase5Check(false, "the dynamic compound socket audit has no fixtures");
    }
    for (const Stack::NodeMath::CompoundDefinition& compound : compounds) {
        if (compatibilityGraph.AddCompoundDefinition(compound, &error)) {
            const int compoundId = compatibilityGraph.AddCompoundNode(
                compound.identity, { 0.0f, 0.0f })->id;
            for (const EditorNodeGraph::SocketDefinition& socket :
                    compatibilityGraph.GetSockets(*compatibilityGraph.FindNode(compoundId), false)) {
                ok &= Phase5Check(!socket.semanticRoleKey.empty() &&
                        socket.logicalType != Stack::NodeMath::LogicalValueType::Invalid,
                    "a dynamic compound socket was not normalized");
            }
        } else {
            ok &= Phase5Check(false, "a dynamic compound socket audit fixture could not be built: " + error);
        }
    }

    const int outputId = compatibilityGraph.AddOutputNode({ 0.0f, 0.0f }, true)->id;
    EditorNodeGraph::SocketDefinition redChannel;
    ok &= Phase5Check(
        compatibilityGraph.FindSocket(outputId, "r", &redChannel) &&
        redChannel.type == EditorNodeGraph::SocketType::ScalarField &&
        redChannel.logicalType == Stack::NodeMath::LogicalValueType::ScalarField,
        "dynamic channel lookup still disagrees with the scalar-field socket catalog");

    const EditorNodeGraph::SocketType formatterTypes[] = {
        EditorNodeGraph::SocketType::Image,
        EditorNodeGraph::SocketType::Mask,
        EditorNodeGraph::SocketType::ScalarField,
        EditorNodeGraph::SocketType::Scalar,
        EditorNodeGraph::SocketType::Vector4,
        EditorNodeGraph::SocketType::Raw,
        EditorNodeGraph::SocketType::Analysis
    };
    for (EditorNodeGraph::SocketType type : formatterTypes) {
        EditorNodeGraph::SocketDefinition socket {
            "fixture", 1, EditorNodeGraph::SocketDirection::Output,
            type, "Fixture", false, true };
        NormalizeSocketDefinition(EditorNodeGraph::NodeKind::Output, socket);
        ok &= Phase5Check(!PrimaryDescription(socket).empty(),
            "a socket family produced no plain-language wire description");
    }
    EditorNodeGraph::SocketDefinition spectrum {
        EditorNodeGraph::kImageOutputSocketId, 1,
        EditorNodeGraph::SocketDirection::Output,
        EditorNodeGraph::SocketType::Image, "Spectrum", false, true };
    NormalizeSocketDefinition(EditorNodeGraph::NodeKind::FrequencyFft, spectrum);
    ok &= Phase5Check(
        PrimaryDescription(spectrum) == "Complex spectrum · 2 components",
        "complex-spectrum formatting does not report its declared component shape");
    ok &= Phase5Check(
        PrimaryDescription(redChannel) == "Red channel · 1 channel",
        "channel formatting does not distinguish a channel from a semantic mask");
    Stack::NodeMath::ValueDescriptor imageState;
    imageState.color = Stack::NodeMath::SemanticField<Stack::NodeMath::ColorIdentity>::Known(
        { "srgb-d65", {}, Stack::NodeMath::ColorRelation::Standard });
    imageState.transfer = Stack::NodeMath::SemanticField<Stack::NodeMath::TransferDescriptor>::Known(
        { Stack::NodeMath::TransferKind::Srgb, 0.0, {} });
    imageState.alpha = Stack::NodeMath::SemanticField<Stack::NodeMath::AlphaMode>::Known(
        Stack::NodeMath::AlphaMode::Straight);
    ok &= Phase5Check(
        ImageStateDescription(imageState) == "sRGB · encoded · straight alpha",
        "image wire state formatting changed from the adopted two-line wording");

    using namespace EditorNodeGraph::ConnectionPresentation;
    ok &= Phase5Check(
        PinLabelRange(20.0f, 280.0f, 28.0f, true, 10.0f, 4.0f).HasUsableSpace() &&
        PinLabelRange(20.0f, 280.0f, 272.0f, false, 10.0f, 4.0f).HasUsableSpace(),
        "pin-label geometry does not leave positive text space on both node sides");
    ok &= Phase5Check(
        VisibleLineCount(StackAppearance::GraphConnectionLabelVisibility::Adaptive,
            0.75f, 140.0f, false) == 2 &&
        VisibleLineCount(StackAppearance::GraphConnectionLabelVisibility::Adaptive,
            0.55f, 90.0f, false) == 1 &&
        VisibleLineCount(StackAppearance::GraphConnectionLabelVisibility::Adaptive,
            0.54f, 200.0f, false) == 0 &&
        VisibleLineCount(StackAppearance::GraphConnectionLabelVisibility::InteractionOnly,
            0.2f, 20.0f, true) == 2,
        "adaptive wire-label zoom/length/interaction thresholds changed");
    float gapStart = -1.0f;
    float gapEnd = -1.0f;
    ok &= Phase5Check(
        BreakLineGap(220.0f, 100.0f, 0.5f, gapStart, gapEnd) &&
        gapStart > 0.0f && gapEnd < 1.0f &&
        !BreakLineGap(120.0f, 100.0f, 0.5f, gapStart, gapEnd),
        "break-line geometry does not preserve endpoint segments or short-wire fallback");
    const ImVec2 risingTangent = NormalizedTangent(ImVec2(4.0f, 3.0f));
    const ImVec2 reversedTangent = NormalizedTangent(ImVec2(-4.0f, -3.0f));
    const ImVec2 curveTangent = CubicBezierTangent(
        ImVec2(0.0f, 0.0f),
        ImVec2(80.0f, 0.0f),
        ImVec2(120.0f, 80.0f),
        ImVec2(200.0f, 80.0f),
        0.5f);
    const RotatedBounds rotated = BoundsForRotatedRect(
        ImVec2(100.0f, 60.0f), ImVec2(120.0f, 14.0f), 0.65f);
    ok &= Phase5Check(
        risingTangent.x > 0.0f &&
        reversedTangent.x > 0.0f &&
        curveTangent.x > 0.0f &&
        NormalizeUprightAngle(2.8f) < 1.5708f &&
        rotated.maximum.x > rotated.minimum.x &&
        rotated.maximum.y > rotated.minimum.y,
        "rotated connection text geometry does not keep labels upright with positive hit bounds");
    ok &= Phase5Check(
        std::abs(ConnectionTextSize(
            12.0f, 0.25f, StackAppearance::GraphConnectionTextSizing::Fixed) - 12.0f) < 0.0005f &&
        ConnectionTextSize(
            12.0f, 0.25f, StackAppearance::GraphConnectionTextSizing::ZoomAware) < 12.0f &&
        ConnectionTextSize(
            12.0f, 4.0f, StackAppearance::GraphConnectionTextSizing::ZoomAware) > 12.0f,
        "connection text Fixed and Zoom-Aware sizing no longer behave distinctly");

    std::string appearanceError;
    ok &= Phase5Check(
        StackAppearance::ValidateConnectionPresentationAppearancePersistence(&appearanceError),
        appearanceError.empty() ? "appearance persistence validation failed" : appearanceError);
    return ok;
}

bool RunPhase5ValidationWithContext() {
    const std::vector<unsigned char> pixels {
        64, 32, 16, 128,
        255, 128, 0, 64,
        0, 255, 200, 255,
        10, 20, 30, 0
    };
    constexpr int width = 4;
    constexpr int height = 1;
    constexpr float exposure = 1.0f;
    constexpr double rgba16fTolerance = 2.5e-3;

    std::vector<float> optimizedPixels;
    GraphExecutionStats optimizedStats;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        pipeline.ExecuteGraph(BuildExposurePremultiplyGraph(false, exposure));
        optimizedPixels = ReadPhase5Texture(pipeline.GetOutputTexture(), width, height);
        optimizedStats = pipeline.GetLastGraphExecutionStats();
    }

    std::vector<float> canonicalPixels;
    GraphExecutionStats canonicalStats;
    {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        pipeline.ExecuteGraph(BuildExposurePremultiplyGraph(true, exposure));
        canonicalPixels = ReadPhase5Texture(pipeline.GetOutputTexture(), width, height);
        canonicalStats = pipeline.GetLastGraphExecutionStats();
    }

    const std::vector<float> cpuPixels = EvaluatePhase5CanonicalCpu(pixels, exposure);
    bool ok = ValidatePhase5BSocketPresentation();
    ok &= Phase5Check(
        optimizedStats.fusedPointwiseGroups == 1 &&
        optimizedStats.fusedPointwiseNodes == 2 &&
        !optimizedStats.pointwiseGroups.empty() &&
        optimizedStats.pointwiseGroups.front().authoredNodeIds == std::vector<int>({ 2, 3 }),
        "optimized-equivalent execution did not preserve Exposure-then-Premultiply authored order");
    ok &= Phase5Check(canonicalStats.fusedPointwiseGroups == 0,
        "canonical comparison path did not materialize the internal graph");
    ok &= Phase5Check(
        Phase5MaximumDifference(cpuPixels, canonicalPixels) <= rgba16fTolerance,
        "canonical live GPU path disagrees with the canonical CPU math");
    ok &= Phase5Check(
        Phase5MaximumDifference(cpuPixels, optimizedPixels) <= rgba16fTolerance,
        "optimized-equivalent live GPU path exceeds the declared compound tolerance");
    ok &= Phase5Check(
        Phase5MaximumDifference(canonicalPixels, optimizedPixels) <= rgba16fTolerance,
        "optimized-equivalent and canonical compound execution disagree");

    RenderGraphSnapshot authoredSnapshot;
    RenderGraphSnapshot unpackedSnapshot;
    std::string authoredError;
    ok &= Phase5Check(
        BuildAuthoredCompoundSnapshot(
            pixels, width, height, false, authoredSnapshot, authoredError),
        "the real authored compound chain could not build a renderer snapshot: " + authoredError);
    authoredError.clear();
    ok &= Phase5Check(
        BuildAuthoredCompoundSnapshot(
            pixels, width, height, true, unpackedSnapshot, authoredError),
        "the unpacked canonical chain could not build a renderer snapshot: " + authoredError);

    std::vector<float> authoredPixels;
    std::vector<float> unpackedPixels;
    unsigned int authoredTexture = 0;
    if (!authoredSnapshot.nodes.empty()) {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        pipeline.ExecuteGraph(authoredSnapshot);
        authoredTexture = pipeline.GetOutputTexture();
        authoredPixels = ReadPhase5Texture(authoredTexture, width, height);
    }
    if (!unpackedSnapshot.nodes.empty()) {
        RenderPipeline pipeline;
        pipeline.Initialize();
        pipeline.LoadSourceFromPixels(pixels.data(), width, height, 4);
        pipeline.ExecuteGraph(unpackedSnapshot);
        unpackedPixels = ReadPhase5Texture(pipeline.GetOutputTexture(), width, height);
    }
    ok &= Phase5Check(
        authoredTexture != 0 && !authoredPixels.empty(),
        "the authored Image -> Add/Multiply compound -> Exposure/Premultiply compound -> Output chain produced no viewport texture");
    ok &= Phase5Check(
        Phase5MaximumDifference(authoredPixels, unpackedPixels) <= rgba16fTolerance,
        "the authored compound chain visibly changed when compared with its unpacked canonical graph");

    if (ok) {
        std::cout << "Phase 5 live validation passed: authored compounds, nonzero output, "
                     "canonical CPU/GPU, unpacked equivalence, tolerance, and authored order.\n";
    }
    return ok;
}

} // namespace

bool WriteNodeSocketCatalog(const std::string& outputPath, std::string* errorMessage) {
    namespace Presentation = EditorNodeGraph::SocketPresentation;
    if (errorMessage) errorMessage->clear();
    if (outputPath.empty()) {
        if (errorMessage) *errorMessage = "The output path is empty.";
        return false;
    }

    std::ostringstream markdown;
    markdown << "# Stack Node Socket Catalog\n\n"
        << "- Generated: 2026-07-17\n"
        << "- Source of truth: live node catalog, hidden layer registry entries, "
           "non-browser node kinds, shipped compound definitions, and "
           "`EditorNodeGraph::Graph::GetSockets(..., false)`\n"
        << "- Regenerate: `Stack.exe --write-node-socket-catalog <absolute-output-path>`\n\n"
        << "This is a generated inventory, not a second hand-maintained socket schema. "
           "Every row is normalized through the same presentation glossary used by the graph UI. "
           "`Unknown` is deliberate where Stack does not know a channel shape or units. "
           "Custom compounds use their exact saved public interface and are audited at runtime; "
           "the shipped compounds below demonstrate that dynamic path.\n\n"
        << "| Node | Category | Direction | Socket ID | Short label | Role key | Logical type | Channel/component shape | Units | Optionality | Visibility tier |\n"
        << "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";

    EditorNodeGraph::Graph graph;
    int nodeId = 1000;
    auto appendNode = [&](const std::string& label, const std::string& category,
                          EditorNodeGraph::Node prototype) {
        prototype.id = nodeId++;
        const std::vector<EditorNodeGraph::SocketDefinition> sockets =
            graph.GetSockets(prototype, false);
        if (sockets.empty()) {
            markdown << "| " << EscapeMarkdownCell(label)
                << " | " << EscapeMarkdownCell(category)
                << " | — | — | — | — | — | — | — | — | — |\n";
            return;
        }
        for (const EditorNodeGraph::SocketDefinition& socket : sockets) {
            const std::string shape = Presentation::ChannelShapeName(socket.declaredChannels);
            const std::string units = Presentation::UnitName(socket.declaredUnits);
            markdown << "| " << EscapeMarkdownCell(label)
                << " | " << EscapeMarkdownCell(category)
                << " | " << (socket.direction == EditorNodeGraph::SocketDirection::Input ? "Input" : "Output")
                << " | `" << EscapeMarkdownCell(socket.id) << "`"
                << " | " << EscapeMarkdownCell(socket.label)
                << " | `" << EscapeMarkdownCell(socket.semanticRoleKey) << "`"
                << " | " << Presentation::LogicalTypeName(socket.logicalType)
                << " | " << EscapeMarkdownCell(shape.empty() ? "Not applicable" : shape)
                << " | " << EscapeMarkdownCell(units.empty() ? "Not applicable" : units)
                << " | " << (socket.optional ? "Optional" : "Required")
                << " | " << Presentation::VisibilityName(socket.visibilityTier)
                << " |\n";
        }
    };

    for (const SocketCatalogNode& catalogNode : BuildSocketCatalogNodes()) {
        appendNode(catalogNode.label, catalogNode.category, catalogNode.prototype);
    }

    for (const Stack::NodeMath::CompoundDefinition& definition :
            EditorNodeGraphDefinitions::GetShippedCompoundTemplates()) {
        std::string error;
        if (!graph.AddCompoundDefinition(definition, &error)) {
            if (errorMessage) *errorMessage = error;
            return false;
        }
        EditorNodeGraph::Node* node = graph.AddCompoundNode(definition.identity, { 0.0f, 0.0f });
        if (!node) {
            if (errorMessage) *errorMessage = "Could not instantiate a shipped compound.";
            return false;
        }
        appendNode(definition.label, "Compound (dynamic public interface)", *node);
    }

    const std::filesystem::path path(outputPath);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (errorMessage) *errorMessage = "Could not create the catalog directory: " + ec.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (errorMessage) *errorMessage = "Could not open the catalog output file.";
        return false;
    }
    output << markdown.str();
    if (!output) {
        if (errorMessage) *errorMessage = "Could not finish writing the catalog.";
        return false;
    }
    return true;
}

bool ValidateNodeMathPhase5Integration() {
    if (!glfwInit()) {
        std::cerr << "Phase 5 validation failed: glfwInit failed.\n";
        return false;
    }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Stack Phase 5 Validation", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Phase 5 validation failed: hidden OpenGL context creation failed.\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        std::cerr << "Phase 5 validation failed: OpenGL function loading failed.\n";
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }
    const bool result = RunPhase5ValidationWithContext();
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}

} // namespace Stack::Validation
