#include "Project/RawLayerStackSnapshot.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Persistence/ProjectStore.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLStateGuards.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
using namespace Project;
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

using Kind = RawRecipe::GraphOperationKind;
RawRecipe::RawDevelopmentRecipe Photo(const RawAdjustmentLayer& layer, Kind kind) {
    return RawRecipe::ReadGraphOperation(FindRawOperation(layer, kind)->rawOperation);
}
template<class Edit> void EditPhoto(RawAdjustmentLayer& layer, Kind kind, Edit edit) {
    auto recipe = Photo(layer, kind); edit(recipe);
    RawRecipe::WriteGraphOperation(FindRawOperation(layer, kind)->rawOperation, recipe);
}
void Attach(RawLayerStackState& state, const std::string& layer, Kind kind, const RawMaskReference& mask) {
    const auto* node = FindRawOperation(*FindRawAdjustmentLayer(state, layer), kind);
    std::string error;
    Require(SetRawOperationMask(state, layer, node->instanceUuid, mask, error), error);
}
int CompiledOperation(const RenderGraphSnapshot& graph, const RawLayerStackState& state, const std::string& layer, Kind kind) {
    return graph.rawLayerMaskNodeIds.at(layer).at(FindRawOperation(*FindRawAdjustmentLayer(state, layer), kind)->id);
}

// The small signed/HDR fixture also used by the calibration checks. These
// values exercise the new path without a file decoder or generated assets.
RenderGraphSnapshot BackgroundGraph() {
    auto raw = std::make_shared<Raw::RawImageData>();
    raw->metadata.sourcePath = "raw-layers-signed-samples";
    raw->metadata.rawWidth = raw->metadata.visibleWidth = 4;
    raw->metadata.rawHeight = raw->metadata.visibleHeight = 2;
    raw->metadata.pixelLayout = Raw::RawPixelLayout::LinearRgb;
    raw->metadata.mosaiced = false;
    raw->metadata.linearChannels = 3;
    raw->metadata.cameraWhiteBalance = {1, 1, 1, 1};
    raw->linearFloatBuffer = {-.5f,-.5f,-.5f, 0,0,0, .18f,.18f,.18f, 4,4,4,
        2,-.25f,.5f, .5f,1.5f,-.125f, -.125f,.25f,3, .8f,.3f,.1f};
    RenderGraphNode source;
    source.nodeId = 1;
    source.kind = RenderGraphNodeKind::RawDevelopment;
    source.rawDevelopment.embeddedRawData = raw;
    auto& recipe = source.rawDevelopment.recipe;
    recipe = RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath);
    recipe.rgbDenoise.enabled = false;
    recipe.technical.mosaicDenoise.enabled = false;
    recipe.finishTone.layerJson["enabled"] = false;
    recipe.viewTransform.layerJson["enabled"] = false;
    RenderGraphNode output;
    output.nodeId = 2;
    output.kind = RenderGraphNodeKind::Output;
    RenderGraphSnapshot graph;
    graph.nodes = {source, output};
    graph.links.push_back({1, "imageOut", 2, "imageIn"});
    graph.outputNodeId = 2;
    graph.outputSocketId = "imageOut";
    return graph;
}

