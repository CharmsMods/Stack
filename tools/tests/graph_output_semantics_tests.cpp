#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/GraphWireReadout.h"
#include "Hdr/GraphView.h"
#include "Editor/Internal/EditorRenderWorkerTileGraph.h"
#include "Utils/HashUtils.h"
#include "Renderer/MaskRenderTypes.h"
#include "Project/GraphSnapshotBuilder.h"

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

void RunGraphInputShapeContractTests();

void RunGraphOutputSemanticsTests() {
    RunGraphInputShapeContractTests();
    {
        Graph draft;
        draft.Clear();
        draft.SetAllowNoOutput(true);
        const int id = draft.AddDataMathNode(DataMathMode::Clamp, {})->id;
        Check(draft.FindNode(id)->definitionResolved && draft.Validate().valid,
            "a factory-produced disconnected draft remains structurally valid");
        draft.EditNodes();
        auto* node = draft.FindNode(id);
        node->definitionResolved = false;
        node->definitionResolutionError.clear();
        const auto validation = draft.Validate();
        bool reportsUnresolved = false;
        for (const auto& message : validation.messages)
            reportsUnresolved |= message == "Node " + std::to_string(id) +
                " has an unresolved definition: Exact node definition has not been resolved.";
        Check(!validation.valid && reportsUnresolved,
            "an unresolved definition without error text must fail with a useful diagnostic");
    }
    Graph graph;
    ImagePayload pixels;
    pixels.width = 16;
    pixels.height = 8;
    pixels.originalChannels = 3;
    pixels.channels = 3;
    pixels.pixels.resize(16 * 8 * 3, 128);
    const int image = graph.AddImageNode(std::move(pixels), {})->id;
    const int split = graph.AddChannelSplitNode({})->id;
    const int average = graph.AddDataMathNode(DataMathMode::Mix, {})->id;
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
    const int reference=graph.AddImageNode({},{})->id;
    graph.FindNode(reference)->role=Stack::GraphModel::NodeRole::Reference;
    graph.FindNode(reference)->referenceType=LogicalValueType::ColorImage;
    context.sourceDescriptors[GraphOutputIdentity(reference,kImageOutputSocketId)]=hdr;
    Check(DescribeGraphOutput(graph,reference,kImageOutputSocketId,context).descriptor==hdr,
        "an explicitly resolved graph reference must preserve its source facts, components and precision");
    const auto before = hdrChannel.descriptor;
    WireReadout::Input inspection;
    inspection.hasDescriptor = true;
    inspection.descriptor = before;
    WireReadout::Build(inspection);
    Check(inspection.descriptor == before, "F readout must not modify carried values or their description");

    const int onlyB = graph.AddDataMathNode(DataMathMode::Mix, {})->id;
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

void RunHdrGraphSourceContractTests() {
    using K=RenderGraphNodeKind;
    RenderGraphSnapshot direct;
    RenderGraphNode source;source.nodeId=1;source.kind=K::Image;
    source.image.hdrPixels=std::make_shared<const std::vector<float>>();
    source.image.useHdrPixels=true;
    RenderGraphNode output;output.nodeId=2;output.kind=K::Output;
    direct.nodes={source,output};direct.links.push_back({1,"imageOut",2,"imageIn"});
    direct.outputNodeId=2;direct.outputSocketId="imageOut";
    auto& descriptor=direct.outputDescriptor;
    descriptor=MakeUnknownDescriptor(LogicalValueType::ColorImage);
    descriptor.color=SemanticField<ColorIdentity>::Known({"srgb-d65",{},ColorRelation::Standard});
    descriptor.transfer=SemanticField<TransferDescriptor>::Known({TransferKind::Linear,0,{}});
    descriptor.reference=SemanticField<ReferenceState>::Known(ReferenceState::Display);
    descriptor.range=SemanticField<NumericRange>::Known({0,1,true,true,NonFinitePolicy::Forbidden});
    auto base=direct;
    Check(Stack::Hdr::ConfigureView(base,1.f,true) && base.nodes.size()==2 &&
        !base.nodes[0].image.useHdrPixels,"untouched photo preserves authored SDR base");

    auto processed=direct;
    RenderGraphNode effect;effect.nodeId=3;effect.kind=K::Layer;
    effect.layerJson={{"type","Brightness"},{"brightness",0.25f}};
    processed.nodes.push_back(effect);
    processed.links={{1,"imageOut",3,"imageIn"},{3,"imageOut",2,"imageIn"}};
    processed.nodes[0].image.useHdrPixels=false;
    Check(Stack::Hdr::ConfigureView(processed,1.f,true) && processed.nodes.size()==4 &&
        processed.nodes[0].image.useHdrPixels,"processed SDR rendition consumes linear master before its view");

    auto toned=direct;
    effect.layerJson={{"type","Tone"}};
    toned.nodes.push_back(effect);
    toned.links={{1,"imageOut",3,"imageIn"},{3,"imageOut",2,"imageIn"}};
    toned.nodes[0].image.useHdrPixels=false;
    toned.outputDescriptor.range=SemanticField<NumericRange>{};
    Check(Stack::Hdr::ConfigureView(toned,1.f,true) && toned.nodes.size()==4 &&
        toned.nodes[0].image.useHdrPixels && toned.nodes.back().layerJson.value("_displayLinearInput",false) &&
        toned.outputDescriptor.reference.value==ReferenceState::Display,
        "Tone endpoint with unknown range preserves explicit display-linear interpretation");

    auto encoded=direct;
    encoded.nodes[0].image.useHdrPixels=false;
    RenderGraphNode encode;encode.nodeId=3;encode.kind=K::TechnicalImage;
    encode.technicalImageOperation=TechnicalImageOperation::SrgbEncode;
    encoded.nodes.push_back(encode);
    encoded.links={{1,"imageOut",3,"imageIn"},{3,"imageOut",2,"imageIn"}};
    encoded.outputDescriptor.transfer=SemanticField<TransferDescriptor>::Known({TransferKind::Srgb,0,{}});
    Check(Stack::Hdr::ConfigureView(encoded,1.f,true) && encoded.nodes.size()==3 &&
        encoded.nodes[0].image.useHdrPixels,"already encoded processing endpoint still consumes linear source");

    auto hidden=direct;
    hidden.nodes[0].image.hdrPixels.reset();
    hidden.outputDescriptor.reference=SemanticField<ReferenceState>::Known(ReferenceState::Scene);
    effect.layerJson={{"type","ViewTransform"},{"enabled",true},{"visible",false},{"debugFalseColor",true}};
    hidden.nodes.push_back(effect);
    hidden.links={{1,"imageOut",3,"imageIn"},{3,"imageOut",2,"imageIn"}};
    Check(Stack::Hdr::ConfigureView(hidden,4.f,true) && hidden.nodes.size()==4 &&
        !hidden.nodes[2].layerJson.value("visible",true),"hidden view cannot suppress actual presentation transform");
}

void RunLoadingImageSnapshotTests() {
    Graph graph;graph.ResetFromLayers(0,true);
    const int image=graph.GetActiveImageNodeId();
    const std::vector<std::shared_ptr<LayerBase>> layers;
    const Stack::Timeline::TimelineAnimationState timeline;
    const std::unordered_map<int,std::uint64_t> generations;
    const Stack::RawRecipe::RawDevelopmentRecipe recipe;
    const std::shared_ptr<const Raw::RawImageData> raw;
    const std::vector<Stack::Timeline::AnimatableParameterTarget> previewTargets;
    Stack::Project::GraphSnapshotInputs inputs{graph,layers,timeline,generations,recipe,raw,previewTargets,{}};
    const auto legacy=Stack::Project::BuildGraphSnapshot(inputs);
    Check(!legacy.snapshot.unavailableOutputs.count(image),"anonymous legacy source keeps pipeline fallback");
    graph.FindNode(image)->image.sourcePath="pending-import.png";
    graph.FindNode(image)->image.isLoading=true;
    const auto pending=Stack::Project::BuildGraphSnapshot(inputs);
    Check(pending.snapshot.unavailableOutputs.count(image)!=0,"loading import cannot render unrelated pipeline fallback");
}

void RunGraphInputShapeContractTests() {
    Graph graph;
    ImagePayload pixels;
    pixels.width=2;pixels.height=2;pixels.channels=3;pixels.originalChannels=3;
    pixels.pixels.resize(12,128);
    const int image=graph.AddImageNode(std::move(pixels),{})->id;
    const int split=graph.AddChannelSplitNode({})->id;
    const int math=graph.AddDataMathNode(DataMathMode::Add,{})->id;
    const int patcher=graph.AddLayerNode(LayerType::BackgroundPatcher,0,{})->id;
    const int contrast=graph.AddLayerNode(LayerType::Contrast,1,{})->id;
    const int draft=graph.AddLayerNode(LayerType::Contrast,2,{})->id;
    Check(graph.TryConnectSockets(image,"imageOut",split,"imageIn"),"shape rule RGB source");
    Check(graph.TryConnectSockets(split,"r",math,DataMathInputSocketId(0)),"shape rule arithmetic channel source");
    Check(graph.TryConnectSockets(image,"imageOut",patcher,"imageIn"),"full-image layer accepts RGB");
    Check(!graph.TryConnectSockets(split,"r",patcher,"imageIn"),"full-image layer rejects direct Channel");
    Check(!graph.TryConnectSockets(math,"imageOut",patcher,"imageIn"),"full-image layer rejects arithmetic Channel");
    const auto* retainedImage=graph.FindInputLink(patcher,"imageIn");
    Check(retainedImage && retainedImage->fromNodeId==image,"rejected shape replacement preserves original wire");
    Check(graph.TryConnectSockets(split,"r",contrast,"imageIn"),"channel-safe layer accepts direct Channel");
    Check(graph.TryConnectSockets(math,"imageOut",contrast,"imageIn"),"channel-safe layer accepts arithmetic Channel");
    const auto draftOutput=DescribeGraphOutput(graph,draft,"imageOut");
    Check(draftOutput.descriptor.logicalType==LogicalValueType::ColorImage,
        "disconnected processing draft preserves its declared image shape");
    bool missingDraftInput=false;
    for (const auto& diagnostic:draftOutput.diagnostics)
        missingDraftInput |= diagnostic.message==graph.FindNode(draft)->title+": Missing input imageIn" &&
            diagnostic.severity==DiagnosticSeverity::Information;
    Check(missingDraftInput,"disconnected processing draft reports its missing input");
    Check(graph.TryConnectSockets(draft,"imageOut",patcher,"imageIn"),"unresolved draft can be wired for later repair");
    std::string normalized,error;
    Check(graph.CanConnectSockets(split,"r",patcher,"imageIn",&normalized,&error,true) && normalized=="imageIn",
        "retained shape mismatch remains repairable rather than silently disconnected");
    const std::vector<std::shared_ptr<LayerBase>> layers;
    const Stack::Timeline::TimelineAnimationState timeline;
    const std::unordered_map<int,std::uint64_t> generations;
    const Stack::RawRecipe::RawDevelopmentRecipe recipe;
    const std::shared_ptr<const Raw::RawImageData> raw;
    const std::vector<Stack::Timeline::AnimatableParameterTarget> previewTargets;
    for (const int producer : {split, math}) {
        const std::string socket=producer==split ? "r" : "imageOut";
        for (auto& link:graph.EditLinks()) if (link.toNodeId==patcher && link.toSocketId=="imageIn") {
            link.fromNodeId=producer;link.fromSocketId=socket;
        }
        Check(graph.CanConnectSockets(producer,socket,patcher,"imageIn",&normalized,&error,true),
            "retained incompatible input stays editable");
        Check(DescribeGraphOutput(graph,patcher,"imageOut").invalidWire,
            "retained incompatible input marks dependent output invalid");
        Check(!DescribeGraphOutput(graph,contrast,"imageOut").invalidWire,
            "valid channel-safe processing remains available");
        Stack::Project::GraphSnapshotInputs inputs{graph,layers,timeline,generations,recipe,raw,previewTargets,{}};
        const auto snapshot=Stack::Project::BuildGraphSnapshot(inputs);
        Check(snapshot.snapshot.FindOutputFailure(patcher,"imageOut")!=nullptr,
            "retained incompatible input blocks execution");
        graph.FindNode(patcher)->layerParameters["enabled"]=false;
        graph.EditNodes();
        Check(!DescribeGraphOutput(graph,patcher,"imageOut").invalidWire,
            "disabled full-image layer preserves channel passthrough");
        const auto bypass=Stack::Project::BuildGraphSnapshot(inputs);
        Check(bypass.snapshot.FindOutputFailure(patcher,"imageOut")==nullptr,
            "disabled full-image layer remains executable passthrough");
        graph.FindNode(patcher)->layerParameters["enabled"]=true;
        graph.EditNodes();
    }
}

void RunTileGraphMasterCropTests() {
    RenderGraphSnapshot original;
    RenderGraphNode source;source.nodeId=1;source.kind=RenderGraphNodeKind::Image;
    source.image.width=4;source.image.height=3;source.image.channels=3;
    std::vector<unsigned char> bytes(4*3*3);
    std::vector<float> master(4*3*4);
    for(std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<unsigned char>(i);
    for(std::size_t i=0;i<master.size();++i) master[i]=static_cast<float>(i)+.25f;
    source.image.pixels=MakeSharedPixelBufferOwned(bytes);
    source.image.hdrPixels=std::make_shared<const std::vector<float>>(master);
    source.image.hdrFingerprint=123;source.image.useHdrPixels=true;
    original.nodes.push_back(source);original.outputNodeId=1;original.outputSocketId="imageOut";
    Stack::EditorRenderWorkerTiles::TileGraphBatch batch(original,4,3);
    const auto verify=[&](int x,int y) {
        RenderTileRect tile;tile.haloX=x;tile.haloY=y;tile.haloWidth=2;tile.haloHeight=2;
        Check(batch.Prepare(tile),"master tile preparation");
        const auto& image=batch.Graph().nodes[0].image;
        Check(image.width==2 && image.height==2 && image.channels==3 && image.useHdrPixels,
            "master tile preserves dimensions, byte channels and source selection");
        Check(image.pixels.size()==12 && image.hdrPixels && image.hdrPixels->size()==16,
            "master RGBA stride is independent of RGB byte stride");
        for(int row=0;row<2;++row) for(int col=0;col<2;++col) {
            for(int c=0;c<3;++c) Check(image.pixels.data()[(row*2+col)*3+c]==bytes[((y+row)*4+x+col)*3+c],
                "byte tile uses original source origin");
            for(int c=0;c<4;++c) Check((*image.hdrPixels)[(row*2+col)*4+c]==master[((y+row)*4+x+col)*4+c],
                "float tile uses original source origin and RGBA stride");
        }
        Check(image.hdrFingerprint==StackHash::HashBytes(reinterpret_cast<const unsigned char*>(image.hdrPixels->data()),
            image.hdrPixels->size()*sizeof(float)),"master tile fingerprint matches cropped content");
        return image.hdrFingerprint;
    };
    const auto first=verify(0,0);
    const auto retainedFirst=batch.Graph().nodes[0].image.hdrPixels;
    const auto firstContents=*retainedFirst;
    const auto second=verify(2,1);
    Check(first!=0 && second!=0 && first!=second,"different master tiles must not share content identity");
    Check(*retainedFirst==firstContents,"preparing another tile cannot mutate retained tile storage");
    Check(original.nodes[0].image.hdrPixels==source.image.hdrPixels && *source.image.hdrPixels==master &&
        original.nodes[0].image.width==4 && original.nodes[0].image.height==3 && source.image.hdrFingerprint==123,
        "tiling cannot mutate original master or graph dimensions");
    for(std::size_t i=0;i<bytes.size();++i) Check(original.nodes[0].image.pixels.data()[i]==bytes[i],
        "tiling cannot mutate original bytes");
    RenderTileRect invalid;invalid.haloX=3;invalid.haloWidth=2;invalid.haloHeight=2;
    Check(!batch.Prepare(invalid),"out-of-bounds tile cannot advertise unclamped payload dimensions");
    auto incomplete=original;incomplete.nodes[0].image.hdrPixels=std::make_shared<const std::vector<float>>(4);
    Stack::EditorRenderWorkerTiles::TileGraphBatch broken(incomplete,4,3);
    RenderTileRect valid;valid.haloWidth=2;valid.haloHeight=2;
    Check(!broken.Prepare(valid),"incomplete float master must fail before upload");
}