std::vector<float> Render(RenderPipeline& pipeline, const RenderGraphSnapshot& graph) {
    pipeline.Resize(4, 2);
    pipeline.ExecuteGraph(graph);
    Require(pipeline.GetOutputTexture() != 0 && !pipeline.GetLastGraphExecutionStats().allocationFailed,
        "Layer rendering failed: " + pipeline.GetLastGraphExecutionStats().lastSpecializedFailure);
    Require(pipeline.GetCanvasWidth() == 4 && pipeline.GetCanvasHeight() == 2, "Layer canvas changed.");
    std::vector<float> pixels(4 * 2 * 4);
    const Renderer::GLState::PixelPackState pack;
    pack.ConfigureTightCpuReadback();
    glBindTexture(GL_TEXTURE_2D, pipeline.GetOutputTexture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    pack.Restore();
    Require(glGetError() == GL_NO_ERROR, "Layer readback failed.");
    for (float value : pixels) Require(std::isfinite(value), "Layer produced a nonfinite pixel.");
    return pixels;
}

void CompareGain(const std::vector<float>& base, const std::vector<float>& actual, float gain) {
    for (std::size_t i = 0; i < base.size(); ++i) {
        const float expected = i % 4 == 3 ? base[i] : base[i] * gain;
        Require(std::abs(actual[i] - expected) <= .004f * std::max(.25f, std::abs(expected)),
            "Tool/layer coverage or scene-linear gain differs at component " + std::to_string(i) +
            ": expected " + std::to_string(expected) + ", got " + std::to_string(actual[i]));
    }
}

RawMaskReference SolidMask(RawLayerStackState& state, const std::string& owner, float coverage) {
    auto reference = AddRawGeneratedMask(state, owner, EditorNodeGraph::MaskGeneratorKind::Solid, "Coverage");
    auto* layer = FindRawAdjustmentLayer(state, owner);
    const int output = FindRawMaskOutput(state, reference)->nodeId;
    const int generator = layer->graph.FindInputLink(output, "imageIn")->fromNodeId;
    layer->graph.FindNode(generator)->maskSettings.value = coverage;
    return reference;
}

void CheckPixelsAndView() {
    RenderPipeline pipeline;
    pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);
    pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    auto graph = BackgroundGraph();
    std::string error;
    for (auto space : {Raw::RawWorkingSpace::LinearSrgbD65, Raw::RawWorkingSpace::LinearRec2020D65}) {
        graph.nodes[0].rawDevelopment.recipe.technical.workingSpace = space;
        const auto base = Render(pipeline, graph);
        Require(*std::min_element(base.begin(), base.end()) < -.1f &&
            *std::max_element(base.begin(), base.end()) > 3.0f, "Upstream input lost signed/HDR values.");
        RawLayerStackState state;
        const auto first = AddRawAdjustmentLayer(state);
        std::cout << "Checking neutral layer in " << RawRecipe::WorkingSpaceStableString(space) << '\n';
        auto lowered = graph;
        Require(LowerRawLayerStack(lowered, state, 1, 1, error), error);
        CompareGain(base, Render(pipeline, lowered), 1.0f);
        auto typed = state;
        const auto scalarId = typed.layers.front().graph.AddValueNode(NodeMath::MakeUniformScalar(1.0),{0,600})->id;
        Require(PublishRawLayerResult(typed,RawLayerEndpoint(typed.layers.front(),
            *typed.layers.front().graph.FindNode(scalarId),"valueOut"),kRawBackgroundId,error),error);
        const auto referenceId = typed.background.graph.GetNodes().back().id;
        Require(typed.background.graph.TryConnectSockets(referenceId,"imageOut",
            FindRawOperation(typed.background,Kind::Exposure)->id,"param:ev",&error),error);
        lowered = graph;
        Require(LowerRawLayerStack(lowered,typed,1,2,error),error);
        CompareGain(base,Render(pipeline,lowered),2.f);
        typed.layers.front().graph.FindNode(scalarId)->value.value = NodeMath::MakeUniformScalar(-1.0);
        lowered = graph;
        Require(LowerRawLayerStack(lowered,typed,1,3,error),error);
        CompareGain(base,Render(pipeline,lowered),.5f);
        std::cout << "Cross-layer scalar publication and cache invalidation passed.\n";
        // A compound's public output and its internal reference must both
        // survive lowering. The later layer's independent scalar is legal.
        auto compounded = state;
        auto& producer = compounded.layers.front();
        const int scalarSource = producer.graph.AddValueNode(NodeMath::MakeUniformScalar(1.0),{0,600})->id;
        EditorNodeGraph::Graph internal;
        internal.SetAllowNoOutput(true);
        auto* internalReference = internal.AddImageNode({},{});
        internalReference->role = GraphModel::NodeRole::Reference;
        internalReference->referenceType = NodeMath::LogicalValueType::Scalar;
        internalReference->reference = RawLayerEndpoint(producer,*producer.graph.FindNode(scalarSource),"valueOut");
        NodeMath::CompoundDefinition definition;
        definition.definitionUuid = NodeMath::GenerateCanonicalUuid();
        definition.identity.id = "project:compound/" + definition.definitionUuid;
        definition.identity.version = {1,0,0};
        definition.label = "Published reference";
        definition.ports.push_back({"value","Value",NodeMath::PortDirection::Output,
            NodeMath::LogicalValueType::Scalar,false,internalReference->instanceUuid,"imageOut"});
        definition.canonicalGraph = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),internal);
        NodeMath::RefreshCompoundDefinitionContentHash(definition);
        Require(producer.graph.AddCompoundDefinition(definition,&error),error);
        const int compoundId = producer.graph.AddCompoundNode(definition.identity,{250,600})->id;
        Require(PublishRawLayerResult(compounded,RawLayerEndpoint(producer,*producer.graph.FindNode(compoundId),"value"),
            kRawBackgroundId,error),error);
        const int compoundReferenceId = compounded.background.graph.GetNodes().back().id;
        Require(compounded.background.graph.TryConnectSockets(compoundReferenceId,"imageOut",
            FindRawOperation(compounded.background,Kind::Exposure)->id,"param:ev",&error),error);
        lowered = graph;
        Require(LowerRawLayerStack(lowered,compounded,1,4,error),error);
        CompareGain(base,Render(pipeline,lowered),2.f);
        std::cout << "Compound publication and internal cross-layer reference passed.\n";
        auto bright = state;
        EditPhoto(bright.layers.front(), Kind::Exposure, [](auto& r) { r.preToneExposureEv = 18.f; });
        lowered = graph;
        Require(LowerRawLayerStack(lowered, bright, 1, 8, error), error);
        CompareGain(base, Render(pipeline, lowered), std::exp2(18.0f));
        GLint internalFormat = 0;
        glBindTexture(GL_TEXTURE_2D, pipeline.GetOutputTexture());
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
        Require(internalFormat == GL_RGBA32F, "Layer image was reduced to half precision.");
        auto sampled = state;
        const auto sampledMask = SolidMask(sampled, first, 0);
        auto& sampledLayer = sampled.layers.front();
        auto& sampledGraph = sampledLayer.graph;
        const int sampledOutput = FindRawMaskOutput(sampled, sampledMask)->nodeId;
        const auto oldLink = *sampledGraph.FindInputLink(sampledOutput, "imageIn");
        sampledGraph.RemoveLink(oldLink.fromNodeId, oldLink.fromSocketId, sampledOutput, "imageIn");
        const int image = sampledGraph.AddImageNode({}, {0,300})->id;
        const int split = sampledGraph.AddChannelSplitNode({160,300})->id;
        Require(sampledGraph.TryConnectSockets(image, "imageOut", split, "imageIn", &error), error);
        Require(sampledGraph.TryConnectSockets(split, "r", sampledOutput, "imageIn", &error), error);
        sampledGraph.FindNode(image)->role = GraphModel::NodeRole::Reference;
        sampledGraph.FindNode(image)->reference = RawLayerEndpoint(sampledLayer, *FindRawRole(sampledLayer, GraphModel::NodeRole::CurrentImage));
        EditPhoto(sampledLayer, Kind::Exposure, [](auto& r) { r.preToneExposureEv = 1; });
        Attach(sampled, first, Kind::Exposure, sampledMask);
        lowered = graph;
        Require(LowerRawLayerStack(lowered, sampled, 1, 9, error), error);
        const int maskedExposure = CompiledOperation(lowered, state, first, Kind::Exposure);
        const auto factor = std::find_if(lowered.links.begin(), lowered.links.end(), [&](const auto& link) {
            return link.toNodeId == maskedExposure && link.toSocketId == "maskIn";
        });
        Require(factor != lowered.links.end(), "Missing sampled mask attachment.");
        const auto resolvedMask = std::find_if(lowered.links.begin(), lowered.links.end(), [&](const auto& link) {
            return link.toNodeId == factor->fromNodeId && link.toSocketId == "imageIn";
        });
        Require(resolvedMask != lowered.links.end() && resolvedMask->fromNodeId == lowered.rawLayerMaskNodeIds.at(first).at(split)
            && resolvedMask->fromSocketId == "r", "Stable mask reference did not bind its numeric producer.");
        auto sampledPreview = lowered;
        sampledPreview.outputNodeId = lowered.rawLayerMaskNodeIds.at(first).at(split);
        sampledPreview.outputSocketId = "r";
        const auto maskValues = Render(pipeline, sampledPreview);
        for (std::size_t i=0; i<base.size(); i+=4)
            Require(std::abs(maskValues[i]-base[i]) < .004f*std::max(.25f,std::abs(base[i])),
                "Sampled mask source differs at " + std::to_string(i) + ": expected " +
                std::to_string(base[i]) + ", got " + std::to_string(maskValues[i]));
        const auto sampledPixels = Render(pipeline, lowered);
        for (std::size_t i=0; i<base.size(); i+=4) {
            const float gain = 1 + std::clamp(base[i], 0.0f, 1.0f);
            for (int c=0; c<3; ++c)
                Require(std::abs(sampledPixels[i+c]-base[i+c]*gain) < .004f*std::max(.25f,std::abs(base[i+c]*gain)),
                    "Pipeline-derived mask differs at " + std::to_string(i+c) + ": expected " +
                    std::to_string(base[i+c]*gain) + ", got " + std::to_string(sampledPixels[i+c]));
        }
        RawLayerStackState sampledReopened;
        Require(DeserializeRawLayerStack(nlohmann::json::parse(SerializeRawLayerStack(sampled).dump()), sampledReopened, error), error);
        Require(SerializeRawLayerStack(sampledReopened) == SerializeRawLayerStack(sampled), "Sample point changed on reopen.");
        std::cout << "Image-derived mask samples the declared scene-linear input.\n";
        for (float coverage : {0.0f, .5f, 1.0f}) {
            std::cout << "Checking tool coverage " << coverage << '\n';
            auto masked = state;
            const auto tool = SolidMask(masked, first, coverage);
            const auto whole = SolidMask(masked, first, .5f);
            auto* layer = FindRawAdjustmentLayer(masked, first);
            FindRawOperation(*layer, Kind::LuminanceTone)->rawOperation.enabled = false;
            EditPhoto(*layer, Kind::Exposure, [](auto& r) { r.preToneExposureEv = 2.f; });
            Attach(masked, first, Kind::Exposure, tool);
            layer->layerMask = whole;
            layer->opacity = .5f;
            lowered = graph;
            Require(LowerRawLayerStack(lowered, masked, 1, 2, error), error);
            CompareGain(base, Render(pipeline, lowered), 1.0f + 3.0f * coverage * .5f * .5f);
            const auto output = FindRawMaskOutput(masked, tool)->nodeId;
            layer->graph.RemoveNode(output);
            lowered = graph;
            Require(LowerRawLayerStack(lowered, masked, 1, 3, error), error);
            CompareGain(base, Render(pipeline, lowered), 1.0f);
        }
        auto curved = state;
        const auto toneMask = SolidMask(curved, first, .5f);
        const auto entireMask = SolidMask(curved, first, .5f);
        auto* toneLayer = FindRawAdjustmentLayer(curved, first);
        RawRecipe::RawPointCurveComponent curve;
        curve.points = {{0,0}, {.5f,.57f}, {1,1}};
        EditPhoto(*toneLayer, Kind::RgbCurves, [&](auto& r) { RawRecipe::StorePointCurveComponentInFinishToneJson(r.finishTone.layerJson,
            RawRecipe::RawPointCurveChannel::Composite, curve); });
        // A curve inside a mask graph changes coverage, independently of the
        // photo curve owned by RAW. Then break its input while keeping Output.
        auto graphEditedMask = state;
        const auto shaped = SolidMask(graphEditedMask, first, .25f);
        auto& maskOwner = graphEditedMask.layers.front();
        auto maskCurve = Photo(*toneLayer, Kind::RgbCurves).finishTone.layerJson;
        maskCurve["type"] = "ToneCurve";
        maskCurve["truthfulV2SignedMath"] = true;
        maskCurve["inputWorkingSpace"] = RawRecipe::WorkingSpaceStableString(space);
        maskOwner.processingSettings.push_back(maskCurve);
        auto& maskGraph = maskOwner.graph;
        const int shapedOutput = FindRawMaskOutput(graphEditedMask, shaped)->nodeId;
        const int generator = maskGraph.FindInputLink(shapedOutput, "imageIn")->fromNodeId;
        maskGraph.RemoveLink(generator, "maskOut", shapedOutput, "imageIn");
        const int maskCurveNode = maskGraph.AddLayerNode(LayerType::ToneCurve, 0, {160,0})->id;
        Require(maskGraph.TryConnectSockets(generator, "maskOut", maskCurveNode, "imageIn", &error), error);
        Require(maskGraph.TryConnectSockets(maskCurveNode, "imageOut", shapedOutput, "imageIn", &error), error);
        EditPhoto(maskOwner, Kind::Exposure, [](auto& r) { r.preToneExposureEv = 1.f; });
        Attach(graphEditedMask, first, Kind::Exposure, shaped);
        lowered = graph;
        Require(LowerRawLayerStack(lowered, graphEditedMask, 1, 20, error), error);
        const auto shapedValue = RawRecipe::EvaluateFinishTonePointCurveRgb(maskCurve, {.25f,.25f,.25f}, space);
        CompareGain(base, Render(pipeline, lowered), 1.0f + std::clamp(shapedValue[0], 0.0f, 1.0f));
        maskGraph.RemoveNode(generator);
        lowered = graph;
        Require(LowerRawLayerStack(lowered, graphEditedMask, 1, 21, error), error);
        CompareGain(base, Render(pipeline, lowered), 1.0f);
        std::cout << "Mask graph curve and incomplete branch passed.\n";
        Attach(curved, first, Kind::RgbCurves, toneMask);
        toneLayer->layerMask = entireMask;
        lowered = graph;
        Require(LowerRawLayerStack(lowered, curved, 1, 5, error), error);
        const auto tonePixels = Render(pipeline, lowered);
        std::cout << "Checking masked Tone Curve against the RAW curve evaluator\n";
        for (std::size_t i = 0; i < base.size(); i += 4) {
            const auto mapped = RawRecipe::EvaluateFinishTonePointCurveRgb(Photo(*toneLayer, Kind::RgbCurves).finishTone.layerJson,
                {base[i],base[i+1],base[i+2]}, space);
            for (int channel=0; channel<3; ++channel) {
                const float expected = base[i+channel] + .25f*(mapped[channel]-base[i+channel]);
                Require(std::abs(tonePixels[i+channel]-expected) < .006f*std::max(.25f,std::abs(expected)),
                    "Tone mask or curve evaluation differs from RAW math.");
            }
            Require(tonePixels[i+3] == base[i+3], "Tone layer changed alpha.");
        }
        const auto second = AddRawAdjustmentLayer(curved, "Exposure after curve");
        EditPhoto(*FindRawAdjustmentLayer(curved, second), Kind::Exposure, [](auto& r) { r.preToneExposureEv = 1.f; });
        lowered = graph;
        Require(LowerRawLayerStack(lowered, curved, 1, 6, error), error);
        const auto toneThenExposure = Render(pipeline, lowered);
        CompareGain(tonePixels, toneThenExposure, 2.0f);
        Require(MoveRawAdjustmentLayer(curved, second, 0, error), error);
        lowered = graph;
        Require(LowerRawLayerStack(lowered, curved, 1, 7, error), error);
        lowered.rawLayerScopeNodeId = CompiledOperation(lowered, state, first, Kind::Exposure);
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::FinishToneInput, 8);
        const auto exposureThenTone = Render(pipeline, lowered);
        const auto& scope = pipeline.GetRawDevelopmentGraphScopeReadback();
        Require(scope.valid && scope.width == 4 && scope.height == 2 &&
            scope.measurementDomain == "scene-linear-raw-layer-tool-input", "Selected layer scope is missing.");
        for (int y=0; y<2; ++y) for (int x=0; x<4; ++x) for (int c=0; c<3; ++c) {
            const float expected = base[((1-y)*4+x)*4+c]*2;
            Require(std::abs(scope.pixels[(y*4+x)*3+c]-expected) < .004f*std::max(.25f,std::abs(expected)),
                "Selected layer scope sampled Background or the wrong processing stage.");
        }
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None, 0);
        float orderDifference = 0;
        for (std::size_t i=0; i<base.size(); ++i)
            orderDifference = std::max(orderDifference, std::abs(toneThenExposure[i]-exposureThenTone[i]));
        Require(orderDifference > .01f, "Reordering a nonlinear curve and exposure did not change the pixels.");
        auto mappedRecipe = graph.nodes[0].rawDevelopment.recipe;
        mappedRecipe.viewTransform = RawRecipe::MakeDefaultRecipe("").viewTransform;
        mappedRecipe.viewTransform.layerJson["inputWorkingSpace"] = RawRecipe::WorkingSpaceStableString(space);
        std::cout << "Checking final view\n";
        auto mapped = graph;
        mapped.nodes[0].rawDevelopment.recipe = mappedRecipe;
        const auto expectedView = Render(pipeline, mapped);
        Require(LowerRawLayerStack(mapped, state, 1, 4, error), error);
        Require(mapped.rawLayerBackgroundNodeId > 0 && mapped.rawLayerViewNodeId > 0, "Missing view boundaries.");
        EditorRendering::ApplyRawRecipeOverlayToRenderGraph(mapped, mappedRecipe, 0, 0, 0);
        int activeViews = 0;
        for (const auto& node : mapped.nodes) {
            if (node.kind == RenderGraphNodeKind::RawDevelopment)
                Require(!RawRecipe::IsViewTransformEnabled(node.rawDevelopment.recipe), "Preview remapped Background.");
            if (node.kind == RenderGraphNodeKind::Layer && node.layerJson.value("type", "") == "ViewTransform")
                ++activeViews;
        }
        Require(activeViews == 1, "View transform is not applied once.");
        CompareGain(expectedView, Render(pipeline, mapped), 1.0f);
        std::cout << "Checking layer and numeric mask thumbnails\n";
        auto thumbnailState = state;
        EditPhoto(thumbnailState.layers.front(), Kind::Exposure, [](auto& r) { r.preToneExposureEv = 1.f; });
        const auto thumbnailMask = SolidMask(thumbnailState, first, .25f);
        auto expectedFirst = graph;
        expectedFirst.nodes[0].rawDevelopment.recipe = mappedRecipe;
        Require(LowerRawLayerStack(expectedFirst, thumbnailState, 1, 30, error), error);
        const auto expectedFirstPixels = Render(pipeline, expectedFirst);
        const auto secondThumbnailLayer = AddRawAdjustmentLayer(thumbnailState);
        EditPhoto(thumbnailState.layers.back(), Kind::Exposure, [](auto& r) { r.preToneExposureEv = -2.f; });
        auto thumbnailGraph = graph;
        thumbnailGraph.nodes[0].rawDevelopment.recipe = mappedRecipe;
        Require(LowerRawLayerStack(thumbnailGraph, thumbnailState, 1, 31, error), error);
        const auto finalThumbnailPixels = Render(pipeline, thumbnailGraph);
        auto previewGraph = thumbnailGraph;
        Require(ConfigureRawLayerThumbnail(previewGraph, first, {}, error), error);
        CompareGain(expectedFirstPixels, Render(pipeline, previewGraph), 1);
        previewGraph = thumbnailGraph;
        Require(ConfigureRawLayerThumbnail(previewGraph, secondThumbnailLayer, {}, error), error);
        CompareGain(finalThumbnailPixels, Render(pipeline, previewGraph), 1);
        previewGraph = thumbnailGraph;
        Require(ConfigureRawLayerThumbnail(previewGraph, kRawBackgroundId, {}, error), error);
        CompareGain(expectedView, Render(pipeline, previewGraph), 1);
        previewGraph = thumbnailGraph;
        Require(ConfigureRawLayerThumbnail(previewGraph, first, thumbnailMask.outputId, error), error);
        const auto coveragePixels = Render(pipeline, previewGraph);
        for (std::size_t i=0; i<coveragePixels.size(); i+=4)
            for (int c=0; c<3; ++c)
                Require(std::abs(coveragePixels[i+c]-.25f)<.001f, "Mask thumbnail was display-mapped or tinted.");
        Require(!ConfigureRawLayerThumbnail(previewGraph, first, "missing-mask", error), "Missing thumbnail target accepted.");
        CompareGain(finalThumbnailPixels, Render(pipeline, thumbnailGraph), 1);
    }
    std::cout << "GPU: neutral layer, Exposure/Tone masks, whole-layer mask, opacity, empty output, order, signed/HDR data, two working spaces and final view passed.\n";
}

void CheckCreativeGpu() {
    using namespace RawRecipe;
    RenderPipeline pipeline; pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    std::string error;
    for(auto space : {Raw::RawWorkingSpace::LinearSrgbD65,Raw::RawWorkingSpace::LinearRec2020D65}) {
        auto graph=BackgroundGraph();graph.nodes[0].rawDevelopment.recipe.technical.workingSpace=space;
        auto nearBlack=std::make_shared<Raw::RawImageData>(*graph.nodes[0].rawDevelopment.embeddedRawData);
        // The decoder's existing half-float boundary can represent this input.
        // A separate tone adjustment below tests much smaller output values.
        nearBlack->linearFloatBuffer[3]=nearBlack->linearFloatBuffer[4]=nearBlack->linearFloatBuffer[5]=1e-5f;
        graph.nodes[0].rawDevelopment.embeddedRawData=nearBlack;
        const auto base=Render(pipeline,graph);
        std::size_t nearBlackIndex=base.size();
        for(std::size_t i=0;i<base.size();i+=4)
            if(base[i]>0 && std::max({base[i],base[i+1],base[i+2]})<1e-4f)nearBlackIndex=i;
        Require(nearBlackIndex<base.size(),"RAW intermediates lost near-black values");
        RawLayerStackState state;const auto id=AddRawAdjustmentLayer(state);
        SceneTone tone;tone.contrast=1.35f;tone.outerProtection=.65f;
        EditPhoto(state.layers[0], Kind::LuminanceTone, [&](auto& r) { r.finishTone.layerJson["luminanceTone"] = SerializeSceneTone(tone); });
        auto lowered=graph;Require(LowerRawLayerStack(lowered,state,1,30,error),error);
        const auto mapped=Render(pipeline,lowered);
        const std::array<float,3> weights=space==Raw::RawWorkingSpace::LinearRec2020D65?
            std::array<float,3>{.2627002f,.6779981f,.0593017f}:std::array<float,3>{.2126729f,.7151522f,.0721750f};
        for(std::size_t i=0;i<base.size();i+=4) {
            const auto expected=ApplySceneTone(tone,{base[i],base[i+1],base[i+2]},weights);
            for(int c=0;c<3;++c) Require(std::abs(mapped[i+c]-expected[c])<.003f*std::max(.01f,std::abs(expected[c])),"GPU tone differs from the authoritative EV curve");
            Require(mapped[i+3]==base[i+3],"Tone changed alpha");
            if(i==nearBlackIndex) Require(std::abs(mapped[i]-expected[0])<std::abs(expected[0])*.003f,
                "Near-black tone differs from the authoritative EV curve");
        }
        const auto coverage=SolidMask(state,id,.5f);Attach(state, id, Kind::LuminanceTone, coverage);
        lowered=graph;Require(LowerRawLayerStack(lowered,state,1,31,error),error);
        const auto masked=Render(pipeline,lowered);
        for(std::size_t i=0;i<base.size();++i) Require(std::abs(masked[i]-(base[i]+mapped[i])*.5f)<.002f,"Tone mask is not result blending");
        auto background=graph;background.nodes[0].rawDevelopment.recipe.finishTone=Photo(state.layers[0], Kind::LuminanceTone).finishTone;
        const auto backgroundTone=Render(pipeline,background);
        for(std::size_t i=0;i<mapped.size();++i)Require(std::abs(backgroundTone[i]-mapped[i])<.003f*std::max(.25f,std::abs(mapped[i])),"Background and layer tone disagree at " + std::to_string(i) + ": " + std::to_string(backgroundTone[i]) + " vs " + std::to_string(mapped[i]));
        SceneTone deepTone;deepTone.anchorOffsetEv=-12;
        background.nodes[0].rawDevelopment.recipe.finishTone.layerJson["luminanceTone"]=SerializeSceneTone(deepTone);
        const auto deepOutput=Render(pipeline,background);
        const float expectedBlack=base[nearBlackIndex]*std::exp2(-12.f);
        Require(deepOutput[nearBlackIndex]>0 && std::abs(deepOutput[nearBlackIndex]-expectedBlack)<expectedBlack*.003f,
            "Identity RGB curves lifted or lost the luminance result near black");
        EditPhoto(state.layers[0], Kind::LuminanceTone, [](auto& r) { r.finishTone.layerJson = DefaultFinishToneJson(); });
        auto local=DefaultLocalRangeRecipe();local.enabled=true;
        local.highlightProtection=0;local.detailProtection=0;
        for(auto& point:local.points)point.deltaEv=.5f;
        EditPhoto(state.layers[0], Kind::LocalEv, [&](auto& r) { r.localRange = local; });
        lowered=graph;Require(LowerRawLayerStack(lowered,state,1,32,error),error);
        CompareGain(base,Render(pipeline,lowered),std::exp2(.5f));
        local.enabled=false;
        EditPhoto(state.layers[0], Kind::LocalEv, [&](auto& r) { r.localRange = local; });
        // Compare the extracted, graph-owned generators with the established
        // shared-input EV accumulation, using the existing validation image.
        auto localRecipe = MakeDefaultRecipe({});
        for (int i=0; i<2; ++i) {
            RawGradientEvAdjustment gradient;
            gradient.mask.id = "graph-gradient-" + std::to_string(i);
            gradient.mask.centerU = i == 0 ? .3f : .7f;
            gradient.curve = local; gradient.curve.enabled = true;
            for (auto& point : gradient.curve.points) point.deltaEv = i == 0 ? .5f : -.25f;
            localRecipe.evGradients.push_back(gradient);
        }
        const auto operationUuid = FindRawOperation(state.layers[0], Kind::LocalEv)->instanceUuid;
        Require(WriteRawLayerOperation(state.layers[0], operationUuid, localRecipe, error), error);
        auto legacyGradients = graph;
        legacyGradients.nodes[0].rawDevelopment.recipe.evGradients = localRecipe.evGradients;
        const auto expectedGradients = Render(pipeline,legacyGradients);
        lowered=graph; Require(LowerRawLayerStack(lowered,state,1,320,error),error);
        CompareGain(expectedGradients,Render(pipeline,lowered),1.f);
        localRecipe.evGradients.clear();
        localRecipe.localRange = local; localRecipe.localRange.enabled = true;
        RawZoneArea area; area.id = "graph-painted-area"; area.enabled = true; area.offsetEv = .5f;
        RawZoneBrushStroke stroke; stroke.radius = .4f; stroke.path = {{.5f,.5f}};
        area.strokes.push_back(stroke); localRecipe.localRange.areas.push_back(area);
        Require(WriteRawLayerOperation(state.layers[0], operationUuid, localRecipe, error), error);
        auto legacyArea = graph; legacyArea.nodes[0].rawDevelopment.recipe.localRange = localRecipe.localRange;
        const auto expectedArea = Render(pipeline,legacyArea);
        lowered=graph; Require(LowerRawLayerStack(lowered,state,1,321,error),error);
        CompareGain(expectedArea,Render(pipeline,lowered),1.f);
        const auto measuredArea = [&](RenderGraphSnapshot& snapshot) {
            const int operation=CompiledOperation(snapshot,state,id,Kind::LocalEv);
            snapshot.rawLayerScopeNodeId=operation;
            snapshot.rawLayerScopeOperationId=operation;
            snapshot.rawLayerScopeSocketId="measurementImageOut";
            pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::LocalRangeInput,8);
            Render(pipeline,snapshot);
            const auto& scope=pipeline.GetRawDevelopmentGraphScopeReadback();
            Require(scope.valid && scope.graphInputFingerprint && scope.zoneAreas.size()==1,
                "Local EV scope lost its actual graph input identity or area measurement.");
            Require(scope.controlSignalDomain=="edge-aware-scene-ev" &&
                scope.controlSignal.size()==static_cast<std::size_t>(scope.width)*scope.height &&
                std::all_of(scope.controlSignal.begin(),scope.controlSignal.end(),[](float ev){return std::isfinite(ev);}),
                "Local EV scope cannot populate its histogram from the captured guide.");
            Require(scope.zoneAreas[0].maskFingerprint==ZoneAreaMaskFingerprint(area) && scope.zoneAreas[0].maskPreview,
                "Local EV area preview lost its authored generator through the graph.");
            return scope.zoneAreas[0].maskPreview;
        };
        const auto directCoverage=measuredArea(lowered);
        auto& areaGraph=state.layers[0].graph;
        const int localId=FindRawOperation(state.layers[0],Kind::LocalEv)->id;
        const auto areaLink=*areaGraph.FindInputLink(localId,"area:"+area.id);
        const int invertId=areaGraph.AddMaskUtilityNode(EditorNodeGraph::MaskUtilityKind::Invert,{600,400})->id;
        Require(areaGraph.TryConnectSockets(areaLink.fromNodeId,areaLink.fromSocketId,invertId,
            EditorNodeGraph::kMaskUtilityInputSocketId,&error),error);
        Require(areaGraph.TryConnectSockets(invertId,"maskOut",localId,"area:"+area.id,&error),error);
        lowered=graph; Require(LowerRawLayerStack(lowered,state,1,322,error),error);
        const auto processedCoverage=measuredArea(lowered);
        Require(processedCoverage->coverage.size()==directCoverage->coverage.size(),"Processed area preview changed extent.");
        for(std::size_t i=0;i<directCoverage->coverage.size();++i)
            Require(std::abs(processedCoverage->coverage[i]+directCoverage->coverage[i]-1.f)<.001f,
                "Area preview showed the generator instead of the inverted graph coverage.");
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(RawDevelopmentGraphScopeStage::None,0);
        Require(WriteRawLayerOperation(state.layers[0], operationUuid, MakeDefaultRecipe({}), error), error);
        std::cout << "Graph-owned additive gradients and painted area coverage passed.\n";

        EditPhoto(state.layers[0], Kind::DetailContrast, [](auto& r) {
            r.detailContrast.scaleGains[2]=1.4f; r.detailContrast.scaleGains[5]=.7f;
            r.detailContrast.evScaleResidual[8*kDetailBands+2]=.2f; });
        lowered=graph;Require(LowerRawLayerStack(lowered,state,1,33,error),error);
        const auto detail=Render(pipeline,lowered);
        bool changed=false;
        for(std::size_t i=0;i<base.size();i+=4) {
            Require(detail[i+3]==base[i+3],"Detail changed alpha");
            for(int c=0;c<3;++c)changed|=std::abs(detail[i+c]-base[i+c])>.00001f;
        }
        Require(changed,"Active Detail Contrast produced no change");
        background=graph;background.nodes[0].rawDevelopment.recipe.detailContrast=Photo(state.layers[0], Kind::DetailContrast).detailContrast;
        const auto backgroundDetail=Render(pipeline,background);
        for(std::size_t i=0;i<detail.size();++i)Require(std::abs(backgroundDetail[i]-detail[i])<.003f*std::max(.25f,std::abs(detail[i])),"Background and layer detail disagree at " + std::to_string(i) + ": " + std::to_string(backgroundDetail[i]) + " vs " + std::to_string(detail[i]));
        Attach(state, id, Kind::DetailContrast, coverage);
        lowered=graph;Require(LowerRawLayerStack(lowered,state,1,34,error),error);
        const auto maskedDetail=Render(pipeline,lowered);
        for(std::size_t i=0;i<base.size();++i)Require(std::abs(maskedDetail[i]-(base[i]+detail[i])*.5f)<.002f,"Detail mask is not result blending");
    }
    std::cout<<"Luminance tone, guided Local EV and Detail GPU/layer checks passed.\n";
}

void CheckStore(const std::filesystem::path& folder) {
    RawLayerStackState layers;
    const auto first = AddRawAdjustmentLayer(layers, "Exposure layer");
    AddRawAdjustmentLayer(layers, "Tone layer");
    const auto mask = SolidMask(layers, first, .3f);
    Attach(layers, first, Kind::Exposure, mask);
    EditPhoto(layers.layers[0], Kind::LocalEv, [](auto& r) { r.localRange.enabled=true; r.localRange.points[2].deltaEv=.6f; });
    Attach(layers, first, Kind::LocalEv, mask);
    EditPhoto(layers.layers[0], Kind::DetailContrast, [](auto& r) { r.detailContrast.scaleGains[4]=1.3f; r.detailContrast.evScaleResidual[64]=-.3f; });
    Attach(layers, first, Kind::DetailContrast, mask);
    RawRecipe::SceneTone sceneTone;sceneTone.contrast=1.4f;sceneTone.outerProtection=.7f;
    EditPhoto(layers.layers[1], Kind::LuminanceTone, [&](auto& r) { r.finishTone.layerJson["luminanceTone"]=RawRecipe::SerializeSceneTone(sceneTone); });
    layers.layers[1].layerMask = mask;
    const auto square = AddRawGeneratedMask(layers, first, EditorNodeGraph::MaskGeneratorKind::Square, "Custom square");
    const auto* squareOutput = FindRawMaskOutput(layers, square);
    auto& maskGraph = layers.layers[0].graph;
    const int squareNode = maskGraph.FindInputLink(squareOutput->nodeId, "imageIn")->fromNodeId;
    maskGraph.FindNode(squareNode)->maskSettings.radiusY = .2f;
    EditorNodeGraph::ImagePayload imported;
    imported.label = "Embedded mask image";
    imported.width = imported.height = 2;
    imported.channels = 4;
    imported.pixels = {0,0,0,255, 64,64,64,255, 128,128,128,255, 255,255,255,255};
    imported.sourceColorMetadata = NodeMath::InspectSourceColorMetadata({}, 2, 2, 4,
        NodeMath::LogicalPrecision::UInt8, imported.label);
    const auto importedPixels = imported.pixels;
    const int importedId = maskGraph.AddImageNode(std::move(imported), {0,600})->id;
    const auto expected = SerializeRawLayerStack(layers);
    RawProjectSnapshot initial;
    initial.projectId = GenerateStableUuid();
    initial.projectName = "Layer save/reopen validation";
    initial.pipelineData = {{"rawLayerStack", expected}};
    const auto path = folder / ("save-reopen-" + initial.projectId);
    auto created = CreateProjectStore(path, ProjectStorageKind::DirectoryBundle, initial);
    Require(static_cast<bool>(created), "Cannot create layer project: " + created.message);
    auto transaction = created.store->BeginTransaction(created.snapshot.persistedStorageRevision);
    auto saved = created.snapshot;
    saved.pipelineData["rawLayerStack"] = expected;
    const auto committed = created.store->Commit(transaction, saved);
    Require(static_cast<bool>(committed), "Cannot save layer project: " + committed.message);
    created.store.reset();
    auto reopened = OpenProjectStore(path);
    Require(static_cast<bool>(reopened), "Cannot reopen layer project: " + reopened.message);
    RawLayerStackState restored;
    std::string error;
    Require(ReadRawLayersFromPipeline(reopened.snapshot.pipelineData, restored, error), error);
    const auto actual = SerializeRawLayerStack(restored);
    if (actual != expected)
        std::cerr << "Save/reopen difference: " << nlohmann::json::diff(expected, actual).dump(2) << '\n';
    Require(actual == expected, "Disk save/reopen changed the layer graph or references.");
    Require(restored.layers[0].graph.FindNode(importedId)->image.pixels == importedPixels,
        "Disk save/reopen lost an embedded mask source.");
    std::cout << "Disk project store save/reopen passed: " << path.string() << '\n';
}
} // namespace

bool ValidateRawLayers(const std::filesystem::path& folder) {
    if (!glfwInit()) return false;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(64, 64, "RAW layer validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    bool ok = false;
    try {
        Require(LoadGLFunctions(), "Cannot initialize OpenGL.");
        CheckPixelsAndView();
        CheckCreativeGpu();
        CheckStore(folder);
        ok = true;
    } catch (const std::exception& error) {
        std::cerr << "RAW layer validation failed: " << error.what() << '\n';
    }
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok;
}
} // namespace Stack::Validation
